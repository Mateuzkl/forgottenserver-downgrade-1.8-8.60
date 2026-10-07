#include "../otpch.h"

#include "../chat.h"
#include "../configmanager.h"
#include "../events.h"
#include "../game.h"
#include "../globalevent.h"
#include "../house.h"
#include "../inbox.h"
#include "../iologindata.h"
#include "../iomapserialize.h"
#include "../luascript.h"
#include "../movement.h"
#include "../save_manager.h"
#include "../scheduler.h"
#include "../scriptmanager.h"
#include "../tasks.h"
#include "../thread_pool.h"
#include "../town.h"
#include "../weapons.h"
#include "test_support.h"

#include <cstdlib>
#include <future>
#ifndef _WIN32
#include <csignal>
#endif

extern Vocations g_vocations;
extern LuaEnvironment g_luaEnvironment;

struct SaveManagerTestAccess
{
	static void busy(bool value)
	{
		if (value)
			g_saveManager.flushInFlight.insert(7);
		else
			g_saveManager.flushInFlight.erase(7);
	}
	static void recoveryBlocked(bool value)
	{
		if (value)
			g_saveManager.failedRecoveryGuids.insert(7);
		else
			g_saveManager.failedRecoveryGuids.erase(7);
	}
};

namespace {
constexpr Position housePosition{210, 210, 7};
std::shared_ptr<Item> ground()
{
	for (size_t id = 1; id < Item::items.size(); ++id) {
		const auto& type = Item::items[id];
		if (type.id && type.isGroundTile() && !type.blockSolid && !type.blockPathFind) return Item::CreateItem(type.id);
	}
	throw std::runtime_error("No walkable ground type");
}
Database& db() { return Database::getInstance(); }
void sql(std::string_view query) { CHECK(db().executeQuery(query)); }
uint64_t number(std::string_view query)
{
	const auto row = db().storeQuery(query);
	CHECK(row);
	return row->getNumber<uint64_t>("value");
}

template <typename Function>
void dispatch(Function function)
{
	g_dispatcher.start();
	g_scheduler.start();
	std::exception_ptr error;
	g_dispatcher.addTask([&] {
		try {
			function();
		} catch (...) {
			error = std::current_exception();
		}
		g_reactor.shutdown();
	});
	g_reactor.runLoop();
	g_scheduler.shutdown();
	g_dispatcher.shutdown();
	if (error) std::rethrow_exception(error);
}

// Crash only this disposable test process, after running the real owner UPDATE
// inside the production transfer transaction, before its COMMIT.
class CrashHouse final : public House
{
public:
	using House::House;
	bool crashBeforeCommit = false;

protected:
	bool updateOwnerInDatabase(uint32_t owner, bool reset) override
	{
		const bool result = House::updateOwnerInDatabase(owner, reset);
		if (result && crashBeforeCommit) std::_Exit(73);
		return result;
	}
};

void reset()
{
	sql("DROP TRIGGER IF EXISTS house_transfer_failure");
	sql("DELETE FROM player_save_journal");
	sql("DELETE FROM player_save_async_pending");
	sql("DELETE FROM tile_store");
	sql("DELETE FROM house_lists");
	sql("DELETE FROM houses");
	sql("DELETE FROM players WHERE id IN (7,8)");
	sql("INSERT INTO players (id,name,account_id,town_id,vocation) VALUES (7,'TransferOwner',1,1,1),(8,'OtherOwner',1,1,1)");
	sql("INSERT INTO houses (id,owner,name,town_id,is_protected) VALUES (701,7,'TransferHouse',1,1),(702,8,'UntouchedHouse',1,0)");
	sql("INSERT INTO house_lists (house_id,listid,list) VALUES (701,256,'*')");
	sql("INSERT INTO house_guests (house_id,player_id) VALUES (701,8)");
}

struct World
{
	std::shared_ptr<CrashHouse> house = std::make_shared<CrashHouse>(701);
	std::shared_ptr<HouseTile> tile;
	std::shared_ptr<Container> root;
	std::shared_ptr<Container> nested;
	std::shared_ptr<Item> sword;
	std::shared_ptr<Item> coins;
	std::shared_ptr<Player> owner = std::make_shared<Player>(nullptr);
	bool registered = false;

