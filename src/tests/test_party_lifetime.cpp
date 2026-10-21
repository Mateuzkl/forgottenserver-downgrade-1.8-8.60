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
	}
	static void remove(const Player& player)
	{
		std::unique_lock lock(g_game.creatureRefsMutex);
		g_game.creatureSharedRefs.erase(player.getID());
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

TFS_TEST_MAIN()
