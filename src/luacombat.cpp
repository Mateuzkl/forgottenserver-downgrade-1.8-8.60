// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "combat.h"
#include "game.h"
#include "luascript.h"
#include "luavariant.h"
#include "matrixarea.h"

extern Game g_game;
extern LuaEnvironment g_luaEnvironment;

namespace {
using namespace Lua;

// Combat
int luaCombatCreate(lua_State* L)
{
	// Combat()
	pushSharedPtr(L, g_luaEnvironment.createCombatObject(LuaScriptInterface::getScriptEnv()->getScriptInterface()));
	setMetatable(L, -1, "Combat");
	return 1;
}

int luaCombatDelete(lua_State* L)
{
	if (!isType<Combat>(L, 1)) {
		return 0;
	}

	Combat_ptr& combat = getSharedPtr<Combat>(L, 1);
	if (combat) {
		combat.reset();
	}
	return 0;
}

int luaCombatSetParameter(lua_State* L)
{
	// combat:setParameter(key, value)
	if (!isType<Combat>(L, 1)) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::COMBAT_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	const Combat_ptr& combat = getSharedPtr<Combat>(L, 1);
	if (!combat) {
		lua_pushnil(L);
		return 1;
	}

	CombatParam_t key = getInteger<CombatParam_t>(L, 2);
	uint32_t value;
	if (isBoolean(L, 3)) {
		value = getBoolean(L, 3) ? 1 : 0;
	} else {
		value = getInteger<uint32_t>(L, 3);
	}
	combat->setParam(key, value);
	pushBoolean(L, true);
	return 1;
}

int luaCombatGetParameter(lua_State* L)
{
	// combat:getParameter(key)
	if (!isType<Combat>(L, 1)) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::COMBAT_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	const Combat_ptr& combat = getSharedPtr<Combat>(L, 1);
	if (!combat) {
		lua_pushnil(L);
		return 1;
	}

	int32_t value = combat->getParam(getInteger<CombatParam_t>(L, 2));
	if (value == std::numeric_limits<int32_t>().max()) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, value);
	return 1;
}

int luaCombatSetFormula(lua_State* L)
{
	// combat:setFormula(type, mina, minb, maxa, maxb)
	if (!isType<Combat>(L, 1)) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::COMBAT_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	const Combat_ptr& combat = getSharedPtr<Combat>(L, 1);
	if (!combat) {
		lua_pushnil(L);
		return 1;
	}

	formulaType_t type = getInteger<formulaType_t>(L, 2);
	double mina = getNumber<double>(L, 3);
	double minb = getNumber<double>(L, 4);
	double maxa = getNumber<double>(L, 5);
	double maxb = getNumber<double>(L, 6);
	combat->setPlayerCombatValues(type, mina, minb, maxa, maxb);
	pushBoolean(L, true);
	return 1;
}

int luaCombatSetArea(lua_State* L)
{
	// combat:setArea(area)
	if (LuaScriptInterface::getScriptEnv()->getScriptId() != EVENT_ID_LOADING) {
		reportErrorFunc(L, "This function can only be used while loading the script.");
		lua_pushnil(L);
		return 1;
	}

	const AreaCombat* area = g_luaEnvironment.getAreaObject(getInteger<uint32_t>(L, 2));
	if (!area) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::AREA_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	if (!isType<Combat>(L, 1)) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::COMBAT_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	const Combat_ptr& combat = getSharedPtr<Combat>(L, 1);
	if (!combat) {
		lua_pushnil(L);
		return 1;
	}

	combat->setArea(new AreaCombat(*area));
	pushBoolean(L, true);
	return 1;
}

