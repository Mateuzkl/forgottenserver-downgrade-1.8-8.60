// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "game.h"
#include "guild.h"
#include "luascript.h"

extern Game g_game;

namespace {
using namespace Lua;

// Guild
int luaGuildCreate(lua_State* L)
{
	// Guild(id)
	uint32_t id = getInteger<uint32_t>(L, 2);

	if (const auto& guild = g_game.getGuild(id)) {
		pushSharedPtr(L, guild);
		setMetatable(L, -1, "Guild");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildGetId(lua_State* L)
{
	// guild:getId()
	const auto& guild = getSharedPtr<const Guild>(L, 1);
	if (guild) {
		lua_pushinteger(L, guild->getId());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildGetName(lua_State* L)
{
	// guild:getName()
	const auto& guild = getSharedPtr<const Guild>(L, 1);
	if (guild) {
		pushString(L, guild->getName());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildGetMembersOnline(lua_State* L)
{
	// guild:getMembersOnline()
	const auto& guild = getSharedPtr<const Guild>(L, 1);
	if (!guild) {
		lua_pushnil(L);
		return 1;
	}

	const auto members = guild->getMembersOnlineRefs();
	lua_createtable(L, members.size(), 0);

	int index = 0;
	for (const auto& player : members) {
		pushUserdata<Player>(L, player.get());
		setMetatable(L, -1, "Player");
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaGuildGetMemberCount(lua_State* L)
{
	// guild:getMemberCount()
	const auto& guild = getSharedPtr<const Guild>(L, 1);
	if (guild) {
		lua_pushinteger(L, guild->getMemberCount());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildAddRank(lua_State* L)
{
	// guild:addRank(id, name, level)
	if (const auto& guild = getSharedPtr<Guild>(L, 1)) {
		uint32_t id = getInteger<uint32_t>(L, 2);
		const std::string& name = getString(L, 3);
		uint8_t level = getInteger<uint8_t>(L, 4);
		guild->addRank(id, name, level);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildGetRankById(lua_State* L)
{
	// guild:getRankById(id)
	const auto& guild = getSharedPtr<const Guild>(L, 1);
	if (!guild) {
		lua_pushnil(L);
		return 1;
	}

	uint32_t id = getInteger<uint32_t>(L, 2);
	if (auto rank = guild->getRankById(id)) {
		lua_createtable(L, 0, 3);
		setField(L, "id", rank->id);
		setField(L, "name", rank->name);
		setField(L, "level", rank->level);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildGetRankByLevel(lua_State* L)
{
	// guild:getRankByLevel(level)
	const auto& guild = getSharedPtr<const Guild>(L, 1);
	if (!guild) {
		lua_pushnil(L);
		return 1;
	}

	uint8_t level = getInteger<uint8_t>(L, 2);
	if (auto rank = guild->getRankByLevel(level)) {
		lua_createtable(L, 0, 3);
		setField(L, "id", rank->id);
		setField(L, "name", rank->name);
		setField(L, "level", rank->level);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildGetMotd(lua_State* L)
{
	// guild:getMotd()
	const auto& guild = getSharedPtr<const Guild>(L, 1);
	if (guild) {
		pushString(L, guild->getMotd());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildSetMotd(lua_State* L)
{
	// guild:setMotd(motd)
	if (const auto& guild = getSharedPtr<Guild>(L, 1)) {
		const std::string& motd = getString(L, 2);
		guild->setMotd(motd);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildGetBankBalance(lua_State* L)
{
	// guild:getBankBalance()
	const auto& guild = getSharedPtr<const Guild>(L, 1);
	if (guild) {
		lua_pushinteger(L, guild->getBankBalance());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildSetBankBalance(lua_State* L)
{
	// guild:setBankBalance(balance)
	if (const auto& guild = getSharedPtr<Guild>(L, 1)) {
		uint64_t balance = getInteger<uint64_t>(L, 2);
		pushBoolean(L, guild->setBankBalance(balance));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildIsBankBalanceBlocked(lua_State* L)
{
	// guild:isBankBalanceBlocked() or Guild.isBankBalanceBlocked(id)
	if (lua_type(L, 1) == LUA_TNUMBER) {
		pushBoolean(L, IOGuild::isBankBalanceBlocked(getInteger<uint32_t>(L, 1)));
	} else if (const auto& guild = getSharedPtr<const Guild>(L, 1)) {
		pushBoolean(L, IOGuild::isBankBalanceBlocked(guild->getId()));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildGetOwnerGUID(lua_State* L)
{
	// guild:getOwnerGUID()
	const auto& guild = getSharedPtr<const Guild>(L, 1);
	if (guild) {
		lua_pushinteger(L, guild->getOwnerGUID());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGuildGetHouseId(lua_State* L)
{
	// guild:getHouseId()
	const auto& guild = getSharedPtr<const Guild>(L, 1);
	if (guild) {
		lua_pushinteger(L, guild->getHouseId());
	} else {
		lua_pushnil(L);
	}
	return 1;
}
int luaGuildDelete(lua_State* L)
{
	if (!isType<Guild>(L, 1)) {
		return 0;
	}

	auto& guild = getSharedPtr<Guild>(L, 1);
	if (guild) {
		guild.reset();
	}
	return 0;
}
} // namespace

void LuaScriptInterface::registerGuild()
{
	// Guild
	registerClass("Guild", "", luaGuildCreate);
	registerMetaMethod("Guild", "__eq", LuaScriptInterface::luaUserdataCompare);
	registerMetaMethod("Guild", "__gc", luaGuildDelete);

	registerMethod("Guild", "getId", luaGuildGetId);
	registerMethod("Guild", "getName", luaGuildGetName);
	registerMethod("Guild", "getMembersOnline", luaGuildGetMembersOnline);
	registerMethod("Guild", "getMemberCount", luaGuildGetMemberCount);

	registerMethod("Guild", "addRank", luaGuildAddRank);
	registerMethod("Guild", "getRankById", luaGuildGetRankById);
	registerMethod("Guild", "getRankByLevel", luaGuildGetRankByLevel);

	registerMethod("Guild", "getMotd", luaGuildGetMotd);
	registerMethod("Guild", "setMotd", luaGuildSetMotd);

	registerMethod("Guild", "getBankBalance", luaGuildGetBankBalance);
	registerMethod("Guild", "setBankBalance", luaGuildSetBankBalance);
	registerMethod("Guild", "isBankBalanceBlocked", luaGuildIsBankBalanceBlocked);
	registerMethod("Guild", "getOwnerGUID", luaGuildGetOwnerGUID);
	registerMethod("Guild", "getHouseId", luaGuildGetHouseId);
}
