// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "otserv.h"

#include "configmanager.h"
#include "console_styles.h"
#include "databasemanager.h"
#include "databasetasks.h"
#include "game.h"
#include "imbuement.h"
#include "outfit.h"
#include "logger.h"
#include "mapcache.h"
#include "outputmessage.h"
#include "protocollogin.h"
#include "protocoladmin.h"
#include "protocolstatus.h"
#include "performance_metrics.h"
#include "reactor.h"
#include "rsa.h"
#include "save_manager.h"
#include "scheduler.h"
#include "script.h"
#include "scriptmanager.h"
#include "server.h"
#include "signals.h"
#include "startup_progress.h"
#include "luascript.h"
#include "lua.hpp"
#include "thread_pool.h"
#include "zoneweather.h"
#include "zones.h"

#include <fmt/format.h>
#include <fmt/color.h>
#include <future>
#if __has_include("gitmetadata.h")
#include "gitmetadata.h"
#endif

DatabaseTasks g_databaseTasks;
Dispatcher g_dispatcher;
Scheduler g_scheduler;
Stats g_stats;

Game g_game;
Monsters g_monsters;
Vocations g_vocations;

namespace {

std::optional<double> startupMapLoadSeconds;

struct BootstrapOptions {
	bool logToFile = false;
	LogLevel logLevel = LogLevel::INFO;
	bool startupProgressBar = true;
	bool startupDetailedLogs = false;
	bool consoleColors = true;
};

struct StartupRuntimeState {
	bool threadPoolStarted = false;
	bool databaseTasksStarted = false;
	bool statsStarted = false;
	bool scriptEngineLoaded = false;
	bool revScriptsLoaded = false;
	bool npcEngineLoaded = false;
	std::optional<uint64_t> guildCount;
};

template <typename... Args>
void consolePrint(fmt::text_style style, fmt::format_string<Args...> format, Args&&... args)
{
	const std::string rendered = fmt::format(format, std::forward<Args>(args)...);
	if (startupProgress().colorsEnabled()) {
		fmt::print(style, "{}", rendered);
	} else {
		fmt::print("{}", rendered);
	}
}

BootstrapOptions getBootstrapOptions(const std::string& configFile)
{
	lua_State* L = luaL_newstate();
	if (!L) {
		return {};
	}
	luaL_openlibs(L);

	lua_pushinteger(L, 1);
	lua_setglobal(L, "TEXTCOLOR_WHITE");
	lua_pushinteger(L, 2);
	lua_setglobal(L, "TEXTCOLOR_RED");
	lua_pushinteger(L, 3);
	lua_setglobal(L, "TEXTCOLOR_ORANGE");

	BootstrapOptions options;
	auto readOptions = [&]() {
		auto readBoolean = [&](const char* name, bool& value) {
			lua_getglobal(L, name);
			if (lua_isboolean(L, -1)) {
				value = lua_toboolean(L, -1) != 0;
			}
			lua_pop(L, 1);
		};
		readBoolean("logToFile", options.logToFile);
		readBoolean("startupProgressBar", options.startupProgressBar);
		readBoolean("startupDetailedLogs", options.startupDetailedLogs);
		readBoolean("consoleColors", options.consoleColors);

		lua_getglobal(L, "logLevel");
		if (lua_isstring(L, -1)) {
			options.logLevel = parseLogLevel(lua_tostring(L, -1));
		}
		lua_pop(L, 1);
	};

	if (luaL_dofile(L, configFile.c_str()) == 0) {
		readOptions();
	} else {
		fmt::print(stderr, "Warning: Failed to parse config file '{}': {}\n", configFile, lua_tostring(L, -1));
	}

	constexpr const char* serverConfigFile = "data/server_config.lua";
	if (std::ifstream customConfig(serverConfigFile); customConfig.good()) {
		if (luaL_dofile(L, serverConfigFile) == 0) {
			readOptions();
		} else {
			fmt::print(stderr, "Warning: Failed to parse server config '{}': {}\n", serverConfigFile, lua_tostring(L, -1));
		}
	}

	lua_close(L);
	return options;
}

void startupErrorMessage(std::string_view errorStr)
{
	startupProgress().fail(errorStr);
	if (isLoggerInitialized()) {
		LOG_ERROR(errorStr);
	} else {
		fmt::print(stderr, "[ERROR   ] {}\n", errorStr);
	}
}

void printDatabaseConnectionFailure(const Database::ConnectionError& error)
{
	using namespace ConsoleStyle;
	using ErrorKind = Database::ConnectionError::Kind;

	const std::string errorMessage = error.message.empty() ? "Unknown MySQL/MariaDB connection error" : error.message;
	std::string_view explanation;
	std::vector<std::string_view> steps;

	switch (error.kind) {
		case ErrorKind::UNKNOWN_DATABASE:
			explanation = "The configured database does not exist or could not\n    be accessed.";
			steps = {
			    "Create the MySQL/MariaDB database.",
			    "Import the server schema.sql file.",
			    "Check mysqlHost, mysqlUser, mysqlPass, mysqlDatabase and mysqlPort in config.lua.",
			    "Confirm that MySQL/MariaDB is running.",
			};
			break;
		case ErrorKind::ACCESS_DENIED:
			explanation = "The configured account was rejected by MySQL/MariaDB.";
			steps = {
			    "Check mysqlUser and mysqlPass in config.lua.",
			    "Confirm that the database account exists.",
			    "Grant the account access to the configured database.",
			    "Confirm that MySQL/MariaDB is running.",
			};
			break;
		case ErrorKind::CANNOT_CONNECT:
			explanation = "MySQL/MariaDB is not reachable at the configured host and port.";
			steps = {
			    "Start the MySQL/MariaDB service.",
			    "Check mysqlHost and mysqlPort in config.lua.",
			    "Check mysqlSock when using a local socket.",
			    "Confirm that the port is reachable and not blocked.",
			};
			break;
		case ErrorKind::TIMED_OUT:
			explanation = "The connection timed out before MySQL/MariaDB responded.";
			steps = {
			    "Confirm that MySQL/MariaDB is running.",
			    "Check mysqlHost and mysqlPort in config.lua.",
			    "Check network routing and firewall rules.",
			    "Confirm that the database accepts remote connections.",
			};
			break;
		case ErrorKind::UNKNOWN_HOST:
			explanation = "The configured MySQL/MariaDB host could not be resolved.";
			steps = {
			    "Correct mysqlHost in config.lua.",
			    "Check DNS or use a valid IP address.",
			    "Confirm network connectivity to the database host.",
			    "Restart the server after correcting the host.",
			};
			break;
		case ErrorKind::OTHER:
			explanation = "The database connection could not be established.";
			steps = {
			    "Check the MySQL/MariaDB connection settings in config.lua.",
			    "Confirm that MySQL/MariaDB is running.",
			    "Confirm that the database and account exist.",
			    "Review the MySQL error shown above.",
			};
			break;
	}

	std::string persisted = fmt::format(
	    "DATABASE CONNECTION FAILED\n"
	    "Host: {}\nPort: {}\nDatabase: {}\nUser: {}\nMySQL Error: {}\n\n{}\n\nRequired steps:\n",
	    getString(ConfigManager::MYSQL_HOST), getInteger(ConfigManager::SQL_PORT),
	    getString(ConfigManager::MYSQL_DB), getString(ConfigManager::MYSQL_USER), errorMessage, explanation);
	for (size_t index = 0; index < steps.size(); ++index) {
		persisted += fmt::format("{}. {}\n", index + 1, steps[index]);
	}
	persisted += "\nServer startup was safely aborted.";

	g_logger().writeConsoleBlock([&]() {
		consolePrint(red_b, "\n    ✖  DATABASE CONNECTION FAILED\n");
		consolePrint(dark_gray, "    ──────────────────────────────────────────────────────\n");
		consolePrint(gray, "    {:<20}", "Host");
		consolePrint(white_b, "{}\n", getString(ConfigManager::MYSQL_HOST));
		consolePrint(gray, "    {:<20}", "Port");
		consolePrint(white_b, "{}\n", getInteger(ConfigManager::SQL_PORT));
		consolePrint(gray, "    {:<20}", "Database");
		consolePrint(white_b, "{}\n", getString(ConfigManager::MYSQL_DB));
		consolePrint(gray, "    {:<20}", "User");
		consolePrint(white_b, "{}\n", getString(ConfigManager::MYSQL_USER));
		consolePrint(gray, "    {:<20}", "MySQL Error");
		consolePrint(red_b, "{}\n\n", errorMessage);
		consolePrint(white_b, "    {}\n\n", explanation);
		consolePrint(white_b, "    Required steps:\n");
		for (size_t index = 0; index < steps.size(); ++index) {
			consolePrint(gray, "    {}. ", index + 1);
			consolePrint(white_b, "{}\n", steps[index]);
		}
		consolePrint(red_b, "\n    Server startup was safely aborted.\n\n");
	}, persisted);
}

#ifdef STATS_ENABLED
void printStatsStatus()
{
	using namespace ConsoleStyle;
	if (!g_stats.isEnabled()) {
		consolePrint(cyan_b, "    ⚙  OTS STATISTICS\n");
		consolePrint(dark_gray, "    ────────────────────────────────────────\n");
		consolePrint(gray, "    {:<20}", "Status");
		consolePrint(dark_gray, "Disabled by config\n");
		return;
	}

	const auto fileStatus = g_stats.getFileLoggingStatus();

	if (fileStatus.requested && !fileStatus.available) {
		consolePrint(yellow_b, "    ⚠  OTS STATISTICS\n");
		consolePrint(dark_gray, "    ────────────────────────────────────────\n");
		consolePrint(gray, "    {:<20}", "Log directory");
		consolePrint(yellow_b, "unavailable\n");
		consolePrint(gray, "    {:<20}", "Path");
		consolePrint(white_b, "{}\n", fileStatus.absoluteDirectory.string());
		consolePrint(gray, "    {:<20}", "Reason");
		consolePrint(white_b, "{}\n", fileStatus.reason);
		consolePrint(gray, "    {:<20}", "File logging");
		consolePrint(yellow_b, "disabled for this session\n");
		return;
	}

	consolePrint(cyan_b, "    ⚙  OTS STATISTICS\n");
	consolePrint(dark_gray, "    ────────────────────────────────────────\n");
	consolePrint(gray, "    {:<20}", "Status");
	consolePrint(green_b, "Enabled ✔\n");
	consolePrint(gray, "    {:<20}", "Report Interval");
	consolePrint(white_b, "{} s\n", getInteger(ConfigManager::STATS_DUMP_INTERVAL));

	if (fileStatus.requested) {
		for (std::string_view label : {"Dispatcher Logs", "Lua Logs", "SQL Logs", "Special Logs"}) {
			consolePrint(gray, "    {:<20}", label);
			consolePrint(green_b, "Ready ✔\n");
		}
	} else {
		consolePrint(gray, "    {:<20}", "File logging");
		consolePrint(dark_gray, "Disabled by config\n");
	}
	consolePrint(gray, "    {:<20}", "Directory");
	consolePrint(white_b, "{}\n", fileStatus.directory.generic_string());
}
#endif

std::string formatFeatureStatus(std::string_view name, ConfigManager::Boolean key)
{
	return fmt::format("{} [{}]", name, ConfigManager::getBoolean(key) ? "ON" : "OFF");
}

void printFeatureStatus()
{
	LOG_INFO(">> Systems: {} | {} | {} | {} | {}", formatFeatureStatus("Forge", ConfigManager::FORGE_SYSTEM_ENABLED),
	         formatFeatureStatus("Imbuements", ConfigManager::IMBUEMENT_SYSTEM_ENABLED),
	         formatFeatureStatus("Wheel", ConfigManager::WHEEL_SYSTEM_ENABLED),
	         formatFeatureStatus("Augments", ConfigManager::AUGMENT_SYSTEM_ENABLED),
	         formatFeatureStatus("Proficiency", ConfigManager::WEAPON_PROFICIENCY_SYSTEM_ENABLED));
	LOG_INFO(">> Modules: {} | {} | {} | {} | {} | {}",
	         formatFeatureStatus("Bestiary", ConfigManager::BESTIARY_SYSTEM_ENABLED),
	         formatFeatureStatus("Prey", ConfigManager::PREY_SYSTEM_ENABLED),
	         formatFeatureStatus("Market", ConfigManager::MARKET_SYSTEM_ENABLED),
	         formatFeatureStatus("Familiar", ConfigManager::FAMILIAR_SYSTEM_ENABLED),
	         formatFeatureStatus("Monk", ConfigManager::MONK_VOCATION_ENABLED),
	         formatFeatureStatus("Monster levels", ConfigManager::MONSTER_LEVEL_ENABLED));
}

constexpr std::string_view getPlatformName()
{
#if defined(__amd64__) || defined(_M_X64)
	return "x64";
#elif defined(__i386__) || defined(_M_IX86) || defined(_X86_)
	return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
	return "ARM64";
#elif defined(__arm__)
	return "ARM";
#else
	return "unknown";
#endif
}

constexpr std::string_view getLuaRuntimeName()
{
#if defined(LUAJIT_VERSION)
	return LUAJIT_VERSION;
#else
	return LUA_RELEASE;
#endif
}

std::string getCompilerName()
{
#if defined(__clang__)
	return fmt::format("Clang {}", __clang_version__);
#elif defined(_MSC_VER)
	return fmt::format("Microsoft Visual C++ version {}", _MSC_VER);
#elif defined(__GNUC__)
	return fmt::format("GCC {}", __VERSION__);
#else
	return "unknown";
#endif
}

std::string getDatabaseClientName()
{
#if defined(MARIADB_VERSION_ID)
	return fmt::format("MariaDB {}", Database::getClientVersion());
#else
	return fmt::format("MySQL {}", Database::getClientVersion());
#endif
}

bool mainLoader(const std::shared_ptr<ServiceManager>& services, StartupRuntimeState& runtimeState)
{
	// reactor thread
	UPDATE_OTSYS_TIME();
	g_game.setGameState(GAME_STATE_STARTUP);

#ifdef STATS_ENABLED
	g_stats.setEnabled(false);
#endif

	// check if config.lua or config.lua.dist exist
	auto configFile = getString(ConfigManager::CONFIG_FILE);
	if (configFile.empty()) {
		configFile = "config.lua";
		ConfigManager::setString(ConfigManager::CONFIG_FILE, configFile);
	}
	std::ifstream c_test(fmt::format("./{}", configFile));
	bool copiedConfig = false;
	if (!c_test.is_open()) {
		std::ifstream config_lua_dist("./config.lua.dist");
		if (config_lua_dist.is_open()) {
			std::ofstream config_lua(std::string{configFile});
			config_lua << config_lua_dist.rdbuf();
			config_lua.close();
			config_lua_dist.close();
			copiedConfig = true;
		}
	} else {
		c_test.close();
	}

	const BootstrapOptions bootstrap = getBootstrapOptions(std::string{configFile});

	if (!initLogger(bootstrap.logLevel, "data/logs/server.log", 5 * 1024 * 1024, 3, bootstrap.logToFile)) {
		fmt::print(stderr, "Failed to initialize logger!\n");
		return false;
	}

	setupLoggerSignalHandlers();
	startupProgress().configure(bootstrap.startupProgressBar, bootstrap.startupDetailedLogs, bootstrap.consoleColors);
	g_logger().setConsoleColors(startupProgress().colorsEnabled());
	g_logger().setConsoleLevel(bootstrap.startupDetailedLogs ? bootstrap.logLevel : LogLevel::WARNING);

	srand(static_cast<unsigned int>(OTSYS_TIME()));
#ifdef _WIN32
	SetConsoleTitle(STATUS_SERVER_NAME);
#endif

	printServerVersion();
	startupProgress().start();
	startupProgress().begin(StartupStage::CONFIGURATION, configFile);

	if (copiedConfig) {
		LOG_INFO(fmt::format(">> copying config.lua.dist to {}", configFile));
	}

	// read global config
	LOG_INFO(">> Loading config");
	if (!ConfigManager::load()) {
		startupErrorMessage(fmt::format("Unable to load {}!", configFile));
		return false;
	}
	g_logger().setLevel(parseLogLevel(getString(ConfigManager::LOG_LEVEL)));
#ifdef STATS_ENABLED
	g_stats.prepareFileLogging();
#endif
	printFeatureStatus();
	startupProgress().complete("configuration loaded");
	startupProgress().begin(StartupStage::RUNTIME, "map cache fingerprint");
	if (caseInsensitiveEqual(getString(ConfigManager::MAP_CACHE_MODE), "auto")) {
		MapCache::precomputeFingerprint(
		    fmt::format("data/world/{}.otbm", getString(ConfigManager::MAP_NAME)));
	}
	startupProgress().update(1, 3, "map cache ready");

	const auto workerThreads = static_cast<uint32_t>(
		std::clamp<int64_t>(getInteger(ConfigManager::NETWORK_THREADS), 1, 64));
	g_threadPool.start(workerThreads);
	runtimeState.threadPoolStarted = true;
	startupProgress().update(2, 3, "worker pool started");

#ifdef _WIN32
	auto defaultPriority = getString(ConfigManager::DEFAULT_PRIORITY);
	if (caseInsensitiveEqual(defaultPriority, "high")) {
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	} else if (caseInsensitiveEqual(defaultPriority, "above-normal")) {
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	}
#endif

	// set RSA key
	try {
		std::ifstream key{"key.pem"};
		std::string pem{std::istreambuf_iterator<char>{key}, std::istreambuf_iterator<char>{}};
		tfs::rsa::loadPEM(pem);
	} catch (const std::exception& e) {
		startupErrorMessage(e.what());
		return false;
	}
	startupProgress().update(3, 3, "RSA key loaded");
	startupProgress().complete("runtime ready");
	startupProgress().begin(StartupStage::DATABASE, "connecting");

	LOG_DATABASE(">> Establishing database connection...");

	if (!Database::getInstance().connect()) {
		startupProgress().fail("Failed to connect to database.");
		printDatabaseConnectionFailure(Database::getInstance().getLastConnectionError());
		return false;
	}
	startupProgress().update(1, 5, "connected");

	LOG_DATABASE(fmt::format(">> MySQL {}", Database::getClientVersion()));

	// run database manager
	LOG_INFO(">> Running database manager");

	if (!DatabaseManager::isDatabaseSetup()) {
		startupErrorMessage(
		    "The database you have specified in config.lua is empty, please import the schema.sql to your database.");
		return false;
	}
	startupProgress().update(2, 5, "schema verified");
	g_databaseTasks.start();
	runtimeState.databaseTasksStarted = true;
	startupProgress().update(3, 5, "database worker started");

	DatabaseManager::updateDatabase();
	startupProgress().update(4, 5, "migrations checked");

	if (const auto guildCountResult = Database::getInstance().storeQuery(
	        "SELECT COUNT(*) AS `count` FROM `guilds`")) {
		runtimeState.guildCount = guildCountResult->getNumber<uint64_t>("count");
	}

	// Recover any pending async saves from a previous crash
	g_saveManager.recoverPendingFlushes();

	if (getBoolean(ConfigManager::OPTIMIZE_DATABASE) && !DatabaseManager::optimizeTables()) {
		LOG_INFO(">> No tables were optimized.");
	}
	startupProgress().update(5, 5, "recovery and optimization complete");
	startupProgress().complete("database ready");
	startupProgress().begin(StartupStage::GAME_DATA, "vocations");

	// load vocations
	if (!g_vocations.loadFromXml()) {
		startupErrorMessage("Unable to load vocations!");
		return false;
	}
	startupProgress().update(1, 6, "vocations");

	// instantiate required script systems for items
	if (!ScriptingManager::getInstance().loadPreItems()) {
		startupErrorMessage("Failed to initialize pre-item script systems");
		return false;
	}
	startupProgress().update(2, 6, "pre-item systems");

	// load item data
	LOG_INFO(">> Loading items... ");
	if (!Item::items.loadFromOtb("data/items/items.otb")) {
		startupErrorMessage("Unable to load items (OTB)!");
		return false;
	}
	startupProgress().update(3, 6, "items OTB");
	LOG_INFO(fmt::format(">> OTB v{:d}.{:d}.{:d}", Item::items.majorVersion, Item::items.minorVersion,
	                         Item::items.buildNumber));

	if (!Item::items.loadFromXml()) {
		startupErrorMessage("Unable to load items (XML)!");
		return false;
	}
	startupProgress().update(4, 6, "items XML");

	if (ConfigManager::getBoolean(ConfigManager::IMBUEMENT_SYSTEM_ENABLED)) {
		LOG_INFO(">> Loading imbuements");
		if (!Imbuements::getInstance().loadFromXml()) {
			startupErrorMessage("Unable to load imbuements!");
			return false;
		}
	}
	startupProgress().update(5, 6, ConfigManager::getBoolean(ConfigManager::IMBUEMENT_SYSTEM_ENABLED) ? "imbuements" : "imbuements disabled");

	LOG_INFO(">> Preparing native OTBM zones");
	if (!Zones::load()) {
		startupErrorMessage("Unable to load zones!");
		return false;
	}
	if (!ZoneWeather::load()) {
		startupErrorMessage("Unable to load zone weather!");
		return false;
	}
	startupProgress().update(6, 6, "zones");
	startupProgress().complete("core data loaded");
	startupProgress().begin(StartupStage::SCRIPT_SYSTEMS, "script registries");

	LOG_INFO(">> Loading script systems");
	if (!ScriptingManager::getInstance().loadScriptSystems()) {
		startupErrorMessage("Failed to load script systems");
		return false;
	}
	runtimeState.scriptEngineLoaded = true;

	g_game.raids.getScriptInterface().initState();
	startupProgress().complete("script systems ready");
	startupProgress().begin(StartupStage::SPELL_SCRIPTS, "discovering files");

	LOG_INFO(">> Loading spells");
	if (!g_scripts->loadScripts("scripts/spells", false, false)) {
		startupErrorMessage("Failed to load spell scripts");
		return false;
	}
	startupProgress().complete("spell scripts ready");
	startupProgress().begin(StartupStage::MONSTER_SCRIPTS, "discovering files");

	LOG_INFO(">> Loading monster scripts");
	if (!g_scripts->loadScripts("monsters", false, false)) {
		startupErrorMessage("Failed to load monster scripts");
		return false;
	}
	startupProgress().complete("monster scripts ready");
	startupProgress().begin(StartupStage::LUA_SCRIPTS, "discovering files");

	LOG_INFO(">> Loading lua scripts");
	if (!g_scripts->loadScripts("scripts", false, false)) {
		startupErrorMessage("Failed to load lua scripts");
		return false;
	}
	runtimeState.revScriptsLoaded = true;
	startupProgress().complete("Lua scripts ready");
	startupProgress().begin(StartupStage::NPC_SCRIPTS, "discovering files");


	LOG_INFO(">> Loading lua npcs");
	if (!Npcs::loadScripts(false)) {
		startupErrorMessage("Failed to load lua npcs");
		return false;
	}
	runtimeState.npcEngineLoaded = true;
	startupProgress().complete("NPC scripts ready");
	startupProgress().begin(StartupStage::FINAL_GAME_DATA, "outfits");

	LOG_INFO(fmt::format(">> Loaded monster definitions: {}", g_monsters.monsters.size()));

	LOG_INFO(">> Loading outfits");
	if (!Outfits::getInstance().loadFromXml()) {
		startupErrorMessage("Unable to load outfits!");
		return false;
	}
	startupProgress().update(1, 2, "outfits");

	LOG_INFO(">> Checking world type... ");
	auto worldType = asLowerCaseString(std::string{getString(ConfigManager::WORLD_TYPE)});
	if (worldType == "pvp") {
		g_game.setWorldType(WORLD_TYPE_PVP);
	} else if (worldType == "no-pvp") {
		g_game.setWorldType(WORLD_TYPE_NO_PVP);
	} else if (worldType == "pvp-enforced") {
		g_game.setWorldType(WORLD_TYPE_PVP_ENFORCED);
	} else {
		LOG_INFO("\n");
		startupErrorMessage(
		    fmt::format("Unknown world type: {:s}, valid world types are: pvp, no-pvp and pvp-enforced.",
		                getString(ConfigManager::WORLD_TYPE)));
		return false;
	}
	LOG_INFO(fmt::format(">> {}", asUpperCaseString(worldType)));
	startupProgress().update(2, 2, "world type");
	startupProgress().complete("game data finalized");
	startupProgress().begin(StartupStage::MAP_DATA, getString(ConfigManager::MAP_NAME));

	startupMapLoadSeconds.reset();
	const auto mapStartupStart = std::chrono::steady_clock::now();
	LOG_INFO(">> Loading map");
	if (!g_game.loadMainMap(std::string{getString(ConfigManager::MAP_NAME)})) {
		startupErrorMessage("Failed to load map");
		return false;
	}
	startupMapLoadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - mapStartupStart).count();

