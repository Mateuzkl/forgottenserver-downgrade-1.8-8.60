// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "condition.h"
#include "luascript.h"

namespace {
using namespace Lua;

// Condition
int luaConditionCreate(lua_State* L)
{
	// Condition(conditionType[, conditionId = CONDITIONID_COMBAT])
	ConditionType_t conditionType = getInteger<ConditionType_t>(L, 2);
	ConditionId_t conditionId = getInteger<ConditionId_t>(L, 3, CONDITIONID_COMBAT);

	auto condition = Condition::createCondition(conditionId, conditionType, 0, 0);
	if (condition) {
		pushSharedPtr(L, std::move(condition));
		setMetatable(L, -1, "Condition");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionGetId(lua_State* L)
{
	// condition:getId()
	Condition* condition = getSharedUserdata<Condition>(L, 1);
	if (condition) {
		lua_pushinteger(L, condition->getId());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionGetSubId(lua_State* L)
{
	// condition:getSubId()
	Condition* condition = getSharedUserdata<Condition>(L, 1);
	if (condition) {
		lua_pushinteger(L, condition->getSubId());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionGetType(lua_State* L)
{
	// condition:getType()
	Condition* condition = getSharedUserdata<Condition>(L, 1);
	if (condition) {
		lua_pushinteger(L, condition->getType());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionGetIcons(lua_State* L)
{
	// condition:getIcons()
	Condition* condition = getSharedUserdata<Condition>(L, 1);
	if (condition) {
		lua_pushinteger(L, condition->getIcons());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionGetEndTime(lua_State* L)
{
	// condition:getEndTime()
	const Condition* condition = getSharedUserdata<const Condition>(L, 1);
	if (condition) {
		lua_pushinteger(L, condition->getEndTime());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionClone(lua_State* L)
{
	// condition:clone()
	const Condition* condition = getSharedUserdata<const Condition>(L, 1);
	if (condition) {
		pushSharedPtr(L, condition->clone());
		setMetatable(L, -1, "Condition");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionGetTicks(lua_State* L)
{
	// condition:getTicks()
	const Condition* condition = getSharedUserdata<const Condition>(L, 1);
	if (condition) {
		lua_pushinteger(L, condition->getTicks());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionSetTicks(lua_State* L)
{
	// condition:setTicks(ticks)
	int32_t ticks = getInteger<int32_t>(L, 2);
	Condition* condition = getSharedUserdata<Condition>(L, 1);
	if (condition) {
		condition->setTicks(ticks);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionSetParameter(lua_State* L)
{
	// condition:setParameter(key, value)
	Condition* condition = getSharedUserdata<Condition>(L, 1);
	if (!condition) {
		lua_pushnil(L);
		return 1;
	}

	ConditionParam_t key = getInteger<ConditionParam_t>(L, 2);
	int32_t value;
	if (isBoolean(L, 3)) {
		value = getBoolean(L, 3) ? 1 : 0;
	} else {
		value = getInteger<int32_t>(L, 3);
	}
	condition->setParam(key, value);
	pushBoolean(L, true);
	return 1;
}

int luaConditionGetParameter(lua_State* L)
{
	// condition:getParameter(key)
	const Condition* condition = getSharedUserdata<const Condition>(L, 1);
	if (!condition) {
		lua_pushnil(L);
		return 1;
	}

	int32_t value = condition->getParam(getInteger<ConditionParam_t>(L, 2));
	if (value == std::numeric_limits<int32_t>().max()) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, value);
	return 1;
}

int luaConditionSetFormula(lua_State* L)
{
	// condition:setFormula(mina, minb, maxa, maxb)
	double maxb = getNumber<double>(L, 5);
	double maxa = getNumber<double>(L, 4);
	double minb = getNumber<double>(L, 3);
	double mina = getNumber<double>(L, 2);
	ConditionSpeed* condition = dynamic_cast<ConditionSpeed*>(getSharedUserdata<Condition>(L, 1));
	if (condition) {
		condition->setFormulaVars(mina, minb, maxa, maxb);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionSetOutfit(lua_State* L)
{
	// condition:setOutfit(outfit)
	// condition:setOutfit(lookTypeEx, lookType, lookHead, lookBody, lookLegs, lookFeet[, lookAddons])
	Outfit_t outfit;
	if (isTable(L, 2)) {
		outfit = getOutfit(L, 2);
	} else {
		outfit.lookAddons = getInteger<uint8_t>(L, 8, outfit.lookAddons);
		outfit.lookFeet = getInteger<uint8_t>(L, 7);
		outfit.lookLegs = getInteger<uint8_t>(L, 6);
		outfit.lookBody = getInteger<uint8_t>(L, 5);
		outfit.lookHead = getInteger<uint8_t>(L, 4);
		outfit.lookType = getInteger<uint16_t>(L, 3);
		outfit.lookTypeEx = getInteger<uint16_t>(L, 2);
	}

	ConditionOutfit* condition = dynamic_cast<ConditionOutfit*>(getSharedUserdata<Condition>(L, 1));
	if (condition) {
		condition->setOutfit(outfit);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionAddDamage(lua_State* L)
{
	// condition:addDamage(rounds, time, value)
	int32_t value = getInteger<int32_t>(L, 4);
	int32_t time = getInteger<int32_t>(L, 3);
	int32_t rounds = getInteger<int32_t>(L, 2);
	ConditionDamage* condition = dynamic_cast<ConditionDamage*>(getSharedUserdata<Condition>(L, 1));
	if (condition) {
		pushBoolean(L, condition->addDamage(rounds, time, value));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionSetInitDamage(lua_State* L)
{
	// condition:setInitDamage(initDamage)
	ConditionDamage* condition = dynamic_cast<ConditionDamage*>(getSharedUserdata<Condition>(L, 1));
	if (condition) {
		int32_t initDamage = getInteger<int32_t>(L, 2);
		condition->setInitDamage(initDamage);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionGetTotalDamage(lua_State* L)
{
	// condition:getTotalDamage()
	const ConditionDamage* condition =
	    dynamic_cast<const ConditionDamage*>(getSharedUserdata<const Condition>(L, 1));
	if (condition) {
		lua_pushinteger(L, condition->getTotalDamage());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionIsConstant(lua_State* L)
{
	// condition:isConstant()
	const Condition* condition = getSharedUserdata<const Condition>(L, 1);
	if (condition) {
		pushBoolean(L, condition->isConstant());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionSetConstant(lua_State* L)
{
	// condition:setConstant(bool)
	Condition* condition = getSharedUserdata<Condition>(L, 1);
	if (condition) {
		condition->setConstant(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaConditionDelete(lua_State* L)
{
	return luaSharedPtrDelete<Condition>(L);
}
} // namespace

void LuaScriptInterface::registerCondition()
{
	// Condition
	registerClass("Condition", "", luaConditionCreate);
	registerMetaMethod("Condition", "__eq", LuaScriptInterface::luaUserdataCompare);
	registerMetaMethod("Condition", "__gc", LuaScriptInterface::luaSharedPtrGC<Condition>);
	registerMetaMethod("Condition", "__close", luaConditionDelete);
	registerMethod("Condition", "delete", luaConditionDelete);

	registerMethod("Condition", "getId", luaConditionGetId);
	registerMethod("Condition", "getSubId", luaConditionGetSubId);
	registerMethod("Condition", "getType", luaConditionGetType);
	registerMethod("Condition", "getIcons", luaConditionGetIcons);
	registerMethod("Condition", "getEndTime", luaConditionGetEndTime);

	registerMethod("Condition", "clone", luaConditionClone);

	registerMethod("Condition", "getTicks", luaConditionGetTicks);
	registerMethod("Condition", "setTicks", luaConditionSetTicks);

	registerMethod("Condition", "setParameter", luaConditionSetParameter);
	registerMethod("Condition", "getParameter", luaConditionGetParameter);

	registerMethod("Condition", "setFormula", luaConditionSetFormula);
	registerMethod("Condition", "setOutfit", luaConditionSetOutfit);

	registerMethod("Condition", "addDamage", luaConditionAddDamage);

	registerMethod("Condition", "setInitDamage", luaConditionSetInitDamage);
	registerMethod("Condition", "getTotalDamage", luaConditionGetTotalDamage);

	registerMethod("Condition", "isConstant", luaConditionIsConstant);
	registerMethod("Condition", "setConstant", luaConditionSetConstant);
}
