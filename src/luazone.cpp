// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "game.h"
#include "luascript.h"
#include "tile.h"
#include "zones.h"

extern Game g_game;

namespace {
using namespace Lua;

std::shared_ptr<Zone> getZoneHandle(lua_State* L, int32_t index)
{
	if (!isType<Zone>(L, index)) {
		return nullptr;
	}

	return getSharedPtr<Zone>(L, index);
}

bool hasTileFlags(const Tile* tile, uint32_t flags)
{
	return (tile->getFlags() & flags) == flags;
}

int luaGetZones(lua_State* L)
{
	// Zones()
	const auto zones = Zones::getAll();
	lua_createtable(L, static_cast<int>(zones.size()), 0);

	int index = 0;
	for (const auto& zone : zones) {
		pushSharedPtr(L, zone);
		setMetatable(L, -1, "Zone");
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaZoneCreate(lua_State* L)
{
	// Zone(id)
	// Zone(id, positions)
	const auto zoneId = getInteger<ZoneId>(L, 2);
	if (zoneId == 0) {
		lua_pushnil(L);
		return 1;
	}

	std::shared_ptr<Zone> zone;
	if (isTable(L, 3)) {
		std::vector<Position> positions;
		positions.reserve(static_cast<size_t>(lua_rawlen(L, 3)));

		lua_pushnil(L);
		while (lua_next(L, 3) != 0) {
			if (isTable(L, -1)) {
				positions.emplace_back(getPosition(L, -1));
			}
			lua_pop(L, 1);
		}

		zone = Zones::createZone(zoneId, std::move(positions));
	} else {
		zone = Zones::getZone(zoneId);
	}

	if (zone) {
		pushSharedPtr(L, zone);
		setMetatable(L, -1, "Zone");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaZoneDelete(lua_State* L)
{
	if (!isType<Zone>(L, 1)) {
		return 0;
	}

	auto& zone = getSharedPtr<Zone>(L, 1);
	if (zone) {
		zone.reset();
	}
	return 0;
}

int luaZoneGetId(lua_State* L)
{
	// zone:getId()
	if (const auto zone = getZoneHandle(L, 1)) {
		lua_pushinteger(L, zone->getId());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaZoneGetCreatures(lua_State* L)
{
	// zone:getCreatures([creatureType])
	const auto zone = getZoneHandle(L, 1);
	if (!zone) {
		lua_pushnil(L);
		return 1;
	}

	const bool isFiltered = isNumber(L, 2);
	CreatureType_t creatureType = CREATURETYPE_PLAYER;
	if (isFiltered) {
		creatureType = getInteger<CreatureType_t>(L, 2);
	}

	lua_createtable(L, static_cast<int>(zone->getPositions().size() * 4), 0);
	int index = 0;

	for (const Position& position : zone->getPositions()) {
		Tile* tile = g_game.map.getTile(position);
		if (!tile) {
			continue;
		}

		const auto creatures = tile->getCreatures();
		if (!creatures) {
			continue;
		}

		for (const auto& creature : *creatures) {
			if (isFiltered && creature->getType() != creatureType) {
				continue;
			}

			pushUserdata<Creature>(L, creature.get());
			setCreatureMetatable(L, -1, creature.get());
			lua_rawseti(L, -2, ++index);
		}
	}

	return 1;
}

int luaZoneGetGrounds(lua_State* L)
{
	// zone:getGrounds()
	const auto zone = getZoneHandle(L, 1);
	if (!zone) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, static_cast<int>(zone->getPositions().size()), 0);
	int index = 0;

	for (const Position& position : zone->getPositions()) {
		Tile* tile = g_game.map.getTile(position);
		if (!tile) {
			continue;
		}

		Item* ground = tile->getGround();
		if (!ground) {
			continue;
		}

		pushItem(L, ground);
		lua_rawseti(L, -2, ++index);
	}

	return 1;
}

int luaZoneGetItems(lua_State* L)
{
	// zone:getItems([itemId])
	const auto zone = getZoneHandle(L, 1);
	if (!zone) {
		lua_pushnil(L);
		return 1;
	}

	const bool isFiltered = isNumber(L, 2);
	const auto itemId = getInteger<uint16_t>(L, 2, 0);

	lua_createtable(L, static_cast<int>(zone->getPositions().size() * 8), 0);
	int index = 0;

	for (const Position& position : zone->getPositions()) {
		Tile* tile = g_game.map.getTile(position);
		if (!tile) {
			continue;
		}

		const auto items = tile->getItemList();
		if (!items) {
			continue;
		}

		for (const auto& item : *items) {
			if (isFiltered && item->getID() != itemId) {
				continue;
			}

			pushSharedPtr(L, item);
			setItemMetatable(L, -1, item.get());
			lua_rawseti(L, -2, ++index);
		}
	}

	return 1;
}

int luaZoneGetTiles(lua_State* L)
{
	// zone:getTiles([flags])
	const auto zone = getZoneHandle(L, 1);
	if (!zone) {
		lua_pushnil(L);
		return 1;
	}

	const bool isFiltered = isNumber(L, 2);
	const auto flags = getInteger<uint32_t>(L, 2, 0);

	lua_createtable(L, static_cast<int>(zone->getPositions().size()), 0);
	int index = 0;

	for (const Position& position : zone->getPositions()) {
		Tile* tile = g_game.map.getTile(position);
		if (!tile) {
			continue;
		}

		if (isFiltered && !hasTileFlags(tile, flags)) {
			continue;
		}

		pushUserdata<Tile>(L, tile);
		setMetatable(L, -1, "Tile");
		lua_rawseti(L, -2, ++index);
	}

	return 1;
}

int luaZoneGetCreatureCount(lua_State* L)
{
	// zone:getCreatureCount([creatureType])
	const auto zone = getZoneHandle(L, 1);
	if (!zone) {
		lua_pushnil(L);
		return 1;
	}

	const bool isFiltered = isNumber(L, 2);
	const auto creatureType = getInteger<CreatureType_t>(L, 2, CREATURETYPE_PLAYER);
	size_t count = 0;

	for (const Position& position : zone->getPositions()) {
		Tile* tile = g_game.map.getTile(position);
		if (!tile) {
			continue;
		}

		if (!isFiltered) {
			count += tile->getCreatureCount();
			continue;
		}

		const auto creatures = tile->getCreatures();
		if (!creatures) {
			continue;
		}

		for (const auto& creature : *creatures) {
			if (creature->getType() == creatureType) {
				++count;
			}
		}
	}

	lua_pushinteger(L, static_cast<lua_Integer>(count));
	return 1;
}

int luaZoneGetItemCount(lua_State* L)
{
	// zone:getItemCount([itemId])
	const auto zone = getZoneHandle(L, 1);
	if (!zone) {
		lua_pushnil(L);
		return 1;
	}

	const bool isFiltered = isNumber(L, 2);
	const auto itemId = getInteger<uint16_t>(L, 2, 0);
	size_t count = 0;

	for (const Position& position : zone->getPositions()) {
		Tile* tile = g_game.map.getTile(position);
		if (!tile) {
			continue;
		}

		if (!isFiltered) {
			count += tile->getItemCount();
			continue;
		}

		const auto items = tile->getItemList();
		if (!items) {
			continue;
		}

		for (const auto& item : *items) {
			if (item->getID() == itemId) {
				++count;
			}
		}
	}

	lua_pushinteger(L, static_cast<lua_Integer>(count));
	return 1;
}

int luaZoneGetTileCount(lua_State* L)
{
	// zone:getTileCount([flags])
	const auto zone = getZoneHandle(L, 1);
	if (!zone) {
		lua_pushnil(L);
		return 1;
	}

	const bool isFiltered = isNumber(L, 2);
	const auto flags = getInteger<uint32_t>(L, 2, 0);
	size_t count = 0;

	for (const Position& position : zone->getPositions()) {
		Tile* tile = g_game.map.getTile(position);
		if (!tile) {
			continue;
		}

		if (isFiltered && !hasTileFlags(tile, flags)) {
			continue;
		}

		++count;
	}

	lua_pushinteger(L, static_cast<lua_Integer>(count));
	return 1;
}
} // namespace

void LuaScriptInterface::registerZone()
{
	registerGlobalMethod("Zones", luaGetZones);

	registerClass("Zone", "", luaZoneCreate);
	registerMetaMethod("Zone", "__eq", LuaScriptInterface::luaUserdataCompare);
	registerMetaMethod("Zone", "__gc", luaZoneDelete);
	registerMetaMethod("Zone", "__close", luaZoneDelete);

	registerMethod("Zone", "delete", luaZoneDelete);
	registerMethod("Zone", "getId", luaZoneGetId);
	registerMethod("Zone", "getCreatures", luaZoneGetCreatures);
	registerMethod("Zone", "getGrounds", luaZoneGetGrounds);
	registerMethod("Zone", "getItems", luaZoneGetItems);
	registerMethod("Zone", "getTiles", luaZoneGetTiles);
	registerMethod("Zone", "getCreatureCount", luaZoneGetCreatureCount);
	registerMethod("Zone", "getItemCount", luaZoneGetItemCount);
	registerMethod("Zone", "getTileCount", luaZoneGetTileCount);
}
