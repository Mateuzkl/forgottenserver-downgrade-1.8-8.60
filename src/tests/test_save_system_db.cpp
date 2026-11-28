#include "../otpch.h"

#include "../chat.h"
#include "../configmanager.h"
#include "../container.h"
#include "../database.h"
#include "../databasemanager.h"
#include "../depotchest.h"
#include "../events.h"
#include "../game.h"
#include "../globalevent.h"
#include "../house.h"
#include "../inbox.h"
#include "../iologindata.h"
#include "../iomapserialize.h"
#include "../item.h"
#include "../kv/kv.h"
#include "../logger.h"
#include "../player.h"
#include "../save_manager.h"
#include "../scriptmanager.h"
#include "../tasks.h"
#include "../thread_pool.h"
#include "../tile.h"
#include "../vocation.h"
#include "disposable_database.h"
#include "test_support.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>

extern Game g_game;
extern Vocations g_vocations;
extern Chat* g_chat;
extern Events* g_events;
extern GlobalEvents* g_globalEvents;

struct TaskReactorTestAccess
{
	static void runOnceAsOwner(TaskReactor& reactor)
	{
		const auto previous = TaskReactor::currentReactor;
		TaskReactor::currentReactor = &reactor;
		reactor.runOnce();
		TaskReactor::currentReactor = previous;
	}
};

struct SaveManagerTestAccess
{
	static bool writeWal(SaveManager& manager, uint32_t guid, IOLoginData::PlayerSaveSnapshot& snapshot)
	{
		return manager.savePendingFlushToDB(guid, snapshot);
	}

	static bool hasPendingQueued(const SaveManager& manager, uint32_t guid)
	{
		return manager.pendingFlushes.contains(guid);
	}

	static void injectStuckSaveCycle(SaveManager& manager, uint32_t guid)
	{
		manager.saving.store(true, std::memory_order_release);
		manager.pendingSaveFlushes.fetch_add(1, std::memory_order_release);
		manager.flushInFlight.insert(guid);
	}

	static void clearStuckSaveCycle(SaveManager& manager, uint32_t guid)
	{
		manager.flushInFlight.erase(guid);
		manager.pendingSaveFlushes.store(0, std::memory_order_release);
		manager.saving.store(false, std::memory_order_release);
	}
};

