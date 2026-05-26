// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "luascript.h"
#include "teleport.h"

namespace {
using namespace Lua;

// Teleport
int luaTeleportCreate(lua_State* L)
{
	// Teleport(uid)
	uint32_t id = getInteger<uint32_t>(L, 2);

	Item* item = LuaScriptInterface::getScriptEnv()->getItemByUID(id);
	if (item && item->getTeleport()) {
		pushItem(L, item);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTeleportGetDestination(lua_State* L)
{
	// teleport:getDestination()
	Teleport* teleport = getUserdata<Teleport>(L, 1);
	if (teleport) {
		pushPosition(L, teleport->getDestPos());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaTeleportSetDestination(lua_State* L)
{
	// teleport:setDestination(position)
	Teleport* teleport = getUserdata<Teleport>(L, 1);
	if (teleport) {
		teleport->setDestPos(getPosition(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}
} // namespace

void LuaScriptInterface::registerTeleport()
{
	// Teleport
	registerClass("Teleport", "Item", luaTeleportCreate);
	registerMetaMethod("Teleport", "__eq", LuaScriptInterface::luaUserdataCompare);
	registerMetaMethod("Teleport", "__gc", LuaScriptInterface::luaItemGC);

	registerMethod("Teleport", "getDestination", luaTeleportGetDestination);
	registerMethod("Teleport", "setDestination", luaTeleportSetDestination);
}