	LOG_INFO(">> Initializing gamestate");
	startupProgress().begin(StartupStage::GAME_INITIALIZATION, "groups and chat");
	g_game.setGameState(GAME_STATE_INIT);
	startupProgress().complete("game state initialized");
	g_logger().info(">> Map-to-GAME_STATE_INIT total: {:.3f} s.",
	                std::chrono::duration<double>(std::chrono::steady_clock::now() - mapStartupStart).count());
	startupProgress().begin(StartupStage::SERVICES, "binding ports");

	// Game client protocols
	if (!services->add<ProtocolGame>(static_cast<uint16_t>(getInteger(ConfigManager::GAME_PORT))) ||
	    !services->add<ProtocolLogin>(static_cast<uint16_t>(getInteger(ConfigManager::LOGIN_PORT)))) {
		startupErrorMessage("Failed to bind required game/login service ports.");
		return false;
	}

	// OT protocols
	if (!services->add<ProtocolStatus>(static_cast<uint16_t>(getInteger(ConfigManager::STATUS_PORT)))) {
		startupErrorMessage("Failed to bind the required status service port.");
		return false;
	}
	const auto adminPort = static_cast<uint16_t>(getInteger(ConfigManager::ADMIN_PORT));
	if (adminPort != 0 && !services->add<ProtocolAdmin>(adminPort)) {
		startupErrorMessage("Failed to bind the configured admin service port.");
		return false;
	}
	if (!services->hasOpenServices()) {
		startupErrorMessage("One or more network service ports are not open.");
		return false;
	}
	startupProgress().update(1, 5, "ports bound");