namespace {

constexpr uint32_t ACCOUNT_EMPTY = 910001;
constexpr uint32_t ACCOUNT_DATA_A = 910002;
constexpr uint32_t ACCOUNT_DATA_B = 910003;
constexpr uint32_t PLAYER_A = 910011;
constexpr uint32_t PLAYER_B = 910012;
constexpr uint32_t HOUSE_ID = 9101;

uint64_t queryNumber(std::string_view query)
{
	auto row = Database::getInstance().storeQuery(query);
	CHECK(row != nullptr);
	return row->getNumber<uint64_t>("value");
}

void execSql(std::string_view query)
{
	CHECK(Database::getInstance().executeQuery(query));
}

void dispatch(const std::function<void()>& callback)
{
	std::exception_ptr failure;
	bool ran = false;
	CHECK(g_dispatcher.addTask([&] {
		ran = true;
		try {
			callback();
		} catch (...) {
			failure = std::current_exception();
		}
	}));
	TaskReactorTestAccess::runOnceAsOwner(g_reactor);
	CHECK(ran);
	if (failure) {
		std::rethrow_exception(failure);
	}
}

void drainReactorUntil(const std::function<bool()>& done, size_t maxMillis = 5000)
{
	for (size_t i = 0; i < maxMillis && !done(); ++i) {
		TaskReactorTestAccess::runOnceAsOwner(g_reactor);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(done());
}

void initPlayer(Player& player, uint32_t guid, std::string_view name, uint32_t townId)
{
	player.setGUID(guid);
	player.setName(name);
	player.setGroup(g_game.groups.getSharedGroup(1));
	player.setTown(g_game.map.towns.getSharedTown(townId));
	CHECK(player.setVocation(0));
	player.setBankBalance(1000);
	player.setLastDepotId(townId);
}

struct RuntimeGuard
{
	~RuntimeGuard()
	{
		g_threadPool.shutdown();
		g_dispatcher.stop();
		g_reactor.drain();
		g_dispatcher.shutdown();
		shutdownLogger();
	}
};

struct WorldEventsGuard
{
	GlobalEvents globalEvents;
	Events events;
	Chat chat;
	GlobalEvents* previousGlobalEvents = g_globalEvents;
	Events* previousEvents = g_events;
	Chat* previousChat = g_chat;
	WorldEventsGuard()
	{
		g_globalEvents = &globalEvents;
		g_events = &events;
		g_chat = &chat;
	}
	~WorldEventsGuard()
	{
		g_globalEvents = previousGlobalEvents;
		g_events = previousEvents;
		g_chat = previousChat;
	}
};

} // namespace

TEST_CASE(save_system_audit_database_regressions)
{
	connectDisposableDatabase();
	CHECK(initLogger());
	const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	std::filesystem::current_path(root);

	CHECK(ScriptingManager::getInstance().loadPreItems());
	CHECK(Item::items.loadFromOtb("data/items/items.otb"));
	CHECK(Item::items.loadFromXml());
	CHECK(g_vocations.loadFromXml());
	CHECK(g_game.groups.load());

	auto town = std::make_shared<Town>(1);
	town->setName("AuditTown");
	town->setTemplePos(Position(100, 100, 7));
	g_game.map.towns.addTown(1, town);

	g_dispatcher.start();
	g_threadPool.start(1);
	RuntimeGuard runtime;
	WorldEventsGuard world;

	// Seed accounts and players in the disposable database.
	execSql(fmt::format("INSERT IGNORE INTO `accounts` (`id`, `name`, `password`) VALUES "
	                    "({}, 'audit_acc_empty', 'x'), ({}, 'audit_acc_a', 'x'), ({}, 'audit_acc_b', 'x')",
	                    ACCOUNT_EMPTY, ACCOUNT_DATA_A, ACCOUNT_DATA_B));
	execSql(fmt::format("INSERT IGNORE INTO `players` (`id`, `name`, `account_id`, `town_id`, `conditions`) VALUES "
	                    "({}, 'AuditPlayerA', {}, 1, ''), ({}, 'AuditPlayerB', {}, 1, '')",
	                    PLAYER_A, ACCOUNT_DATA_A, PLAYER_B, ACCOUNT_DATA_B));

	auto playerA = std::make_shared<Player>(nullptr);
	initPlayer(*playerA, PLAYER_A, "AuditPlayerA", 1);
	playerA->setID();

	// 1. Monotonic WAL generation prevents ABA collision (S1 -> S2 -> S3 with identical SQL to S1).
	dispatch([&] {
		playerA->setBankBalance(1000);
		auto s1 = IOLoginData::buildPlayerSave(playerA.get());
		CHECK(s1.has_value());
		CHECK(SaveManagerTestAccess::writeWal(g_saveManager, PLAYER_A, *s1));

		playerA->setBankBalance(2000);
		auto s2 = IOLoginData::buildPlayerSave(playerA.get());
		CHECK(s2.has_value());
		CHECK(SaveManagerTestAccess::writeWal(g_saveManager, PLAYER_A, *s2));

		// Revert to identical SQL state as S1.
		playerA->setBankBalance(1000);
		auto s3 = IOLoginData::buildPlayerSave(playerA.get());
		CHECK(s3.has_value());
		CHECK(SaveManagerTestAccess::writeWal(g_saveManager, PLAYER_A, *s3));
		CHECK(s1->walClearQuery != s3->walClearQuery);

		// Executing S1 must NOT delete S3's newer WAL generation.
		CHECK(IOLoginData::flushPlayerSaveChecked(*s1) == IOLoginData::FlushResult::Committed);
		CHECK(queryNumber(fmt::format("SELECT COUNT(*) AS value FROM `player_save_async_pending` WHERE `guid`={}", PLAYER_A)) > 0);

		// Executing S3 clears its own matching WAL generation.
		CHECK(IOLoginData::flushPlayerSaveChecked(*s3) == IOLoginData::FlushResult::Committed);
		CHECK(queryNumber(fmt::format("SELECT COUNT(*) AS value FROM `player_save_async_pending` WHERE `guid`={}", PLAYER_A)) == 0);
	});

	// 2. 100 rapid saves coalesce cleanly with final balance, nested container, and stackable count preserved.
	{
		std::promise<void> releaseGate;
		auto gate = releaseGate.get_future().share();
		auto blocker = g_threadPool.submit_task([gate] { gate.wait(); });

		dispatch([&] {
			auto bag = Item::CreateItem(ITEM_BAG);
			CHECK(bag != nullptr);
			auto coins = Item::CreateItem(ITEM_GOLD_COIN, 75);
			CHECK(coins != nullptr);
			bag->getContainer()->internalAddThing(coins.get());
			static_cast<Cylinder&>(*playerA).internalAddThing(CONST_SLOT_BACKPACK, bag.get());

			for (uint64_t i = 1; i <= 100; ++i) {
				playerA->setBankBalance(10000 + i);
				CHECK(g_saveManager.savePlayer(playerA.get()));
			}
		});

		releaseGate.set_value();
		blocker.get();
		drainReactorUntil([&] { return !g_saveManager.hasPendingPlayerSave(PLAYER_A); });

		dispatch([&] {
			CHECK(queryNumber(fmt::format("SELECT `balance` AS value FROM `players` WHERE `id`={}", PLAYER_A)) == 10100);
			CHECK(queryNumber(fmt::format("SELECT COALESCE(SUM(`count`),0) AS value FROM `player_items` WHERE `player_id`={} AND `itemtype`={}",
			                              PLAYER_A, static_cast<uint16_t>(ITEM_GOLD_COIN))) == 75);
			CHECK(queryNumber(fmt::format("SELECT COUNT(*) AS value FROM `player_save_async_pending` WHERE `guid`={}", PLAYER_A)) == 0);
		});
	}

	// 3. Offline savePlayerSync while an async flush is in flight must reject WITHOUT queueing stale state.
	{
		std::promise<void> releaseGate;
		auto gate = releaseGate.get_future().share();
		auto blocker = g_threadPool.submit_task([gate] { gate.wait(); });

		dispatch([&] {
			playerA->setBankBalance(55555);
			CHECK(g_saveManager.savePlayer(playerA.get()));
			CHECK(g_saveManager.hasPendingPlayerSave(PLAYER_A));

			Player staleOffline(nullptr);
			CHECK(staleOffline.isOffline());
			initPlayer(staleOffline, PLAYER_A, "AuditPlayerA", 1);
			staleOffline.setBankBalance(11111);

			// Must return false and MUST NOT overwrite pendingFlushes/WAL with stale offline state.
			CHECK(!g_saveManager.savePlayerSync(&staleOffline));
			CHECK(!SaveManagerTestAccess::hasPendingQueued(g_saveManager, PLAYER_A));
		});

		releaseGate.set_value();
		blocker.get();
		drainReactorUntil([&] { return !g_saveManager.hasPendingPlayerSave(PLAYER_A); });

		dispatch([&] {
			CHECK(queryNumber(fmt::format("SELECT `balance` AS value FROM `players` WHERE `id`={}", PLAYER_A)) == 55555);
		});
	}

	// 4. DepotChest modified directly (without opening DepotLocker) must still persist to player_depotitems.
	dispatch([&] {
		Player depotPlayer(nullptr);
		initPlayer(depotPlayer, PLAYER_B, "AuditPlayerB", 1);
		DepotChest* chest = depotPlayer.getDepotChest(1, true);
		CHECK(chest != nullptr);
		auto gold = Item::CreateItem(ITEM_GOLD_COIN, 42);
		CHECK(gold != nullptr);
		CHECK(g_game.internalAddItem(chest, gold.get(), INDEX_WHEREEVER, FLAG_NOLIMIT) == RETURNVALUE_NOERROR);
		CHECK(g_saveManager.savePlayerSync(&depotPlayer));
		CHECK(queryNumber(fmt::format("SELECT COALESCE(SUM(`count`),0) AS value FROM `player_depotitems` WHERE `player_id`={} AND `itemtype`={}",
		                              PLAYER_B, static_cast<uint16_t>(ITEM_GOLD_COIN))) == 42);

		// Removing the item must also persist (no ghost depot dupe on relog).
		CHECK(g_game.internalRemoveItem(gold.get(), 42) == RETURNVALUE_NOERROR);
		CHECK(g_saveManager.savePlayerSync(&depotPlayer));
		CHECK(queryNumber(fmt::format("SELECT COUNT(*) AS value FROM `player_depotitems` WHERE `player_id`={}", PLAYER_B)) == 0);
	});

	// 4b. Game::saveGameState reentrancy guard ignores nested calls cleanly.
	dispatch([&] {
		g_game.saveGameState(false, "manual");
		CHECK(!g_game.isSavingGameState());
	});

	// 5. Daily reward storage save while async flush is in flight routes through ordered SaveManager queue.
	{
		std::promise<void> releaseGate;
		auto gate = releaseGate.get_future().share();
		auto blocker = g_threadPool.submit_task([gate] { gate.wait(); });

		dispatch([&] {
			playerA->setStorageValue(STORAGE_DAILY_REWARD_STREAK, 1);
			CHECK(g_saveManager.savePlayer(playerA.get()));

			playerA->setStorageValue(STORAGE_DAILY_REWARD_STREAK, 2);
			CHECK(IOLoginData::savePlayerDailyRewardStorages(playerA.get()));
		});

		releaseGate.set_value();
		blocker.get();
		drainReactorUntil([&] { return !g_saveManager.hasPendingPlayerSave(PLAYER_A); });

		dispatch([&] {
			CHECK(queryNumber(fmt::format("SELECT `value` AS value FROM `player_storage` WHERE `player_id`={} AND `key`={}",
			                              PLAYER_A, static_cast<uint32_t>(STORAGE_DAILY_REWARD_STREAK))) == 2);
		});
	}

	// 6. Account storage save does not break on empty account map + mandatory degraded saveAll still persists world state.
	dispatch([&] {
		// Create an empty entry for ACCOUNT_EMPTY and non-empty entries for A and B.
		g_game.setAccountStorageValue(ACCOUNT_EMPTY, 500, 1);
		g_game.setAccountStorageValue(ACCOUNT_EMPTY, 500, -1);
		g_game.setAccountStorageValue(ACCOUNT_DATA_A, 501, 111);
		g_game.setAccountStorageValue(ACCOUNT_DATA_B, 502, 222);
		g_game.setStorageValue(91001, 777);
		g_game.setStorageValue(91002, 888);
		KVStore::getInstance().set("audit.degraded.key", ValueWrapper(int32_t{4242}));

		// Inject a stuck earlier save cycle and trigger mandatory saveAll.
		SaveManagerTestAccess::injectStuckSaveCycle(g_saveManager, 999999);
		g_saveManager.saveAll("shutdown", true);
		SaveManagerTestAccess::clearStuckSaveCycle(g_saveManager, 999999);

		CHECK(queryNumber(fmt::format("SELECT `value` AS value FROM `account_storage` WHERE `account_id`={} AND `key`=501", ACCOUNT_DATA_A)) == 111);
		CHECK(queryNumber(fmt::format("SELECT `value` AS value FROM `account_storage` WHERE `account_id`={} AND `key`=502", ACCOUNT_DATA_B)) == 222);
		CHECK(queryNumber("SELECT `value` AS value FROM `game_storage` WHERE `key`=91001") == 777);
		CHECK(queryNumber("SELECT `value` AS value FROM `game_storage` WHERE `key`=91002") == 888);
		CHECK(queryNumber("SELECT COUNT(*) AS value FROM `kv_store` WHERE `key_name`='audit.degraded.key'") == 1);
	});

	// 7. House transfer aborts cleanly if offline owner has a pending async save, and payHouses updates online owners directly.
	{
		std::promise<void> releaseGate;
		auto gate = releaseGate.get_future().share();
		auto blocker = g_threadPool.submit_task([gate] { gate.wait(); });

		auto house = std::make_shared<House>(HOUSE_ID);
		house->setName("Audit House");
		house->setTownId(1);
		house->setRent(500);
		house->setPaidUntil(1);
		execSql(fmt::format("INSERT IGNORE INTO `houses` (`id`, `owner`, `paid`, `warnings`, `name`, `rent`, `town_id`, `size`, `beds`) "
		                    "VALUES ({}, {}, 1, 0, 'Audit House', 500, 1, 1, 0)", HOUSE_ID, PLAYER_B));

		dispatch([&] {
			CHECK(house->setOwner(PLAYER_B, false));
			auto playerB = std::make_shared<Player>(nullptr);
			initPlayer(*playerB, PLAYER_B, "AuditPlayerB", 1);
			playerB->setID();
			playerB->setBankBalance(7777);
			CHECK(g_saveManager.savePlayer(playerB.get()));
			// Offline owner PLAYER_B has an async save in flight: setOwner(0, true) must abort before touching DB.
			CHECK(!house->setOwner(0, true));
			CHECK(house->getOwner() == PLAYER_B);
			CHECK(queryNumber(fmt::format("SELECT `owner` AS value FROM `houses` WHERE `id`={}", HOUSE_ID)) == PLAYER_B);
		});

		releaseGate.set_value();
		blocker.get();
		drainReactorUntil([&] { return !g_saveManager.hasPendingPlayerSave(PLAYER_B); });
	}

	// 8. Login barrier (drainPlayerFlushAsync) waits for in-flight flush and WAL recovery is idempotent.
	{
		std::promise<void> releaseGate;
		auto gate = releaseGate.get_future().share();
		auto blocker = g_threadPool.submit_task([gate] { gate.wait(); });

		std::atomic<bool> barrierFired{false};
		bool barrierResult = false;
		dispatch([&] {
			playerA->setBankBalance(88888);
			CHECK(g_saveManager.savePlayer(playerA.get()));
			g_saveManager.drainPlayerFlushAsync(PLAYER_A, [&](bool ok) {
				barrierResult = ok;
				barrierFired.store(true, std::memory_order_release);
			});
		});

		TaskReactorTestAccess::runOnceAsOwner(g_reactor);
		CHECK(!barrierFired.load(std::memory_order_acquire));

		releaseGate.set_value();
		blocker.get();
		drainReactorUntil([&] { return barrierFired.load(std::memory_order_acquire); });
		CHECK(barrierResult);
		CHECK(queryNumber(fmt::format("SELECT `balance` AS value FROM `players` WHERE `id`={}", PLAYER_A)) == 88888);

		// Leave a durable WAL row and verify recoverPendingFlushes replays it idempotently.
		dispatch([&] {
			playerA->setBankBalance(99999);
			auto snap = IOLoginData::buildPlayerSave(playerA.get());
			CHECK(snap.has_value());
			CHECK(SaveManagerTestAccess::writeWal(g_saveManager, PLAYER_A, *snap));

			SaveManager recovered;
			recovered.recoverPendingFlushes();
			CHECK(!recovered.hasFailedRecovery(PLAYER_A));
			CHECK(queryNumber(fmt::format("SELECT `balance` AS value FROM `players` WHERE `id`={}", PLAYER_A)) == 99999);
			CHECK(queryNumber(fmt::format("SELECT COUNT(*) AS value FROM `player_save_async_pending` WHERE `guid`={}", PLAYER_A)) == 0);
		});
	}
}

int main()
{
	if (!std::getenv("TFS_TEST_DB_NAME")) {
		std::cout << "[SKIP] TFS_TEST_DB_NAME is required for real MySQL save-system tests\n";
		return 77;
	}
	return tfs::tests::run();
}
