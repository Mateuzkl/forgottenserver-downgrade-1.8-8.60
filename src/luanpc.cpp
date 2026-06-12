// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "game.h"
#include "luascript.h"
#include "npc.h"

extern Game g_game;

namespace {
using namespace Lua;

// Npc
int luaNpcCreate(lua_State* L)
{
	// Npc([id or name or userdata])
	Npc* npc;
	if (lua_gettop(L) >= 2) {
		if (isInteger(L, 2)) {
			npc = g_game.getNpcByID(getInteger<uint32_t>(L, 2));
		} else if (isString(L, 2)) {
			npc = g_game.getNpcByName(getString(L, 2));
		} else if (isUserdata(L, 2)) {
			npc = getUserdata<Npc>(L, 2);
		} else {
			npc = nullptr;
		}
	} else {
		npc = LuaScriptInterface::getScriptEnv()->getNpc();
	}

	if (npc) {
		pushUserdata<Npc>(L, npc);
		setCreatureMetatable(L, -1, npc);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNpcIsNpc(lua_State* L)
{
	// npc:isNpc()
	pushBoolean(L, getUserdata<const Npc>(L, 1) != nullptr);
	return 1;
}

int luaNpcSetMasterPos(lua_State* L)
{
	// npc:setMasterPos(pos[, radius])
	Npc* npc = getUserdata<Npc>(L, 1);
	if (!npc) {
		lua_pushnil(L);
		return 1;
	}

	const Position& pos = getPosition(L, 2);
	int32_t radius = getInteger<int32_t>(L, 3, 1);
	npc->setMasterPos(pos, radius);
	pushBoolean(L, true);
	return 1;
}

int luaNpcGetSpectators(lua_State* L)
{
	// npc:getSpectators()
	Npc* npc = getUserdata<Npc>(L, 1);
	if (!npc) {
		lua_pushnil(L);
		return 1;
	}

	const auto& spectators = npc->getSpectators();
	lua_createtable(L, spectators.size(), 0);

	int index = 0;
	for (const auto& spectatorPlayer : npc->getSpectators()) {
		pushUserdata<const Player>(L, spectatorPlayer.get());
		setCreatureMetatable(L, -1, spectatorPlayer.get());
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaNpcGetSpeechBubble(lua_State* L)
{
	// npc:getSpeechBubble()
	Npc* npc = getUserdata<Npc>(L, 1);
	if (npc) {
		lua_pushinteger(L, npc->getSpeechBubble());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNpcSetSpeechBubble(lua_State* L)
{
	// npc:setSpeechBubble(speechBubble)
	Npc* npc = getUserdata<Npc>(L, 1);
	if (!npc) {
		lua_pushnil(L);
		return 1;
	}
	
	uint8_t bubbleType = getInteger<uint8_t>(L, 2);
	
	if (bubbleType > SPEECHBUBBLE_QUESTTRADER && bubbleType != SPEECHBUBBLE_HIRELING) {
		reportErrorFunc(L, "Invalid speech bubble type. Valid values: 0-4 or 7 (SPEECHBUBBLE_HIRELING)");
		pushBoolean(L, false);
		return 1;
	}
	
	npc->setSpeechBubble(bubbleType);
	pushBoolean(L, true);
	return 1;
}
} // namespace

void LuaScriptInterface::registerNpc()
{
	// Npc
	registerClass("Npc", "Creature", luaNpcCreate);
	registerMetaMethod("Npc", "__eq", LuaScriptInterface::luaUserdataCompare);
	registerMetaMethod("Npc", "__gc", LuaScriptInterface::luaCreatureGC);

	registerMethod("Npc", "isNpc", luaNpcIsNpc);

	registerMethod("Npc", "setMasterPos", luaNpcSetMasterPos);

	registerMethod("Npc", "getSpectators", luaNpcGetSpectators);

	registerMethod("Npc", "getSpeechBubble", luaNpcGetSpeechBubble);
	registerMethod("Npc", "setSpeechBubble", luaNpcSetSpeechBubble);
}

// ─── NpcType ─────────────────────────────────────────────────────────────────

namespace {
using namespace Lua;

int luaNpcTypeCreate(lua_State* L)
{
	// NpcType(name)
	const std::string& name = getString(L, 1);
	auto npcType = Npcs::getNpcType(name);
	if (npcType) {
		pushUserdata<NpcType>(L, npcType.get());
		setMetatable(L, -1, "NpcType");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNpcTypeName(lua_State* L)
{
	// get: npcType:name() set: npcType:name(name)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		pushString(L, npcType->name);
	} else {
		npcType->name = getString(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeEventType(lua_State* L)
{
	// get: npcType:eventType() set: npcType:eventType(type)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		pushString(L, npcType->eventType);
	} else {
		npcType->eventType = getString(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeSpeechBubble(lua_State* L)
{
	// get: npcType:speechBubble() set: npcType:speechBubble(speechBubble)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->speechBubble);
	} else {
		npcType->speechBubble = getInteger<uint8_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeWalkTicks(lua_State* L)
{
	// get: npcType:walkInterval() set: npcType:walkInterval(interval)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->walkTicks);
	} else {
		npcType->walkTicks = getInteger<uint32_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeBaseSpeed(lua_State* L)
{
	// get: npcType:walkSpeed() set: npcType:walkSpeed(speed)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->baseSpeed);
	} else {
		npcType->baseSpeed = getInteger<uint32_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeMasterRadius(lua_State* L)
{
	// get: npcType:spawnRadius() set: npcType:spawnRadius(radius)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->masterRadius);
	} else {
		npcType->masterRadius = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeFloorChange(lua_State* L)
{
	// get: npcType:floorChange() set: npcType:floorChange(bool)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		pushBoolean(L, npcType->floorChange);
	} else {
		npcType->floorChange = getBoolean(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeAttackable(lua_State* L)
{
	// get: npcType:attackable() set: npcType:attackable(bool)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		pushBoolean(L, npcType->attackable);
	} else {
		npcType->attackable = getBoolean(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeIgnoreHeight(lua_State* L)
{
	// get: npcType:ignoreHeight() set: npcType:ignoreHeight(bool)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		pushBoolean(L, npcType->ignoreHeight);
	} else {
		npcType->ignoreHeight = getBoolean(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeIsIdle(lua_State* L)
{
	// get: npcType:isIdle() set: npcType:isIdle(bool)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		pushBoolean(L, npcType->isIdle);
	} else {
		npcType->isIdle = getBoolean(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypePushable(lua_State* L)
{
	// get: npcType:pushable() set: npcType:pushable(bool)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		pushBoolean(L, npcType->pushable);
	} else {
		npcType->pushable = getBoolean(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeDefaultOutfit(lua_State* L)
{
	// get: npcType:outfit() set: npcType:outfit(outfit)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		pushOutfit(L, npcType->defaultOutfit);
	} else {
		npcType->defaultOutfit = getOutfit(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeParameter(lua_State* L)
{
	// get: npcType:parameters() or npcType:parameters(key) set: npcType:parameters(key, value)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	int n = lua_gettop(L);
	if (n == 1) {
		// return all parameters as table
		lua_createtable(L, 0, npcType->parameters.size());
		for (const auto& [key, value] : npcType->parameters) {
			pushString(L, value);
			lua_setfield(L, -2, key.c_str());
		}
	} else if (n == 2) {
		// get single parameter
		const std::string& key = getString(L, 2);
		auto it = npcType->parameters.find(key);
		if (it != npcType->parameters.end()) {
			pushString(L, it->second);
		} else {
			lua_pushnil(L);
		}
	} else {
		// set parameter
		const std::string& key = getString(L, 2);
		const std::string& value = getString(L, 3);
		npcType->parameters[key] = value;
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeHealth(lua_State* L)
{
	// get: npcType:health() set: npcType:health(health)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->health);
	} else {
		npcType->health = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeMaxHealth(lua_State* L)
{
	// get: npcType:maxHealth() set: npcType:maxHealth(maxHealth)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->healthMax);
	} else {
		npcType->healthMax = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeEmblem(lua_State* L)
{
	// get: npcType:emblem() set: npcType:emblem(emblem)
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->emblem);
	} else {
		npcType->emblem = getInteger<GuildEmblems_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeOnCallback(lua_State* L)
{
	// npcType:onSay(callback) / onAppear / onDisappear / onMove / onPlayerCloseChannel / onPlayerEndTrade / onThink
	auto* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		lua_pushnil(L);
		return 1;
	}

	if (!npcType->loadCallback(Npcs::getScriptInterface())) {
		pushBoolean(L, false);
		return 1;
	}
	pushBoolean(L, true);
	return 1;
}

} // namespace

void LuaScriptInterface::registerNpcType()
{
	// NpcType
	registerClass("NpcType", "", luaNpcTypeCreate);
	registerMetaMethod("NpcType", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("NpcType", "name", luaNpcTypeName);
	registerMethod("NpcType", "eventType", luaNpcTypeEventType);
	registerMethod("NpcType", "speechBubble", luaNpcTypeSpeechBubble);
	registerMethod("NpcType", "walkInterval", luaNpcTypeWalkTicks);
	registerMethod("NpcType", "walkSpeed", luaNpcTypeBaseSpeed);
	registerMethod("NpcType", "spawnRadius", luaNpcTypeMasterRadius);
	registerMethod("NpcType", "floorChange", luaNpcTypeFloorChange);
	registerMethod("NpcType", "attackable", luaNpcTypeAttackable);
	registerMethod("NpcType", "ignoreHeight", luaNpcTypeIgnoreHeight);
	registerMethod("NpcType", "isIdle", luaNpcTypeIsIdle);
	registerMethod("NpcType", "pushable", luaNpcTypePushable);
	registerMethod("NpcType", "outfit", luaNpcTypeDefaultOutfit);
	registerMethod("NpcType", "parameters", luaNpcTypeParameter);
	registerMethod("NpcType", "health", luaNpcTypeHealth);
	registerMethod("NpcType", "maxHealth", luaNpcTypeMaxHealth);
	registerMethod("NpcType", "emblem", luaNpcTypeEmblem);

	registerMethod("NpcType", "onSay", luaNpcTypeOnCallback);
	registerMethod("NpcType", "onDisappear", luaNpcTypeOnCallback);
	registerMethod("NpcType", "onAppear", luaNpcTypeOnCallback);
	registerMethod("NpcType", "onMove", luaNpcTypeOnCallback);
	registerMethod("NpcType", "onPlayerCloseChannel", luaNpcTypeOnCallback);
	registerMethod("NpcType", "onPlayerEndTrade", luaNpcTypeOnCallback);
	registerMethod("NpcType", "onThink", luaNpcTypeOnCallback);
}