	explicit World(bool seed = true)
	{
		if (seed) reset();
		CHECK(IOLoginData::loadPlayerById(owner.get(), 7));
		CHECK(house->setOwner(static_cast<uint32_t>(number("SELECT owner AS value FROM houses WHERE id=701")), false));
		house->setTownId(1);
		house->setProtected(true);
		house->setAccessList(GUEST_LIST, "*");
		house->getProtectionGuests().insert(8);
		auto newTile = std::make_unique<HouseTile>(housePosition.x, housePosition.y, housePosition.z, house);
		newTile->internalAddThing(ground().get());
		g_game.map.setTile(housePosition, std::move(newTile));
		tile = std::static_pointer_cast<HouseTile>(g_game.map.getTile(housePosition)->shared_from_this());
		house->addTile(tile);
		if (!seed) return;
		root = std::dynamic_pointer_cast<Container>(Item::CreateItem(ITEM_BAG));
		nested = std::dynamic_pointer_cast<Container>(Item::CreateItem(ITEM_BAG));
		sword = Item::CreateItem(3264);
		coins = Item::CreateItem(3035, 57);
		CHECK(root && nested && sword && coins);
		root->setItemUID(701001);
		nested->setItemUID(701002);
		sword->setItemUID(701003);
		sword->setCustomAttribute("dupe_marker", std::string("house-701-sword"));
		sword->setIntAttr(ITEM_ATTRIBUTE_ACTIONID, 1701);
		nested->internalAddThing(sword.get());
		root->internalAddThing(nested.get());
		root->internalAddThing(coins.get());
		tile->internalAddThing(root.get());
		CHECK(IOMapSerialize::saveHouse(house.get()));
		sql("INSERT INTO tile_store (house_id,data) VALUES (702,'untouched')");
	}
	void online()
	{
		// The real online registry, not a simulated previousPlayer argument.
		CHECK(g_game.internalPlaceCreature(owner.get(), {213, 210, 7}, true, true));
		CHECK(g_game.getPlayerByGUID(7) == owner);
		registered = true;
	}
	~World()
	{
		SaveManagerTestAccess::busy(false);
		if (registered) dispatch([&] { CHECK(g_game.removeCreature(owner.get(), true)); });
		g_game.map.removeTile(housePosition);
		g_game.cleanup();
	}
};

struct Counts
{
	size_t roots = 0, nested = 0, swords = 0;
	uint32_t coins = 0;
};
Counts count(const Player& player, const HouseTile& tile)
{
	Counts result;
	const auto visit = [&](auto&& self, const Item* item) -> void {
		if (item->getItemUID() == 701001) ++result.roots;
		if (item->getItemUID() == 701002) ++result.nested;
		if (item->getItemUID() == 701003) {
			++result.swords;
			CHECK(item->getActionId() == 1701);
			CHECK(item->getCustomAttribute("dupe_marker"));
		}
		if (item->getID() == 3035) result.coins += item->getItemCount();
		if (const auto* container = item->getContainer()) {
			for (const auto& child : container->getItemList()) self(self, child.get());
		}
	};
	ItemBlockList items;
	IOLoginData::collectInboxItems(&player, items);
	for (const auto& [town, item] : items) {
		CHECK(town == 1);
		visit(visit, item);
	}
	if (const auto* items = tile.getItemList())
		for (const auto& item : *items) visit(visit, item.get());
	return result;
}

bool dupeComplete = false;
bool dupeFailed = false;
std::unordered_set<int> dupePhases;
int dupePrint(lua_State* L)
{
	for (int index = 1; index <= lua_gettop(L); ++index) {
		size_t length = 0;
		const char* data = lua_tolstring(L, index, &length);
		if (!data) continue;
		const std::string_view message(data, length);
		std::cout << message << '\n';
		if (message.find("[FAIL]") != message.npos || message.find("FASES FALHARAM") != message.npos) dupeFailed = true;
		if (message.find("[PASS]") != message.npos) {
			const auto phase = message.find("Phase ");
			if (phase != message.npos && phase + 6 < message.size()) dupePhases.insert(message[phase + 6] - '0');
		}
		if (message.find("TODAS AS 9 FASES PASSARAM") != message.npos ||
		    message.find("UMA OU MAIS FASES FALHARAM") != message.npos)
			dupeComplete = true;
	}
	return 0;
}

void runExistingDupePhases()
{
	World world;
	g_dispatcher.start();
	g_scheduler.start();
	std::exception_ptr error;
	g_dispatcher.addTask([&] {
		try {
			world.online();
			world.owner->setGroup(g_game.groups.getSharedGroup(6));
			CHECK(world.owner->getGroup() && world.owner->getGroup()->access);
			auto bag = Item::CreateItem(ITEM_BAG);
			CHECK(g_game.internalAddItem(world.owner.get(), bag.get(), CONST_SLOT_BACKPACK, FLAG_NOLIMIT) ==
			      RETURNVALUE_NOERROR);
			CHECK(bag->getParent() == world.owner.get());
			lua_State* L = g_luaEnvironment.getLuaState();
			CHECK(LuaScriptInterface::reserveScriptEnv());
			LuaScriptInterface::getScriptEnv()->setScriptId(0, &g_luaEnvironment);
			lua_pushcfunction(L, dupePrint);
			lua_setglobal(L, "print");
			// Only registration is adapted to this standalone harness. The nine
			// phase bodies, real Player/DB bindings, timers and saves are unchanged.
			CHECK(luaL_dostring(L,
			                    "TalkAction=function() local t={} "
			                    "function t:separator() end function t:access() end function t:accountType() end "
			                    "function t:register() dupeAction=self end return t end") == LUA_OK);
			CHECK(luaL_dofile(L, "data/scripts/talkactions/stress/dupe_test.lua") == LUA_OK);
			Lua::pushUserdata<Player>(L, world.owner.get());
			Lua::setMetatable(L, -1, "Player");
			lua_setglobal(L, "dupeOwner");
			CHECK(luaL_dostring(L, "dupeAction.onSay(dupeOwner,'/dupe','start')") == LUA_OK);
			LuaScriptInterface::resetScriptEnv();
		} catch (...) {
			error = std::current_exception();
			if (LuaScriptInterface::hasScriptEnv()) LuaScriptInterface::resetScriptEnv();
			g_reactor.shutdown();
		}
	});
	std::function<void()> wait;
	wait = [&] {
		if (!dupeComplete) {
			g_scheduler.addEvent(250, wait);
			return;
		}
		g_saveManager.drainPlayerFlushAsync(7, [&](bool success) {
			dupeFailed |= !success;
			g_reactor.shutdown();
		});
	};
	g_scheduler.addEvent(250, wait);
	g_scheduler.addEvent(180000, [&] {
		dupeFailed = true;
		g_reactor.shutdown();
	});
	g_reactor.runLoop();
	g_scheduler.shutdown();
	g_dispatcher.shutdown();
	if (error) std::rethrow_exception(error);
	CHECK(dupeComplete && !dupeFailed && dupePhases.size() == 9);
	CHECK(number("SELECT COUNT(*) AS value FROM player_items WHERE player_id=7 AND itemtype IN (3264,3035)") == 0);
	CHECK(number("SELECT COUNT(*) AS value FROM player_save_journal WHERE guid=7") == 0);
	std::cout << "All 9 existing /dupe phases passed through production Lua/Player/DB/save paths\n";
}

void verifyRestart(bool committed, uint32_t newOwner = 0)
{
	World restored(false);
	IOMapSerialize::loadHouseItems(&g_game.map);
	const auto counts = count(*restored.owner, *restored.tile);
	CHECK(counts.roots == 1 && counts.nested == 1 && counts.swords == 1 && counts.coins == 57);
	CHECK(restored.house->getOwner() == (committed ? newOwner : 7));
	CHECK(number("SELECT is_protected AS value FROM houses WHERE id=701") == (committed ? 0 : 1));
	CHECK(number("SELECT COUNT(*) AS value FROM player_save_journal") == 0);
	CHECK(number("SELECT COUNT(*) AS value FROM tile_store WHERE house_id=702 AND data='untouched'") == 1);
	CHECK(number("SELECT COUNT(*) AS value FROM house_lists WHERE house_id=701") == (committed ? 0 : 1));
	CHECK(number("SELECT COUNT(*) AS value FROM house_guests WHERE house_id=701") == (committed ? 0 : 1));
	if (committed) {
		CHECK(!restored.tile->getItemList() || restored.tile->getItemList()->empty());
		CHECK(number("SELECT COUNT(*) AS value FROM player_inboxitems WHERE player_id=7") == 4);
	} else {
		CHECK(number("SELECT COUNT(*) AS value FROM player_inboxitems WHERE player_id=7") == 0);
	}
}

void verifyTradeRestart(bool committed)
{
	verifyRestart(committed, 8);
	Player seller(nullptr), buyer(nullptr);
	CHECK(IOLoginData::loadPlayerById(&seller, 7) && IOLoginData::loadPlayerById(&buyer, 8));
	CHECK(seller.getItemTypeCount(3043) == (committed ? 17 : 0));
	CHECK(buyer.getItemTypeCount(3043) == (committed ? 0 : 17));
	CHECK(number(fmt::format("SELECT COUNT(*) AS value FROM player_items WHERE itemtype={}",
	                         static_cast<uint16_t>(ITEM_DOCUMENT_RO))) == 0);
}

void runRealTrade(bool failBuyerSave = false, std::string_view crash = {})
{
	World world;
	auto buyer = std::make_shared<Player>(nullptr);
	CHECK(IOLoginData::loadPlayerById(buyer.get(), 8));
	auto buyerTile = std::make_unique<DynamicTile>(214, 210, 7);
	buyerTile->internalAddThing(ground().get());
	g_game.map.setTile({214, 210, 7}, std::move(buyerTile));
	auto payment = Item::CreateItem(3043, 17);
	payment->setItemUID(701004);
	bool registered = false;
	try {
		dispatch([&] {
			world.online();
			CHECK(g_game.internalPlaceCreature(buyer.get(), {214, 210, 7}, true, true));
			registered = true;
			for (Player* player : {world.owner.get(), buyer.get()}) {
				auto bag = Item::CreateItem(ITEM_BAG);
				CHECK(g_game.internalAddItem(player, bag.get(), CONST_SLOT_BACKPACK, FLAG_NOLIMIT) ==
				      RETURNVALUE_NOERROR);
			}
			CHECK(g_game.internalAddItem(buyer.get(), payment.get(), INDEX_WHEREEVER, FLAG_NOLIMIT) ==
			      RETURNVALUE_NOERROR);
			CHECK(g_saveManager.savePlayersSync({world.owner.get(), buyer.get()}));
			if (failBuyerSave) {
				sql("CREATE TRIGGER house_transfer_buyer_failure BEFORE UPDATE ON players FOR EACH ROW "
				    "BEGIN IF OLD.id=8 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='buyer save rejected'; END IF; END");
			}
			if (crash == "before") world.house->crashBeforeCommit = true;
			if (crash == "after") {
				auto event = std::make_unique<MoveEvent>(g_moveEvents->getScriptInterfacePtr());
				event->setEventType(MOVE_EVENT_REMOVE_ITEM);
				event->addPosList(housePosition);
				event->moveFunction = [](Item*, Item*, const Position&) -> uint32_t { std::_Exit(74); };
				CHECK(g_moveEvents->registerLuaEvent(event.release()));
			}
			const auto document = world.house->getTransferItem();
			CHECK(g_game.internalStartTrade(world.owner.get(), buyer.get(), document.get()));
			CHECK(g_game.internalStartTrade(buyer.get(), world.owner.get(), payment.get()));
			g_game.playerAcceptTrade(world.owner->getID());
			g_game.playerAcceptTrade(buyer->getID());
			CHECK(world.owner->getTradeState() == TRADE_NONE && buyer->getTradeState() == TRADE_NONE);
			CHECK(world.house->getOwner() == (failBuyerSave ? 7 : 8));
			CHECK(payment->getTopParent() == (failBuyerSave ? buyer.get() : world.owner.get()));
			CHECK(number(
			          "SELECT COALESCE(SUM(count),0) AS value FROM player_items WHERE player_id=7 AND itemtype=3043") ==
			      (failBuyerSave ? 0 : 17));
			CHECK(number(
			          "SELECT COALESCE(SUM(count),0) AS value FROM player_items WHERE player_id=8 AND itemtype=3043") ==
			      (failBuyerSave ? 17 : 0));
			CHECK(number("SELECT COUNT(*) AS value FROM player_save_journal") == 0);
		});
	} catch (...) {
		sql("DROP TRIGGER IF EXISTS house_transfer_buyer_failure");
		if (registered) dispatch([&] { g_game.removeCreature(buyer.get(), true); });
		g_game.map.removeTile({214, 210, 7});
		throw;
	}
	sql("DROP TRIGGER IF EXISTS house_transfer_buyer_failure");
	dispatch([&] { CHECK(g_game.removeCreature(buyer.get(), true)); });
	g_game.map.removeTile({214, 210, 7});
}

void runTransfer(bool online, uint32_t newOwner = 0)
{
	{
		World world;
		dispatch([&] {
			if (online) world.online();
			CHECK(world.house->setOwner(newOwner));
			CHECK(world.house->setOwner(newOwner)); // duplicate request is a no-op
			if (online) {
				const auto counts = count(*world.owner, *world.tile);
				CHECK(counts.roots == 1 && counts.nested == 1 && counts.swords == 1 && counts.coins == 57);
				CHECK(g_saveManager.savePlayerSync(world.owner.get()) == SaveResult::Persisted);
			}
		});
	}
	verifyRestart(true, newOwner);
	CHECK(number("SELECT COUNT(*) AS value FROM player_inboxitems WHERE player_id=8") == 0);
}
} // namespace

