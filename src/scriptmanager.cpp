// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "scriptmanager.h"

#include "actions.h"
#include "chat.h"
#include "events.h"
#include "globalevent.h"
#include "logger.h"
#include "movement.h"
#include "script.h"
#include "spells.h"
#include "talkaction.h"
#include "weapons.h"

Actions* g_actions = nullptr;
CreatureEvents* g_creatureEvents = nullptr;
Chat* g_chat = nullptr;
Events* g_events = nullptr;
GlobalEvents* g_globalEvents = nullptr;
Spells* g_spells = nullptr;
TalkActions* g_talkActions = nullptr;
MoveEvents* g_moveEvents = nullptr;
Weapons* g_weapons = nullptr;
Scripts* g_scripts = nullptr;

extern LuaEnvironment g_luaEnvironment;

ScriptingManager::~ScriptingManager()
{
	delete g_events;
	delete g_weapons;
	delete g_spells;
	delete g_actions;
	delete g_talkActions;
	delete g_moveEvents;
	delete g_chat;
	delete g_creatureEvents;
	delete g_globalEvents;
	delete g_scripts;
}

bool ScriptingManager::loadPreItems()
{
	// Ensure g_luaEnvironment is properly initialized
	if (!g_luaEnvironment.getLuaState()) {
		if (!g_luaEnvironment.initState()) {
			LOG_ERROR("> ERROR: Failed to initialize Lua environment!");
			return false;
		}
	}

	if (!g_weapons) g_weapons = new Weapons();
	if (!g_moveEvents) g_moveEvents = new MoveEvents();

	return true;
}

bool ScriptingManager::loadScriptSystems()
{
	// Ensure g_luaEnvironment is properly initialized
	if (!g_luaEnvironment.getLuaState()) {
		if (!g_luaEnvironment.initState()) {
			LOG_ERROR("> ERROR: Failed to initialize Lua environment!");
			return false;
		}
	}

	if (g_luaEnvironment.loadFile("data/global.lua") == -1) {
		LOG_WARN("[Warning - ScriptingManager::loadScriptSystems] Can not load data/global.lua");
	}

	g_scripts = new Scripts();
	LOG_INFO(">> Loading lua libs");
	if (!g_scripts->loadScripts("scripts/lib", true, false)) {
		LOG_ERROR("> ERROR: Unable to load lua libs!");
		return false;
	}

	g_chat = new Chat();

	if (!g_scripts->loadScripts("items", false, false)) {
		LOG_ERROR("> ERROR: Unable to load items (LUA)!");
		return false;
	}

	if (!g_weapons) g_weapons = new Weapons();
	g_weapons->loadDefaults();
	g_spells = new Spells();
	g_actions = new Actions();
	g_talkActions = new TalkActions();
	if (!g_moveEvents) g_moveEvents = new MoveEvents();
	g_creatureEvents = new CreatureEvents();
	g_globalEvents = new GlobalEvents();

	g_events = new Events();
	if (!g_events->load()) {
		LOG_ERROR("> ERROR: Unable to load events!");
		return false;
	}

	return true;
}
