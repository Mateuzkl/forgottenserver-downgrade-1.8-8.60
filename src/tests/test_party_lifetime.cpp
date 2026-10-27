#include "../otpch.h"

#include "../chat.h"
#include "../events.h"
#include "../game.h"
#include "../party.h"
#include "../scriptmanager.h"
#include "test_support.h"

extern LuaEnvironment g_luaEnvironment;

struct PartyLifetimeTestAccess
{
	static void add(const std::shared_ptr<Player>& player)
	{
		player->setID();
		std::unique_lock lock(g_game.creatureRefsMutex);
		g_game.creatureSharedRefs[player->getID()] = player;
		std::unique_lock playersLock(g_game.playersMutex);
		g_game.players[player->getID()] = player;
	}
	static void remove(const Player& player)
	{
		std::unique_lock lock(g_game.creatureRefsMutex);
		g_game.creatureSharedRefs.erase(player.getID());
		std::unique_lock playersLock(g_game.playersMutex);
		g_game.players.erase(player.getID());
	}
	static void disbandHook(Events& events, const char* code)
	{
		CHECK(luaL_dostring(events.scriptInterface.getLuaState(), code) == LUA_OK);
		events.info.partyOnDisband = events.scriptInterface.getEvent("auditOnDisband");
		CHECK(events.info.partyOnDisband != -1);
	}
	static void leaveHook(Events& events, const char* code)
	{
		CHECK(luaL_dostring(events.scriptInterface.getLuaState(), code) == LUA_OK);
		events.info.partyOnLeave = events.scriptInterface.getEvent("auditOnLeave");
		CHECK(events.info.partyOnLeave != -1);
	}
	static void inviteHook(Events& events, const char* code)
	{
		CHECK(luaL_dostring(events.scriptInterface.getLuaState(), code) == LUA_OK);
		events.info.partyOnInvite = events.scriptInterface.getEvent("auditOnInvite");
		CHECK(events.info.partyOnInvite != -1);
	}
	static void gainHook(Events& events, const char* code)
	{
		CHECK(luaL_dostring(events.scriptInterface.getLuaState(), code) == LUA_OK);
		events.info.playerOnGainExperience = events.scriptInterface.getEvent("auditOnGainExperience");
		CHECK(events.info.playerOnGainExperience != -1);
	}
	static void leadershipHook(Events& events, const char* code)
	{
		CHECK(luaL_dostring(events.scriptInterface.getLuaState(), code) == LUA_OK);
		events.info.partyOnPassLeadership = events.scriptInterface.getEvent("auditOnPassLeadership");
		CHECK(events.info.partyOnPassLeadership != -1);
	}
};

namespace {

struct Fixture
{
	Events* previous = g_events;
	Chat* previousChat = g_chat;
	std::unique_ptr<Events> events;
	std::unique_ptr<Chat> chat;
	std::vector<std::shared_ptr<Player>> players;
	void track(const std::shared_ptr<Player>& player)
	{
		PartyLifetimeTestAccess::add(player);
		players.push_back(player);
	}
	Fixture()
	{
		CHECK(g_luaEnvironment.initState());
		events = std::make_unique<Events>();
		g_events = events.get();
		chat = std::make_unique<Chat>();
		g_chat = chat.get();
	}
	~Fixture()
	{
		for (const auto& player : players) {
			if (auto* party = player->getParty()) party->disband();
			PartyLifetimeTestAccess::remove(*player);
		}
		g_events = previous;
		g_chat = previousChat;
		events.reset();
		chat.reset();
		g_luaEnvironment.closeState();
	}
};

std::shared_ptr<Player> player(const char* name)
{
	auto result = std::make_shared<Player>(nullptr);
	result->setName(name);
	result->setGroup(std::make_shared<Group>());
	return result;
}

} // namespace

