// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "globalevent.h"
#include "logger.h"
#include "luascript.h"
#include "script.h"
#include "tools.h"

#include <fmt/format.h>

extern GlobalEvents* g_globalEvents;
extern Scripts* g_scripts;

namespace {
using namespace Lua;

// GlobalEvent
int luaCreateGlobalEvent(lua_State* L)
{
	// GlobalEvent(eventName)
	if (LuaScriptInterface::getScriptEnv()->getScriptInterface() != &g_scripts->getScriptInterface()) {
		reportErrorFunc(L, "GlobalEvents can only be registered in the Scripts interface.");
		lua_pushnil(L);
		return 1;
	}

	GlobalEvent* global = new GlobalEvent(LuaScriptInterface::getScriptEnv()->getScriptInterface());
	global->setName(getString(L, 2));
	global->setEventType(GLOBALEVENT_NONE);
	global->fromLua = true;
	pushUserdata<GlobalEvent>(L, global);
	setMetatable(L, -1, "GlobalEvent");
	return 1;
}

int luaGlobalEventType(lua_State* L)
{
	// globalevent:type(callback)
	GlobalEvent* global = getUserdata<GlobalEvent>(L, 1);
	if (global) {
		std::string typeName = getString(L, 2);
		std::string tmpStr = boost::algorithm::to_lower_copy<std::string>(typeName);
		if (tmpStr == "startup") {
			global->setEventType(GLOBALEVENT_STARTUP);
		} else if (tmpStr == "shutdown") {
			global->setEventType(GLOBALEVENT_SHUTDOWN);
		} else if (tmpStr == "record") {
			global->setEventType(GLOBALEVENT_RECORD);
		} else if (tmpStr == "save") {
			global->setEventType(GLOBALEVENT_SAVE);
		} else {
			LOG_ERROR(
			    fmt::format("[Error - CreatureEvent::configureLuaEvent] Invalid type for global event: {}", typeName));
			pushBoolean(L, false);
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGlobalEventRegister(lua_State* L)
{
	// globalevent:register()
	GlobalEvent** globaleventPtr = getRawUserdata<GlobalEvent>(L, 1);
	if (auto globalevent = *globaleventPtr) {
		if (!globalevent->isScripted()) {
			pushBoolean(L, false);
			return 1;
		}

		if (globalevent->getEventType() == GLOBALEVENT_NONE && globalevent->getInterval() == 0) {
			LOG_ERROR(fmt::format("[Error - luaGlobalEventRegister] No interval for globalevent with name {}",
			                      globalevent->getName()));
			pushBoolean(L, false);
			return 1;
		}

		pushBoolean(L, g_globalEvents->registerLuaEvent(globalevent));
		*globaleventPtr = nullptr;
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGlobalEventOnCallback(lua_State* L)
{
	// globalevent:onThink / record / etc. (callback)
	GlobalEvent* globalevent = getUserdata<GlobalEvent>(L, 1);
	if (globalevent) {
		if (!globalevent->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGlobalEventTime(lua_State* L)
{
	// globalevent:time(time)
	GlobalEvent* globalevent = getUserdata<GlobalEvent>(L, 1);
	if (globalevent) {
		std::string timer = getString(L, 2);
		std::vector<int32_t> params = vectorAtoi(explodeString(timer, ":"));

		int32_t hour = params.front();
		if (hour < 0 || hour > 23) {
			LOG_ERROR(
			    fmt::format("[Error - GlobalEvent::configureEvent] Invalid hour \"{}\" for globalevent with name: {}",
			                timer, globalevent->getName()));
			pushBoolean(L, false);
			return 1;
		}

		globalevent->setInterval(hour << 16);

		int32_t min = 0;
		int32_t sec = 0;
		if (params.size() > 1) {
			min = params[1];
			if (min < 0 || min > 59) {
				LOG_ERROR(fmt::format(
				    "[Error - GlobalEvent::configureEvent] Invalid minute \"{}\" for globalevent with name: {}", timer,
				    globalevent->getName()));
				pushBoolean(L, false);
				return 1;
			}

			if (params.size() > 2) {
				sec = params[2];
				if (sec < 0 || sec > 59) {
					LOG_ERROR(fmt::format(
					    "[Error - GlobalEvent::configureEvent] Invalid second \"{}\" for globalevent with name: {}",
					    timer, globalevent->getName()));
					pushBoolean(L, false);
					return 1;
				}
			}
		}

		time_t current_time = time(nullptr);
		std::tm timeinfo;
#if defined(_WIN32)
		localtime_s(&timeinfo, &current_time);
#else
		localtime_r(&current_time, &timeinfo);
#endif
		timeinfo.tm_hour = hour;
		timeinfo.tm_min = min;
		timeinfo.tm_sec = sec;

		time_t difference = static_cast<time_t>(difftime(mktime(&timeinfo), current_time));
		if (difference < 0) {
			difference += 86400;
		}

		globalevent->setNextExecution(OTSYS_TIME() + (difference * 1000));
		globalevent->setEventType(GLOBALEVENT_TIMER);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGlobalEventInterval(lua_State* L)
{
	// globalevent:interval(interval)
	GlobalEvent* globalevent = getUserdata<GlobalEvent>(L, 1);
	if (globalevent) {
		auto interval = getInteger<uint32_t>(L, 2);
		globalevent->setInterval(interval);
		globalevent->setNextExecution(OTSYS_TIME() + interval);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}
} // namespace

void LuaScriptInterface::registerGlobalEvents()
{
	// GlobalEvent
	registerClass("GlobalEvent", "", luaCreateGlobalEvent);

	registerMethod("GlobalEvent", "type", luaGlobalEventType);
	registerMethod("GlobalEvent", "register", luaGlobalEventRegister);
	registerMethod("GlobalEvent", "time", luaGlobalEventTime);
	registerMethod("GlobalEvent", "interval", luaGlobalEventInterval);
	registerMethod("GlobalEvent", "onThink", luaGlobalEventOnCallback);
	registerMethod("GlobalEvent", "onTime", luaGlobalEventOnCallback);
	registerMethod("GlobalEvent", "onStartup", luaGlobalEventOnCallback);
	registerMethod("GlobalEvent", "onShutdown", luaGlobalEventOnCallback);
	registerMethod("GlobalEvent", "onRecord", luaGlobalEventOnCallback);
	registerMethod("GlobalEvent", "onSave", luaGlobalEventOnCallback);
}