	RentPeriod_t rentPeriod;
	auto strRentPeriod =
		asLowerCaseString(std::string{getString(ConfigManager::HOUSE_RENT_PERIOD)});

	if (strRentPeriod == "yearly" || strRentPeriod == "annual") {
		rentPeriod = RENTPERIOD_YEARLY;
	} else if (strRentPeriod == "weekly") {
		rentPeriod = RENTPERIOD_WEEKLY;
	} else if (strRentPeriod == "monthly") {
		rentPeriod = RENTPERIOD_MONTHLY;
	} else if (strRentPeriod == "daily") {
		rentPeriod = RENTPERIOD_DAILY;
	} else if (strRentPeriod == "dev") {
		rentPeriod = RENTPERIOD_DEV;
	} else {
		rentPeriod = RENTPERIOD_NEVER;
	}

	g_game.map.houses.payHouses(rentPeriod);
	startupProgress().update(2, 5, "house rent processed");

	LOG_INFO(">> Loaded all modules, server starting up...");

#ifndef _WIN32
	if (getuid() == 0 || geteuid() == 0) {
		LOG_INFO(fmt::format("> Warning: {} has been executed as root user, please consider running it as a normal user.", STATUS_SERVER_NAME));
	}
#endif

	g_game.start(services);
	g_game.setGameState(GAME_STATE_NORMAL);
	startupProgress().update(3, 5, "game state normal");