TEST_CASE(party_operations_ignore_expired_members_and_disband_live_members)
{
	Fixture f;
	auto leader = player("Leader");
	auto member = player("Member");
	auto expired = player("Expired");
	f.track(leader);
	f.track(member);
	auto party = Party::create(leader.get());
	CHECK(party->invitePlayer(*member));
	CHECK(party->joinParty(*member));
	party->getMembers().push_back(expired);
	expired.reset();
	party->broadcastPartyMessage(MESSAGE_INFO_DESCR, "lifetime regression", true);
	party->updateAllPartyIcons();
	CHECK(party->passPartyLeadership(member.get()));
	CHECK(party->getLeader() == member);
	party->disband();
	CHECK(leader->getParty() == nullptr && member->getParty() == nullptr);
	CHECK(party->getMembers().empty() && party->getInvitees().empty());
	CHECK(!party->getLeader());
}

TEST_CASE(lua_party_getters_skip_expired_members_and_preserve_live_owners)
{
	Fixture f;
	auto leader = player("Leader");
	auto member = player("Member");
	auto expired = player("Expired");
	f.track(leader);
	f.track(member);
	auto party = Party::create(leader.get());
	CHECK(party->invitePlayer(*member));
	CHECK(party->joinParty(*member));
	party->getMembers().push_back(expired);
	expired.reset();
	lua_State* L = g_luaEnvironment.getLuaState();
	Lua::pushUserdata<Party>(L, party.get());
	Lua::setMetatable(L, -1, "Party");
	lua_setglobal(L, "auditParty");
	CHECK(luaL_dostring(L, "local members = auditParty:getMembers(); assert(#members == 1); "
	                       "assert(members[1]:getName() == 'Member'); assert(#auditParty:getInvitees() == 0)") == LUA_OK);
	CHECK(party->leaveParty(member.get(), true));
	CHECK(member->getParty() == nullptr);
	// leaveParty can disband automatically; a live external owner keeps this
	// test's Party valid while checking its post-disband getters.
	if (party->getLeader()) party->disband();
	CHECK(luaL_dostring(L, "assert(#auditParty:getMembers() == 0); auditParty = nil; collectgarbage('collect')") == LUA_OK);
}

TEST_CASE(lua_party_alias_after_disband_does_not_access_freed_party)
{
	Fixture f;
	auto leader = player("Leader");
	f.track(leader);
	auto party = Party::create(leader.get());
	std::weak_ptr<Party> weakParty = party;
	lua_State* L = g_luaEnvironment.getLuaState();
	for (const auto* alias : {"auditPartyA", "auditPartyB"}) {
		Lua::pushUserdata<Party>(L, party.get());
		Lua::setMetatable(L, -1, "Party");
		lua_setglobal(L, alias);
	}
	party.reset(); // Production has only Party::self, not this test's external owner.
	CHECK(luaL_dostring(L, "assert(auditPartyA:disband())") == LUA_OK);
	CHECK(weakParty.expired());
	weakParty.reset(); // Do not let a weak control block conceal access to the freed make_shared allocation.
	CHECK(luaL_dostring(L, "assert(auditPartyB:getLeader() == nil); "
	                       "assert(auditPartyB:getMembers() == nil); "
	                       "assert(auditPartyB:disband() == nil); "
	                       "auditPartyA = nil; auditPartyB = nil; collectgarbage('collect')") == LUA_OK);
	CHECK(leader->getParty() == nullptr);
}

TEST_CASE(lua_party_disband_veto_preserves_the_handle_and_group)
{
	Fixture f;
	auto leader = player("Leader");
	f.track(leader);
	auto party = Party::create(leader.get());
	lua_State* L = g_luaEnvironment.getLuaState();
	Lua::pushUserdata<Party>(L, party.get());
	Lua::setMetatable(L, -1, "Party");
	lua_setglobal(L, "auditParty");
	PartyLifetimeTestAccess::disbandHook(*f.events, "function auditOnDisband(party) return false end");
	CHECK(luaL_dostring(L, "assert(auditParty:disband() == false); assert(auditParty:getLeader():getName() == 'Leader')") == LUA_OK);
	CHECK(leader->getParty() == party.get());
	PartyLifetimeTestAccess::disbandHook(*f.events, "function auditOnDisband(party) return true end");
	CHECK(luaL_dostring(L, "assert(auditParty:disband()); assert(auditParty:getLeader() == nil)") == LUA_OK);
	CHECK(leader->getParty() == nullptr);
}

