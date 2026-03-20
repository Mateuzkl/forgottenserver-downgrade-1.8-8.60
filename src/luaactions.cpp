// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "actions.h"
#include "luascript.h"
#include "script.h"

extern Actions* g_actions;
extern Scripts* g_scripts;

namespace {
using namespace Lua;

// Action
int luaCreateAction(lua_State* L)
{
	// Action()
	if (LuaScriptInterface::getScriptEnv()->getScriptInterface() != &g_scripts->getScriptInterface()) {
		reportErrorFunc(L, "Actions can only be registered in the Scripts interface.");
		lua_pushnil(L);
		return 1;
	}

	Action* action = new Action(LuaScriptInterface::getScriptEnv()->getScriptInterface());
	action->fromLua = true;
	pushUserdata<Action>(L, action);
	setMetatable(L, -1, "Action");
	return 1;
}

int luaActionOnUse(lua_State* L)
{
	// action:onUse(callback)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		if (!action->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}

		action->scripted = true;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaActionRegister(lua_State* L)
{
	// action:register()
	Action** actionPtr = getRawUserdata<Action>(L, 1);
	if (auto action = *actionPtr) {
		if (!action->isScripted()) {
			pushBoolean(L, false);
			return 1;
		}

		pushBoolean(L, g_actions->registerLuaEvent(action));
		*actionPtr = nullptr;
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaActionItemId(lua_State* L)
{
	// action:id(ids)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				action->addItemId(getInteger<uint16_t>(L, 2 + i));
			}
		} else {
			action->addItemId(getInteger<uint16_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaActionActionId(lua_State* L)
{
	// action:aid(aids)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				action->addActionId(getInteger<uint16_t>(L, 2 + i));
			}
		} else {
			action->addActionId(getInteger<uint16_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaActionUniqueId(lua_State* L)
{
	// action:uid(uids)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				action->addUniqueId(getInteger<uint16_t>(L, 2 + i));
			}
		} else {
			action->addUniqueId(getInteger<uint16_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaActionAllowFarUse(lua_State* L)
{
	// action:allowFarUse(bool)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		action->setAllowFarUse(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaActionBlockWalls(lua_State* L)
{
	// action:blockWalls(bool)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		action->setCheckLineOfSight(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaActionCheckFloor(lua_State* L)
{
	// action:checkFloor(bool)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		action->setCheckFloor(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaActionPosition(lua_State* L)
{
	// action:position(position)
	// action:position({position1, position2, ...})
	Action* action = getUserdata<Action>(L, 1);
	if (!action) {
		lua_pushnil(L);
		return 1;
	}

	std::vector<Position> positionList;

	if (isUserdata(L, 2) || lua_istable(L, 2)) {
		lua_getfield(L, 2, "x");
		bool hasX = !lua_isnil(L, -1);
		lua_pop(L, 1);

		if (hasX) {
			Position pos = getPosition(L, 2);
			positionList.push_back(pos);
		} else {
			lua_pushnil(L);
			while (lua_next(L, 2) != 0) {
				if (isUserdata(L, -1) || lua_istable(L, -1)) {
					Position pos = getPosition(L, -1);
					positionList.push_back(pos);
				}
				lua_pop(L, 1);
			}
		}
	} else {
		lua_pushnil(L);
		return 1;
	}

	const auto& existingPositions = action->getPositionList();
	positionList.insert(positionList.end(), existingPositions.begin(), existingPositions.end());
	action->setPositionList(positionList);

	pushBoolean(L, true);
	return 1;
}
} // namespace

void LuaScriptInterface::registerActions()
{
	registerClass("Action", "", luaCreateAction);

	registerMethod("Action", "onUse", luaActionOnUse);
	registerMethod("Action", "register", luaActionRegister);
	registerMethod("Action", "id", luaActionItemId);
	registerMethod("Action", "aid", luaActionActionId);
	registerMethod("Action", "uid", luaActionUniqueId);
	registerMethod("Action", "position", luaActionPosition);
	registerMethod("Action", "allowFarUse", luaActionAllowFarUse);
	registerMethod("Action", "blockWalls", luaActionBlockWalls);
	registerMethod("Action", "checkFloor", luaActionCheckFloor);
}