TEST_CASE(offline_house_transfer_survives_restart_without_duplicate_items) { runTransfer(false); }
TEST_CASE(online_house_transfer_is_durable_without_waiting_for_logout) { runTransfer(true); }
TEST_CASE(house_sale_credits_the_old_owner_not_the_buyer) { runTransfer(false, 8); }

TEST_CASE(real_house_trade_commits_payment_and_both_inventories_with_ownership)
{
	runRealTrade();
	verifyTradeRestart(true);
}

TEST_CASE(real_house_trade_restores_payment_when_buyer_save_fails)
{
	runRealTrade(true);
	verifyTradeRestart(false);
}

TEST_CASE(failure_at_each_write_rolls_back_house_owner_and_inbox_together)
{
	for (const auto table : {"players", "player_inboxitems", "tile_store", "houses", "house_lists", "house_guests"}) {
		{
			World world;
			const std::string event = std::string_view(table) == "player_inboxitems" ? "INSERT"
			                          : std::string_view(table) == "tile_store" ||
			                                  std::string_view(table) == "house_lists" ||
			                                  std::string_view(table) == "house_guests"
			                              ? "DELETE"
			                              : "UPDATE";
			sql(fmt::format(
			    "CREATE TRIGGER house_transfer_failure BEFORE {} ON {} FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected transfer failure'",
			    event, table));
			dispatch([&] {
				CHECK(!world.house->setOwner(0, true, world.owner.get()));
				CHECK(world.house->getOwner() == 7 && world.house->getProtected());
				CHECK(world.house->getAccessList(GUEST_LIST).value() == "*");
				CHECK(world.house->isProtectionGuest(8));
				CHECK(world.root->getParent() == world.tile.get());
				CHECK(world.sword->getParent() == world.nested.get());
			});
			sql("DROP TRIGGER house_transfer_failure");
		}
		verifyRestart(false);
	}
}