TEST_CASE(party_disband_reentry_calls_the_callback_only_once)
{
	Fixture f;
	auto leader = player("Leader");
	f.track(leader);
	auto party = Party::create(leader.get());
	PartyLifetimeTestAccess::disbandHook(*f.events,
	    "auditDisbandCalls = 0; function auditOnDisband(party) "
	    "auditDisbandCalls = auditDisbandCalls + 1; assert(party:disband() == false); return true end");
	CHECK(party->disband());
	CHECK(luaL_dostring(g_luaEnvironment.getLuaState(), "assert(auditDisbandCalls == 1)") == LUA_OK);
	CHECK(!party->disband());
	CHECK(leader->getParty() == nullptr);
}

TEST_CASE(native_party_leave_handles_disband_from_inside_its_callback)
{
	Fixture f;
	auto leader = player("Leader"), member = player("Member");
	f.track(leader);
	f.track(member);
	auto party = Party::create(leader.get());
	CHECK(party->invitePlayer(*member));
	CHECK(party->joinParty(*member));
	PartyLifetimeTestAccess::leaveHook(*f.events,
	    "function auditOnLeave(party, player) assert(party:disband()); return true end");
	Party* raw = party.get();
	std::weak_ptr<Party> weak = party;
	party.reset();
	CHECK(!raw->leaveParty(member.get()));
	CHECK(weak.expired());
	CHECK(member->getParty() == nullptr && leader->getParty() == nullptr);
}

TEST_CASE(leader_leave_stops_when_forced_transfer_callback_disbands_party)
{
	Fixture f;
	auto leader = player("Leader"), first = player("First"), second = player("Second");
	for (const auto& member : {leader, first, second}) f.track(member);
	auto party = Party::create(leader.get());
	CHECK(party->invitePlayer(*first) && party->joinParty(*first));
	CHECK(party->invitePlayer(*second) && party->joinParty(*second));
	PartyLifetimeTestAccess::leadershipHook(*f.events,
	    "function auditOnPassLeadership(party, player) assert(party:disband()); return true end");
	CHECK(!party->leaveParty(leader.get()));
	CHECK(!leader->getParty() && !first->getParty() && !second->getParty());
}

TEST_CASE(shared_experience_leadership_change_does_not_duplicate_or_lose_a_share)
{
	Fixture f;
	auto leader = player("Leader"), first = player("First"), second = player("Second");
	for (const auto& p : {leader, first, second}) f.track(p);
	auto party = Party::create(leader.get());
	CHECK(party->invitePlayer(*first) && party->joinParty(*first));
	CHECK(party->invitePlayer(*second) && party->joinParty(*second));
	lua_State* L = g_luaEnvironment.getLuaState();
	Lua::pushUserdata<Player>(L, second.get());
	Lua::setMetatable(L, -1, "Player");
	lua_setglobal(L, "auditNextLeader");
	PartyLifetimeTestAccess::gainHook(*f.events,
	    "auditMoved = false; function auditOnGainExperience(player, source, exp) "
	    "if not auditMoved and player:getName() == 'First' then auditMoved = true; "
	    "assert(player:getParty():setLeader(auditNextLeader)) end; return exp end");
	party->shareExperience(1);
	CHECK(party->getLeader() == second);
	CHECK(leader->getExperience() == 1);
	CHECK(first->getExperience() == 1 && second->getExperience() == 1);
}

TEST_CASE(shared_experience_disband_from_gain_callback_stops_remaining_recipients)
{
	Fixture f;
	auto leader = player("Leader"), first = player("First"), second = player("Second");
	for (const auto& p : {leader, first, second}) f.track(p);
	auto party = Party::create(leader.get());
	CHECK(party->invitePlayer(*first) && party->joinParty(*first));
	CHECK(party->invitePlayer(*second) && party->joinParty(*second));
	PartyLifetimeTestAccess::gainHook(*f.events,
	    "function auditOnGainExperience(player, source, exp) "
	    "if player:getName() == 'First' then assert(player:getParty():disband()) end; return exp end");
	party->shareExperience(1);
	CHECK(first->getExperience() == 1);
	CHECK(leader->getExperience() == 0 && second->getExperience() == 0);
	CHECK(leader->getParty() == nullptr && first->getParty() == nullptr && second->getParty() == nullptr);
}

