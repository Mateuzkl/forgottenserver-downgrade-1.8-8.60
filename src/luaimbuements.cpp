// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "imbuement.h"
#include "item.h"
#include "luascript.h"

namespace {
using namespace Lua;
}

// Item Imbuement methods

int LuaScriptInterface::luaItemGetImbuementSlots(lua_State* L)
{
	// item:getImbuementSlots() -- returns how many total slots
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		lua_pushnumber(L, item->getImbuementSlots());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaItemGetFreeImbuementSlots(lua_State* L)
{
	// item:getFreeImbuementSlots() -- returns how many slots are available for use
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		lua_pushnumber(L, item->getFreeImbuementSlots());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaItemCanImbue(lua_State* L)
{
	// item:canImbue() -- returns true if item has slots that are free
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		pushBoolean(L, item->canImbue());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaItemAddImbuementSlots(lua_State* L)
{
	// item:addImbuementSlots(amount) -- tries to add imbuement slot(s), returns true if successful
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		pushBoolean(L, item->addImbuementSlots(getNumber<uint32_t>(L, 2)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaItemRemoveImbuementSlots(lua_State* L)
{
	// item:removeImbuementSlots(amount, destroy) -- tries to remove imbuement slot(s), returns true if successful
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		pushBoolean(L, item->removeImbuementSlots(getNumber<uint32_t>(L, 2), getBoolean(L, 3, false)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaItemHasImbuementType(lua_State* L)
{
	// item:hasImbuementType(type)
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		pushBoolean(L, item->hasImbuementType(getNumber<ImbuementType>(L, 2, IMBUEMENT_TYPE_NONE)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaItemHasImbuement(lua_State* L)
{
	// item:hasImbuement(imbuement)
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 2);
		if (imbue) {
			pushBoolean(L, item->hasImbuement(imbue));
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaItemHasImbuements(lua_State* L)
{
	// item:hasImbuements() -- returns true if item has any imbuements
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		pushBoolean(L, item->hasImbuements());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaItemAddImbuement(lua_State* L)
{
	// item:addImbuement(imbuement) -- returns true if it successfully adds the imbuement
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 2);
		if (imbue) {
			pushBoolean(L, item->addImbuement(imbue, true));
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaItemRemoveImbuement(lua_State* L)
{
	// item:removeImbuement(imbuement)
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 2);
		if (imbue) {
			pushBoolean(L, item->removeImbuement(imbue, false));
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaItemGetImbuements(lua_State* L)
{
	// item:getImbuements() -- returns a table that contains imbuement userdata
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	std::vector<std::shared_ptr<Imbuement>> imbuements = item->getImbuements();
	lua_createtable(L, imbuements.size(), 0);

	int index = 0;
	for (std::shared_ptr<Imbuement> imbuement : imbuements) {
		pushSharedPtr(L, imbuement);
		setMetatable(L, -1, "Imbuement");
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int LuaScriptInterface::luaItemCanApplyImbuement(lua_State* L)
{
	// item:canApplyImbuement(categoryId, tier)
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		pushBoolean(L, false);
		return 1;
	}

	uint16_t categoryId = getNumber<uint16_t>(L, 2);
	uint8_t tier = getNumber<uint8_t>(L, 3, 3);
	pushBoolean(L, item->canApplyImbuement(categoryId, tier));
	return 1;
}

// Imbuement class

int LuaScriptInterface::luaImbuementCreate(lua_State* L)
{
	// Imbuement(type, amount, duration, decayType, baseId)
	ImbuementType imbueType = getNumber<ImbuementType>(L, 2, IMBUEMENT_TYPE_NONE);
	uint32_t amount = getNumber<uint32_t>(L, 3);
	uint32_t duration = getNumber<uint32_t>(L, 4);
	ImbuementDecayType decayType = getNumber<ImbuementDecayType>(L, 5, IMBUEMENT_DECAY_EQUIPPED);
	uint16_t baseId = getNumber<uint16_t>(L, 6, 0);
	pushSharedPtr(L, std::make_shared<Imbuement>(Imbuement(imbueType, amount, duration, decayType, baseId)));
	setMetatable(L, -1, "Imbuement");
	return 1;
}

int LuaScriptInterface::luaImbuementGetBaseId(lua_State* L)
{
	// imbuement:getBaseId()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		lua_pushnumber(L, imbue->baseId);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementGetType(lua_State* L)
{
	// imbuement:getType()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		lua_pushnumber(L, static_cast<uint8_t>(imbue->imbuetype));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementGetValue(lua_State* L)
{
	// imbuement:getValue()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		lua_pushnumber(L, imbue->value);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementGetDuration(lua_State* L)
{
	// imbuement:getDuration()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		lua_pushnumber(L, imbue->duration);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementIsSkill(lua_State* L)
{
	// imbuement:isSkill()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		pushBoolean(L, imbue->isSkill());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementIsSpecialSkill(lua_State* L)
{
	// imbuement:isSpecialSkill()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		pushBoolean(L, imbue->isSpecialSkill());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementIsDamage(lua_State* L)
{
	// imbuement:isDamage()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		pushBoolean(L, imbue->isDamage());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementIsResist(lua_State* L)
{
	// imbuement:isResist()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		pushBoolean(L, imbue->isResist());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementIsStat(lua_State* L)
{
	// imbuement:isStat()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		pushBoolean(L, imbue->isStat());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementSetValue(lua_State* L)
{
	// imbuement:setValue(amount)
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		imbue->value = getNumber<uint32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementSetDuration(lua_State* L)
{
	// imbuement:setDuration(duration)
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		imbue->duration = getNumber<uint32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementSetEquipDecay(lua_State* L)
{
	// imbuement:makeEquipDecayed()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		imbue->decaytype = IMBUEMENT_DECAY_EQUIPPED;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementSetInfightDecay(lua_State* L)
{
	// imbuement:makeInfightDecayed()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		imbue->decaytype = IMBUEMENT_DECAY_INFIGHT;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementIsEquipDecay(lua_State* L)
{
	// imbuement:isEquipDecayed()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		pushBoolean(L, imbue->decaytype == IMBUEMENT_DECAY_EQUIPPED);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaImbuementIsInfightDecay(lua_State* L)
{
	// imbuement:isInfightDecayed()
	std::shared_ptr<Imbuement> imbue = getSharedPtr<Imbuement>(L, 1);
	if (imbue) {
		pushBoolean(L, imbue->decaytype == IMBUEMENT_DECAY_INFIGHT);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// ============ Game.getImbuementByScroll(scrollId) ============
int LuaScriptInterface::luaGameGetImbuementByScroll(lua_State* L)
{
	// Game.getImbuementByScroll(scrollId) -> table with definition data or nil
	uint16_t scrollId = getNumber<uint16_t>(L, 1);
	const ImbuementDefinition* def = Imbuements::getInstance().getDefinitionByScrollId(scrollId);
	if (!def) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, 0, 16);

	setField(L, "name", def->name);
	setField(L, "baseName", def->baseName);
	setField(L, "description", def->description);
	setField(L, "baseId", def->baseId);
	setField(L, "categoryId", def->categoryId);
	setField(L, "iconId", def->iconId);
	setField(L, "scrollId", def->scrollId);
	setField(L, "premium", def->premium);
	setField(L, "storage", def->storage);
	setField(L, "price", def->price);
	setField(L, "removeCost", def->removeCost);
	setField(L, "duration", def->duration);
	setField(L, "imbuementType", static_cast<uint32_t>(def->imbuementType));
	setField(L, "decayType", static_cast<uint32_t>(def->decayType));
	setField(L, "value", def->resolvedValue);

	// items sub-table
	lua_createtable(L, static_cast<int>(def->items.size()), 0);
	int idx = 1;
	for (const auto& req : def->items) {
		lua_createtable(L, 0, 2);
		setField(L, "itemId", req.itemId);
		setField(L, "count", req.count);
		lua_rawseti(L, -2, idx++);
	}
	lua_setfield(L, -2, "items");

	return 1;
}

// ============ Game.getImbuementBases() ============
int LuaScriptInterface::luaGameGetImbuementBases(lua_State* L)
{
	// Game.getImbuementBases() -> table of bases
	const auto& bases = Imbuements::getInstance().getBases();
	lua_createtable(L, static_cast<int>(bases.size()), 0);
	int idx = 1;
	for (const auto& [id, base] : bases) {
		lua_createtable(L, 0, 5);
		setField(L, "id", base.id);
		setField(L, "name", base.name);
		setField(L, "price", base.price);
		setField(L, "removeCost", base.removeCost);
		setField(L, "duration", base.duration);
		lua_rawseti(L, -2, idx++);
	}
	return 1;
}

// ============ Game.getImbuementCategories() ============
int LuaScriptInterface::luaGameGetImbuementCategories(lua_State* L)
{
	// Game.getImbuementCategories() -> table of categories
	const auto& categories = Imbuements::getInstance().getCategories();
	lua_createtable(L, static_cast<int>(categories.size()), 0);
	int idx = 1;
	for (const auto& [id, cat] : categories) {
		lua_createtable(L, 0, 3);
		setField(L, "id", cat.id);
		setField(L, "name", cat.name);
		setField(L, "agressive", cat.agressive);
		lua_rawseti(L, -2, idx++);
	}
	return 1;
}

// ============ Game.getImbuementDefinitions() ============
int LuaScriptInterface::luaGameGetImbuementDefinitions(lua_State* L)
{
	// Game.getImbuementDefinitions() -> array of all imbuement definitions (including tier 1 with no scroll)
	const auto& defs = Imbuements::getInstance().getDefinitions();
	lua_createtable(L, static_cast<int>(defs.size()), 0);
	int idx = 1;
	for (const auto& def : defs) {
		lua_createtable(L, 0, 16);
		setField(L, "name", def.name);
		setField(L, "baseName", def.baseName);
		setField(L, "description", def.description);
		setField(L, "baseId", def.baseId);
		setField(L, "categoryId", def.categoryId);
		setField(L, "iconId", def.iconId);
		setField(L, "scrollId", def.scrollId);
		setField(L, "premium", def.premium);
		setField(L, "storage", def.storage);
		setField(L, "price", def.price);
		setField(L, "removeCost", def.removeCost);
		setField(L, "duration", def.duration);
		setField(L, "imbuementType", static_cast<uint32_t>(def.imbuementType));
		setField(L, "decayType", static_cast<uint32_t>(def.decayType));
		setField(L, "value", def.resolvedValue);

		lua_createtable(L, static_cast<int>(def.items.size()), 0);
		int itemIdx = 1;
		for (const auto& req : def.items) {
			lua_createtable(L, 0, 2);
			setField(L, "itemId", req.itemId);
			setField(L, "count", req.count);
			lua_rawseti(L, -2, itemIdx++);
		}
		lua_setfield(L, -2, "items");

		lua_rawseti(L, -2, idx++);
	}
	return 1;
}

// Registration

void LuaScriptInterface::registerImbuement()
{
	// Imbuement
	registerClass("Imbuement", "", LuaScriptInterface::luaImbuementCreate);
	registerMetaMethod("Imbuement", "__eq", LuaScriptInterface::luaUserdataCompare);
	registerMethod("Imbuement", "getType", LuaScriptInterface::luaImbuementGetType);
	registerMethod("Imbuement", "isSkill", LuaScriptInterface::luaImbuementIsSkill);
	registerMethod("Imbuement", "isSpecialSkill", LuaScriptInterface::luaImbuementIsSpecialSkill);
	registerMethod("Imbuement", "isStat", LuaScriptInterface::luaImbuementIsStat);
	registerMethod("Imbuement", "isDamage", LuaScriptInterface::luaImbuementIsDamage);
	registerMethod("Imbuement", "isResist", LuaScriptInterface::luaImbuementIsResist);
	registerMethod("Imbuement", "getValue", LuaScriptInterface::luaImbuementGetValue);
	registerMethod("Imbuement", "setValue", LuaScriptInterface::luaImbuementSetValue);
	registerMethod("Imbuement", "getDuration", LuaScriptInterface::luaImbuementGetDuration);
	registerMethod("Imbuement", "setDuration", LuaScriptInterface::luaImbuementSetDuration);
	registerMethod("Imbuement", "makeEquipDecayed", LuaScriptInterface::luaImbuementSetEquipDecay);
	registerMethod("Imbuement", "makeInfightDecayed", LuaScriptInterface::luaImbuementSetInfightDecay);
	registerMethod("Imbuement", "isEquipDecayed", LuaScriptInterface::luaImbuementIsEquipDecay);
	registerMethod("Imbuement", "isInfightDecayed", LuaScriptInterface::luaImbuementIsInfightDecay);
	registerMethod("Imbuement", "getBaseId", LuaScriptInterface::luaImbuementGetBaseId);

	// Game.getImbuementByScroll(scrollId) -> table or nil
	registerMethod("Game", "getImbuementByScroll", LuaScriptInterface::luaGameGetImbuementByScroll);

	// Game.getImbuementBases() -> table
	registerMethod("Game", "getImbuementBases", LuaScriptInterface::luaGameGetImbuementBases);

	// Game.getImbuementCategories() -> table
	registerMethod("Game", "getImbuementCategories", LuaScriptInterface::luaGameGetImbuementCategories);

	// Game.getImbuementDefinitions() -> array of all definitions (including tier 1 with no scroll)
	registerMethod("Game", "getImbuementDefinitions", LuaScriptInterface::luaGameGetImbuementDefinitions);
}
