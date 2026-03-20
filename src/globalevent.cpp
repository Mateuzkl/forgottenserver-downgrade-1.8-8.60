// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "globalevent.h"

#include "configmanager.h"
#include "logger.h"
#include "pugicast.h"
#include "scheduler.h"
#include "tools.h"

#include <fmt/format.h>

GlobalEvents::GlobalEvents() : scriptInterface("GlobalEvent Interface") { scriptInterface.initState(); }

GlobalEvents::~GlobalEvents() { clear(false); }

void GlobalEvents::clearMap(GlobalEventMap& map, bool fromLua)
{
	for (auto it = map.begin(); it != map.end();) {
		if (fromLua == it->second.fromLua) {
			it = map.erase(it);
		} else {
			++it;
		}
	}
}

void GlobalEvents::clear(bool fromLua)
{
	g_scheduler.stopEvent(thinkEventId);
	thinkEventId = 0;
	g_scheduler.stopEvent(timerEventId);
	timerEventId = 0;

	clearMap(thinkMap, fromLua);
	clearMap(serverMap, fromLua);
	clearMap(timerMap, fromLua);

	reInitState(fromLua);
}

Event_ptr GlobalEvents::getEvent(std::string_view nodeName)
{
	if (!caseInsensitiveEqual(nodeName, "globalevent")) {
		return nullptr;
	}
	return Event_ptr(new GlobalEvent(&scriptInterface));
}

bool GlobalEvents::registerEvent(Event_ptr event, const pugi::xml_node&)
{
	GlobalEvent_ptr globalEvent{static_cast<GlobalEvent*>(event.release())}; // event is guaranteed to be a GlobalEvent
	if (globalEvent->getEventType() == GLOBALEVENT_TIMER) {
		auto result = timerMap.emplace(globalEvent->getName(), std::move(*globalEvent));
		if (result.second) {
			if (timerEventId == 0) {
				timerEventId = g_scheduler.addEvent(createSchedulerTask(SCHEDULER_MINTICKS, [this]() { timer(); }));
			}
			return true;
		}
	} else if (globalEvent->getEventType() != GLOBALEVENT_NONE) {
		auto result = serverMap.emplace(globalEvent->getName(), std::move(*globalEvent));
		if (result.second) {
			return true;
		}
	} else { // think event
		auto result = thinkMap.emplace(globalEvent->getName(), std::move(*globalEvent));
		if (result.second) {
			if (thinkEventId == 0) {
				thinkEventId = g_scheduler.addEvent(createSchedulerTask(SCHEDULER_MINTICKS, [this]() { think(); }));
			}
			return true;
		}
	}

	LOG_WARN(fmt::format("[Warning - GlobalEvents::configureEvent] Duplicate registered globalevent with name: {}",
	                     globalEvent->getName()));
	return false;
}

bool GlobalEvents::registerLuaEvent(GlobalEvent* event)
{
	GlobalEvent_ptr globalEvent{event};
	if (globalEvent->getEventType() == GLOBALEVENT_TIMER) {
		auto result = timerMap.emplace(globalEvent->getName(), std::move(*globalEvent));
		if (result.second) {
			if (timerEventId == 0) {
				timerEventId = g_scheduler.addEvent(createSchedulerTask(SCHEDULER_MINTICKS, [this]() { timer(); }));
			}
			return true;
		}
	} else if (globalEvent->getEventType() != GLOBALEVENT_NONE) {
		auto result = serverMap.emplace(globalEvent->getName(), std::move(*globalEvent));
		if (result.second) {
			return true;
		}
	} else { // think event
		auto result = thinkMap.emplace(globalEvent->getName(), std::move(*globalEvent));
		if (result.second) {
			if (thinkEventId == 0) {
				thinkEventId = g_scheduler.addEvent(createSchedulerTask(SCHEDULER_MINTICKS, [this]() { think(); }));
			}
			return true;
		}
	}

	LOG_WARN(fmt::format("[Warning - GlobalEvents::configureEvent] Duplicate registered globalevent with name: {}",
	                     globalEvent->getName()));
	return false;
}

