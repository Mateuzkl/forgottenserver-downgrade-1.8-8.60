// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "otserv.h"

#include "configmanager.h"
#include "databasemanager.h"
#include "databasetasks.h"
#include "game.h"
#include "imbuement.h"
#include "logger.h"
#include "outputmessage.h"
#include "protocollogin.h"
#include "protocoladmin.h"
#include "protocolstatus.h"
#include "reactor.h"
#include "rsa.h"
#include "save_manager.h"
#include "scheduler.h"
#include "script.h"
#include "scriptmanager.h"
#include "server.h"
#include "signals.h"
#include "luascript.h"
#include "lua.hpp"
#include "thread_pool.h"
#include "zones.h"

#include <fmt/format.h>
#include <fmt/color.h>
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

std::pair<bool, LogLevel> getLogToFileFromConfig(const std::string& configFile)
{
	lua_State* L = luaL_newstate();
	if (!L) {
		return {false, LogLevel::INFO};
	}
	luaL_openlibs(L);

	lua_pushinteger(L, 1);
	lua_setglobal(L, "TEXTCOLOR_WHITE");
	lua_pushinteger(L, 2);
	lua_setglobal(L, "TEXTCOLOR_RED");
	lua_pushinteger(L, 3);
	lua_setglobal(L, "TEXTCOLOR_ORANGE");

	bool logToFile = false;
	LogLevel logLevel = LogLevel::INFO;
	if (luaL_dofile(L, configFile.c_str()) == 0) {
		lua_getglobal(L, "logToFile");
		if (lua_isboolean(L, -1)) {
			logToFile = lua_toboolean(L, -1) != 0;
		}
		lua_pop(L, 1);

		lua_getglobal(L, "logLevel");
		if (lua_isstring(L, -1)) {
			logLevel = parseLogLevel(lua_tostring(L, -1));
		}
		lua_pop(L, 1);
	} else {
		fmt::print(stderr, "Warning: Failed to parse config file '{}': {}\n", configFile, lua_tostring(L, -1));
	}

	constexpr const char* serverConfigFile = "data/server_config.lua";
	if (std::ifstream customConfig(serverConfigFile); customConfig.good()) {
		if (luaL_dofile(L, serverConfigFile) == 0) {
			lua_getglobal(L, "logToFile");
			if (lua_isboolean(L, -1)) {
				logToFile = lua_toboolean(L, -1) != 0;
			}
			lua_pop(L, 1);

			lua_getglobal(L, "logLevel");
			if (lua_isstring(L, -1)) {
				logLevel = parseLogLevel(lua_tostring(L, -1));
			}
			lua_pop(L, 1);
		} else {
			fmt::print(stderr, "Warning: Failed to parse server config '{}': {}\n", serverConfigFile, lua_tostring(L, -1));
		}
	}

	lua_close(L);
	return {logToFile, logLevel};
}

void startupErrorMessage(std::string_view errorStr)
{
	LOG_ERROR(errorStr);
}

std::string formatFeatureStatus(std::string_view name, ConfigManager::Boolean key)
{
	const bool enabled = ConfigManager::getBoolean(key);
	const auto color = enabled ? fmt::color::lime_green : fmt::color::yellow;
	const auto* status = enabled ? "ON" : "OFF";

	return fmt::format("{} [{}]", name, fmt::format(fg(color), "{}", status));
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
	return fmt::format("MSVC {}", _MSC_VER);
#elif defined(__GNUC__)
	return fmt::format("GCC {}", __VERSION__);
#else
	return "unknown";
#endif
}

void mainLoader(const std::shared_ptr<ServiceManager>& services)
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

	auto [logToFile, logLevel] = getLogToFileFromConfig(std::string{configFile});

	if (!initLogger(logLevel, "data/logs/server.log", 5 * 1024 * 1024, 3, logToFile)) {
		fmt::print(stderr, "Failed to initialize logger!\n");
		startupErrorMessage("Failed to initialize logger!");
		return;
	}

	setupLoggerSignalHandlers();

	srand(static_cast<unsigned int>(OTSYS_TIME()));