// Unlike dispatch(), keeps running until the test calls stop(), so asynchronous
// save completions from workers can still be delivered. stop() cancels the
// timeout guard first: timers outlive runLoop() in the shared reactor, and the
// reactor ignores cancellation once it has stopped, so a guard left behind would
// fire during a later test and write through a dangling reference.
template <typename Function>
void dispatchUntilStopped(Function function)
{
	g_dispatcher.start();
	g_scheduler.start();
	std::exception_ptr error;
	bool timedOut = false;
	uint32_t guard = 0;
	const auto stop = [&guard] {
		g_scheduler.stopEvent(std::exchange(guard, 0));
		g_reactor.shutdown();
	};
	guard = g_scheduler.addEvent(10000, [&] {
		guard = 0;
		timedOut = true;
		g_reactor.shutdown();
	});
	g_dispatcher.addTask([&] {
		try {
			function(stop);
		} catch (...) {
			error = std::current_exception();
			stop();
		}
	});
	g_reactor.runLoop();
	g_scheduler.shutdown();
	g_dispatcher.shutdown();
	if (error) std::rethrow_exception(error);
	CHECK(!timedOut);
}

// A transient failure while saving an online player must not block that player
// for the rest of the session. The live Player supersedes the failed chain: the
// next save takes a higher generation and replaces the stale journal row.
TEST_CASE(failed_save_of_online_player_does_not_block_later_saves)
{
	World world;
	bool retried = false;
	sql("CREATE TRIGGER fail_online_save BEFORE UPDATE ON players FOR EACH ROW "
	    "SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected online save failure'");
	dispatchUntilStopped([&](const auto& stop) {
		world.online();
		CHECK(g_saveManager.savePlayer(world.owner.get()) == SaveResult::Queued);
		g_saveManager.drainPlayerFlushAsync(7, [&, stop](bool drained) {
			CHECK(!drained);
			CHECK(!g_saveManager.hasFailedRecovery(7));
			CHECK(db().executeQuery("DROP TRIGGER fail_online_save"));
			CHECK(g_saveManager.savePlayerSync(world.owner.get()) == SaveResult::Persisted);
			retried = true;
			stop();
		});
	});
	db().executeQuery("DROP TRIGGER IF EXISTS fail_online_save");
	CHECK(retried);
	CHECK(number("SELECT COUNT(*) AS value FROM player_save_journal WHERE guid=7") == 0);
	CHECK(number("SELECT save_generation AS value FROM players WHERE id=7") == world.owner->getSaveGeneration());
}

