// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "creatureevent.h"
#include "logger.h"
#include "luascript.h"
#include "script.h"

#include <fmt/format.h>

extern CreatureEvents* g_creatureEvents;
extern Scripts* g_scripts;

namespace {
using namespace Lua;

// CreatureEvent
int luaCreateCreatureEvent(lua_State* L)
{
	// CreatureEvent(eventName)
	if (LuaScriptInterface::getScriptEnv()->getScriptInterface() != &g_scripts->getScriptInterface()) {
		reportErrorFunc(L, "CreatureEvents can only be registered in the Scripts interface.");
		lua_pushnil(L);
		return 1;
	}

	CreatureEvent* creature = new CreatureEvent(LuaScriptInterface::getScriptEnv()->getScriptInterface());
	creature->setName(getString(L, 2));
	creature->fromLua = true;
	pushUserdata<CreatureEvent>(L, creature);
	setMetatable(L, -1, "CreatureEvent");
	return 1;
}

int luaCreatureEventType(lua_State* L)
{
	// creatureevent:type(callback)
	CreatureEvent* creature = getUserdata<CreatureEvent>(L, 1);
	if (creature) {
		std::string typeName = getString(L, 2);
		std::string tmpStr = boost::algorithm::to_lower_copy<std::string>(typeName);
		if (tmpStr == "login") {
			creature->setEventType(CREATURE_EVENT_LOGIN);
		} else if (tmpStr == "logout") {
			creature->setEventType(CREATURE_EVENT_LOGOUT);
		} else if (tmpStr == "reconnect") {
			creature->setEventType(CREATURE_EVENT_RECONNECT);
		} else if (tmpStr == "think") {
			creature->setEventType(CREATURE_EVENT_THINK);
		} else if (tmpStr == "preparedeath") {
			creature->setEventType(CREATURE_EVENT_PREPAREDEATH);
		} else if (tmpStr == "death") {
			creature->setEventType(CREATURE_EVENT_DEATH);
		} else if (tmpStr == "kill") {
			creature->setEventType(CREATURE_EVENT_KILL);
		} else if (tmpStr == "advance") {
			creature->setEventType(CREATURE_EVENT_ADVANCE);
		} else if (tmpStr == "modalwindow") {
			creature->setEventType(CREATURE_EVENT_MODALWINDOW);
		} else if (tmpStr == "textedit") {
			creature->setEventType(CREATURE_EVENT_TEXTEDIT);
		} else if (tmpStr == "healthchange") {
			creature->setEventType(CREATURE_EVENT_HEALTHCHANGE);
		} else if (tmpStr == "manachange") {
			creature->setEventType(CREATURE_EVENT_MANACHANGE);
		} else if (tmpStr == "extendedopcode") {
			creature->setEventType(CREATURE_EVENT_EXTENDED_OPCODE);
		} else {
			LOG_ERROR(fmt::format("[Error - CreatureEvent::configureLuaEvent] Invalid type for creature event: {}",
			                      typeName));
			pushBoolean(L, false);
		}
		creature->setLoaded(true);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureEventRegister(lua_State* L)
{
	// creatureevent:register()
	CreatureEvent** creaturePtr = getRawUserdata<CreatureEvent>(L, 1);
	if (auto creature = *creaturePtr) {
		if (!creature->isScripted()) {
			pushBoolean(L, false);
			return 1;
		}

		pushBoolean(L, g_creatureEvents->registerLuaEvent(creature));
		*creaturePtr = nullptr;
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureEventOnCallback(lua_State* L)
{
	// creatureevent:onLogin / logout / etc. (callback)
	CreatureEvent* creature = getUserdata<CreatureEvent>(L, 1);
	if (creature) {
		if (!creature->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}
} // namespace

void LuaScriptInterface::registerCreatureEvents()
{
	// CreatureEvent
	registerClass("CreatureEvent", "", luaCreateCreatureEvent);

	registerMethod("CreatureEvent", "type", luaCreatureEventType);
	registerMethod("CreatureEvent", "register", luaCreatureEventRegister);
	registerMethod("CreatureEvent", "onLogin", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onLogout", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onReconnect", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onThink", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onPrepareDeath", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onDeath", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onKill", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onAdvance", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onModalWindow", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onTextEdit", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onHealthChange", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onManaChange", luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onExtendedOpcode", luaCreatureEventOnCallback);
}