#ifdef _WIN32
	SetConsoleTitle(STATUS_SERVER_NAME);

	// fixes a problem with escape characters not being processed in Windows consoles
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD dwMode = 0;
	GetConsoleMode(hOut, &dwMode);
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(hOut, dwMode);
#endif

	printServerVersion();

	if (copiedConfig) {
		LOG_INFO(fmt::format(">> copying config.lua.dist to {}", configFile));
	}

	// read global config
	LOG_INFO(">> Loading config");
	if (!ConfigManager::load()) {
		startupErrorMessage(fmt::format("Unable to load {}!", configFile));
		return;
	}
	g_logger().setLevel(parseLogLevel(getString(ConfigManager::LOG_LEVEL)));
	printFeatureStatus();

	const auto workerThreads = static_cast<uint32_t>(
		std::clamp<int64_t>(getInteger(ConfigManager::NETWORK_THREADS), 1, 64));
	g_threadPool.start(workerThreads);

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
		return;
	}

	LOG_INFO(">> Establishing database connection...");

	if (!Database::getInstance().connect()) {
		startupErrorMessage("Failed to connect to database.");
		return;
	}

	LOG_INFO(fmt::format(">> MySQL {}", Database::getClientVersion()));

	// run database manager
	LOG_INFO(">> Running database manager");

	if (!DatabaseManager::isDatabaseSetup()) {
		startupErrorMessage(
		    "The database you have specified in config.lua is empty, please import the schema.sql to your database.");
		return;
	}
	g_databaseTasks.start();

	DatabaseManager::updateDatabase();

	// Recover any pending async saves from a previous crash
	g_saveManager.recoverPendingFlushes();

	if (getBoolean(ConfigManager::OPTIMIZE_DATABASE) && !DatabaseManager::optimizeTables()) {
		LOG_INFO(">> No tables were optimized.");
	}

	// load vocations
	if (!g_vocations.loadFromXml()) {
		startupErrorMessage("Unable to load vocations!");
		return;
	}

	// instantiate required script systems for items
	if (!ScriptingManager::getInstance().loadPreItems()) {
		startupErrorMessage("Failed to initialize pre-item script systems");
		return;
	}

	// load item data
	LOG_INFO(">> Loading items... ");
	if (!Item::items.loadFromOtb("data/items/items.otb")) {
		startupErrorMessage("Unable to load items (OTB)!");
		return;
	}
	LOG_INFO(fmt::format(">> OTB v{:d}.{:d}.{:d}", Item::items.majorVersion, Item::items.minorVersion,
	                         Item::items.buildNumber));

	if (!Item::items.loadFromXml()) {
		startupErrorMessage("Unable to load items (XML)!");
		return;
	}

	if (ConfigManager::getBoolean(ConfigManager::IMBUEMENT_SYSTEM_ENABLED)) {
		LOG_INFO(">> Loading imbuements");
		if (!Imbuements::getInstance().loadFromXml()) {
			startupErrorMessage("Unable to load imbuements!");
			return;
		}
	}

	LOG_INFO(">> Preparing native OTBM zones");
	if (!Zones::load()) {
		startupErrorMessage("Unable to load zones!");
		return;
	}

	LOG_INFO(">> Loading script systems");
	if (!ScriptingManager::getInstance().loadScriptSystems()) {
		startupErrorMessage("Failed to load script systems");
		return;
	}

	g_game.raids.getScriptInterface().initState();

	LOG_INFO(">> Loading spells");
	if (!g_scripts->loadScripts("scripts/spells", false, false)) {
		startupErrorMessage("Failed to load spell scripts");
		return;
	}

	LOG_INFO(">> Loading monster scripts");
	if (!g_scripts->loadScripts("monsters", false, false)) {
		startupErrorMessage("Failed to load monster scripts");
		return;
	}

	LOG_INFO(">> Loading lua scripts");
	if (!g_scripts->loadScripts("scripts", false, false)) {
		startupErrorMessage("Failed to load lua scripts");
		return;
	}


	LOG_INFO(">> Loading lua npcs");
	if (!Npcs::loadScripts(false)) {
		startupErrorMessage("Failed to load lua npcs");
		return;
	}

	LOG_INFO(fmt::format(">> Loading monsters... [\033[1;33m{}\033[0m]", g_monsters.monsters.size()));

	LOG_INFO(">> Loading outfits");
	if (!Outfits::getInstance().loadFromXml()) {
		startupErrorMessage("Unable to load outfits!");
		return;
	}

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
		return;
	}
	LOG_INFO(fmt::format(">> {}", asUpperCaseString(worldType)));

	LOG_INFO(">> Loading map");
	if (!g_game.loadMainMap(std::string{getString(ConfigManager::MAP_NAME)})) {
		startupErrorMessage("Failed to load map");
		return;
	}

	LOG_INFO(">> Initializing gamestate");
	g_game.setGameState(GAME_STATE_INIT);

	// Game client protocols
	services->add<ProtocolGame>(static_cast<uint16_t>(getInteger(ConfigManager::GAME_PORT)));
	services->add<ProtocolLogin>(static_cast<uint16_t>(getInteger(ConfigManager::LOGIN_PORT)));

	// OT protocols
	services->add<ProtocolStatus>(static_cast<uint16_t>(getInteger(ConfigManager::STATUS_PORT)));
	services->add<ProtocolAdmin>(static_cast<uint16_t>(getInteger(ConfigManager::ADMIN_PORT)));

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

	LOG_INFO(">> Loaded all modules, server starting up...");

#ifndef _WIN32
	if (getuid() == 0 || geteuid() == 0) {
		LOG_INFO(fmt::format("> Warning: {} has been executed as root user, please consider running it as a normal user.", STATUS_SERVER_NAME));
	}
#endif

	g_game.start(services);
	g_game.setGameState(GAME_STATE_NORMAL);

	// Pre-warm the OutputMessage pool to avoid operator new() on first connections
	OutputMessagePool::prewarmPool(128);
}

[[noreturn]] void badAllocationHandler()
{
	// Use functions that only use stack allocation
	puts("Allocation failed, server out of memory.\nDecrease the size of your map or compile in 64 bits mode.\n");
	getchar();
	exit(-1);
}

} // namespace