TEST_CASE(pending_old_save_and_wrong_recipient_cannot_remove_source_items)
{
	World world;
	dispatch([&] {
		SaveManagerTestAccess::busy(true);
		CHECK(!world.house->setOwner(0, true, world.owner.get()));
		SaveManagerTestAccess::busy(false);
		SaveManagerTestAccess::recoveryBlocked(true);
		CHECK(!world.house->setOwner(0, true, world.owner.get()));
		SaveManagerTestAccess::recoveryBlocked(false);
		Player wrong(nullptr);
		wrong.setGUID(8);
		CHECK(!world.house->setOwner(0, true, &wrong));
		CHECK(world.root->getParent() == world.tile.get());
		CHECK(number("SELECT owner AS value FROM houses WHERE id=701") == 7);
	});
}

TEST_CASE(deleted_house_owner_releases_ownership_without_moving_items)
{
	World world;
	const auto before = db().storeQuery("SELECT data FROM tile_store WHERE house_id=701");
	CHECK(before);
	const std::string image(before->getString("data"));
	sql("DELETE FROM players WHERE id=7");
	dispatch([&] {
		CHECK(world.house->setOwner(0));
		CHECK(world.house->getOwner() == 0 && world.root->getParent() == world.tile.get());
		CHECK(count(*world.owner, *world.tile).swords == 1);
		CHECK(number("SELECT owner AS value FROM houses WHERE id=701") == 0);
		const auto after = db().storeQuery("SELECT data FROM tile_store WHERE house_id=701");
		CHECK(after && after->getString("data") == image);
		CHECK(number("SELECT COUNT(*) AS value FROM house_lists WHERE house_id=701") == 0);
	});
}