	// Pre-warm the OutputMessage pool to avoid operator new() on first connections
	OutputMessagePool::prewarmPool(128);
	startupProgress().update(4, 5, "runtime pools ready");
	return true;
}

[[noreturn]] void badAllocationHandler()
{
	// Use functions that only use stack allocation
	puts("Allocation failed, server out of memory.\nDecrease the size of your map or compile in 64 bits mode.\n");
	getchar();
	exit(-1);
}

} // namespace

int startServer()
{
	std::set_new_handler(badAllocationHandler);

	auto serviceManager = std::make_shared<ServiceManager>();
	StartupRuntimeState runtimeState;
	bool startupCompleted = false;

#ifdef STATS_ENABLED
	g_stats.setEnabled(false);
#endif

	g_dispatcher.start();
	g_scheduler.start();

	const bool startupLoaded = mainLoader(serviceManager, runtimeState);

	std::jthread serviceThread;

	if (startupLoaded && serviceManager->hasOpenServices()) {
		// Configure reactor production limits: fairness, time budget, and backpressure
		g_reactor.setMaxTasksPerCycle(static_cast<uint32_t>(getInteger(ConfigManager::REACTOR_MAX_TASKS_PER_CYCLE)));
		g_reactor.setTimeBudget(std::chrono::milliseconds(getInteger(ConfigManager::REACTOR_TIME_BUDGET_MS)));
		g_reactor.setMaxInboxSize(static_cast<size_t>(getInteger(ConfigManager::REACTOR_MAX_INBOX_SIZE)));
		g_performanceMetrics.setEnabled(getBoolean(ConfigManager::PERFORMANCE_METRICS_ENABLED));

		LOG_INFO(">> Reactor limits: maxTasks={}, timeBudget={}ms, maxInbox={}",
		    getInteger(ConfigManager::REACTOR_MAX_TASKS_PER_CYCLE),
		    getInteger(ConfigManager::REACTOR_TIME_BUDGET_MS),
		    getInteger(ConfigManager::REACTOR_MAX_INBOX_SIZE));

		const auto networkThreads = std::clamp<int64_t>(getInteger(ConfigManager::NETWORK_THREADS), 1, 64);
#ifdef STATS_ENABLED
		const bool statsEnabled = getBoolean(ConfigManager::STATS_MONITOR_ENABLED);
		g_stats.setEnabled(statsEnabled);
		if (statsEnabled) {
			g_stats.configureDispatchers(static_cast<std::size_t>(networkThreads) + 1);
		}
#endif
		auto serviceStarted = std::make_shared<std::promise<void>>();
		auto serviceStartedFuture = serviceStarted->get_future();
		serviceThread = std::jthread([serviceManager, serviceStarted]() {
			serviceManager->run([serviceStarted]() { serviceStarted->set_value(); });
		});
		serviceStartedFuture.get();
		if (!serviceManager->is_running()) {
			startupErrorMessage("Network I/O stopped before startup completed.");
		} else {
			startupProgress().complete("network I/O running");
			startupProgress().finish();
			for (const auto& stage : startupProgress().snapshot()) {
				LOG_STARTUP("{} (weight {}%): {:.3f} s", stage.name, stage.weight, stage.seconds);
			}
			LOG_STARTUP("Startup completed in {:.3f} s", startupProgress().elapsedSeconds());
		}
		if (serviceManager->is_running()) {
		g_logger().writeConsoleBlock([&]() {
		using namespace ConsoleStyle;

		// ── Server Config ──
		consolePrint(cyan_b, "    ⚙  SERVER CONFIG\n");
		consolePrint(dark_gray, "    ────────────────────────────────────────\n");
		consolePrint(gray, "    {:<20}", "World Map");
		consolePrint(white_b, "{}\n", getString(ConfigManager::MAP_NAME));
		consolePrint(gray, "    {:<20}", "World Size");
		consolePrint(white_b, "{}x{}\n", g_game.map.getWidth(), g_game.map.getHeight());
		consolePrint(gray, "    {:<20}", "Map Load Time");
		if (startupMapLoadSeconds) {
			consolePrint(green_b, "{:.3f} s \u2714\n", *startupMapLoadSeconds);
		} else {
			consolePrint(dark_gray, "unavailable\n");
		}
		consolePrint(gray, "    {:<20}", "World Type");
		consolePrint(white_b, "{}\n", getString(ConfigManager::WORLD_TYPE));
		consolePrint(gray, "    {:<20}", "Account Manager");
		consolePrint(white_b, "{}\n", getBoolean(ConfigManager::ACCOUNT_MANAGER) ? "enabled" : "disabled");
		consolePrint(gray, "    {:<20}", "Game Port");
		consolePrint(white_b, "{} ✔\n", getInteger(ConfigManager::GAME_PORT));
		consolePrint(gray, "    {:<20}", "Login Port");
		consolePrint(white_b, "{} ✔\n", getInteger(ConfigManager::LOGIN_PORT));
		consolePrint(gray, "    {:<20}", "Status Port");
		consolePrint(white_b, "{} ✔\n", getInteger(ConfigManager::STATUS_PORT));
		fmt::print("\n");

		// ── Threads ──
		consolePrint(cyan_b, "    ⚙  THREADS\n");
		consolePrint(dark_gray, "    ────────────────────────────────────────\n");
		consolePrint(gray, "    {:<20}", "Network I/O");
		consolePrint(white_b, "{}\n", networkThreads);
		consolePrint(gray, "    {:<20}", "ThreadPool Workers");
		consolePrint(white_b, "{}\n", g_threadPool.get_thread_count());
		consolePrint(gray, "    {:<20}", "Dispatcher");
		consolePrint(white_b, "1\n");
		consolePrint(gray, "    {:<20}", "Scheduler");
		consolePrint(white_b, "1\n");
		consolePrint(gray, "    {:<20}", "DB Tasks");
		consolePrint(white_b, "1\n");
		fmt::print("\n");

#ifdef STATS_ENABLED
		printStatsStatus();
		fmt::print("\n");
#endif

		// ── Game Data ──
		consolePrint(cyan_b, "    ⚙  GAME DATA\n");
		consolePrint(dark_gray, "    ────────────────────────────────────────\n");
		const auto printCount = [](std::string_view label, size_t count) {
			consolePrint(gray, "    {:<20}", label);
			consolePrint(white_b, "{} ", count);
			consolePrint(green_b, "✔\n");
		};
		printCount("Items", Item::items.size());
		printCount("Vocations", g_vocations.getVocations().size());
		consolePrint(gray, "    {:<20}", "Outfits");
		consolePrint(white_b, "{} (M) + {} (F) ",
		             Outfits::getInstance().getOutfits(PLAYERSEX_MALE).size(),
		             Outfits::getInstance().getOutfits(PLAYERSEX_FEMALE).size());
		consolePrint(green_b, "✔\n");
		printCount("NPCs", g_game.getNpcs().size());
		printCount("Monsters", g_monsters.monsters.size());
		consolePrint(gray, "    {:<20}", "Guilds");
		if (runtimeState.guildCount) {
			consolePrint(white_b, "{} ", *runtimeState.guildCount);
			consolePrint(green_b, "✔\n");
		} else {
			consolePrint(dark_gray, "unavailable\n");
		}
		printCount("Zones", Zones::count());
		const auto printEngineStatus = [](std::string_view label, std::string_view engine, bool loaded) {
			consolePrint(gray, "    {:<20}", label);
			if (loaded) {
				consolePrint(white_b, "{} ", engine);
				consolePrint(green_b, "✔\n");
			} else {
				consolePrint(red_b, "Not loaded\n");
			}
		};
		const bool castSystemLoaded = g_scripts && g_scripts->isFileLoaded(
		    "data/scripts/talkactions/player/misc/cast_system.lua");
		printEngineStatus("Cast System", "Loaded", castSystemLoaded);
		printEngineStatus("Script Engine", "RevScripts",
		                  runtimeState.scriptEngineLoaded && runtimeState.revScriptsLoaded);
		if (g_scripts) {
			printCount("Lua Scripts", g_scripts->getLoadedFileCount());
		} else {
			consolePrint(gray, "    {:<20}", "Lua Scripts");
			consolePrint(red_b, "Not loaded\n");
		}
		printEngineStatus("NPC Engine", "Loaded", runtimeState.npcEngineLoaded && Npcs::isLoaded());
		consolePrint(gray, "    {:<20}", "Map");
		consolePrint(green_b, "Loaded ✔\n");
		const auto printMapLoadStatus = [](std::string_view name, MapLoadStatus status) {
			consolePrint(gray, "    {:<20}", name);
			switch (status) {
				case MapLoadStatus::LOADED:
					consolePrint(green_b, "Loaded ✔\n");
					break;
				case MapLoadStatus::SKIPPED:
					consolePrint(dark_gray, "Skipped\n");
					break;
				case MapLoadStatus::FAILED:
					consolePrint(red_b, "Failed\n");
					break;
			}
		};
		printMapLoadStatus("Houses", g_game.map.getHouseLoadStatus());
		printMapLoadStatus("Spawns", g_game.map.getSpawnLoadStatus());
		fmt::print("\n");

		// Real ConfigManager toggles only; no inferred or hardcoded feature state.
		consolePrint(cyan_b, "    ⚙  SERVER FEATURES\n");
		consolePrint(dark_gray, "    ────────────────────────────────────────\n");
		const std::array featureRows{
		    std::pair{"Forge", ConfigManager::FORGE_SYSTEM_ENABLED},
		    std::pair{"Imbuements", ConfigManager::IMBUEMENT_SYSTEM_ENABLED},
		    std::pair{"Wheel", ConfigManager::WHEEL_SYSTEM_ENABLED},
		    std::pair{"Bestiary", ConfigManager::BESTIARY_SYSTEM_ENABLED},
		    std::pair{"Market", ConfigManager::MARKET_SYSTEM_ENABLED},
		    std::pair{"Prey", ConfigManager::PREY_SYSTEM_ENABLED},
		    std::pair{"Battle Pass", ConfigManager::BATTLEPASS_SYSTEM_ENABLED},
		    std::pair{"Weapon Proficiency", ConfigManager::WEAPON_PROFICIENCY_SYSTEM_ENABLED},
		    std::pair{"Augments", ConfigManager::AUGMENT_SYSTEM_ENABLED},
		    std::pair{"Monk Vocation", ConfigManager::MONK_VOCATION_ENABLED},
		    std::pair{"Familiars", ConfigManager::FAMILIAR_SYSTEM_ENABLED},
		    std::pair{"Hirelings", ConfigManager::HIRELING_SYSTEM_ENABLED},
		    std::pair{"Monster Levels", ConfigManager::MONSTER_LEVEL_ENABLED},
		    std::pair{"Monster Factions", ConfigManager::MONSTER_FACTION_SYSTEM},
		    std::pair{"Chain Combat", ConfigManager::CHAIN_SYSTEM_ENABLED},
		    std::pair{"Quick Loot", ConfigManager::QUICK_LOOT_ENABLED},
		    std::pair{"Autoloot", ConfigManager::AUTOLOOT_ENABLED},
		    std::pair{"Task Hunting", ConfigManager::TASK_HUNTING_SYSTEM_ENABLED},
		    std::pair{"Bounty Tasks", ConfigManager::BOUNTY_TASKS_ENABLED},
		    std::pair{"Weekly Tasks", ConfigManager::WEEKLY_TASKS_ENABLED},
		    std::pair{"Soulpit", ConfigManager::SOULPIT_SYSTEM_ENABLED},
		    std::pair{"Soulseals", ConfigManager::SOULSEALS_SYSTEM_ENABLED},
		    std::pair{"Cleave", ConfigManager::CLEAVE_SYSTEM_ENABLED},
		    std::pair{"Character Bazaar", ConfigManager::CHARACTER_BAZAAR_ENABLED},
		    std::pair{"Reset/Reborn", ConfigManager::RESET_SYSTEM_ENABLED},
		};
		const auto printFeatureColumn = [=](const auto& feature) {
			const auto& [name, key] = feature;
			const bool enabled = ConfigManager::getBoolean(key);
			consolePrint(gray, "{:<24}", name);
			consolePrint(enabled ? green_b : red_b, "{:<3}", enabled ? "ON" : "OFF");
		};

		for (size_t index = 0; index < featureRows.size(); index += 2) {
			consolePrint(gray, "    ");
			printFeatureColumn(featureRows[index]);
			if (index + 1 < featureRows.size()) {
				consolePrint(gray, "{:6}", "");
				printFeatureColumn(featureRows[index + 1]);
			}
			fmt::print("\n");
		}
		fmt::print("\n");

		// ── Server info ──
		consolePrint(cyan_b, "    ◈  SERVER INFO\n");
		consolePrint(dark_gray, "    ────────────────────────────────────────\n");
		consolePrint(gray, "    {:<16}", "TFS Version");
		consolePrint(white_b, "{:<14}", STATUS_SERVER_VERSION);
		consolePrint(gray, "{:<12}", "Protocol");
		consolePrint(white_b, "{}\n", CLIENT_VERSION_STR);
		consolePrint(gray, "    {:<16}", "IP Address");
		consolePrint(white_b, "{:<14}", getString(ConfigManager::IP));
		consolePrint(gray, "{:<12}", "Ports");
		consolePrint(white_b, "{} / {}\n\n",
		             getInteger(ConfigManager::LOGIN_PORT),
		             getInteger(ConfigManager::GAME_PORT));

		// ── Online ──
		consolePrint(dark_gray, "    ─────────────────────────────────────────────────────────\n");
		consolePrint(green_b, "    ◆ ");
		consolePrint(white_b, "{}", getString(ConfigManager::SERVER_NAME));
		consolePrint(gray, " — ");
		consolePrint(green_b, "SERVER ONLINE");
		consolePrint(green_b, " ◆\n");
		consolePrint(dark_gray, "    ─────────────────────────────────────────────────────────\n");
		fmt::print("\n");
		std::fflush(stdout);
		});

		// Restore console output now that all startup printing is done
		g_logger().setConsoleLevel(parseLogLevel(getString(ConfigManager::LOG_LEVEL)));

#ifdef STATS_ENABLED
		if (statsEnabled) {
			g_stats.start();
			runtimeState.statsStarted = true;
		}
#endif
		startupCompleted = true;
		g_reactor.runLoop();
		}
	} else {
		if (startupLoaded) {
			startupErrorMessage("No open services. The server is NOT online.");
		}
	}

	// --- Shutdown Watchdog ---
	// If shutdown takes longer than 60 seconds, force terminate to prevent hanging forever.
	std::jthread watchdog([](std::stop_token st) {
		for (int i = 0; i < 60 && !st.stop_requested(); ++i) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
		if (!st.stop_requested()) {
			LOG_ERROR("[Watchdog] Shutdown exceeded 60 seconds. Forcing termination.");
			std::_Exit(EXIT_FAILURE);
		}
	});

	if (serviceManager->is_running()) {
		serviceManager->stop();
	}
	if (serviceThread.joinable()) {
		serviceThread.join();
	}

	// Shutdown ThreadPool before the database connection goes away.
	if (runtimeState.threadPoolStarted && g_threadPool.isRunning()) {
		g_threadPool.shutdown();
	}

	// Wait for all background tasks to finish before closing the Lua environment.
	// NPCs and their NpcScriptInterface
	if (g_scheduler.getState() != THREAD_STATE_TERMINATED) {
		g_scheduler.shutdown();
	}
	g_scheduler.join();
	if (runtimeState.databaseTasksStarted) {
		if (g_databaseTasks.isRunning()) {
			g_databaseTasks.shutdown();
		}
		g_databaseTasks.join();
	}
	if (g_dispatcher.getState() != THREAD_STATE_TERMINATED) {
		g_dispatcher.shutdown();
	}
	g_dispatcher.join();
#ifdef STATS_ENABLED
	if (runtimeState.statsStarted) {
		if (g_stats.isRunning()) {
			g_stats.shutdown();
		}
		g_stats.join();
	}
#endif

	// Only now is it safe to close Lua — all NpcScriptInterface destructors
	// have already run and released their eventTableRef handles.
	LuaEnvironment::shutdown();

	// Cleanup MySQL connection and library
	Database::shutdown();
	return startupCompleted ? EXIT_SUCCESS : EXIT_FAILURE;
}

