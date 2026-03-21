// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "combat.h"
#include "game.h"
#include "item.h"
#include "luascript.h"

extern Game g_game;

namespace {
using namespace Lua;

// Item
int luaItemCreate(lua_State* L)
{
	// Item(uid)
	uint32_t id = getInteger<uint32_t>(L, 2);

	Item* item = LuaScriptInterface::getScriptEnv()->getItemByUID(id);
	if (item) {
		pushUserdata<Item>(L, item);
		setItemMetatable(L, -1, item);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemIsItem(lua_State* L)
{
	// item:isItem()
	pushBoolean(L, getUserdata<const Item>(L, 1) != nullptr);
	return 1;
}

int luaItemGetParent(lua_State* L)
{
	// item:getParent()
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	Cylinder* parent = item->getParent();
	if (!parent) {
		lua_pushnil(L);
		return 1;
	}

	pushCylinder(L, parent);
	return 1;
}

int luaItemGetTopParent(lua_State* L)
{
	// item:getTopParent()
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	Cylinder* topParent = item->getTopParent();
	if (!topParent) {
		lua_pushnil(L);
		return 1;
	}

	pushCylinder(L, topParent);
	return 1;
}

int luaItemGetId(lua_State* L)
{
	// item:getId()
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		lua_pushinteger(L, item->getID());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemClone(lua_State* L)
{
	// item:clone()
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	Item* clone = item->clone();
	if (!clone) {
		lua_pushnil(L);
		return 1;
	}

	LuaScriptInterface::getScriptEnv()->addTempItem(clone);
	clone->setParent(VirtualCylinder::virtualCylinder);

	pushUserdata<Item>(L, clone);
	setItemMetatable(L, -1, clone);
	return 1;
}

int luaItemSplit(lua_State* L)
{
	// item:split([count = 1])
	Item** itemPtr = getRawUserdata<Item>(L, 1);
	if (!itemPtr) {
		lua_pushnil(L);
		return 1;
	}

	Item* item = *itemPtr;
	if (!item || !item->isStackable()) {
		lua_pushnil(L);
		return 1;
	}

	uint16_t count = std::min<uint16_t>(getInteger<uint16_t>(L, 2, 1), item->getItemCount());
	uint16_t diff = item->getItemCount() - count;

	Item* splitItem = item->clone();
	if (!splitItem) {
		lua_pushnil(L);
		return 1;
	}

	splitItem->setItemCount(count);

	ScriptEnvironment* env = LuaScriptInterface::getScriptEnv();
	uint32_t uid = env->addThing(item);

	Item* newItem = g_game.transformItem(item, item->getID(), diff);
	if (item->isRemoved()) {
		env->removeItemByUID(uid);
	}

	if (newItem && newItem != item) {
		env->insertItem(uid, newItem);
	}

	*itemPtr = newItem;

	splitItem->setParent(VirtualCylinder::virtualCylinder);
	env->addTempItem(splitItem);

	pushUserdata<Item>(L, splitItem);
	setItemMetatable(L, -1, splitItem);
	return 1;
}

int luaItemRemove(lua_State* L)
{
	// item:remove([count = -1])
	Item** itemPtr = getRawUserdata<Item>(L, 1);
	if (!itemPtr || !*itemPtr) {
		lua_pushnil(L);
		return 1;
	}

	Item* item = *itemPtr;
	int32_t count = getInteger<int32_t>(L, 2, -1);
	ReturnValue ret = g_game.internalRemoveItem(item, count);

	if (ret == RETURNVALUE_NOTPOSSIBLE) {
		*itemPtr = nullptr; // Reset the pointer to prevent future access.
		lua_pushnil(L);
	} else {
		pushBoolean(L, ret == RETURNVALUE_NOERROR);
	}
	return 1;
}

int luaItemGetUniqueId(lua_State* L)
{
	// item:getUniqueId()
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		uint32_t uniqueId = item->getUniqueId();
		if (uniqueId == 0) {
			uniqueId = LuaScriptInterface::getScriptEnv()->addThing(item);
		}
		lua_pushinteger(L, uniqueId);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetActionId(lua_State* L)
{
	// item:getActionId()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		lua_pushinteger(L, item->getActionId());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemSetActionId(lua_State* L)
{
	// item:setActionId(actionId)
	uint16_t actionId = getInteger<uint16_t>(L, 2);
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		item->setActionId(actionId);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetCount(lua_State* L)
{
	// item:getCount()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		lua_pushinteger(L, item->getItemCount());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetCharges(lua_State* L)
{
	// item:getCharges()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		lua_pushinteger(L, item->getCharges());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetFluidType(lua_State* L)
{
	// item:getFluidType()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		lua_pushinteger(L, item->getFluidType());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetWeight(lua_State* L)
{
	// item:getWeight()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		lua_pushinteger(L, item->getWeight());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetWorth(lua_State* L)
{
	// item:getWorth()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		lua_pushinteger(L, item->getWorth());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetSubType(lua_State* L)
{
	// item:getSubType()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		lua_pushinteger(L, item->getSubType());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetName(lua_State* L)
{
	// item:getName()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		pushString(L, item->getName());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetPluralName(lua_State* L)
{
	// item:getPluralName()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		pushString(L, item->getPluralName());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetArticle(lua_State* L)
{
	// item:getArticle()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		pushString(L, item->getArticle());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetPosition(lua_State* L)
{
	// item:getPosition()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		pushPosition(L, item->getPosition());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetTile(lua_State* L)
{
	// item:getTile()
	const Item* item = getUserdata<const Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	const Tile* tile = item->getTile();
	if (tile) {
		pushUserdata<const Tile>(L, tile);
		setMetatable(L, -1, "Tile");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemHasAttribute(lua_State* L)
{
	// item:hasAttribute(key)
	const Item* item = getUserdata<const Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	itemAttrTypes attribute;
	if (isInteger(L, 2)) {
		attribute = getInteger<itemAttrTypes>(L, 2);
	} else if (isString(L, 2)) {
		attribute = stringToItemAttribute(getString(L, 2));
	} else {
		attribute = ITEM_ATTRIBUTE_NONE;
	}

	pushBoolean(L, item->hasAttribute(attribute));
	return 1;
}

int luaItemGetAttribute(lua_State* L)
{
	// item:getAttribute(key)
	const Item* item = getUserdata<const Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	itemAttrTypes attribute;
	if (isInteger(L, 2)) {
		attribute = getInteger<itemAttrTypes>(L, 2);
	} else if (isString(L, 2)) {
		attribute = stringToItemAttribute(getString(L, 2));
	} else {
		attribute = ITEM_ATTRIBUTE_NONE;
	}

	if (ItemAttributes::isIntAttrType(attribute)) {
		if (attribute == ITEM_ATTRIBUTE_DURATION) {
			lua_pushnumber(L, item->getDuration());
			return 1;
		}
		lua_pushinteger(L, item->getIntAttr(attribute));
	} else if (ItemAttributes::isStrAttrType(attribute)) {
		pushString(L, item->getStrAttr(attribute));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemSetAttribute(lua_State* L)
{
	// item:setAttribute(key, value)
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	itemAttrTypes attribute;
	if (isInteger(L, 2)) {
		attribute = getInteger<itemAttrTypes>(L, 2);
	} else if (isString(L, 2)) {
		attribute = stringToItemAttribute(getString(L, 2));
	} else {
		attribute = ITEM_ATTRIBUTE_NONE;
	}

	if (ItemAttributes::isIntAttrType(attribute)) {
		switch (attribute) {
			case ITEM_ATTRIBUTE_UNIQUEID: {
				reportErrorFunc(L, "Attempt to set protected key \"uid\"");
				pushBoolean(L, false);
				return 1;
			}
			case ITEM_ATTRIBUTE_DECAYSTATE: {
				ItemDecayState_t decayState = getNumber<ItemDecayState_t>(L, 3);
				if (decayState == DECAYING_FALSE || decayState == DECAYING_STOPPING) {
					g_game.stopDecay(item);
				} else {
					g_game.startDecay(item);
				}
				pushBoolean(L, true);
				return 1;
			}
			case ITEM_ATTRIBUTE_DURATION: {
				item->setDecaying(DECAYING_PENDING);
				item->setDuration(getInteger<int32_t>(L, 3));
				g_game.startDecay(item);
				pushBoolean(L, true);
				return 1;
			}
			case ITEM_ATTRIBUTE_DURATION_TIMESTAMP: {
				reportErrorFunc(L, "Attempt to set protected key \"duration timestamp\"");
				pushBoolean(L, false);
				return 1;
			}
			default:
				break;
		}

		item->setIntAttr(attribute, getInteger<int64_t>(L, 3));
		pushBoolean(L, true);
	} else if (ItemAttributes::isStrAttrType(attribute)) {
		item->setStrAttr(attribute, getString(L, 3));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemRemoveAttribute(lua_State* L)
{
	// item:removeAttribute(key)
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	itemAttrTypes attribute;
	if (isInteger(L, 2)) {
		attribute = getInteger<itemAttrTypes>(L, 2);
	} else if (isString(L, 2)) {
		attribute = stringToItemAttribute(getString(L, 2));
	} else {
		attribute = ITEM_ATTRIBUTE_NONE;
	}

	bool ret = (attribute != ITEM_ATTRIBUTE_UNIQUEID);
	if (ret) {
		ret = (attribute != ITEM_ATTRIBUTE_DURATION_TIMESTAMP);
		if (ret) {
			item->removeAttribute(attribute);
		} else {
			reportErrorFunc(L, "Attempt to erase protected key \"duration timestamp\"");
		}
	} else {
		reportErrorFunc(L, "Attempt to erase protected key \"uid\"");
	}
	pushBoolean(L, ret);
	return 1;
}

int luaItemGetCustomAttribute(lua_State* L)
{
	// item:getCustomAttribute(key)
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	const ItemAttributes::CustomAttribute* attr;
	if (isInteger(L, 2)) {
		attr = item->getCustomAttribute(getInteger<int64_t>(L, 2));
	} else if (isString(L, 2)) {
		attr = item->getCustomAttribute(getString(L, 2));
	} else {
		lua_pushnil(L);
		return 1;
	}

	if (attr) {
		attr->pushToLua(L);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemSetCustomAttribute(lua_State* L)
{
	// item:setCustomAttribute(key, value)
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	std::string key;
	if (isInteger(L, 2)) {
		key = std::to_string(getInteger<int64_t>(L, 2));
	} else if (isString(L, 2)) {
		key = getString(L, 2);
	} else {
		lua_pushnil(L);
		return 1;
	}

	ItemAttributes::CustomAttribute val;
	if (isInteger(L, 3)) {
		double tmp = getNumber<double>(L, 3);
		if (std::floor(tmp) < tmp) {
			val.set<double>(tmp);
		} else {
			val.set<int64_t>(tmp);
		}
	} else if (isString(L, 3)) {
		val.set<std::string>(getString(L, 3));
	} else if (isBoolean(L, 3)) {
		val.set<bool>(getBoolean(L, 3));
	} else {
		lua_pushnil(L);
		return 1;
	}

	item->setCustomAttribute(key, val);
	pushBoolean(L, true);
	return 1;
}

int luaItemRemoveCustomAttribute(lua_State* L)
{
	// item:removeCustomAttribute(key)
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	if (isInteger(L, 2)) {
		pushBoolean(L, item->removeCustomAttribute(getInteger<int64_t>(L, 2)));
	} else if (isString(L, 2)) {
		pushBoolean(L, item->removeCustomAttribute(getString(L, 2)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemMoveTo(lua_State* L)
{
	// item:moveTo(position or cylinder[, flags])
	Item** itemPtr = getRawUserdata<Item>(L, 1);
	if (!itemPtr) {
		lua_pushnil(L);
		return 1;
	}

	Item* item = *itemPtr;
	if (!item || item->isRemoved()) {
		lua_pushnil(L);
		return 1;
	}

	Cylinder* toCylinder;
	if (isUserdata(L, 2)) {
		const LuaDataType type = getUserdataType(L, 2);
		switch (type) {
			case LuaData_Container:
				toCylinder = getUserdata<Container>(L, 2);
				break;
			case LuaData_Player:
				toCylinder = getUserdata<Player>(L, 2);
				break;
			case LuaData_Tile:
				toCylinder = getUserdata<Tile>(L, 2);
				break;
			default:
				toCylinder = nullptr;
				break;
		}
	} else {
		toCylinder = g_game.map.getTile(getPosition(L, 2));
	}

	if (!toCylinder) {
		lua_pushnil(L);
		return 1;
	}

	if (item->getParent() == toCylinder) {
		pushBoolean(L, true);
		return 1;
	}

	uint32_t flags = getInteger<uint32_t>(
	    L, 3, FLAG_NOLIMIT | FLAG_IGNOREBLOCKITEM | FLAG_IGNOREBLOCKCREATURE | FLAG_IGNORENOTMOVEABLE);

	if (item && item->getParent() == VirtualCylinder::virtualCylinder) {
		pushBoolean(L, g_game.internalAddItem(toCylinder, item, INDEX_WHEREEVER, flags) == RETURNVALUE_NOERROR);
	} else {
		Item* moveItem = nullptr;
		ReturnValue ret = g_game.internalMoveItem(item->getParent(), toCylinder, INDEX_WHEREEVER, item,
		                                          item->getItemCount(), &moveItem, flags);
		if (moveItem) {
			*itemPtr = moveItem;
		}
		pushBoolean(L, ret == RETURNVALUE_NOERROR);
	}
	return 1;
}

int luaItemTransform(lua_State* L)
{
	// item:transform(itemId[, count/subType = -1])
	Item** itemPtr = getRawUserdata<Item>(L, 1);
	if (!itemPtr) {
		lua_pushnil(L);
		return 1;
	}

	Item*& item = *itemPtr;
	if (!item) {
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
	if (item->getID() == itemId && (subType == -1 || subType == item->getSubType())) {
		pushBoolean(L, true);
		return 1;
	}

	const ItemType& it = Item::items[itemId];
	if (it.stackable) {
		subType = std::min<int32_t>(subType, it.stackSize);
	}

	ScriptEnvironment* env = LuaScriptInterface::getScriptEnv();
	uint32_t uid = env->addThing(item);

	Item* newItem = g_game.transformItem(item, itemId, subType);
	if (item->isRemoved()) {
		env->removeItemByUID(uid);
	}

	if (newItem && newItem != item) {
		env->insertItem(uid, newItem);
	}

	item = newItem;
	pushBoolean(L, true);
	return 1;
}

int luaItemDecay(lua_State* L)
{
	// item:decay(decayId)
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		if (isInteger(L, 2)) {
			ItemType& it = Item::items.getItemType(item->getID());
			it.decayTo = getInteger<int32_t>(L, 2);
		}

		item->startDecaying();
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetSpecialDescription(lua_State* L)
{
	// item:getSpecialDescription()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		pushString(L, item->getSpecialDescription());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemHasProperty(lua_State* L)
{
	// item:hasProperty(property)
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		ITEMPROPERTY property = getInteger<ITEMPROPERTY>(L, 2);
		pushBoolean(L, item->hasProperty(property));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemIsLoadedFromMap(lua_State* L)
{
	// item:isLoadedFromMap()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		pushBoolean(L, item->isLoadedFromMap());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemSetReflect(lua_State* L)
{
	// item:setReflect(combatType, reflect)
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	item->setReflect(getInteger<CombatType_t>(L, 2), getReflect(L, 3));
	pushBoolean(L, true);
	return 1;
}

int luaItemGetReflect(lua_State* L)
{
	// item:getReflect(combatType[, total = true])
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		pushReflect(L, item->getReflect(getInteger<CombatType_t>(L, 2), getBoolean(L, 3, true)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemSetBoostPercent(lua_State* L)
{
	// item:setBoostPercent(combatType, percent)
	Item* item = getUserdata<Item>(L, 1);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	item->setBoostPercent(getInteger<CombatType_t>(L, 2), getInteger<uint16_t>(L, 3));
	pushBoolean(L, true);
	return 1;
}

int luaItemGetBoostPercent(lua_State* L)
{
	// item:getBoostPercent(combatType[, total = true])
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		lua_pushinteger(L, item->getBoostPercent(getInteger<CombatType_t>(L, 2), getBoolean(L, 3, true)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemIsMagicField(lua_State* L)
{
	// item:isMagicField()
	const Item* item = getUserdata<const Item>(L, 1);
	if (item) {
		pushBoolean(L, item->getMagicField() != nullptr);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemGetMagicField(lua_State* L)
{
	// item:getMagicField()
	Item* item = getUserdata<Item>(L, 1);
	if (item) {
		MagicField* field = item->getMagicField();
		if (field) {
			pushUserdata<Item>(L, field);
			setItemMetatable(L, -1, field);
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemOnStepInField(lua_State* L)
{
	// item:onStepInField(creature)
	Item* item = getUserdata<Item>(L, 1);
	Creature* creature = getCreature(L, 2);
	if (item && creature) {
		MagicField* field = item->getMagicField();
		if (field) {
			field->onStepInField(creature);
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

void LuaScriptInterface::registerItem()
{
	// Item
	registerClass("Item", "", luaItemCreate);
	registerMetaMethod("Item", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("Item", "isItem", luaItemIsItem);

	registerMethod("Item", "getParent", luaItemGetParent);
	registerMethod("Item", "getTopParent", luaItemGetTopParent);

	registerMethod("Item", "getId", luaItemGetId);

	registerMethod("Item", "clone", luaItemClone);
	registerMethod("Item", "split", luaItemSplit);
	registerMethod("Item", "remove", luaItemRemove);

	registerMethod("Item", "getUniqueId", luaItemGetUniqueId);
	registerMethod("Item", "getActionId", luaItemGetActionId);
	registerMethod("Item", "setActionId", luaItemSetActionId);

	registerMethod("Item", "getCount", luaItemGetCount);
	registerMethod("Item", "getCharges", luaItemGetCharges);
	registerMethod("Item", "getFluidType", luaItemGetFluidType);
	registerMethod("Item", "getWeight", luaItemGetWeight);
	registerMethod("Item", "getWorth", luaItemGetWorth);

	registerMethod("Item", "getSubType", luaItemGetSubType);

	registerMethod("Item", "getName", luaItemGetName);
	registerMethod("Item", "getPluralName", luaItemGetPluralName);
	registerMethod("Item", "getArticle", luaItemGetArticle);

	registerMethod("Item", "getPosition", luaItemGetPosition);
	registerMethod("Item", "getTile", luaItemGetTile);

	registerMethod("Item", "hasAttribute", luaItemHasAttribute);
	registerMethod("Item", "getAttribute", luaItemGetAttribute);
	registerMethod("Item", "setAttribute", luaItemSetAttribute);
	registerMethod("Item", "removeAttribute", luaItemRemoveAttribute);
	registerMethod("Item", "getCustomAttribute", luaItemGetCustomAttribute);
	registerMethod("Item", "setCustomAttribute", luaItemSetCustomAttribute);
	registerMethod("Item", "removeCustomAttribute", luaItemRemoveCustomAttribute);

	registerMethod("Item", "moveTo", luaItemMoveTo);
	registerMethod("Item", "transform", luaItemTransform);
	registerMethod("Item", "decay", luaItemDecay);

	registerMethod("Item", "getSpecialDescription", luaItemGetSpecialDescription);

	registerMethod("Item", "hasProperty", luaItemHasProperty);
	registerMethod("Item", "isLoadedFromMap", luaItemIsLoadedFromMap);

	registerMethod("Item", "setReflect", luaItemSetReflect);
	registerMethod("Item", "getReflect", luaItemGetReflect);

	registerMethod("Item", "setBoostPercent", luaItemSetBoostPercent);
	registerMethod("Item", "getBoostPercent", luaItemGetBoostPercent);

	registerMethod("Item", "isMagicField", luaItemIsMagicField);
	registerMethod("Item", "getMagicField", luaItemGetMagicField);
	registerMethod("Item", "onStepInField", luaItemOnStepInField);

	registerMethod("Item", "getImbuementSlots", LuaScriptInterface::luaItemGetImbuementSlots);
	registerMethod("Item", "getFreeImbuementSlots", LuaScriptInterface::luaItemGetFreeImbuementSlots);
	registerMethod("Item", "canImbue", LuaScriptInterface::luaItemCanImbue);
	registerMethod("Item", "addImbuementSlots", LuaScriptInterface::luaItemAddImbuementSlots);
	registerMethod("Item", "removeImbuementSlots", LuaScriptInterface::luaItemRemoveImbuementSlots);
	registerMethod("Item", "hasImbuementType", LuaScriptInterface::luaItemHasImbuementType);
	registerMethod("Item", "hasImbuement", LuaScriptInterface::luaItemHasImbuement);
	registerMethod("Item", "hasImbuements", LuaScriptInterface::luaItemHasImbuements);
	registerMethod("Item", "canApplyImbuement", LuaScriptInterface::luaItemCanApplyImbuement);
	registerMethod("Item", "addImbuement", LuaScriptInterface::luaItemAddImbuement);
	registerMethod("Item", "removeImbuement", LuaScriptInterface::luaItemRemoveImbuement);
	registerMethod("Item", "getImbuements", LuaScriptInterface::luaItemGetImbuements);
}
