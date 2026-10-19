// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in LICENSE.
// Runs actual Lua callbacks and verifies the optional Stats sink and independent
// slow warnings. Every generated log belongs to a private temporary fixture.
#include "../otpch.h"

#include "../configmanager.h"
#include "../logger.h"
#include "../luascript.h"
#include "../stats.h"
#include "test_support.h"

#include <fstream>

extern LuaEnvironment g_luaEnvironment;

namespace {
std::string readFile(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int pauseCallback(lua_State*)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	return 0;
}

struct Fixture
{
	std::filesystem::path previousDirectory = std::filesystem::current_path();
	std::filesystem::path directory =
	    std::filesystem::temp_directory_path() /
	    ("tfs-lua-stats-gate-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	bool previousEnabled = g_stats.isEnabled();
	bool previousFileLog = ConfigManager::getBoolean(ConfigManager::LOG_TO_FILE);
	bool previousWarning = ConfigManager::getBoolean(ConfigManager::SLOW_TASK_WARNING);
	int64_t previousDump = ConfigManager::getInteger(ConfigManager::STATS_DUMP_INTERVAL);
	int64_t previousSlow = ConfigManager::getInteger(ConfigManager::STATS_SLOW_LOG_TIME);
	int64_t previousVerySlow = ConfigManager::getInteger(ConfigManager::STATS_VERY_SLOW_LOG_TIME);
	int64_t previousBudget = ConfigManager::getInteger(ConfigManager::REACTOR_TIME_BUDGET_MS);
	LuaScriptInterface interface{"Lua stats gate test"};
	lua_State* state = nullptr;
	int32_t trueEvent = -1;
	int32_t falseEvent = -1;
	int32_t voidEvent = -1;
	int32_t errorEvent = -1;
	int32_t slowBooleanEvent = -1;
	int32_t slowVoidEvent = -1;

	Fixture(bool enabled, bool running, bool warnings = false)
	{
		CHECK(!g_stats.isRunning());
		CHECK(!LuaScriptInterface::hasScriptEnv());
		CHECK(!isLoggerInitialized());
		CHECK(std::filesystem::create_directory(directory));
		std::filesystem::current_path(directory);
		ConfigManager::setBoolean(ConfigManager::LOG_TO_FILE, true);
		ConfigManager::setBoolean(ConfigManager::SLOW_TASK_WARNING, warnings);
		ConfigManager::setInteger(ConfigManager::STATS_DUMP_INTERVAL, 60);
		ConfigManager::setInteger(ConfigManager::STATS_SLOW_LOG_TIME, 0);
		ConfigManager::setInteger(ConfigManager::STATS_VERY_SLOW_LOG_TIME, 0);
		ConfigManager::setInteger(ConfigManager::REACTOR_TIME_BUDGET_MS, 1);
		g_stats.setEnabled(enabled);
		g_stats.prepareFileLogging();
		CHECK(g_stats.getFileLoggingStatus().available);
		if (warnings) CHECK(initLogger(LogLevel::WARNING, (directory / "warning.log").string(), 1024 * 1024, 1, true));
		CHECK(g_luaEnvironment.initState());
		CHECK(interface.initState());
		state = interface.getLuaState();
		lua_pushcfunction(state, pauseCallback);
		lua_setglobal(state, "gate_pause_callback");
		CHECK(luaL_dostring(state,
		                    "gate_total=0; "
		                    "function gate_return_true(value) return value == 3 end; "
		                    "function gate_return_false() return false end; "
		                    "function gate_void(value) gate_total=gate_total+value end; "
		                    "function gate_error() error('intentional callback failure') end; "
		                    "function gate_slow_boolean() gate_pause_callback(); return true end; "
		                    "function gate_slow_void() gate_pause_callback(); gate_total=gate_total+3 end") == LUA_OK);
		trueEvent = interface.getEvent("gate_return_true");
		falseEvent = interface.getEvent("gate_return_false");
		voidEvent = interface.getEvent("gate_void");
		errorEvent = interface.getEvent("gate_error");
		slowBooleanEvent = interface.getEvent("gate_slow_boolean");
		slowVoidEvent = interface.getEvent("gate_slow_void");
		CHECK(trueEvent >= 0 && falseEvent >= 0 && voidEvent >= 0 && errorEvent >= 0);
		CHECK(slowBooleanEvent >= 0 && slowVoidEvent >= 0);
		CHECK(lua_gettop(state) == 0);
		if (running) {
			g_stats.start();
			CHECK(g_stats.isRunning());
		}
	}

	void stopStats()
	{
		if (g_stats.isRunning()) g_stats.shutdown();
		g_stats.join();
		CHECK(!g_stats.isRunning());
	}

	~Fixture()
	{
		stopStats();
		shutdownLogger();
		while (LuaScriptInterface::hasScriptEnv()) LuaScriptInterface::resetScriptEnv();
		g_stats.setEnabled(previousEnabled);
		ConfigManager::setBoolean(ConfigManager::LOG_TO_FILE, previousFileLog);
		ConfigManager::setBoolean(ConfigManager::SLOW_TASK_WARNING, previousWarning);
		ConfigManager::setInteger(ConfigManager::STATS_DUMP_INTERVAL, previousDump);
		ConfigManager::setInteger(ConfigManager::STATS_SLOW_LOG_TIME, previousSlow);
		ConfigManager::setInteger(ConfigManager::STATS_VERY_SLOW_LOG_TIME, previousVerySlow);
		ConfigManager::setInteger(ConfigManager::REACTOR_TIME_BUDGET_MS, previousBudget);
		std::filesystem::current_path(previousDirectory);
		std::error_code ignored;
		std::filesystem::remove_all(directory, ignored);
	}

	bool invoke(int32_t event, bool withResult, bool argument = false, std::string timerOrigin = {})
	{
		const bool hadOuter = LuaScriptInterface::hasScriptEnv();
		ScriptEnvironment* outer = hadOuter ? LuaScriptInterface::getScriptEnv() : nullptr;
		CHECK(LuaScriptInterface::reserveScriptEnv());
		LuaScriptInterface::getScriptEnv()->setScriptId(event, &interface);
		if (!timerOrigin.empty()) LuaScriptInterface::getScriptEnv()->setTimerEvent(std::move(timerOrigin));
		CHECK(interface.pushFunction(event));
		if (argument) lua_pushinteger(state, 3);
		bool result = false;
		if (withResult)
			result = interface.callFunction(argument ? 1 : 0);
		else
			interface.callVoidFunction(argument ? 1 : 0);
		CHECK(lua_gettop(state) == 0);
		CHECK(LuaScriptInterface::hasScriptEnv() == hadOuter);
		if (hadOuter) CHECK(LuaScriptInterface::getScriptEnv() == outer);
		return result;
	}

	void verifyBehavior()
	{
		for (int call = 0; call < 3; ++call) {
			CHECK(invoke(trueEvent, true, true));
			CHECK(!invoke(falseEvent, true));
			invoke(voidEvent, false, true);
		}
		CHECK(!invoke(errorEvent, true));
		invoke(errorEvent, false);
		lua_getglobal(state, "gate_total");
		CHECK(lua_tointeger(state, -1) == 9);
		lua_pop(state, 1);
		CHECK(LuaScriptInterface::reserveScriptEnv());
		auto* outer = LuaScriptInterface::getScriptEnv();
		outer->setScriptId(trueEvent, &interface);
		CHECK(invoke(trueEvent, true, true));
		CHECK(outer->getScriptId() == trueEvent);
		LuaScriptInterface::resetScriptEnv();
		CHECK(!LuaScriptInterface::hasScriptEnv());
	}

	std::string statsText()
	{
		stopStats();
		return readFile(directory / "data/logs/stats/lua.log");
	}

	std::string warningText()
	{
		shutdownLogger();
		std::string result;
		for (const auto& entry : std::filesystem::directory_iterator(directory)) {
			if (entry.is_regular_file() && entry.path().extension() == ".log") result += readFile(entry.path());
		}
		return result;
	}
};
} // namespace

TEST_CASE(lua_callbacks_preserve_results_errors_and_nested_environments_with_monitor_off)
{
	Fixture fixture(false, false);
	fixture.verifyBehavior();
	CHECK(fixture.statsText().find("gate_return_true") == std::string::npos);
}

TEST_CASE(lua_callbacks_do_not_emit_stats_to_a_disabled_running_monitor)
{
	Fixture fixture(false, true);
	fixture.verifyBehavior();
	CHECK(fixture.statsText().find("gate_return_true") == std::string::npos);
}

TEST_CASE(lua_callbacks_preserve_behavior_when_stats_enabled_but_worker_stopped)
{
	Fixture fixture(true, false);
	fixture.verifyBehavior();
	CHECK(fixture.statsText().find("gate_return_true") == std::string::npos);
}

TEST_CASE(lua_callbacks_emit_stats_and_timer_origins_when_monitor_is_active)
{
	Fixture fixture(true, true);
	fixture.verifyBehavior();
	fixture.invoke(fixture.voidEvent, false, true, "data/events/timer_origin.lua:42");
	const auto stats = fixture.statsText();
#ifdef STATS_ENABLED
	CHECK(stats.find("gate_return_true") != std::string::npos);
	CHECK(stats.find("gate_void") != std::string::npos);
	CHECK(stats.find("data/events/timer_origin.lua:42") != std::string::npos);
#else
	CHECK(stats.find("gate_return_true") == std::string::npos);
#endif
}

TEST_CASE(lua_slow_callback_warnings_work_independently_of_stats_monitor)
{
	Fixture fixture(false, false, true);
	CHECK(fixture.invoke(fixture.slowBooleanEvent, true));
	fixture.invoke(fixture.slowVoidEvent, false, false, "data/events/slow_timer.lua:9");
	const auto warnings = fixture.warningText();
	CHECK(warnings.find("Slow Lua callback detected") != std::string::npos);
	CHECK(warnings.find("gate_slow_boolean") != std::string::npos);
	CHECK(warnings.find("data/events/slow_timer.lua:9") != std::string::npos);
	CHECK(fixture.statsText().find("gate_slow_boolean") == std::string::npos);
}

TFS_TEST_MAIN()
