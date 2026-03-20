// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "logger.h"
#include "luascript.h"
#include "monsters.h"

#include <fmt/format.h>

namespace {
using namespace Lua;

// Loot
int luaCreateLoot(lua_State* L)
{
	// Loot() will create a new loot item
	Loot* loot = new Loot();
	pushUserdata<Loot>(L, loot);
	setMetatable(L, -1, "Loot");
	return 1;
}

int luaDeleteLoot(lua_State* L)
{
	// loot:delete() or loot:__gc() or loot:__close()
	Loot** lootPtr = getRawUserdata<Loot>(L, 1);
	if (lootPtr && *lootPtr) {
		delete *lootPtr;
		*lootPtr = nullptr;
	}
	return 0;
}

int luaLootSetId(lua_State* L)
{
	// loot:setId(id or name)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		if (isInteger(L, 2)) {
			loot->lootBlock.id = getInteger<uint16_t>(L, 2);
		} else {
			auto name = getString(L, 2);
			auto ids = Item::items.nameToItems.equal_range(boost::algorithm::to_lower_copy<std::string>(name));

			if (ids.first == Item::items.nameToItems.cend()) {
				LOG_WARN(fmt::format("[Warning - Loot:setId] Unknown loot item \"{}\"", name));
				pushBoolean(L, false);
				return 1;
			}

			if (std::next(ids.first) != ids.second) {
				LOG_WARN(fmt::format("[Warning - Loot:setId] Non-unique loot item \"{}\"", name));
				pushBoolean(L, false);
				return 1;
			}

			loot->lootBlock.id = ids.first->second;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaLootSetSubType(lua_State* L)
{
	// loot:setSubType(type)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.subType = getInteger<uint16_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaLootSetChance(lua_State* L)
{
	// loot:setChance(chance)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.chance = getInteger<uint32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaLootSetMinCount(lua_State* L)
{
	// loot:setMinCount(min)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.countmin = getInteger<uint32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaLootSetMaxCount(lua_State* L)
{
	// loot:setMaxCount(max)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.countmax = getInteger<uint32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaLootSetActionId(lua_State* L)
{
	// loot:setActionId(actionid)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.actionId = getInteger<uint32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaLootSetUnique(lua_State* L)
{
	// loot:setUnique(unique)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.unique = getBoolean(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaLootSetIdFromName(lua_State* L)
{
	// loot:setIdFromName(name)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot && isString(L, 2)) {
		auto name = getString(L, 2);
		auto ids = Item::items.nameToItems.equal_range(boost::algorithm::to_lower_copy<std::string>(name));

		if (ids.first == Item::items.nameToItems.cend()) {
			LOG_WARN(fmt::format("[Warning - Loot:setIdFromName] Unknown loot item \"{}\"", name));
			pushBoolean(L, false);
			return 1;
		}

		if (std::next(ids.first) != ids.second) {
			LOG_WARN(fmt::format("[Warning - Loot:setIdFromName] Non-unique loot item \"{}\"", name));
			pushBoolean(L, false);
			return 1;
		}

		loot->lootBlock.id = ids.first->second;
		pushBoolean(L, true);
	} else {
		pushBoolean(L, false);
	}
	return 1;
}

int luaLootSetDescription(lua_State* L)
{
	// loot:setDescription(desc)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.text = getString(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaLootAddChildLoot(lua_State* L)
{
	// loot:addChildLoot(loot)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		if (Loot* childLoot = getUserdata<Loot>(L, 2)) {
			loot->lootBlock.childLoot.push_back(childLoot->lootBlock);
			pushBoolean(L, true);
		} else {
			pushBoolean(L, false);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}
} // namespace

void LuaScriptInterface::registerLoot()
{
	// Loot
	registerClass("Loot", "", luaCreateLoot);
	registerMetaMethod("Loot", "__gc", luaDeleteLoot);
	registerMetaMethod("Loot", "__close", luaDeleteLoot);
	registerMethod("Loot", "delete", luaDeleteLoot);

	registerMethod("Loot", "setId", luaLootSetId);
	registerMethod("Loot", "setIdFromName", luaLootSetIdFromName);
	registerMethod("Loot", "setMinCount", luaLootSetMinCount);
	registerMethod("Loot", "setMaxCount", luaLootSetMaxCount);
	registerMethod("Loot", "setSubType", luaLootSetSubType);
	registerMethod("Loot", "setChance", luaLootSetChance);
	registerMethod("Loot", "setActionId", luaLootSetActionId);
	registerMethod("Loot", "setUnique", luaLootSetUnique);
	registerMethod("Loot", "setDescription", luaLootSetDescription);
	registerMethod("Loot", "addChildLoot", luaLootAddChildLoot);
}