// Migration 13 drops `ondelete_players` and asks admins to recreate it by hand,
// so production databases exist both with and without it. Without the trigger
// houses.owner still holds the deleted GUID, and the release must work too.
TEST_CASE(deleted_house_owner_without_delete_trigger_releases_ownership)
{
	World world;
	sql("DROP TRIGGER IF EXISTS ondelete_players");
	sql("DELETE FROM players WHERE id=7");
	CHECK(number("SELECT owner AS value FROM houses WHERE id=701") == 7);
	bool released = false;
	dispatch([&] {
		released = world.house->setOwner(0);
		CHECK(world.house->getOwner() == 0 && world.root->getParent() == world.tile.get());
		CHECK(number("SELECT owner AS value FROM houses WHERE id=701") == 0);
	});
	sql("CREATE TRIGGER ondelete_players BEFORE DELETE ON players FOR EACH ROW "
	    "UPDATE houses SET owner = 0 WHERE owner = OLD.id");
	CHECK(released);
}

TEST_CASE(missing_guild_releases_ownership_without_moving_items)
{
	World world;
	CHECK(number("SELECT COUNT(*) AS value FROM guilds WHERE id=7") == 0);
	world.house->setType(HOUSE_TYPE_GUILDHALL);
	dispatch([&] {
		CHECK(world.house->setOwner(0));
		CHECK(world.house->getOwner() == 0 && world.root->getParent() == world.tile.get());
		CHECK(count(*world.owner, *world.tile).swords == 1);
		CHECK(number("SELECT owner AS value FROM houses WHERE id=701") == 0);
		CHECK(number("SELECT COUNT(*) AS value FROM player_inboxitems WHERE player_id=7") == 0);
	});
}

TEST_CASE(existing_owner_load_failure_cannot_be_treated_as_a_deleted_owner)
{
	World world;
	sql("UPDATE players SET group_id=65535 WHERE id=7");
	dispatch([&] {
		CHECK(!world.house->setOwner(0));
		CHECK(world.house->getOwner() == 7 && world.root->getParent() == world.tile.get());
		CHECK(number("SELECT owner AS value FROM houses WHERE id=701") == 7);
	});
}

TEST_CASE(stale_player_generation_does_not_commit_source_removal_without_credit)
{
	World world;
	sql("UPDATE players SET save_generation=900 WHERE id=7");
	dispatch([&] {
		CHECK(!world.house->setOwner(0, true, world.owner.get()));
		CHECK(world.root->getParent() == world.tile.get());
		CHECK(number("SELECT owner AS value FROM houses WHERE id=701") == 7);
	});
}

TEST_CASE(open_browse_field_cannot_retain_a_transferred_house_item)
{
	{
		World world;
		dispatch([&] {
			world.online();
			CHECK(g_game.internalTeleport(world.owner.get(), housePosition, true) == RETURNVALUE_NOERROR);
			g_game.playerBrowseField(world.owner->getID(), housePosition);
			const auto browse = g_game.getBrowseFieldContainer(world.tile.get());
			CHECK(browse && browse->getThingIndex(world.root.get()) >= 0);
			CHECK(world.root->getParent() == browse.get());
			CHECK(world.house->setOwner(0));
			CHECK(world.tile->getThingIndex(world.root.get()) < 0);
			CHECK(browse->getThingIndex(world.root.get()) < 0);
			CHECK(world.root->getParent() == world.owner->getInbox(1));
			CHECK(IOMapSerialize::saveHouse(world.house.get()));
			CHECK(g_saveManager.savePlayerSync(world.owner.get()) == SaveResult::Persisted);
		});
	}
	verifyRestart(true);
}