void printServerVersion()
{
	using fmt::fg;
	using fmt::emphasis;

	const auto purple      = fg(fmt::color::medium_purple);
	using namespace ConsoleStyle;

	const auto magenta_b   = fg(fmt::color::magenta) | emphasis::bold;

	// ── ASCII Banner ──
	fmt::print("\n");
	consolePrint(purple | emphasis::bold,
		"    ████████╗███████╗███████╗    ██████╗  ██████╗ ██╗    ██╗███╗   ██╗\n");
	consolePrint(fg(fmt::color::medium_orchid),
		"    ╚══██╔══╝██╔════╝██╔════╝    ██╔══██╗██╔═══██╗██║    ██║████╗  ██║\n");
	consolePrint(fg(fmt::color::orchid),
		"       ██║   █████╗  ███████╗    ██║  ██║██║   ██║██║ █╗ ██║██╔██╗ ██║\n");
	consolePrint(fg(fmt::color::violet),
		"       ██║   ██╔══╝  ╚════██║    ██║  ██║██║   ██║██║███╗██║██║╚██╗██║\n");
	consolePrint(cyan_b,
		"       ██║   ██║     ███████║    ██████╔╝╚██████╔╝╚███╔███╔╝██║ ╚████║\n");
	consolePrint(fg(fmt::color::dark_cyan),
		"       ╚═╝   ╚═╝     ╚══════╝    ╚═════╝  ╚═════╝  ╚══╝╚══╝ ╚═╝  ╚═══╝\n");
	fmt::print("\n");

	// ── Version bar ──
	consolePrint(dark_gray, "    ─────────────────────────────────────────────────────────\n");
	consolePrint(gray, "    ◆ ");
	consolePrint(gray, "VERSION ");
	consolePrint(white_b, "{}", STATUS_SERVER_VERSION);
	consolePrint(dark_gray, "  ·  ");
	consolePrint(gray, "CLIENT ");
	consolePrint(white_b, "{}", CLIENT_VERSION_STR);
	consolePrint(dark_gray, "  ·  ");
	consolePrint(gray, "BUILD ");
#if defined(GIT_RETRIEVED_STATE) && GIT_RETRIEVED_STATE
	consolePrint(green_b, "{}", GIT_SHORT_SHA1);
#if GIT_IS_DIRTY
	consolePrint(fg(fmt::color::gold) | emphasis::bold, " DIRTY");
#endif
#else
	consolePrint(green_b, "RELEASE");
#endif
	fmt::print("\n");
	consolePrint(dark_gray, "    ─────────────────────────────────────────────────────────\n");

	// ── Build info section ──
	consolePrint(cyan_b, "\n    ⚙  BUILD INFO\n");
	consolePrint(dark_gray, "    ────────────────────────────────────────\n");
	consolePrint(gray, "    {:<20}", "Compiler");
	consolePrint(white_b, "{}\n", getCompilerName());
	consolePrint(gray, "    {:<20}", "Compiled");
	consolePrint(white_b, "{} {}\n", __DATE__, __TIME__);
	consolePrint(gray, "    {:<20}", "Platform");
	consolePrint(white_b, "{}\n", getPlatformName());
	consolePrint(gray, "    {:<20}", "Lua Version");
	consolePrint(white_b, "{}\n", getLuaRuntimeName());
	consolePrint(gray, "    {:<20}", "Database");
	consolePrint(white_b, "{}\n", getDatabaseClientName());
	consolePrint(gray, "    {:<20}", "CPU Threads");
	consolePrint(white_b, "{}\n", std::max(1u, std::thread::hardware_concurrency()));
	fmt::print("\n");

	// ── Credits ──
	consolePrint(dark_gray, "    ─────────────────────────────────────────────────────────\n");
	consolePrint(gray, "    ► Developed by ");
	consolePrint(white_b, "{}\n", STATUS_SERVER_DEVELOPERS);
	consolePrint(gray, "    ► Downgraded by ");
	consolePrint(magenta_b, "Nekiro / MillhioreBT\n");
	consolePrint(gray, "    ► Custom fork by ");
	consolePrint(red_b, "Mateuzkl\n");
	consolePrint(dark_gray, "    ─────────────────────────────────────────────────────────\n");
	fmt::print("\n");

	std::fflush(stdout);
}

#ifndef _WIN32
// Called by GDB on crash — must be extern "C" and __attribute__((used)) to prevent stripping
extern "C" __attribute__((used)) void saveServer()
{
	if (g_game.getPlayersOnline() > 0) {
		g_game.saveGameState(true);
	}
}
#endif
