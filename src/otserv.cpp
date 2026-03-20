// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "otserv.h"

#include "configmanager.h"
#include "databasemanager.h"
#include "databasetasks.h"
#include "game.h"
#include "logger.h"
#include "luascript.h"
#include "protocoladmin.h"
#include "protocollogin.h"
#include "protocolstatus.h"
#include "rsa.h"
#include "scheduler.h"
#include "script.h"
#include "scriptmanager.h"
#include "server.h"
#include "signals.h"
#include "thread_pool.h"

#include <fmt/color.h>
#include <fmt/format.h>
#include <fstream>
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
extern Scripts* g_scripts;

std::mutex g_loaderLock;
std::condition_variable g_loaderSignal;
std::unique_lock<std::mutex> g_loaderUniqueLock(g_loaderLock);

namespace {

void startupErrorMessage(std::string_view errorStr)
{
	LOG_ERROR(errorStr);
	g_loaderSignal.notify_all();
}

void mainLoader(ServiceManager* services)
{
	// dispatcher thread
	g_game.setGameState(GAME_STATE_STARTUP);

#ifdef STATS_ENABLED
	g_stats.setEnabled(false);
#endif

	if (!initLogger(LogLevel::INFO)) {
		startupErrorMessage("Failed to initialize logger!");
		return;
	}

	setupLoggerSignalHandlers();

	g_threadPool.start();

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

	// check if config.lua or config.lua.dist exist
	auto configFile = getString(ConfigManager::CONFIG_FILE);
	std::ifstream c_test(fmt::format("./{}", configFile));
	if (!c_test.is_open()) {
		std::ifstream config_lua_dist("./config.lua.dist");
		if (config_lua_dist.is_open()) {
			LOG_INFO(fmt::format(">> copying config.lua.dist to {}", configFile));
			std::ofstream config_lua(std::string{configFile});
			config_lua << config_lua_dist.rdbuf();
			config_lua.close();
			config_lua_dist.close();
		}
	} else {
		c_test.close();
	}

	// read global config
	LOG_INFO(">> Loading config");
	if (!ConfigManager::load()) {
		startupErrorMessage(fmt::format("Unable to load {}!", configFile));
		return;
	}
	g_logger().setLevel(parseLogLevel(getString(ConfigManager::LOG_LEVEL)));

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

	LOG_INFO(">> Loading script systems");
	if (!ScriptingManager::getInstance().loadScriptSystems()) {
		startupErrorMessage("Failed to load script systems");
		return;
	}

	LOG_INFO(">> Loading spells");
	if (!g_scripts->loadScripts("scripts/spells", false, false)) {
		startupErrorMessage("Failed to load spell scripts");
		return;
	}

	LOG_INFO(">> Loading lua scripts");
	if (!g_scripts->loadScripts("scripts", false, false)) {
		startupErrorMessage("Failed to load lua scripts");
		return;
	}

	LOG_INFO(fmt::format(">> Loading monsters... [\033[1;33m{}\033[0m]", g_monsters.monsters.size()));

	LOG_INFO(">> Loading outfits");
	if (!Outfits::getInstance().loadFromXml()) {
		startupErrorMessage("Unable to load outfits!");
		return;
	}

	LOG_INFO(">> Checking world type... ");
	auto worldType = boost::algorithm::to_lower_copy<std::string>(std::string{getString(ConfigManager::WORLD_TYPE)});
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
	LOG_INFO(fmt::format(">> {}", boost::algorithm::to_upper_copy(worldType)));

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
	    boost::algorithm::to_lower_copy<std::string>(std::string{getString(ConfigManager::HOUSE_RENT_PERIOD)});

	if (strRentPeriod == "yearly") {
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
		LOG_INFO(
		    fmt::format("> Warning: {} has been executed as root user, please consider running it as a normal user.",
		                STATUS_SERVER_NAME));
	}
#endif

	g_game.start(services);
	g_game.setGameState(GAME_STATE_NORMAL);

#ifdef STATS_ENABLED
	g_stats.setEnabled(true);
#endif

	g_loaderSignal.notify_all();
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

	ServiceManager serviceManager;

	g_dispatcher.start();
	g_scheduler.start();
#ifdef STATS_ENABLED
	g_stats.start();
#endif

	g_dispatcher.addTask(
	    createTaskWithStats([=, services = &serviceManager]() { mainLoader(services); }, "MainLoader", ""));

	g_loaderSignal.wait(g_loaderUniqueLock);

	if (serviceManager.is_running()) {
		LOG_INFO(">> Version TFS: {} | Protocol: {} | Ports: {} / {} | IP: {}",
		         fmt::format(fg(fmt::color::lime_green), "{}", STATUS_SERVER_VERSION),
		         fmt::format(fg(fmt::color::lime_green), "{}", CLIENT_VERSION_STR),
		         fmt::format(fg(fmt::color::lime_green), "{}", getInteger(ConfigManager::LOGIN_PORT)),
		         fmt::format(fg(fmt::color::lime_green), "{}", getInteger(ConfigManager::GAME_PORT)),
		         fmt::format(fg(fmt::color::lime_green), "{}", getString(ConfigManager::IP)));
		LOG_INFO("");
		LOG_INFO(">> {} Server Online!", getString(ConfigManager::SERVER_NAME));
		serviceManager.run();
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
	std::thread watchdog([]() {
		std::this_thread::sleep_for(std::chrono::seconds(60));
		LOG_ERROR("[Watchdog] Shutdown exceeded 60 seconds. Forcing termination.");
		std::_Exit(EXIT_FAILURE);
	});
	watchdog.detach();

	// Shutdown ThreadPool first - async map saves need DB connection alive
	g_threadPool.shutdown();

	// Cleanup Lua environment before shutting down threads
	LuaEnvironment::shutdown();

	// Cleanup MySQL connection and library
	Database::shutdown();

	g_scheduler.join();
	g_databaseTasks.join();
	g_dispatcher.join();
#ifdef STATS_ENABLED
	g_stats.join();
#endif
}

void printServerVersion()
{
#if defined(GIT_RETRIEVED_STATE) && GIT_RETRIEVED_STATE
	LOG_INFO(fmt::format("{} - Version {}", STATUS_SERVER_NAME, GIT_DESCRIBE));
	LOG_INFO(fmt::format("Git SHA1 {} dated {}", GIT_SHORT_SHA1, GIT_COMMIT_DATE_ISO8601));
#if GIT_IS_DIRTY
	LOG_INFO("*** DIRTY - NOT OFFICIAL RELEASE ***");
#endif
#else
	LOG_INFO(fmt::format("{} - Version {}", STATUS_SERVER_NAME, STATUS_SERVER_VERSION));
#endif
	LOG_INFO("");

	LOG_INFO(fmt::format("Compiled with {}", BOOST_COMPILER));
	LOG_INFO(fmt::format("Compiled on {} {} for platform ", __DATE__, __TIME__));
#if defined(__amd64__) || defined(_M_X64)
	LOG_INFO("x64");
#elif defined(__i386__) || defined(_M_IX86) || defined(_X86_)
	LOG_INFO("x86");
#elif defined(__arm__)
	LOG_INFO("ARM");
#else
	LOG_INFO("unknown");
#endif
#if defined(LUAJIT_VERSION)
	LOG_INFO(fmt::format("Linked with {} for Lua support", LUAJIT_VERSION));
#else
	LOG_INFO(fmt::format("Linked with {} for Lua support", LUA_RELEASE));
#endif
	LOG_INFO("");

	LOG_INFO(fmt::format("A server developed by {}", STATUS_SERVER_DEVELOPERS));
	LOG_INFO(fmt::format(fg(fmt::color::red), "Downgraded and further developed by Nekiro / MillhioreBT"));
	LOG_INFO("Visit our forum for updates, support, and resources: http://otland.net/.");
	LOG_INFO("");
	printCustomInfo();
	LOG_INFO("");
}

void printCustomInfo()
{
	LOG_INFO("");
	LOG_INFO(fmt::format(fg(fmt::color::red), "Further developed by Mateuzkl (Custom Modified Version)"));
	LOG_INFO(fmt::format(fg(fmt::color::yellow),
	                     "Repository ORIGINAL: https://github.com/MillhioreBT/forgottenserver-downgrade"));
	LOG_INFO(fmt::format(fg(fmt::color::yellow),
	                     "Repository CUSTOM: https://github.com/Mateuzkl/forgottenserver-downgrade"));
	LOG_INFO("");
}