TEST_CASE(wrapped_items_preserve_identity_and_attributes_exactly_once)
{
	{
		World world;
		world.sword->setIntAttr(ITEM_ATTRIBUTE_WRAPID, 3265);
		CHECK(IOMapSerialize::saveHouse(world.house.get()));
		dispatch([&] {
			world.online();
			CHECK(world.house->setOwner(0));
			CHECK(world.sword->getParent() == nullptr);
			CHECK(world.root->getParent() == world.owner->getInbox(1));
			CHECK(world.nested->getParent() == world.root.get());
			bool found = false;
			for (const auto& item : world.owner->getInbox(1)->getItemList()) {
				if (item->getItemUID() == 701003) {
					CHECK(item->getID() == 3265);
					found = true;
				}
			}
			CHECK(found);
			CHECK(g_saveManager.savePlayerSync(world.owner.get()) == SaveResult::Persisted);
		});
	}
	verifyRestart(true);
}

TEST_CASE(reentrant_removal_callback_sees_complete_transfer_and_keeps_sources_alive)
{
	{
		World world;
		bool called = false;
		auto event = std::make_unique<MoveEvent>(g_moveEvents->getScriptInterfacePtr());
		event->setEventType(MOVE_EVENT_REMOVE_ITEM);
		event->addPosList(housePosition);
		event->moveFunction = [&](Item*, Item*, const Position&) -> uint32_t {
			if (called) return 1;
			called = true;
			CHECK(world.house->getOwner() == 0);
			CHECK(number("SELECT owner AS value FROM houses WHERE id=701") == 0);
			CHECK(world.root->getParent() == world.owner->getInbox(1));
			CHECK(world.nested->getParent() == world.root.get());
			CHECK(world.sword->getParent() == world.owner->getInbox(1));
			CHECK(!world.house->setOwner(8)); // no nested ownership transition
			CHECK(g_game.internalRemoveItem(world.nested.get()) == RETURNVALUE_NOERROR);
			g_game.cleanup();
			CHECK(IOMapSerialize::saveHouse(world.house.get()));
			CHECK(g_saveManager.savePlayerSync(world.owner.get()) == SaveResult::Persisted);
			return 1;
		};
		CHECK(g_moveEvents->registerLuaEvent(event.release()));
		dispatch([&] {
			world.online();
			CHECK(world.house->setOwner(0));
			CHECK(called);
		});
		g_moveEvents = std::make_unique<MoveEvents>();
	}
	World restored(false);
	IOMapSerialize::loadHouseItems(&g_game.map);
	const auto counts = count(*restored.owner, *restored.tile);
	CHECK(counts.roots == 1 && counts.nested == 0 && counts.swords == 1 && counts.coins == 57);
}