void startServer()
{
	std::set_new_handler(badAllocationHandler);

	auto serviceManager = std::make_shared<ServiceManager>();

#ifdef STATS_ENABLED
	g_stats.setEnabled(false);
#endif

	g_dispatcher.start();
	g_scheduler.start();

	mainLoader(serviceManager);

	// Configure reactor production limits: fairness, time budget, and backpressure
	g_reactor.setMaxTasksPerCycle(static_cast<uint32_t>(getInteger(ConfigManager::REACTOR_MAX_TASKS_PER_CYCLE)));
	g_reactor.setTimeBudget(std::chrono::milliseconds(getInteger(ConfigManager::REACTOR_TIME_BUDGET_MS)));
	g_reactor.setMaxInboxSize(static_cast<size_t>(getInteger(ConfigManager::REACTOR_MAX_INBOX_SIZE)));

	LOG_INFO(">> Reactor limits: maxTasks={}, timeBudget={}ms, maxInbox={}",
	    getInteger(ConfigManager::REACTOR_MAX_TASKS_PER_CYCLE),
	    getInteger(ConfigManager::REACTOR_TIME_BUDGET_MS),
	    getInteger(ConfigManager::REACTOR_MAX_INBOX_SIZE));

	std::jthread serviceThread;

	if (serviceManager->is_running()) {
		const auto networkThreads = std::clamp<int64_t>(getInteger(ConfigManager::NETWORK_THREADS), 1, 64);
#ifdef STATS_ENABLED
		g_stats.configureDispatchers(static_cast<std::size_t>(networkThreads) + 1);
		g_stats.start();
		g_stats.setEnabled(true);
#endif
		LOG_INFO(">> Version TFS: {} | Protocol: {} | Ports: {} / {} | IP: {}",
			fmt::format(fg(fmt::color::lime_green), "{}", STATUS_SERVER_VERSION),
			fmt::format(fg(fmt::color::lime_green), "{}", CLIENT_VERSION_STR),
			fmt::format(fg(fmt::color::lime_green), "{}", getInteger(ConfigManager::LOGIN_PORT)),
			fmt::format(fg(fmt::color::lime_green), "{}", getInteger(ConfigManager::GAME_PORT)),
			fmt::format(fg(fmt::color::lime_green), "{}", getString(ConfigManager::IP)));
		if (networkThreads > 1) {
			LOG_THREADPOOL(">> I/O thread pool ready: {} workers", networkThreads);
		}
		LOG_INFO("");
		LOG_INFO(">> {} Server Online!", getString(ConfigManager::SERVER_NAME));
		serviceThread = std::jthread([serviceManager]() { serviceManager->run(); });
		g_reactor.runLoop();
	} else {
		LOG_INFO(">> No services running. The server is NOT online.");
		g_threadPool.shutdown();
		g_scheduler.shutdown();
		g_databaseTasks.shutdown();
		g_dispatcher.shutdown();
#ifdef STATS_ENABLED
		g_stats.shutdown();
#endif
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

	serviceManager->stop();
	if (serviceThread.joinable()) {
		serviceThread.join();
	}

	// Shutdown ThreadPool before the database connection goes away.
	g_threadPool.shutdown();

	// Wait for all background tasks to finish before closing the Lua environment.
	// NPCs and their NpcScriptInterface
	g_scheduler.join();
	g_databaseTasks.shutdown();
	g_databaseTasks.join();
	g_dispatcher.join();
#ifdef STATS_ENABLED
	g_stats.join();
#endif

	// Only now is it safe to close Lua — all NpcScriptInterface destructors
	// have already run and released their eventTableRef handles.
	LuaEnvironment::shutdown();

	// Cleanup MySQL connection and library
	Database::shutdown();
}

void printServerVersion()
{
#if defined(GIT_RETRIEVED_STATE) && GIT_RETRIEVED_STATE
	LOG_INFO(fmt::format(fg(fmt::color::cyan), "=== {} | {} ===", STATUS_SERVER_NAME, GIT_DESCRIBE));
	LOG_INFO(fmt::format("Build: {} | Git {} ({})", getCompilerName(), GIT_SHORT_SHA1, GIT_COMMIT_DATE_ISO8601));
#if GIT_IS_DIRTY
	LOG_INFO("*** DIRTY - NOT OFFICIAL RELEASE ***");
#endif
#else
	LOG_INFO(fmt::format(fg(fmt::color::cyan), "=== {} | Version {} ===", STATUS_SERVER_NAME, STATUS_SERVER_VERSION));
	LOG_INFO(fmt::format("Build: {}", getCompilerName()));
#endif
	LOG_INFO(fmt::format("Runtime: {} {} | {} | {}", __DATE__, __TIME__, getPlatformName(), getLuaRuntimeName()));
	LOG_INFO(fmt::format("Credits: {} | Nekiro / MillhioreBT", STATUS_SERVER_DEVELOPERS));
	LOG_INFO(fmt::format(fg(fmt::color::yellow), "Repository: https://github.com/Mateuzkl/forgottenserver-downgrade"));
	LOG_INFO("");
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