void GlobalEvents::startup() const { execute(GLOBALEVENT_STARTUP); }
void GlobalEvents::shutdown() const { execute(GLOBALEVENT_SHUTDOWN); }
void GlobalEvents::save() const { execute(GLOBALEVENT_SAVE); }

void GlobalEvents::timer()
{
	auto now = OTSYS_TIME();

	int64_t nextScheduledTime = std::numeric_limits<int64_t>::max();

	auto it = timerMap.begin();
	while (it != timerMap.end()) {
		GlobalEvent& globalEvent = it->second;

		int64_t nextExecutionTime = globalEvent.getNextExecution() - now;
		if (nextExecutionTime > 0) {
			if (nextExecutionTime < nextScheduledTime) {
				nextScheduledTime = nextExecutionTime;
			}

			++it;
			continue;
		}

		if (!globalEvent.executeEvent()) {
			it = timerMap.erase(it);
			continue;
		}

		nextScheduledTime = std::min<int64_t>(nextScheduledTime, 86400000);
		globalEvent.setNextExecution(now + 86400000);

		++it;
	}

	// FIX: Cancel previous event before scheduling a new one
	if (timerEventId != 0) {
		g_scheduler.stopEvent(timerEventId);
		timerEventId = 0;
	}

	if (nextScheduledTime != std::numeric_limits<int64_t>::max()) {
		timerEventId = g_scheduler.addEvent(createSchedulerTask(nextScheduledTime, [this]() { timer(); }));
	}
}

void GlobalEvents::think()
{
	auto now = OTSYS_TIME();

	int64_t nextScheduledTime = std::numeric_limits<int64_t>::max();
	for (auto& it : thinkMap) {
		GlobalEvent& globalEvent = it.second;

		int64_t nextExecutionTime = globalEvent.getNextExecution() - now;
		if (nextExecutionTime > 0) {
			if (nextExecutionTime < nextScheduledTime) {
				nextScheduledTime = nextExecutionTime;
			}
			continue;
		}

		if (!globalEvent.executeEvent()) {
			LOG_ERROR(fmt::format("[Error - GlobalEvents::think] Failed to execute event: {}", globalEvent.getName()));
		}

		nextExecutionTime = globalEvent.getInterval();
		nextExecutionTime = std::min<int64_t>(nextExecutionTime, nextScheduledTime);
		nextScheduledTime = nextExecutionTime;

		globalEvent.setNextExecution(now + nextExecutionTime);
	}

	// FIX: Cancel thinkEventId before scheduling a new one
	if (thinkEventId != 0) {
		g_scheduler.stopEvent(thinkEventId);
		thinkEventId = 0;
	}

	if (nextScheduledTime != std::numeric_limits<int64_t>::max()) {
		thinkEventId = g_scheduler.addEvent(createSchedulerTask(nextScheduledTime, [this]() { think(); }));
	}
}

void GlobalEvents::execute(GlobalEvent_t type) const
{
	for (const auto& it : serverMap) {
		const GlobalEvent& globalEvent = it.second;
		if (globalEvent.getEventType() == type) {
			globalEvent.executeEvent();
		}
	}
}

GlobalEventMap GlobalEvents::getEventMap(GlobalEvent_t type)
{
	// TODO: This should be better implemented. Maybe have a map for every type.
	switch (type) {
		case GLOBALEVENT_NONE:
			return thinkMap;
		case GLOBALEVENT_TIMER:
			return timerMap;
		case GLOBALEVENT_STARTUP:
		case GLOBALEVENT_SHUTDOWN:
		case GLOBALEVENT_RECORD:
		case GLOBALEVENT_SAVE: {
			GlobalEventMap retMap;
			for (const auto& it : serverMap) {
				if (it.second.getEventType() == type) {
					retMap.emplace(it.first, it.second);
				}
			}
			return retMap;
		}
		default:
			return GlobalEventMap();
	}
}

GlobalEvent::GlobalEvent(LuaScriptInterface* interface) : Event(interface) {}