int main(int argc, char** argv)
{
	const char* name = std::getenv("TFS_HOUSE_TEST_DB");
	if (!name) {
		std::cout << "[skip] Requires disposable full-schema TFS_HOUSE_TEST_DB.\n";
		return 0;
	}
	if (!std::string_view(name).starts_with("tfs_save_audit_house_")) return EXIT_FAILURE;
#ifndef _WIN32
	std::signal(SIGPIPE, SIG_IGN);
#endif
	ConfigManager::setString(ConfigManager::MYSQL_HOST, "localhost");
	ConfigManager::setString(ConfigManager::MYSQL_USER,
	                         std::getenv("TFS_SAVE_TEST_USER") ? std::getenv("TFS_SAVE_TEST_USER") : "root");
	ConfigManager::setString(ConfigManager::MYSQL_PASS,
	                         std::getenv("TFS_SAVE_TEST_PASS") ? std::getenv("TFS_SAVE_TEST_PASS") : "");
	ConfigManager::setString(ConfigManager::MYSQL_DB, name);
	ConfigManager::setString(
	    ConfigManager::MYSQL_SOCK, std::getenv("TFS_SAVE_TEST_SOCKET") ? std::getenv("TFS_SAVE_TEST_SOCKET") :
#ifdef _WIN32
	                                                                   ""
#else
	                                                                   "/run/mysqld/mysqld.sock"
#endif
	);
	ConfigManager::setInteger(ConfigManager::SQL_PORT, 3306);
	if (const char* proxyPort = std::getenv("TFS_TEST_PROXY_PORT")) {
		ConfigManager::setString(ConfigManager::MYSQL_HOST, "127.0.0.1");
		ConfigManager::setString(ConfigManager::MYSQL_SOCK, "");
		ConfigManager::setInteger(ConfigManager::SQL_PORT, std::stoi(proxyPort));
	}
	try {
		CHECK(db().connect());
		std::filesystem::current_path(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path());
		CHECK(Item::items.loadFromOtb("data/items/items.otb"));
		CHECK(g_game.groups.load());
		CHECK(g_vocations.loadFromXml());
		CHECK(g_luaEnvironment.initState());
		auto chat = std::make_unique<Chat>();
		auto events = std::make_unique<Events>();
		auto globals = std::make_unique<GlobalEvents>();
		g_chat = chat.get();
		g_events = events.get();
		g_globalEvents = globals.get();
		g_moveEvents = std::make_unique<MoveEvents>();
		auto weapons = std::make_unique<Weapons>();
		g_weapons = weapons.get();
		CHECK(Item::items.loadFromXml());
		auto town = std::make_shared<Town>(1);
		town->setTemplePos({213, 210, 7});
		CHECK(g_game.map.towns.addTown(1, town));
		auto outside = std::make_unique<DynamicTile>(213, 210, 7);
		outside->internalAddThing(ground().get());
		g_game.map.setTile({213, 210, 7}, std::move(outside));
		g_threadPool.start(4);
		int result = 0;
		if (argc == 2) {
			const std::string_view mode = argv[1];
			if (mode == "--dupe")
				runExistingDupePhases();
			else if (mode == "--verify-before")
				verifyRestart(false);
			else if (mode == "--verify-after")
				verifyRestart(true);
			else if (mode == "--trade-verify-before")
				verifyTradeRestart(false);
			else if (mode == "--trade-verify-after")
				verifyTradeRestart(true);
			else if (mode == "--trade-commit-reply-loss") {
				runRealTrade();
				verifyTradeRestart(true);
			}
			else if (mode == "--trade-crash-before")
				runRealTrade(false, "before");
			else if (mode == "--trade-crash-after")
				runRealTrade(false, "after");
			else if (mode == "--commit-reply-loss" || mode == "--commit-unresolved") {
				World world;
				dispatch([&] {
					if (mode == "--commit-reply-loss") {
						CHECK(world.house->setOwner(0));
						CHECK(!g_saveManager.isPersistenceBlocked());
						CHECK(world.tile->getThingIndex(world.root.get()) < 0);
					} else {
						CHECK(!world.house->setOwner(0));
						CHECK(world.house->getOwner() == 7);
						CHECK(world.root->getParent() == world.tile.get());
						CHECK(g_saveManager.isPersistenceBlocked());
						CHECK(g_saveManager.hasFailedRecovery(7) && g_saveManager.hasFailedRecovery(8));
						CHECK(!g_saveManager.saveMap() && !IOMapSerialize::saveHouse(world.house.get()));
						CHECK(g_saveManager.savePlayerSync(world.owner.get()) == SaveResult::Failed);
						bool completed = false;
						g_saveManager.shutdownAsync([&](bool success) {
							CHECK(!success);
							completed = true;
						});
						CHECK(completed);
					}
				});
			} else {
				World world;
				if (mode == "--crash-before")
					world.house->crashBeforeCommit = true;
				else if (mode == "--crash-after") {
					g_moveEvents = std::make_unique<MoveEvents>();
					auto event = std::make_unique<MoveEvent>(g_moveEvents->getScriptInterfacePtr());
					event->setEventType(MOVE_EVENT_REMOVE_ITEM);
					event->addPosList(housePosition);
					event->moveFunction = [](Item*, Item*, const Position&) -> uint32_t { std::_Exit(74); };
					CHECK(g_moveEvents->registerLuaEvent(event.release()));
				} else
					throw std::runtime_error("Unknown crash-test mode");
				dispatch([&] { CHECK(world.house->setOwner(0)); });
				throw std::runtime_error("Crash path did not run");
			}
		} else
			result = tfs::tests::run();
		g_threadPool.shutdown();
		g_moveEvents.reset();
		weapons.reset();
		g_weapons = nullptr;
		globals.reset();
		events.reset();
		chat.reset();
		g_globalEvents = nullptr;
		g_events = nullptr;
		g_chat = nullptr;
		g_luaEnvironment.closeState();
		Database::shutdown();
		return result;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		g_threadPool.shutdown();
		return EXIT_FAILURE;
	}
}