TEST_CASE(game_invite_does_not_reuse_a_party_disbanded_by_its_invite_callback)
{
	for (bool allowed : {false, true}) {
		Fixture f;
		auto leader = player("Leader"), invited = player("Invited");
		f.track(leader);
		f.track(invited);
		auto party = Party::create(leader.get());
		party.reset();
		PartyLifetimeTestAccess::inviteHook(*f.events, allowed ?
		    "function auditOnInvite(party, invited) local alias = party:getLeader():getParty(); "
		    "assert(party:disband()); auditInviteAlive = alias:getMemberCount() == 0; return true end" :
		    "function auditOnInvite(party, invited) local alias = party:getLeader():getParty(); "
		    "assert(party:disband()); auditInviteAlive = alias:getMemberCount() == 0; return false end");
		g_game.playerInviteToParty(leader->getID(), invited->getID());
		CHECK(luaL_dostring(g_luaEnvironment.getLuaState(), "assert(auditInviteAlive == true)") == LUA_OK);
		CHECK(leader->getParty() == nullptr && invited->getParty() == nullptr);
		CHECK(!invited->isInviting(leader.get()));
	}
}

TEST_CASE(lua_party_metatable_supports_absolute_userdata_indices)
{
	Fixture f;
	auto leader = player("Leader");
	f.track(leader);
	auto party = Party::create(leader.get());
	lua_State* L = g_luaEnvironment.getLuaState();
	const int base = lua_gettop(L);
	lua_pushinteger(L, 17);
	Lua::pushUserdata<Party>(L, party.get());
	const int index = lua_gettop(L);
	lua_pushinteger(L, 23);
	Lua::setMetatable(L, index, "Party");
	CHECK(lua_gettop(L) == base + 3);
	CHECK(lua_tointeger(L, base + 1) == 17 && lua_tointeger(L, base + 3) == 23);
	lua_pushvalue(L, index);
	lua_setglobal(L, "auditParty");
	lua_settop(L, base);
	CHECK(luaL_dostring(L, "assert(auditParty:getLeader():getName() == 'Leader')") == LUA_OK);
}

TEST_CASE(lua_party_lifetime_finalizer_is_idempotent)
{
	Fixture f;
	auto leader = player("Leader");
	f.track(leader);
	auto party = Party::create(leader.get());
	lua_State* L = g_luaEnvironment.getLuaState();
	Lua::pushUserdata<Party>(L, party.get());
	Lua::setMetatable(L, -1, "Party");
	lua_setglobal(L, "auditParty");
	CHECK(luaL_dostring(L,
	    "local token = debug.getuservalue(auditParty); local mt = debug.getmetatable(token); "
	    "mt.__gc(token); mt.__gc(token); assert(auditParty:getLeader() == nil); "
	    "auditParty=nil; token=nil; collectgarbage('collect')") == LUA_OK);
	CHECK(party->getLeader() == leader);
}

TEST_CASE(lua_party_lifetime_handles_survive_1000_disband_and_gc_cycles)
{
	Fixture f;
	auto leader = player("Leader");
	f.track(leader);
	lua_State* L = g_luaEnvironment.getLuaState();
	for (size_t cycle = 0; cycle < 1000; ++cycle) {
		auto party = Party::create(leader.get());
		std::weak_ptr<Party> weak = party;
		Lua::pushUserdata<Party>(L, party.get());
		Lua::setMetatable(L, -1, "Party");
		lua_setglobal(L, "auditParty");
		CHECK(party->disband());
		party.reset();
		CHECK(weak.expired());
		weak.reset();
		CHECK(luaL_dostring(L, "assert(auditParty:getLeader() == nil); "
		                       "auditParty = nil; collectgarbage('collect')") == LUA_OK);
		CHECK(leader->getParty() == nullptr);
	}
}

TFS_TEST_MAIN()
