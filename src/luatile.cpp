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

// Tile
int luaTileCreate(lua_State* L)
{
	// Tile(x, y, z)
	// Tile(position)
	Tile* tile;
	if (isTable(L, 2)) {
		tile = g_game.map.getTile(getPosition(L, 2));
	} else {
		uint8_t z = getInteger<uint8_t>(L, 4);
		uint16_t y = getInteger<uint16_t>(L, 3);
		uint16_t x = getInteger<uint16_t>(L, 2);
		tile = g_game.map.getTile(x, y, z);
	}

	if (tile) {
		pushUserdata<Tile>(L, tile);
		setMetatable(L, -1, "Tile");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileRemove(lua_State* L)
{
	// tile:remove()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	if (g_game.isTileInCleanList(tile)) {
		g_game.removeTileToClean(tile);
	}

	g_game.map.removeTile(tile->getPosition());
	pushBoolean(L, true);
	return 1;
}

int luaTileGetPosition(lua_State* L)
{
	// tile:getPosition()
	Tile* tile = getUserdata<Tile>(L, 1);
	if (tile) {
		pushPosition(L, tile->getPosition());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetGround(lua_State* L)
{
	// tile:getGround()
	Tile* tile = getUserdata<Tile>(L, 1);
	if (tile && tile->getGround()) {
		pushItem(L, tile->getGround());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetThing(lua_State* L)
{
	// tile:getThing(index)
	int32_t index = getInteger<int32_t>(L, 2);
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	Thing* thing = tile->getThing(index);
	if (!thing) {
		lua_pushnil(L);
		return 1;
	}

	if (Creature* creature = thing->getCreature()) {
		pushUserdata<Creature>(L, creature);
		setCreatureMetatable(L, -1, creature);
	} else if (Item* item = thing->getItem()) {
		pushItem(L, item);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetThingCount(lua_State* L)
{
	// tile:getThingCount()
	Tile* tile = getUserdata<Tile>(L, 1);
	if (tile) {
		lua_pushinteger(L, tile->getThingCount());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetTopVisibleThing(lua_State* L)
{
	// tile:getTopVisibleThing([creature])
	Creature* creature = getCreature(L, 2);
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	Thing* thing = tile->getTopVisibleThing(creature);
	if (!thing) {
		lua_pushnil(L);
		return 1;
	}

	if (Creature* visibleCreature = thing->getCreature()) {
		pushUserdata<Creature>(L, visibleCreature);
		setCreatureMetatable(L, -1, visibleCreature);
	} else if (Item* visibleItem = thing->getItem()) {
		pushItem(L, visibleItem);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetTopTopItem(lua_State* L)
{
	// tile:getTopTopItem()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	Item* item = tile->getTopTopItem();
	if (item) {
		pushItem(L, item);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetTopDownItem(lua_State* L)
{
	// tile:getTopDownItem()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	Item* item = tile->getTopDownItem();
	if (item) {
		pushItem(L, item);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetFieldItem(lua_State* L)
{
	// tile:getFieldItem()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	Item* item = tile->getFieldItem();
	if (item) {
		pushItem(L, item);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetItemById(lua_State* L)
{
	// tile:getItemById(itemId[, subType = -1])
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	uint16_t itemId;
	if (isInteger(L, 2)) {
		itemId = getInteger<uint16_t>(L, 2);
	} else {
		itemId = Item::items.getItemIdByName(getString(L, 2));
		if (itemId == 0) {
			lua_pushnil(L);
			return 1;
		}
	}
	int32_t subType = getInteger<int32_t>(L, 3, -1);

	Item* item = g_game.findItemOfType(tile, itemId, false, subType);
	if (item) {
		pushItem(L, item);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetItemByType(lua_State* L)
{
	// tile:getItemByType(itemType)
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	bool found;

	ItemTypes_t itemType = getInteger<ItemTypes_t>(L, 2);
	switch (itemType) {
		case ITEM_TYPE_TELEPORT:
			found = tile->hasFlag(TILESTATE_TELEPORT);
			break;
		case ITEM_TYPE_MAGICFIELD:
			found = tile->hasFlag(TILESTATE_MAGICFIELD);
			break;
		case ITEM_TYPE_MAILBOX:
			found = tile->hasFlag(TILESTATE_MAILBOX);
			break;
		case ITEM_TYPE_TRASHHOLDER:
			found = tile->hasFlag(TILESTATE_TRASHHOLDER);
			break;
		case ITEM_TYPE_BED:
			found = tile->hasFlag(TILESTATE_BED);
			break;
		case ITEM_TYPE_DEPOT:
			found = tile->hasFlag(TILESTATE_DEPOT);
			break;
		default:
			found = true;
			break;
	}

	if (!found) {
		lua_pushnil(L);
		return 1;
	}

	if (Item* item = tile->getGround()) {
		const ItemType& it = Item::items[item->getID()];
		if (it.type == itemType) {
			pushItem(L, item);
			return 1;
		}
	}

	if (const TileItemVector* items = tile->getItemList()) {
		for (const auto& item : *items) {
			const ItemType& it = Item::items[item->getID()];
			if (it.type == itemType) {
				pushSharedPtr(L, item);
				setItemMetatable(L, -1, item.get());
				return 1;
			}
		}
	}

	lua_pushnil(L);
	return 1;
}

int luaTileGetItemByTopOrder(lua_State* L)
{
	// tile:getItemByTopOrder(topOrder)
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	int32_t topOrder = getInteger<int32_t>(L, 2);

	Item* item = tile->getItemByTopOrder(topOrder);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	pushItem(L, item);
	return 1;
}

int luaTileGetItemCountById(lua_State* L)
{
	// tile:getItemCountById(itemId[, subType = -1])
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	int32_t subType = getInteger<int32_t>(L, 3, -1);

	uint16_t itemId;
	if (isInteger(L, 2)) {
		itemId = getInteger<uint16_t>(L, 2);
	} else {
		itemId = Item::items.getItemIdByName(getString(L, 2));
		if (itemId == 0) {
			lua_pushnil(L);
			return 1;
		}
	}

	lua_pushinteger(L, tile->getItemTypeCount(itemId, subType));
	return 1;
}

int luaTileGetBottomCreature(lua_State* L)
{
	// tile:getBottomCreature()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	const Creature* creature = tile->getBottomCreature();
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	pushUserdata<const Creature>(L, creature);
	setCreatureMetatable(L, -1, creature);
	return 1;
}

int luaTileGetTopCreature(lua_State* L)
{
	// tile:getTopCreature()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	Creature* creature = tile->getTopCreature();
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	pushUserdata<Creature>(L, creature);
	setCreatureMetatable(L, -1, creature);
	return 1;
}

int luaTileGetBottomVisibleCreature(lua_State* L)
{
	// tile:getBottomVisibleCreature(creature)
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	Creature* creature = getCreature(L, 2);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	const Creature* visibleCreature = tile->getBottomVisibleCreature(creature);
	if (visibleCreature) {
		pushUserdata<const Creature>(L, visibleCreature);
		setCreatureMetatable(L, -1, visibleCreature);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetTopVisibleCreature(lua_State* L)
{
	// tile:getTopVisibleCreature(creature)
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	Creature* creature = getCreature(L, 2);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	Creature* visibleCreature = tile->getTopVisibleCreature(creature);
	if (visibleCreature) {
		pushUserdata<Creature>(L, visibleCreature);
		setCreatureMetatable(L, -1, visibleCreature);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetItems(lua_State* L)
{
	// tile:getItems()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	TileItemVector* itemVector = tile->getItemList();
	if (!itemVector) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, itemVector->size(), 0);

	int index = 0;
	for (const auto& item : *itemVector) {
		pushSharedPtr(L, item);
		setItemMetatable(L, -1, item.get());
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaTileGetItemCount(lua_State* L)
{
	// tile:getItemCount()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	lua_pushinteger(L, tile->getItemCount());
	return 1;
}

int luaTileGetDownItemCount(lua_State* L)
{
	// tile:getDownItemCount()
	Tile* tile = getUserdata<Tile>(L, 1);
	if (tile) {
		lua_pushinteger(L, tile->getDownItemCount());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetTopItemCount(lua_State* L)
{
	// tile:getTopItemCount()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	lua_pushinteger(L, tile->getTopItemCount());
	return 1;
}

int luaTileGetCreatures(lua_State* L)
{
	// tile:getCreatures()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	CreatureVector* creatureVector = tile->getCreatures();
	if (!creatureVector) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, creatureVector->size(), 0);

	int index = 0;
	for (const auto& creature : *creatureVector) {
		pushUserdata<Creature>(L, creature.get());
		setCreatureMetatable(L, -1, creature.get());
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaTileGetCreatureCount(lua_State* L)
{
	// tile:getCreatureCount()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	lua_pushinteger(L, tile->getCreatureCount());
	return 1;
}

int luaTileHasProperty(lua_State* L)
{
	// tile:hasProperty(property[, item])
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	Item* item;
	if (lua_gettop(L) >= 3) {
		item = getItemUserdata<Item>(L, 3);
	} else {
		item = nullptr;
	}

	ITEMPROPERTY property = getInteger<ITEMPROPERTY>(L, 2);
	if (item) {
		pushBoolean(L, tile->hasProperty(item, property));
	} else {
		pushBoolean(L, tile->hasProperty(property));
	}
	return 1;
}

int luaTileGetThingIndex(lua_State* L)
{
	// tile:getThingIndex(thing)
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	Thing* thing = getThing(L, 2);
	if (thing) {
		lua_pushinteger(L, tile->getThingIndex(thing));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileHasFlag(lua_State* L)
{
	// tile:hasFlag(flag)
	Tile* tile = getUserdata<Tile>(L, 1);
	if (tile) {
		tileflags_t flag = getInteger<tileflags_t>(L, 2);
		pushBoolean(L, tile->hasFlag(flag));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileGetZoneIds(lua_State* L)
{
	// tile:getZoneIds()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	const auto& zoneIds = tile->getZoneIds();
	lua_createtable(L, static_cast<int>(zoneIds.size()), 0);

	int index = 0;
	for (ZoneId zoneId : zoneIds) {
		lua_pushinteger(L, zoneId);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaTileHasZone(lua_State* L)
{
	// tile:hasZone([zoneId | zone])
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	if (lua_gettop(L) < 2) {
		pushBoolean(L, tile->hasZone());
		return 1;
	}

	ZoneId expectedZoneId = 0;
	if (isNumber(L, 2)) {
		expectedZoneId = getInteger<ZoneId>(L, 2);
	} else if (isType<Zone>(L, 2)) {
		const auto zone = getSharedPtr<Zone>(L, 2);
		if (zone) {
			expectedZoneId = zone->getId();
		}
	}

	pushBoolean(L, expectedZoneId != 0 && tile->hasZoneId(expectedZoneId));
	return 1;
}

int luaTileQueryAdd(lua_State* L)
{
	// tile:queryAdd(thing[, flags])
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	Thing* thing = getThing(L, 2);
	if (thing) {
		uint32_t flags = getInteger<uint32_t>(L, 3, 0);
		lua_pushinteger(L, tile->queryAdd(0, *thing, 1, flags));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileAddItem(lua_State* L)
{
	// tile:addItem(itemId[, count/subType = 1[, flags = 0]])
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	uint16_t itemId;
	if (isInteger(L, 2)) {
		itemId = getInteger<uint16_t>(L, 2);
	} else {
		itemId = Item::items.getItemIdByName(getString(L, 2));
		if (itemId == 0) {
			lua_pushnil(L);
			return 1;
		}
	}

	uint32_t subType = getInteger<uint32_t>(L, 3, 1);

	const ItemType& it = Item::items[itemId];
	if (it.stackable) {
		subType = std::min<int32_t>(subType, it.stackSize);
	}

	auto itemPtr = Item::CreateItem(itemId, static_cast<uint16_t>(subType));
	if (!itemPtr) {
		lua_pushnil(L);
		return 1;
	}

	uint32_t flags = getInteger<uint32_t>(L, 4, 0);
	Item* item = itemPtr.get();
	Item* mergedItem = nullptr;
	if (item->isStackable()) {
		int32_t destinationIndex = INDEX_WHEREEVER;
		uint32_t destinationFlags = flags;
		tile->queryDestination(destinationIndex, *item, &mergedItem, destinationFlags);
		if (!(mergedItem && mergedItem->equals(item) && mergedItem->getItemCount() < mergedItem->getStackSize())) {
			mergedItem = nullptr;
		}
	}

	ReturnValue ret = g_game.internalAddItem(tile, itemPtr.get(), INDEX_WHEREEVER, flags);
	if (ret == RETURNVALUE_NOERROR) {
		if (item->getParent() == nullptr) {
			if (!mergedItem || mergedItem->isRemoved()) {
				lua_pushnil(L);
				return 1;
			}
			item = mergedItem;
		}

		pushItem(L, item);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTileAddItemEx(lua_State* L)
{
	// tile:addItemEx(item[, flags = 0])
	Item* item = getItemUserdata<Item>(L, 2);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	if (item->getParent() != VirtualCylinder::virtualCylinder) {
		reportErrorFunc(L, "Item already has a parent");
		lua_pushnil(L);
		return 1;
	}

	uint32_t flags = getInteger<uint32_t>(L, 3, 0);
	ReturnValue ret = g_game.internalAddItem(tile, item, INDEX_WHEREEVER, flags);
	if (ret == RETURNVALUE_NOERROR) {
		ScriptEnvironment::removeTempItem(item);
	}
	lua_pushinteger(L, ret);
	return 1;
}

int luaTileGetHouse(lua_State* L)
{
	// tile:getHouse()
	Tile* tile = getUserdataOrPushNil<Tile>(L, 1);
	if (!tile) {
		return 1;
	}

	if (HouseTile* houseTile = tile->getHouseTile()) {
		pushUserdata<House>(L, houseTile->getHouse());
		setMetatable(L, -1, "House");
	} else {
		lua_pushnil(L);
	}
	return 1;
}
} // namespace

void LuaScriptInterface::registerTile()
{
	// Tile
	registerClass("Tile", "", luaTileCreate);
	registerMetaMethod("Tile", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("Tile", "remove", luaTileRemove);

	registerMethod("Tile", "getPosition", luaTileGetPosition);
	registerMethod("Tile", "getGround", luaTileGetGround);
	registerMethod("Tile", "getThing", luaTileGetThing);
	registerMethod("Tile", "getThingCount", luaTileGetThingCount);
	registerMethod("Tile", "getTopVisibleThing", luaTileGetTopVisibleThing);

	registerMethod("Tile", "getTopTopItem", luaTileGetTopTopItem);
	registerMethod("Tile", "getTopDownItem", luaTileGetTopDownItem);
	registerMethod("Tile", "getFieldItem", luaTileGetFieldItem);

	registerMethod("Tile", "getItemById", luaTileGetItemById);
	registerMethod("Tile", "getItemByType", luaTileGetItemByType);
	registerMethod("Tile", "getItemByTopOrder", luaTileGetItemByTopOrder);
	registerMethod("Tile", "getItemCountById", luaTileGetItemCountById);

	registerMethod("Tile", "getBottomCreature", luaTileGetBottomCreature);
	registerMethod("Tile", "getTopCreature", luaTileGetTopCreature);
	registerMethod("Tile", "getBottomVisibleCreature", luaTileGetBottomVisibleCreature);
	registerMethod("Tile", "getTopVisibleCreature", luaTileGetTopVisibleCreature);

	registerMethod("Tile", "getItems", luaTileGetItems);
	registerMethod("Tile", "getItemCount", luaTileGetItemCount);
	registerMethod("Tile", "getDownItemCount", luaTileGetDownItemCount);
	registerMethod("Tile", "getTopItemCount", luaTileGetTopItemCount);

	registerMethod("Tile", "getCreatures", luaTileGetCreatures);
	registerMethod("Tile", "getCreatureCount", luaTileGetCreatureCount);

	registerMethod("Tile", "getThingIndex", luaTileGetThingIndex);

	registerMethod("Tile", "hasProperty", luaTileHasProperty);
	registerMethod("Tile", "hasFlag", luaTileHasFlag);
	registerMethod("Tile", "getZoneIds", luaTileGetZoneIds);
	registerMethod("Tile", "getZoneId", luaTileGetZoneIds);
	registerMethod("Tile", "hasZone", luaTileHasZone);

	registerMethod("Tile", "queryAdd", luaTileQueryAdd);
	registerMethod("Tile", "addItem", luaTileAddItem);
	registerMethod("Tile", "addItemEx", luaTileAddItemEx);

	registerMethod("Tile", "getHouse", luaTileGetHouse);
}
