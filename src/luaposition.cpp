// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "game.h"
#include "luascript.h"
#include "zones.h"

extern Game g_game;

namespace {
using namespace Lua;

uint32_t getPositionInstanceId(lua_State* L, int32_t arg)
{
	lua_getfield(L, arg, "instanceId");
	uint32_t instanceId = 0;
	if (!lua_isnil(L, -1)) {
		instanceId = getInteger<uint32_t>(L, -1);
	}
	lua_pop(L, 1);
	return instanceId;
}

bool hasAudienceArgument(lua_State* L, int32_t arg)
{
	if (lua_gettop(L) < arg || lua_isnil(L, arg)) {
		return false;
	}
	if (lua_isboolean(L, arg) && !getBoolean(L, arg)) {
		return false;
	}
	return true;
}

// Position
int luaPositionCreate(lua_State* L)
{
	// Position([x = 0[, y = 0[, z = 0[, stackpos = 0[, instanceId = 0]]]]])
	// Position([position])
	if (lua_gettop(L) <= 1) {
		pushPosition(L, Position());
		return 1;
	}

	int32_t stackpos;
	uint32_t instanceId = 0;
	if (isTable(L, 2)) {
		const Position& position = getPosition(L, 2, stackpos);
		instanceId = getPositionInstanceId(L, 2);
		pushPosition(L, position, stackpos, instanceId);
	} else {
		uint16_t x = getInteger<uint16_t>(L, 2, 0);
		uint16_t y = getInteger<uint16_t>(L, 3, 0);
		uint8_t z = getInteger<uint8_t>(L, 4, 0);
		stackpos = getInteger<int32_t>(L, 5, 0);
		instanceId = getInteger<uint32_t>(L, 6, 0);

		pushPosition(L, Position(x, y, z), stackpos, instanceId);
	}
	return 1;
}

int luaPositionCompare(lua_State* L)
{
	// position == positionEx
	const Position& positionEx = getPosition(L, 2);
	const Position& position = getPosition(L, 1);
	pushBoolean(L, position == positionEx);
	return 1;
}

int luaPositionIsSightClear(lua_State* L)
{
	// position:isSightClear(positionEx[, sameFloor = true])
	bool sameFloor = getBoolean(L, 3, true);
	const Position& positionEx = getPosition(L, 2);
	const Position& position = getPosition(L, 1);
	pushBoolean(L, g_game.isSightClear(position, positionEx, sameFloor));
	return 1;
}

int luaPositionSendMagicEffect(lua_State* L)
{
	// position:sendMagicEffect(magicEffect[, players = {}|instanceId])
	SpectatorVec spectators;
	const bool hasExplicitAudience = hasAudienceArgument(L, 3);
	uint32_t instanceId = hasExplicitAudience ? 0 : getPositionInstanceId(L, 1);
	if (lua_isnumber(L, 3)) {
		instanceId = getInteger<uint32_t>(L, 3);
	} else if (hasExplicitAudience) {
		getSpectators<Player>(L, 3, spectators);
	}

	MagicEffectClasses magicEffect = getInteger<MagicEffectClasses>(L, 2);

	if (magicEffect == CONST_ME_NONE) {
		pushBoolean(L, false);
		return 1;
	}

	const Position& position = getPosition(L, 1);
	if (!spectators.empty()) {
		Game::addMagicEffect(spectators, position, magicEffect);
	} else {
		g_game.addMagicEffect(position, magicEffect, instanceId);
	}

	pushBoolean(L, true);
	return 1;
}

int luaPositionSendDistanceEffect(lua_State* L)
{
	// position:sendDistanceEffect(positionEx, distanceEffect[, players = {}|instanceId])
	SpectatorVec spectators;
	const bool hasExplicitAudience = hasAudienceArgument(L, 4);
	uint32_t instanceId = hasExplicitAudience ? 0 : getPositionInstanceId(L, 1);
	if (lua_isnumber(L, 4)) {
		instanceId = getInteger<uint32_t>(L, 4);
	} else if (hasExplicitAudience) {
		getSpectators<Player>(L, 4, spectators);
	}

	ShootType_t distanceEffect = getInteger<ShootType_t>(L, 3);
	const Position& positionEx = getPosition(L, 2);
	const Position& position = getPosition(L, 1);
	if (!spectators.empty()) {
		Game::addDistanceEffect(spectators, position, positionEx, distanceEffect);
	} else {
		g_game.addDistanceEffect(position, positionEx, distanceEffect, instanceId);
	}

	pushBoolean(L, true);
	return 1;
}

int luaPositionGetZones(lua_State* L)
{
	// position:getZones()
	const auto zoneIds = Zones::getZonesByPosition(getPosition(L, 1));
	lua_createtable(L, static_cast<int>(zoneIds.size()), 0);

	int index = 0;
	for (uint16_t zoneId : zoneIds) {
		lua_pushinteger(L, zoneId);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaPositionHasZone(lua_State* L)
{
	// position:hasZone([zoneId | zone])
	const auto zoneIds = Zones::getZonesByPosition(getPosition(L, 1));
	if (lua_gettop(L) < 2) {
		pushBoolean(L, !zoneIds.empty());
		return 1;
	}

	uint16_t expectedZoneId = 0;
	if (isNumber(L, 2)) {
		expectedZoneId = getInteger<uint16_t>(L, 2);
	} else if (isType<Zone>(L, 2)) {
		const auto zone = getSharedPtr<Zone>(L, 2);
		if (zone) {
			expectedZoneId = zone->getId();
		}
	}

	pushBoolean(L, expectedZoneId != 0 &&
	                      std::find(zoneIds.begin(), zoneIds.end(), expectedZoneId) != zoneIds.end());
	return 1;
}
} // namespace

void LuaScriptInterface::registerPosition()
{
	// Position
	registerClass("Position", "", luaPositionCreate);
	registerMetaMethod("Position", "__eq", luaPositionCompare);

	registerMethod("Position", "isSightClear", luaPositionIsSightClear);

	registerMethod("Position", "sendMagicEffect", luaPositionSendMagicEffect);
	registerMethod("Position", "sendDistanceEffect", luaPositionSendDistanceEffect);
	registerMethod("Position", "getZones", luaPositionGetZones);
	registerMethod("Position", "hasZone", luaPositionHasZone);
}