bool GlobalEvent::configureEvent(const pugi::xml_node& node)
{
	pugi::xml_attribute nameAttribute = node.attribute("name");
	if (!nameAttribute) {
		LOG_ERROR("[Error - GlobalEvent::configureEvent] Missing name for a globalevent");
		return false;
	}

	name = nameAttribute.as_string();
	eventType = GLOBALEVENT_NONE;

	pugi::xml_attribute attr;
	if ((attr = node.attribute("time"))) {
		std::vector<int32_t> params = vectorAtoi(explodeString(attr.as_string(), ":"));

		int32_t hour = params.front();
		if (hour < 0 || hour > 23) {
			LOG_ERROR(
			    fmt::format("[Error - GlobalEvent::configureEvent] Invalid hour \"{}\" for globalevent with name: {}",
			                attr.as_string(), name));
			return false;
		}

		interval |= hour << 16;

		int32_t min = 0;
		int32_t sec = 0;
		if (params.size() > 1) {
			min = params[1];
			if (min < 0 || min > 59) {
				LOG_ERROR(fmt::format(
				    "[Error - GlobalEvent::configureEvent] Invalid minute \"{}\" for globalevent with name: {}",
				    attr.as_string(), name));
				return false;
			}

			if (params.size() > 2) {
				sec = params[2];
				if (sec < 0 || sec > 59) {
					LOG_ERROR(fmt::format(
					    "[Error - GlobalEvent::configureEvent] Invalid second \"{}\" for globalevent with name: {}",
					    attr.as_string(), name));
					return false;
				}
			}
		}

		time_t current_time = time(nullptr);
		struct tm timeinfo;
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

		nextExecution = OTSYS_TIME() + (difference * 1000);
		eventType = GLOBALEVENT_TIMER;
	} else if ((attr = node.attribute("type"))) {
		const char* value = attr.value();
		if (caseInsensitiveEqual(value, "startup")) {
			eventType = GLOBALEVENT_STARTUP;
		} else if (caseInsensitiveEqual(value, "shutdown")) {
			eventType = GLOBALEVENT_SHUTDOWN;
		} else if (caseInsensitiveEqual(value, "record")) {
			eventType = GLOBALEVENT_RECORD;
		} else if (caseInsensitiveEqual(value, "save")) {
			eventType = GLOBALEVENT_SAVE;
		} else {
			LOG_ERROR(
			    fmt::format("[Error - GlobalEvent::configureEvent] No valid type \"{}\" for globalevent with name {}",
			                attr.as_string(), name));
			return false;
		}
	} else if ((attr = node.attribute("interval"))) {
		interval = std::max<int32_t>(SCHEDULER_MINTICKS, pugi::cast<int32_t>(attr.value()));
		nextExecution = OTSYS_TIME() + interval;
	} else {
		LOG_ERROR(fmt::format("[Error - GlobalEvent::configureEvent] No interval for globalevent with name {}", name));
		return false;
	}
	return true;
}

std::string_view GlobalEvent::getScriptEventName() const
{
	switch (eventType) {
		case GLOBALEVENT_STARTUP:
			return "onStartup";
		case GLOBALEVENT_SHUTDOWN:
			return "onShutdown";
		case GLOBALEVENT_RECORD:
			return "onRecord";
		case GLOBALEVENT_SAVE:
			return "onSave";
		case GLOBALEVENT_TIMER:
			return "onTime";
		default:
			return "onThink";
	}
}

bool GlobalEvent::executeRecord(uint32_t current, uint32_t old)
{
	// onRecord(current, old)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - GlobalEvent::executeRecord] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(scriptId);

	lua_pushinteger(L, current);
	lua_pushinteger(L, old);
	return scriptInterface->callFunction(2);
}

bool GlobalEvent::executeEvent() const
{
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - GlobalEvent::executeEvent] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);
	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(scriptId);

	int32_t params = 0;
	if (eventType == GLOBALEVENT_NONE || eventType == GLOBALEVENT_TIMER) {
		lua_pushinteger(L, interval);
		params = 1;
	}

	return scriptInterface->callFunction(params);
}
