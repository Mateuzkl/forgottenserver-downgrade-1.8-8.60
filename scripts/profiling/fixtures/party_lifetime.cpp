// Baseline-only fixture: keep void Party::disband() and private Events intact.
// check_audit_baseline.sh aliases this accessor to an existing Game friend.
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
	Events* previousEvents = g_events;
	Chat* previousChat = g_chat;
	std::unique_ptr<Events> events;
	std::unique_ptr<Chat> chat;
	std::shared_ptr<Player> leader = std::make_shared<Player>(nullptr);
	Fixture()
	{
		CHECK(g_luaEnvironment.initState());
		events = std::make_unique<Events>();
		chat = std::make_unique<Chat>();
		g_events = events.get();
		g_chat = chat.get();
		leader->setName("Leader");
		leader->setGroup(std::make_shared<Group>());
		PartyLifetimeTestAccess::add(leader);
	}
	~Fixture()
	{
		if (auto* party = leader->getParty()) party->disband();
		PartyLifetimeTestAccess::remove(*leader);
		g_events = previousEvents;
		g_chat = previousChat;
		events.reset();
		chat.reset();
		g_luaEnvironment.closeState();
	}
};
} // namespace

TEST_CASE(baseline_party_ignores_expired_members)
{
	Fixture f;
	auto party = Party::create(f.leader.get());
	{
		auto expired = std::make_shared<Player>(nullptr);
		party->getMembers().push_back(expired);
	}
	party->broadcastPartyMessage(MESSAGE_INFO_DESCR, "baseline lifetime regression");
	party->updateAllPartyIcons();
	party->disband(); // Deliberately ignore the return type; baseline returns void.
	CHECK(!f.leader->getParty());
}

TEST_CASE(baseline_party_alias_must_not_access_a_destroyed_group)
{
	Fixture f;
	auto party = Party::create(f.leader.get());
	std::weak_ptr<Party> weak = party;
	lua_State* L = g_luaEnvironment.getLuaState();
	for (const auto* alias : {"auditPartyA", "auditPartyB"}) {
		Lua::pushUserdata<Party>(L, party.get());
		Lua::setMetatable(L, -1, "Party");
		lua_setglobal(L, alias);
	}
	party.reset();
	CHECK(luaL_dostring(L, "assert(auditPartyA:disband())") == LUA_OK);
	CHECK(weak.expired());
	weak.reset();
	CHECK(luaL_dostring(L,
	    "assert(auditPartyB:getLeader() == nil); assert(auditPartyB:getMembers() == nil); "
	    "auditPartyA=nil; auditPartyB=nil; collectgarbage('collect')") == LUA_OK);
}

TFS_TEST_MAIN()
