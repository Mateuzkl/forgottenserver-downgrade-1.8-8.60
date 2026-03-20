// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "container.h"
#include "game.h"
#include "luascript.h"

extern Game g_game;

namespace {
using namespace Lua;

// Container
int luaContainerCreate(lua_State* L)
{
	// Container(id or userdata)
	Container* container = nullptr;

	if (isNumber(L, 2)) {
		container = LuaScriptInterface::getScriptEnv()->getContainerByUID(getNumber<uint32_t>(L, 2));
	} else if (isUserdata(L, 2)) {
		switch (getUserdataType(L, 2)) {
			case LuaData_Item: {
				Item* item = getUserdata<Item>(L, 2);
				container = item ? item->getContainer() : nullptr;
				break;
			}
			case LuaData_Container:
				container = getUserdata<Container>(L, 2);
				break;
			default:
				break;
		}
	}

	if (!container) {
		lua_pushnil(L);
		return 1;
	}

	pushUserdata(L, container);
	setMetatable(L, -1, "Container");
	return 1;
}

int luaContainerGetSize(lua_State* L)
{
	// container:getSize([recursive = false])
	const Container* container = getUserdata<const Container>(L, 1);
	if (container) {
		lua_pushinteger(L, container->size(getBoolean(L, 2, false)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaContainerGetCapacity(lua_State* L)
{
	// container:getCapacity()
	const Container* container = getUserdata<const Container>(L, 1);
	if (container) {
		lua_pushinteger(L, container->capacity());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaContainerGetEmptySlots(lua_State* L)
{
	// container:getEmptySlots([recursive = false])
	const Container* container = getUserdata<const Container>(L, 1);
	if (!container) {
		lua_pushnil(L);
		return 1;
	}

	uint32_t slots = container->capacity() - container->size();
	if (getBoolean(L, 2, false)) {
		for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
			if (Container* tmpContainer = (*it)->getContainer()) {
				slots += tmpContainer->capacity() - tmpContainer->size();
			}
		}
	}

	lua_pushinteger(L, slots);
	return 1;
}

int luaContainerGetItemHoldingCount(lua_State* L)
{
	// container:getItemHoldingCount()
	const Container* container = getUserdata<const Container>(L, 1);
	if (container) {
		lua_pushinteger(L, container->getItemHoldingCount());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaContainerGetItem(lua_State* L)
{
	// container:getItem(index)
	const Container* container = getUserdata<const Container>(L, 1);
	if (!container) {
		lua_pushnil(L);
		return 1;
	}

	uint32_t index = getInteger<uint32_t>(L, 2);
	Item* item = container->getItemByIndex(index);
	if (item) {
		pushUserdata<Item>(L, item);
		setItemMetatable(L, -1, item);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaContainerHasItem(lua_State* L)
{
	// container:hasItem(item)
	const Container* container = getUserdata<const Container>(L, 1);
	if (container) {
		if (const Item* item = getUserdata<const Item>(L, 2)) {
			pushBoolean(L, container->isHoldingItem(item));
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaContainerAddItem(lua_State* L)
{
	// container:addItem(itemId[, count/subType = 1[, index = INDEX_WHEREEVER[, flags = 0]]])
	Container* container = getUserdata<Container>(L, 1);
	if (!container) {
		lua_pushnil(L);
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

	uint16_t count = getInteger<uint16_t>(L, 3, 1);
	const ItemType& it = Item::items[itemId];
	if (it.stackable) {
		count = std::min<uint16_t>(count, it.stackSize);
	}

	Item* item = Item::CreateItem(itemId, count);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	int32_t index = getInteger<int32_t>(L, 4, INDEX_WHEREEVER);
	uint32_t flags = getInteger<uint32_t>(L, 5, 0);

	ReturnValue ret = g_game.internalAddItem(container, item, index, flags);
	if (ret == RETURNVALUE_NOERROR) {
		pushUserdata<Item>(L, item);
		setItemMetatable(L, -1, item);
	} else {
		delete item;
		lua_pushnil(L);
	}
	return 1;
}

int luaContainerAddItemEx(lua_State* L)
{
	// container:addItemEx(item[, index = INDEX_WHEREEVER[, flags = 0]])
	Item* item = getUserdata<Item>(L, 2);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	Container* container = getUserdata<Container>(L, 1);
	if (!container) {
		lua_pushnil(L);
		return 1;
	}

	if (item->getParent() != VirtualCylinder::virtualCylinder) {
		reportErrorFunc(L, "Item already has a parent");
		lua_pushnil(L);
		return 1;
	}

	int32_t index = getInteger<int32_t>(L, 3, INDEX_WHEREEVER);
	uint32_t flags = getInteger<uint32_t>(L, 4, 0);
	ReturnValue ret = g_game.internalAddItem(container, item, index, flags);
	if (ret == RETURNVALUE_NOERROR) {
		ScriptEnvironment::removeTempItem(item);
	}
	lua_pushinteger(L, ret);
	return 1;
}

int luaContainerGetCorpseOwner(lua_State* L)
{
	// container:getCorpseOwner()
	const Container* container = getUserdata<const Container>(L, 1);
	if (container) {
		lua_pushinteger(L, container->getCorpseOwner());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaContainerGetItemCountById(lua_State* L)
{
	// container:getItemCountById(itemId[, subType = -1])
	const Container* container = getUserdata<const Container>(L, 1);
	if (!container) {
		lua_pushnil(L);
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
	lua_pushinteger(L, container->getItemTypeCount(itemId, subType));
	return 1;
}

int luaContainerGetItems(lua_State* L)
{
	// container:getItems([recursive = false])
	const Container* container = getUserdata<const Container>(L, 1);
	if (!container) {
		lua_pushnil(L);
		return 1;
	}

	bool recursive = getBoolean(L, 2, false);
	const std::vector<Item*> items = container->getItems(recursive);

	lua_createtable(L, items.size(), 0);

	int index = 0;
	for (Item* item : items) {
		pushUserdata(L, item);
		setItemMetatable(L, -1, item);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}
} // namespace

void LuaScriptInterface::registerContainer()
{
	// Container
	registerClass("Container", "Item", luaContainerCreate);
	registerMetaMethod("Container", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("Container", "getSize", luaContainerGetSize);
	registerMethod("Container", "getCapacity", luaContainerGetCapacity);
	registerMethod("Container", "getEmptySlots", luaContainerGetEmptySlots);
	registerMethod("Container", "getItems", luaContainerGetItems);
	registerMethod("Container", "getItemHoldingCount", luaContainerGetItemHoldingCount);
	registerMethod("Container", "getItemCountById", luaContainerGetItemCountById);

	registerMethod("Container", "getItem", luaContainerGetItem);
	registerMethod("Container", "hasItem", luaContainerHasItem);
	registerMethod("Container", "addItem", luaContainerAddItem);
	registerMethod("Container", "addItemEx", luaContainerAddItemEx);
	registerMethod("Container", "getCorpseOwner", luaContainerGetCorpseOwner);
}