int luaCombatAddCondition(lua_State* L)
{
	// combat:addCondition(condition)
	if (!isType<Combat>(L, 1)) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::COMBAT_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	const Combat_ptr& combat = getSharedPtr<Combat>(L, 1);
	if (!combat) {
		lua_pushnil(L);
		return 1;
	}

	Condition* condition = getUserdata<Condition>(L, 2);
	if (condition) {
		combat->addCondition(condition->clone());
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCombatClearConditions(lua_State* L)
{
	// combat:clearConditions()
	if (!isType<Combat>(L, 1)) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::COMBAT_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	const Combat_ptr& combat = getSharedPtr<Combat>(L, 1);
	if (!combat) {
		lua_pushnil(L);
		return 1;
	}

	combat->clearConditions();
	pushBoolean(L, true);
	return 1;
}

int luaCombatSetCallback(lua_State* L)
{
	// combat:setCallback(key, callback)
	// callback can be either a function or a string with the function name
	if (!isType<Combat>(L, 1)) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::COMBAT_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	const Combat_ptr& combat = getSharedPtr<Combat>(L, 1);
	if (!combat) {
		lua_pushnil(L);
		return 1;
	}

	auto key = getInteger<CallBackParam>(L, 2);

	if (!combat->setCallback(key)) {
		reportErrorFunc(L, "Invalid callback parameter");
		pushBoolean(L, false);
		return 1;
	}

	auto callback = combat->getCallback(key);
	if (!callback) {
		reportErrorFunc(L, "Failed to get callback");
		pushBoolean(L, false);
		return 1;
	}

	auto* scriptInterface = LuaScriptInterface::getScriptEnv()->getScriptInterface();
	if (!scriptInterface) {
		reportErrorFunc(L, "Script interface not found");
		pushBoolean(L, false);
		return 1;
	}

	bool success = false;

	if (isFunction(L, 3)) {
		lua_pushvalue(L, 3);
		success = callback->loadCallBack(scriptInterface);
	} else if (isString(L, 3)) {
		std::string functionName = getString(L, 3);
		success = callback->loadCallBack(scriptInterface, functionName);
	} else {
		reportErrorFunc(L, "Callback must be a function or function name");
		pushBoolean(L, false);
		return 1;
	}

	if (!success) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::CALLBACK_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	pushBoolean(L, true);
	return 1;
}

int luaCombatSetOrigin(lua_State* L)
{
	// combat:setOrigin(origin)
	if (!isType<Combat>(L, 1)) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::COMBAT_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	const Combat_ptr& combat = getSharedPtr<Combat>(L, 1);
	if (!combat) {
		lua_pushnil(L);
		return 1;
	}

	combat->setOrigin(getInteger<CombatOrigin>(L, 2));
	pushBoolean(L, true);
	return 1;
}

int luaCombatExecute(lua_State* L)
{
	// combat:execute(creature, variant)
	if (!isType<Combat>(L, 1)) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::COMBAT_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	const Combat_ptr& combat = getSharedPtr<Combat>(L, 1);
	if (!combat) {
		lua_pushnil(L);
		return 1;
	}

	if (isUserdata(L, 2)) {
		LuaDataType type = getUserdataType(L, 2);
		if (type != LuaData_Player && type != LuaData_Monster && type != LuaData_Npc) {
			pushBoolean(L, false);
			return 1;
		}
	}

	Creature* creature = getCreature(L, 2);

	const LuaVariant& variant = getVariant(L, 3);
	switch (variant.type()) {
		case VARIANT_NUMBER: {
			Creature* target = g_game.getCreatureByID(variant.getNumber());
			if (!target) {
				pushBoolean(L, false);
				return 1;
			}

			if (combat->hasArea()) {
				combat->doCombat(creature, target->getPosition());
			} else {
				combat->doCombat(creature, target);
			}
			break;
		}

		case VARIANT_POSITION: {
			combat->doCombat(creature, variant.getPosition());
			break;
		}

		case VARIANT_TARGETPOSITION: {
			if (combat->hasArea()) {
				combat->doCombat(creature, variant.getTargetPosition());
			} else {
				combat->postCombatEffects(creature, variant.getTargetPosition());
				g_game.addMagicEffect(variant.getTargetPosition(), CONST_ME_POFF);
			}
			break;
		}

		case VARIANT_STRING: {
			Player* target = g_game.getPlayerByName(variant.getString());
			if (!target) {
				pushBoolean(L, false);
				return 1;
			}

			combat->doCombat(creature, target);
			break;
		}

		case VARIANT_NONE: {
			reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::VARIANT_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}

		default: {
			break;
		}
	}

	pushBoolean(L, true);
	return 1;
}
} // namespace

void LuaScriptInterface::registerCombat()
{
	// Combat
	registerClass("Combat", "", luaCombatCreate);
	registerMetaMethod("Combat", "__eq", LuaScriptInterface::luaUserdataCompare);
	registerMetaMethod("Combat", "__gc", luaCombatDelete);
	registerMetaMethod("Combat", "__close", luaCombatDelete);
	registerMethod("Combat", "delete", luaCombatDelete);

	registerMethod("Combat", "setParameter", luaCombatSetParameter);
	registerMethod("Combat", "getParameter", luaCombatGetParameter);

	registerMethod("Combat", "setFormula", luaCombatSetFormula);

	registerMethod("Combat", "setArea", luaCombatSetArea);
	registerMethod("Combat", "addCondition", luaCombatAddCondition);
	registerMethod("Combat", "clearConditions", luaCombatClearConditions);
	registerMethod("Combat", "setCallback", luaCombatSetCallback);
	registerMethod("Combat", "setOrigin", luaCombatSetOrigin);

	registerMethod("Combat", "execute", luaCombatExecute);
}
