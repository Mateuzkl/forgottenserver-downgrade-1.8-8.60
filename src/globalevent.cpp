// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "globalevent.h"

#include "configmanager.h"
#include "pugicast.h"
#include "scheduler.h"
#include "tools.h"
#include "logger.h"
#include <fmt/format.h>

GlobalEvents::GlobalEvents() : scriptInterface("GlobalEvent Interface") { scriptInterface.initState(); }

GlobalEvents::~GlobalEvents() { clear(false); }

void GlobalEvents::clearMap(GlobalEventMap& map, bool fromLua)
{
	std::erase_if(map, [fromLua](const auto& entry) { return fromLua == entry.second.fromLua; });
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

	LOG_WARN(fmt::format("[Warning - GlobalEvents::configureEvent] Duplicate registered globalevent with name: {}", globalEvent->getName()));
	return false;
}

void GlobalEvents::startup() const { execute(GLOBALEVENT_STARTUP); }
void GlobalEvents::shutdown() const { execute(GLOBALEVENT_SHUTDOWN); }
void GlobalEvents::save() const { execute(GLOBALEVENT_SAVE); }
void GlobalEvents::periodChange(LightState_t period) const
{
	for (const auto& it : serverMap) {
		const GlobalEvent& globalEvent = it.second;
		if (globalEvent.getEventType() == GLOBALEVENT_PERIODCHANGE) {
			globalEvent.executePeriodChange(period);
		}
	}
}

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

        int64_t nextEventTime = globalEvent.getInterval();
        globalEvent.setNextExecution(now + nextEventTime);

        if (nextEventTime < nextScheduledTime) {
            nextScheduledTime = nextEventTime;
        }
    }

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
		case GLOBALEVENT_SAVE:
		case GLOBALEVENT_PERIODCHANGE: {
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
		case GLOBALEVENT_PERIODCHANGE:
			return "onPeriodChange";
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

bool GlobalEvent::executePeriodChange(LightState_t period) const
{
	// onPeriodChange(period)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - GlobalEvent::executePeriodChange] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(scriptId);

	lua_pushinteger(L, period);
	return scriptInterface->callFunction(1);
}
