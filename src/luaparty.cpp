// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "game.h"
#include "luascript.h"
#include "party.h"

extern Game g_game;

namespace {
using namespace Lua;

// Party
int32_t luaPartyCreate(lua_State* L)
{
	// Party(userdata)
	Player* player = getUserdata<Player>(L, 2);
	if (!player) {
		lua_pushnil(L);
		return 1;
	}

	Party* party = player->getParty();
	if (!party) {
		auto partyPtr = Party::create(player);
		party = partyPtr.get();
		g_game.updatePlayerShield(player);
		player->sendCreatureSkull(player);
		pushUserdata<Party>(L, party);
		setMetatable(L, -1, "Party");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyDisband(lua_State* L)
{
	// party:disband()
	Party** partyPtr = getRawUserdata<Party>(L, 1);
	if (partyPtr && *partyPtr) {
		Party*& party = *partyPtr;
		party->disband();
		party = nullptr;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyGetLeader(lua_State* L)
{
	// party:getLeader()
	Party* party = getUserdata<Party>(L, 1);
	if (!party) {
		lua_pushnil(L);
		return 1;
	}

	auto leader = party->getLeader();
	if (leader) {
		pushUserdata<Player>(L, leader.get());
		setMetatable(L, -1, "Player");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartySetLeader(lua_State* L)
{
	// party:setLeader(player[, force = false])
	Player* player = getPlayer(L, 2);
	Party* party = getUserdata<Party>(L, 1);
	if (party && player) {
		pushBoolean(L, party->passPartyLeadership(player, getBoolean(L, 3, false)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyGetMembers(lua_State* L)
{
	// party:getMembers()
	Party* party = getUserdata<Party>(L, 1);
	if (!party) {
		lua_pushnil(L);
		return 1;
	}

	int index = 0;
	lua_createtable(L, party->getMemberCount(), 0);
	for (auto& weakMember : party->getMembers()) {
		if (const auto playerRef = weakMember.lock(); Player* player = playerRef.get()) {
			pushUserdata<Player>(L, player);
			setMetatable(L, -1, "Player");
			lua_rawseti(L, -2, ++index);
		}
	}
	return 1;
}

int luaPartyGetMemberCount(lua_State* L)
{
	// party:getMemberCount()
	Party* party = getUserdata<Party>(L, 1);
	if (party) {
		lua_pushinteger(L, party->getMemberCount());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyGetInvitees(lua_State* L)
{
	// party:getInvitees()
	Party* party = getUserdata<Party>(L, 1);
	if (party) {
		lua_createtable(L, party->getInvitationCount(), 0);

		int index = 0;
		for (auto& weakInvitee : party->getInvitees()) {
			if (const auto playerRef = weakInvitee.lock(); Player* player = playerRef.get()) {
				pushUserdata<Player>(L, player);
				setMetatable(L, -1, "Player");
				lua_rawseti(L, -2, ++index);
			}
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyGetInviteeCount(lua_State* L)
{
	// party:getInviteeCount()
	Party* party = getUserdata<Party>(L, 1);
	if (party) {
		lua_pushinteger(L, party->getInvitationCount());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyAddInvite(lua_State* L)
{
	// party:addInvite(player)
	Player* player = getPlayer(L, 2);
	Party* party = getUserdata<Party>(L, 1);
	if (party && player) {
		pushBoolean(L, party->invitePlayer(*player));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyRemoveInvite(lua_State* L)
{
	// party:removeInvite(player)
	Player* player = getPlayer(L, 2);
	Party* party = getUserdata<Party>(L, 1);
	if (party && player) {
		pushBoolean(L, party->removeInvite(*player));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyAddMember(lua_State* L)
{
	// party:addMember(player)
	Player* player = getPlayer(L, 2);
	Party* party = getUserdata<Party>(L, 1);
	if (party && player) {
		pushBoolean(L, party->joinParty(*player));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyRemoveMember(lua_State* L)
{
	// party:removeMember(player)
	Player* player = getPlayer(L, 2);
	Party* party = getUserdata<Party>(L, 1);
	if (party && player) {
		pushBoolean(L, party->leaveParty(player));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyIsSharedExperienceActive(lua_State* L)
{
	// party:isSharedExperienceActive()
	Party* party = getUserdata<Party>(L, 1);
	if (party) {
		pushBoolean(L, party->isSharedExperienceActive());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyIsSharedExperienceEnabled(lua_State* L)
{
	// party:isSharedExperienceEnabled()
	Party* party = getUserdata<Party>(L, 1);
	if (party) {
		pushBoolean(L, party->isSharedExperienceEnabled());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartyShareExperience(lua_State* L)
{
	// party:shareExperience(experience)
	uint64_t experience = getInteger<uint64_t>(L, 2);
	Party* party = getUserdata<Party>(L, 1);
	if (party) {
		party->shareExperience(experience);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaPartySetSharedExperience(lua_State* L)
{
	// party:setSharedExperience(active)
	bool active = getBoolean(L, 2);
	Party* party = getUserdata<Party>(L, 1);
	if (party) {
		pushBoolean(L, party->setSharedExperience(party->getLeader().get(), active));
	} else {
		lua_pushnil(L);
	}
	return 1;
}
} // namespace

void LuaScriptInterface::registerParty()
{
	// Party
	registerClass("Party", "", luaPartyCreate);
	registerMetaMethod("Party", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("Party", "disband", luaPartyDisband);

	registerMethod("Party", "getLeader", luaPartyGetLeader);
	registerMethod("Party", "setLeader", luaPartySetLeader);

	registerMethod("Party", "getMembers", luaPartyGetMembers);
	registerMethod("Party", "getMemberCount", luaPartyGetMemberCount);

	registerMethod("Party", "getInvitees", luaPartyGetInvitees);
	registerMethod("Party", "getInviteeCount", luaPartyGetInviteeCount);

	registerMethod("Party", "addInvite", luaPartyAddInvite);
	registerMethod("Party", "removeInvite", luaPartyRemoveInvite);

	registerMethod("Party", "addMember", luaPartyAddMember);
	registerMethod("Party", "removeMember", luaPartyRemoveMember);

	registerMethod("Party", "isSharedExperienceActive", luaPartyIsSharedExperienceActive);
	registerMethod("Party", "isSharedExperienceEnabled", luaPartyIsSharedExperienceEnabled);
	registerMethod("Party", "shareExperience", luaPartyShareExperience);
	registerMethod("Party", "setSharedExperience", luaPartySetSharedExperience);
}
