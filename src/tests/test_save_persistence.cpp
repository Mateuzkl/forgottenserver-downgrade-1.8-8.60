#include "../otpch.h"

#include "../configmanager.h"
#include "../database.h"
#include "../game.h"
#include "../iologindata.h"
#include "../item.h"
#include "../luascript.h"
#include "../save_journal.h"
#include "../save_manager.h"
#include "../scheduler.h"
#include "../tasks.h"
#include "../thread_pool.h"
#include "../town.h"
#include "test_support.h"

#include <cstdlib>
#include <future>
#ifndef _WIN32
#include <csignal>
#endif

extern Vocations g_vocations;
extern LuaEnvironment g_luaEnvironment;

// Private access seeds value snapshots, not an imitation queue. Tests execute
// the production SaveManager, journal SQL, transaction policy and reactor.
struct SaveManagerTestAccess
{
	static void queue(SaveManager& manager, IOLoginData::PlayerSaveSnapshot save, bool durable = false)
	{
		const uint32_t guid = save.guid;
		manager.queueSnapshot(guid, {"fixture", std::move(save), false, durable});
	}
	static size_t waiters(const SaveManager& manager) { return manager.flushChainCallbacks.size(); }
	static uint64_t generation(const SaveManager& manager) { return manager.saveGenerationId; }
	static void markBusy(SaveManager& manager, SaveManager::Completion completion = {})
	{
		manager.saving = true;
		manager.pendingSaveFlushes = 1;
		if (completion) manager.saveCallbacks.push_back(std::move(completion));
	}
	static void complete(SaveManager& manager) { manager.completeTrackedFlush(true); }
	static void blockPersistence(SaveManager& manager) { manager.persistenceBlocked = true; }
};

namespace {
Database& db() { return Database::getInstance(); }
void sql(std::string_view query) { CHECK(db().executeQuery(query)); }
uint64_t number(std::string_view query, std::string_view column = "value")
{
	const auto result = db().storeQuery(query);
	CHECK(result);
	return result->getNumber<uint64_t>(column);
}
void reset()
{
	sql("DELETE FROM `player_save_journal`");
	sql("DELETE FROM `player_save_async_pending`");
	sql("DELETE FROM `players`");
	sql("INSERT INTO `players` (`id`) VALUES (7)");
}
IOLoginData::PlayerSaveSnapshot save(uint64_t generation, uint32_t guid = 7, size_t queryCount = 1)
{
	IOLoginData::PlayerSaveSnapshot result;
	result.guid = guid;
	result.generation = generation;
	for (size_t i = 0; i < queryCount; ++i) {
		result.queries.push_back(
		    fmt::format("UPDATE `players` SET `balance` = {}, `onlinetime` = 120 WHERE `id` = {}", generation, guid));
	}
	return result;
}

template <typename Start>
void dispatch(Start start)
{
	g_dispatcher.start();
	g_scheduler.start();
	std::exception_ptr failure;
	bool timeout = false;
	g_dispatcher.addTask([&] {
		try {
			start();
		} catch (...) {
			failure = std::current_exception();
			g_reactor.shutdown();
		}
	});
	g_scheduler.addEvent(10000, [&] {
		timeout = true;
		g_reactor.shutdown();
	});
	g_reactor.runLoop();
	g_scheduler.shutdown();
	g_dispatcher.shutdown();
	if (failure) std::rethrow_exception(failure);
	CHECK(!timeout);
}

// Hold all workers before queueing to deterministically reproduce in-flight /
// pending and timeout races, rather than depending on database timing.
struct WorkersHeld
{
	std::promise<void> release;
	std::shared_future<void> gate = release.get_future().share();
	std::atomic<unsigned> entered{0};
	bool released = false;
	WorkersHeld()
	{
		for (unsigned i = 0; i < g_threadPool.get_thread_count(); ++i) {
			CHECK(g_threadPool.try_detach_task([this, wait = gate] {
				++entered;
				wait.wait();
			}));
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (entered != g_threadPool.get_thread_count() && std::chrono::steady_clock::now() < deadline) {
			std::this_thread::yield();
		}
		CHECK(entered == g_threadPool.get_thread_count());
	}
	void unblock()
	{
		if (!std::exchange(released, true)) release.set_value();
	}
	~WorkersHeld() { unblock(); }
};
} // namespace

TEST_CASE(journal_and_player_commit_are_idempotent_and_ordered)
{
	reset();
	for (uint64_t i = 1; i <= 20; ++i) {
		const auto snapshot = save(i);
		CHECK(IOLoginData::writePlayerJournal(snapshot));
		CHECK(IOLoginData::flushPlayerSave(snapshot));
		CHECK(IOLoginData::flushPlayerSave(snapshot)); // Repeated recovery/retry.
	}
	CHECK(number("SELECT `onlinetime` AS `value` FROM `players` WHERE `id`=7") == 120);
	CHECK(IOLoginData::flushPlayerSave(save(1))); // Late S1 cannot overwrite S20.
	CHECK(number("SELECT `balance` AS `value` FROM `players` WHERE `id`=7") == 20);
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 0);
}

TEST_CASE(newer_durable_journal_survives_an_older_commit)
{
	reset();
	CHECK(IOLoginData::writePlayerJournal(save(3)));
	CHECK(IOLoginData::writePlayerJournal(save(1))); // Late old WAL cannot replace S3.
	CHECK(IOLoginData::flushPlayerSave(save(1)));
	CHECK(number("SELECT `generation` AS `value` FROM `player_save_journal` WHERE `guid`=7") == 3);
	SaveManager restarted;
	CHECK(restarted.recoverPendingFlushes());
	CHECK(!restarted.hasFailedRecovery(7));
	CHECK(number("SELECT `balance` AS `value` FROM `players` WHERE `id`=7") == 3);
	CHECK(restarted.recoverPendingFlushes());
	CHECK(number("SELECT `onlinetime` AS `value` FROM `players` WHERE `id`=7") == 120);
}

TEST_CASE(sync_api_and_offline_temporary_players_use_monotonic_generations)
{
	reset();
	SaveManager manager;
	Player first(nullptr), second(nullptr);
	first.setGUID(7);
	second.setGUID(7);
	// The production no-save-flag path writes login metadata without requiring
	// a full world fixture. Both temporary objects loaded the same generation.
	first.setSaveFlag(false);
	second.setSaveFlag(false);
	dispatch([&] {
		for (unsigned i = 0; i < 10; ++i) CHECK(manager.savePlayerSync(&first) == SaveResult::Persisted);
		CHECK(manager.savePlayerSync(&second) == SaveResult::Persisted);
		CHECK(second.getSaveGeneration() == 11);
		CHECK(manager.savePlayersSync({&first, &second}) == false); // Duplicate GUID batch.
		first.setRemoved();
		CHECK(manager.savePlayer(&first) == SaveResult::Failed);
		g_reactor.shutdown();
	});
	CHECK(number("SELECT `save_generation` AS `value` FROM `players` WHERE `id`=7") == 11);
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 0);
}

TEST_CASE(failed_sync_transfer_does_not_leave_an_automatically_replayable_snapshot)
{
	reset();
	SaveManager manager;
	Player player(nullptr);
	player.setGUID(7);
	player.setSaveFlag(false);
	sql("CREATE TRIGGER `fail_sync_player_update` BEFORE UPDATE ON `players` FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected sync transfer failure'");
	dispatch([&] {
		CHECK(manager.savePlayerSync(&player) == SaveResult::Failed);
		g_reactor.shutdown();
	});
	sql("DROP TRIGGER `fail_sync_player_update`");
	CHECK(!manager.hasFailedRecovery(7));
	CHECK(number("SELECT `save_generation` AS `value` FROM `players` WHERE `id`=7") == 0);
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 0);
	SaveManager restarted;
	CHECK(restarted.recoverPendingFlushes());
	CHECK(number("SELECT `save_generation` AS `value` FROM `players` WHERE `id`=7") == 0);
	dispatch([&] {
		CHECK(manager.savePlayerSync(&player) == SaveResult::Persisted);
		manager.shutdownAsync([&](bool success) {
			CHECK(success);
			g_reactor.shutdown();
		});
	});
	CHECK(player.getSaveGeneration() == 2 && !manager.hasFailedRecovery(7));
}

TEST_CASE(failed_logout_commit_retains_its_journal_and_blocks_shutdown)
{
	reset();
	SaveManager manager;
	Player player(nullptr);
	player.setGUID(7);
	player.setSaveFlag(false);
	sql("CREATE TRIGGER fail_logout_update BEFORE UPDATE ON players FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected logout failure'");
	dispatch([&] {
		CHECK(manager.savePlayerSync(&player, true) == SaveResult::Failed);
		CHECK(manager.hasFailedRecovery(7));
		manager.shutdownAsync([&](bool success) {
			CHECK(!success);
			g_reactor.shutdown();
		});
	});
	sql("DROP TRIGGER fail_logout_update");
	CHECK(number("SELECT COUNT(*) AS value FROM player_save_journal") == 1);
}

TEST_CASE(logout_contract_queues_durable_snapshot_but_sync_api_does_not)
{
	reset();
	SaveManager manager;
	Player player(nullptr);
	player.setGUID(7);
	player.setSaveFlag(false);
	bool durable = false;
	dispatch([&] {
		WorkersHeld held;
		CHECK(manager.savePlayer(&player) == SaveResult::Queued);
		CHECK(number("SELECT `generation` AS `value` FROM `player_save_journal` WHERE `guid`=7") == 1);
		CHECK(manager.savePlayerSync(&player) == SaveResult::Failed);
		CHECK(number("SELECT `generation` AS `value` FROM `player_save_journal` WHERE `guid`=7") == 1);
		CHECK(manager.savePlayerSync(&player, true) == SaveResult::Queued);
		CHECK(number("SELECT `generation` AS `value` FROM `player_save_journal` WHERE `guid`=7") == 2);
		player.setRemoved(); // Completion must not depend on a live world Player.
		manager.shutdownAsync([&](bool success) {
			durable = success;
			g_reactor.shutdown();
		});
		held.unblock();
	});
	CHECK(durable);
	CHECK(number("SELECT `save_generation` AS `value` FROM `players` WHERE `id`=7") == 2);
}

TEST_CASE(missing_generation_schema_prevents_recovery_success)
{
	reset();
	sql("ALTER TABLE `players` CHANGE `save_generation` `audit_hidden_generation` BIGINT UNSIGNED NOT NULL DEFAULT 0");
	SaveManager manager;
	const bool recovered = manager.recoverPendingFlushes();
	sql("ALTER TABLE `players` CHANGE `audit_hidden_generation` `save_generation` BIGINT UNSIGNED NOT NULL DEFAULT 0");
	CHECK(!recovered);
}

TEST_CASE(journal_cleanup_failure_rolls_back_the_player_commit)
{
	reset();
	CHECK(IOLoginData::writePlayerJournal(save(1)));
	sql("CREATE TRIGGER `audit_delete_failure` BEFORE DELETE ON `player_save_journal` FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected DELETE failure'");
	const bool failed = IOLoginData::flushPlayerSave(save(1));
	SaveManager unresolved;
	const bool inspected = unresolved.recoverPendingFlushes();
	sql("DROP TRIGGER `audit_delete_failure`");
	CHECK(!failed && inspected && unresolved.hasFailedRecovery(7));
	CHECK(number("SELECT `balance` AS `value` FROM `players` WHERE `id`=7") == 0);
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 1);
	CHECK(IOLoginData::writePlayerJournal(save(2)));
	CHECK(IOLoginData::flushPlayerSave(save(2)));
	SaveManager restarted;
	CHECK(restarted.recoverPendingFlushes());
	CHECK(number("SELECT `balance` AS `value` FROM `players` WHERE `id`=7") == 2);
}

TEST_CASE(journal_write_failure_and_permanent_SQL_error_fail_closed)
{
	reset();
	sql("CREATE TRIGGER `audit_insert_failure` BEFORE INSERT ON `player_save_journal` FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected INSERT failure'");
	const bool inserted = IOLoginData::writePlayerJournal(save(1));
	sql("DROP TRIGGER `audit_insert_failure`");
	CHECK(!inserted);
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 0);
	auto bad = save(1);
	bad.queries.push_back("UPDATE `table_does_not_exist` SET x=1");
	CHECK(IOLoginData::writePlayerJournal(bad));
	SaveManager restarted;
	CHECK(restarted.recoverPendingFlushes());
	CHECK(restarted.hasFailedRecovery(7));
	CHECK(number("SELECT `balance` AS `value` FROM `players` WHERE `id`=7") == 0);
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 1);
}

TEST_CASE(corrupt_and_legacy_journals_are_preserved_and_blocked)
{
	reset();
	CHECK(IOLoginData::writePlayerJournal(save(1)));
	sql("UPDATE `player_save_journal` SET `payload`='truncated' WHERE `guid`=7");
	SaveManager corrupt;
	CHECK(corrupt.recoverPendingFlushes());
	CHECK(corrupt.hasFailedRecovery(7));
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 1);
	reset();
	sql("INSERT INTO `player_save_async_pending` VALUES (7,0,'UPDATE players SET onlinetime=onlinetime+50 WHERE id=7',0)");
	SaveManager legacy;
	CHECK(legacy.recoverPendingFlushes());
	CHECK(legacy.hasFailedRecovery(7));
	CHECK(number("SELECT `onlinetime` AS `value` FROM `players` WHERE `id`=7") == 0);
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_async_pending`") == 1);
}

TEST_CASE(missing_player_row_keeps_unresolved_journal_blocked)
{
	reset();
	CHECK(IOLoginData::writePlayerJournal(save(1)));
	sql("DELETE FROM `players` WHERE `id`=7");
	SaveManager manager;
	CHECK(manager.recoverPendingFlushes());
	CHECK(manager.hasFailedRecovery(7));
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 1);
}

TEST_CASE(startup_recovery_login_gate_does_not_block_healthy_session_shutdown)
{
	reset();
	sql("INSERT INTO player_save_async_pending VALUES (7,0,'legacy evidence',0)");
	sql("INSERT INTO players (id) VALUES (8)");
	SaveManager manager;
	CHECK(manager.recoverPendingFlushes() && manager.hasFailedRecovery(7));
	Player healthy(nullptr), blocked(nullptr);
	healthy.setGUID(8);
	healthy.setSaveFlag(false);
	blocked.setGUID(7);
	blocked.setSaveFlag(false);
	dispatch([&] {
		CHECK(manager.savePlayerSync(&blocked) == SaveResult::Failed);
		CHECK(manager.savePlayerSync(&healthy) == SaveResult::Persisted);
		CHECK(manager.saveAll());
		manager.shutdownAsync([&](bool success) {
			CHECK(success && manager.hasFailedRecovery(7));
			g_reactor.shutdown();
		});
	});
	CHECK(number("SELECT COUNT(*) AS value FROM player_save_async_pending") == 1);
}

TEST_CASE(connection_loss_before_commit_preserves_the_durable_journal)
{
	reset();
	CHECK(IOLoginData::writePlayerJournal(save(1)));
	DBTransaction transaction;
	CHECK(transaction.begin());
	CHECK(IOLoginData::applyPlayerSave(save(1)));
	const auto connectionId = number("SELECT CONNECTION_ID() AS `value`");
	// Kill only this fixture's own connection, never a game/server session.
	std::async(std::launch::async, [connectionId] { sql(fmt::format("KILL {}", connectionId)); }).get();
	CHECK(!transaction.commit());
	transaction.rollback();
	CHECK(number("SELECT `balance` AS `value` FROM `players` WHERE `id`=7") == 0);
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 1);
	SaveManager restarted;
	CHECK(restarted.recoverPendingFlushes());
	CHECK(!restarted.hasFailedRecovery(7));
}

TEST_CASE(full_dispatcher_inbox_does_not_drop_save_completion)
{
	reset();
	SaveManager manager;
	bool durable = false;
	unsigned completed = 0;
	dispatch([&] {
		WorkersHeld held;
		g_reactor.setMaxInboxSize(1);
		CHECK(g_dispatcher.addTask([] {})); // Saturate the worker completion inbox.
		CHECK(!g_dispatcher.addTask([] {}));
		SaveManagerTestAccess::queue(manager, save(1));
		manager.drainPlayerFlushAsync(7, [&](bool success) {
			CHECK(success);
			++completed;
		});
		manager.shutdownAsync([&](bool success) {
			durable = success;
			g_reactor.setMaxInboxSize(REACTOR_MAX_INBOX_SIZE);
			g_reactor.shutdown();
		});
		held.unblock();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (number("SELECT `save_generation` AS `value` FROM `players` WHERE `id`=7") == 0 &&
		       std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	});
	g_reactor.setMaxInboxSize(REACTOR_MAX_INBOX_SIZE);
	CHECK(durable && completed == 1);
	CHECK(!manager.hasPendingPlayerSave(7));
}

TEST_CASE(latest_pending_snapshot_and_shutdown_drain_real_workers)
{
	reset();
	SaveManager manager;
	bool drained = false, stopped = false;
	int callbacks = 0;
	dispatch([&] {
		WorkersHeld held;
		SaveManagerTestAccess::queue(manager, save(1));
		for (uint64_t generation = 2; generation <= 20; ++generation) {
			SaveManagerTestAccess::queue(manager, save(generation));
		}
		manager.drainPlayerFlushAsync(7, [&](bool success) {
			drained = success;
			++callbacks;
		});
		manager.shutdownAsync([&](bool success) {
			stopped = success;
			g_reactor.shutdown();
		});
		held.unblock();
	});
	CHECK(drained && stopped && callbacks == 1);
	CHECK(!manager.hasPendingPlayerSave(7));
	CHECK(SaveManagerTestAccess::waiters(manager) == 0);
	CHECK(number("SELECT `balance` AS `value` FROM `players` WHERE `id`=7") == 20);
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 0);
}

TEST_CASE(timeout_releases_callback_and_does_not_cancel_or_duplicate_save_completion)
{
	reset();
	SaveManager manager;
	int calls = 0;
	bool timeoutResult = true, eventuallyDrained = false;
	std::weak_ptr<int> retained;
	std::unique_ptr<WorkersHeld> held;
	dispatch([&] {
		held = std::make_unique<WorkersHeld>();
		SaveManagerTestAccess::queue(manager, save(1));
		auto lifetime = std::make_shared<int>(1);
		retained = lifetime;
		manager.drainPlayerFlushAsync(
		    7,
		    [&, lifetime](bool success) {
			    ++calls;
			    timeoutResult = success;
		    },
		    2);
		g_scheduler.addEvent(20, [&] { held->unblock(); });
		manager.shutdownAsync([&](bool success) {
			eventuallyDrained = success;
			g_reactor.shutdown();
		});
	});
	CHECK(calls == 1 && !timeoutResult && eventuallyDrained);
	CHECK(retained.expired());
	CHECK(SaveManagerTestAccess::waiters(manager) == 0);
}

TEST_CASE(lua_drain_callback_has_script_context_and_releases_registry_reference)
{
	reset();
	ConfigManager::setBoolean(ConfigManager::WARN_UNSAFE_SCRIPTS, false);
	ConfigManager::setBoolean(ConfigManager::CONVERT_UNSAFE_SCRIPTS, false);
	CHECK(g_luaEnvironment.initState());
	struct CloseLua
	{
		~CloseLua() { g_luaEnvironment.closeState(); }
	} closeLua;
	lua_State* L = g_luaEnvironment.getLuaState();
	Player player(nullptr);
	player.setGUID(7);
	// This offline fixture is not registered in the game ownership map. Use
	// the supported raw-userdata validation path, not an empty weak owner.
	Lua::pushUserdata<Player>(L, &player, 0);
	luaL_getmetatable(L, "Player");
	lua_setmetatable(L, -2);
	lua_setglobal(L, "fixturePlayer");
	lua_pushcfunction(L, [](lua_State* state) {
		lua_pushboolean(state, LuaScriptInterface::hasScriptEnv());
		return 1;
	});
	lua_setglobal(L, "hasCallbackScriptContext");
	const int stackBase = lua_gettop(L);
	dispatch([&] {
		WorkersHeld held;
		SaveManagerTestAccess::queue(g_saveManager, save(1));
		CHECK(LuaScriptInterface::reserveScriptEnv());
		const int result = luaL_dostring(L, R"lua(
            captured = setmetatable({}, {__mode = 'v'})
            do
                local token = {}
                captured[1] = token
                assert(fixturePlayer:drainAsyncSave(function(success)
                    callbackSuccess = success
                    callbackContext = hasCallbackScriptContext()
                    callbackToken = token ~= nil
                    error('expected callback failure')
                end, 'ignored extra argument'))
            end
        )lua");
		LuaScriptInterface::resetScriptEnv();
		if (result != LUA_OK) {
			throw std::runtime_error(lua_tostring(L, -1));
		}
		g_saveManager.drainPlayerFlushAsync(7, [&](bool success) {
			CHECK(success);
			g_reactor.shutdown();
		});
		held.unblock();
	});
	CHECK(!LuaScriptInterface::hasScriptEnv());
	CHECK(lua_gettop(L) == stackBase);
	CHECK(
	    luaL_dostring(
	        L,
	        "collectgarbage('collect'); assert(callbackSuccess and callbackContext and callbackToken); assert(captured[1] == nil)") ==
	    LUA_OK);
}

TEST_CASE(shutdown_failure_is_explicit_and_preserves_journal_evidence)
{
	reset();
	SaveManager manager;
	bool shutdownOK = true;
	auto bad = save(1);
	bad.queries.push_back("UPDATE `missing_schema_table` SET x=1");
	dispatch([&] {
		SaveManagerTestAccess::queue(manager, std::move(bad));
		manager.shutdownAsync([&](bool success) {
			shutdownOK = success;
			g_reactor.shutdown();
		});
	});
	CHECK(!shutdownOK && manager.hasFailedRecovery(7));
	CHECK(!manager.hasPendingPlayerSave(7));
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 1);
}

TEST_CASE(reentrant_global_requests_coalesce_one_follow_up_generation)
{
	reset();
	SaveManager manager;
	int completions = 0;
	dispatch([&] {
		// Hold the first generation exactly as while player workers run. The next
		// 20 real saveAll requests coalesce into one follow-up, not a time throttle.
		SaveManagerTestAccess::markBusy(manager);
		for (int i = 0; i < 20; ++i) {
			CHECK(manager.saveAll([&](bool success) {
				CHECK(success);
				++completions;
			}));
		}
		SaveManagerTestAccess::complete(manager);
		manager.shutdownAsync([&](bool success) {
			CHECK(success);
			g_reactor.shutdown();
		});
	});
	CHECK(completions == 20);
	CHECK(SaveManagerTestAccess::generation(manager) == 1);
	CHECK(!manager.isSaving());
}

TEST_CASE(synchronous_follow_up_waits_for_prior_callbacks_before_shutdown)
{
	reset();
	SaveManager manager;
	Player player(nullptr);
	player.setGUID(7);
	player.setSaveFlag(false);
	bool priorCompleted = false, shutdownOK = false, shutdownWasEarly = false;
	dispatch([&] {
		SaveManagerTestAccess::markBusy(manager, [&](bool success) { priorCompleted = success; });
		CHECK(manager.saveAll([&](bool success) {
			CHECK(success);
			manager.shutdownAsync([&](bool drained) {
				shutdownOK = drained;
				shutdownWasEarly = !priorCompleted;
				g_reactor.shutdown();
			});
		}));
		SaveManagerTestAccess::complete(manager);
		CHECK(manager.savePlayerSync(&player) == SaveResult::Failed);
	});
	CHECK(priorCompleted && shutdownOK && !shutdownWasEarly);
}

TEST_CASE(rejected_follow_up_completes_every_callback_before_shutdown)
{
	reset();
	SaveManager manager;
	unsigned completions = 0;
	bool priorCompleted = false, shutdownCompleted = false;
	dispatch([&] {
		SaveManagerTestAccess::markBusy(manager, [&](bool success) { priorCompleted = success; });
		for (unsigned i = 0; i < 20; ++i) {
			CHECK(manager.saveAll([&](bool success) {
				CHECK(!success);
				++completions;
				manager.shutdownAsync([&](bool drained) {
					CHECK(!drained && priorCompleted);
					shutdownCompleted = true;
				});
			}));
		}
		SaveManagerTestAccess::blockPersistence(manager);
		SaveManagerTestAccess::complete(manager);
		CHECK(shutdownCompleted && completions == 20);
		g_reactor.shutdown();
	});
}

TEST_CASE(production_snapshot_uses_absolute_online_time_and_skips_disabled_bestiary)
{
	reset();
	const auto path =
	    std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data/items/items.otb";
	if (Item::items.size() == 0) CHECK(Item::items.loadFromOtb(path.string()));
	Player player(nullptr);
	player.setGUID(7);
	auto group = std::make_shared<Group>();
	group->id = 1;
	player.setGroup(group);
	player.setTown(std::make_shared<Town>(1));
	CHECK(g_vocations.loadFromXml());
	CHECK(player.setVocation(0));
	player.startOnlineTime(time(nullptr) - 120);
	ConfigManager::setBoolean(ConfigManager::BESTIARY_SYSTEM_ENABLED, false);
	for (unsigned i = 0; i < 10; ++i) {
		const auto snapshot = IOLoginData::buildPlayerSave(&player);
		CHECK(snapshot && snapshot->snapshotModifiedBestiaryRaceIds.empty());
		CHECK(snapshot->queries.front().find("`onlinetime` = ") != std::string::npos);
		CHECK(snapshot->queries.front().find("`onlinetime` +") == std::string::npos);
		for (const auto& query : snapshot->queries) {
			CHECK(query.find("player_bestiary") == std::string::npos);
			CHECK(query.find("player_bosstiary") == std::string::npos);
		}
	}
	CHECK(player.getOnlineTime(time(nullptr)) >= 120 && player.getOnlineTime(time(nullptr)) < 130);
}

TEST_CASE(save_queue_benchmark_is_opt_in_and_uses_only_disposable_data)
{
	if (!std::getenv("TFS_SAVE_BENCHMARK")) return;
	using Clock = std::chrono::steady_clock;
	auto milliseconds = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
	auto questions = [] { return number("SHOW SESSION STATUS LIKE 'Questions'", "Value"); };
	std::cout
	    << "players,snapshot_ms,legacy_dispatcher_wal_ms,legacy_wal_queries,new_dispatcher_queue_ms,new_dispatcher_wal_queries,new_durable_total_ms,payload_bytes\n";
	for (const uint32_t count : {100U, 600U, 1000U}) {
		reset();
		std::string insert = "INSERT INTO `players` (`id`) VALUES ";
		for (uint32_t guid = 100; guid < 100 + count; ++guid) {
			if (guid != 100) insert += ',';
			insert += '(' + std::to_string(guid) + ')';
		}
		sql(insert);
		std::vector<IOLoginData::PlayerSaveSnapshot> snapshots;
		const auto buildStart = Clock::now();
		for (uint32_t guid = 100; guid < 100 + count; ++guid) snapshots.push_back(save(1, guid, 10));
		const double buildMs = milliseconds(Clock::now() - buildStart);
		// Reproduce the legacy dispatcher WAL insertion loop against real MariaDB,
		// using exactly the same synthetic snapshots as the new queue benchmark.
		const uint64_t beforeLegacy = questions();
		const auto legacyStart = Clock::now();
		for (const auto& snapshot : snapshots) {
			DBTransaction transaction;
			CHECK(transaction.begin());
			sql(fmt::format("DELETE FROM `player_save_async_pending` WHERE `guid`={}", snapshot.guid));
			for (size_t i = 0; i < snapshot.queries.size(); ++i) {
				sql(fmt::format("INSERT INTO `player_save_async_pending` VALUES ({},{},{},0)", snapshot.guid, i,
				                db().escapeString(snapshot.queries[i])));
			}
			CHECK(transaction.commit());
		}
		const double legacyMs = milliseconds(Clock::now() - legacyStart);
		const uint64_t legacyQueries = questions() - beforeLegacy - 1;
		sql("DELETE FROM `player_save_async_pending`");
		uint64_t payloadBytes = 0;
		for (const auto& snapshot : snapshots)
			payloadBytes += tfs::save::encode(snapshot.guid, 1, snapshot.queries)->size();
		SaveManager manager;
		double queueMs = 0, totalMs = 0;
		uint64_t queueQueries = 0;
		bool durable = false;
		dispatch([&] {
			const uint64_t before = questions();
			const auto queueStart = Clock::now();
			for (auto& snapshot : snapshots) SaveManagerTestAccess::queue(manager, std::move(snapshot));
			queueMs = milliseconds(Clock::now() - queueStart);
			queueQueries = questions() - before - 1;
			manager.shutdownAsync([&, queueStart](bool success) {
				durable = success;
				totalMs = milliseconds(Clock::now() - queueStart);
				g_reactor.shutdown();
			});
		});
		CHECK(durable && queueQueries == 0);
		CHECK(number("SELECT COUNT(*) AS `value` FROM `players` WHERE `save_generation`=1") == count);
		CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 0);
		std::cout << count << ',' << buildMs << ',' << legacyMs << ',' << legacyQueries << ',' << queueMs << ','
		          << queueQueries << ',' << totalMs << ',' << payloadBytes << '\n';
	}
}

TEST_CASE(stopped_dispatcher_never_reports_an_unacknowledged_save_as_drained)
{
	reset();
	SaveManager manager;
	std::unique_ptr<WorkersHeld> held;
	bool waiterCalled = false, shutdownCalled = false;
	dispatch([&] {
		held = std::make_unique<WorkersHeld>();
		SaveManagerTestAccess::queue(manager, save(1));
		manager.drainPlayerFlushAsync(7, [&](bool) { waiterCalled = true; });
		manager.shutdownAsync([&](bool) { shutdownCalled = true; });
		g_dispatcher.stop();
		g_reactor.shutdown();
	});
	CHECK(!g_dispatcher.addTask([] {}));
	held->unblock();
	// Keep manager/callbacks alive until the worker has observed rejection.
	g_threadPool.shutdown();
	CHECK(!waiterCalled && !shutdownCalled && manager.hasPendingPlayerSave(7));
	CHECK(number("SELECT `save_generation` AS `value` FROM `players` WHERE `id`=7") == 1);
	CHECK(number("SELECT COUNT(*) AS `value` FROM `player_save_journal`") == 0);
	g_threadPool.start(4);
}

TEST_CASE(worker_rejection_does_not_strand_an_in_flight_save)
{
	reset();
	g_threadPool.shutdown(); // Last worker test; the pool must explicitly reject.
	SaveManager manager;
	bool drained = true;
	dispatch([&] {
		SaveManagerTestAccess::queue(manager, save(1));
		manager.drainPlayerFlushAsync(7, [&](bool success) { drained = success; });
		g_reactor.shutdown();
	});
	CHECK(!drained && !manager.hasPendingPlayerSave(7));
	CHECK(manager.hasFailedRecovery(7));
}

#ifndef _WIN32
// The production GDB export lives in otserv.cpp (part of tfslib). Defining a
// second copy here breaks the Linux link with "multiple definition of
// `saveServer'" whenever the unity chunk holding otserv.cpp is pulled in.
// Referencing the library symbol instead keeps it in this binary for the
// unmodified production GDB crash script, in both unity and non-unity builds.
extern "C" bool saveServer();
[[maybe_unused]] __attribute__((used)) static bool (*const keepSaveServerExport)() = &saveServer;
#endif

// Exercise the same wait/drain method called by the GDB export, without
// faulting a real game server. Held workers prove acceptance is not completion.
void runCrashSaveWait(std::string_view mode)
{
	reset();
	WorkersHeld held;
	std::thread caller;
	std::atomic<bool> returned{false};
	bool result = true, drained = false;
	const bool fail = mode == "--crash-save-failure";
	const bool timeout = mode == "--crash-save-timeout";
	const bool debugger = mode == "--gdb-save-wait";
	CHECK(!g_game.saveCrashStateAndWait(10)); // Stopped dispatcher rejects immediately.
	dispatch([&] {
		CHECK(!g_game.saveCrashStateAndWait(10)); // Never block the dispatcher on itself.
		SaveManagerTestAccess::queue(g_saveManager, save(1));
		auto latest = save(2);
		if (fail) latest.queries.push_back("UPDATE nonexistent_crash_fixture SET x=1");
		SaveManagerTestAccess::queue(g_saveManager, std::move(latest));
		g_saveManager.drainPlayerFlushAsync(7, [&](bool success) {
			drained = success;
			g_scheduler.addEvent(10, [] { g_reactor.shutdown(); });
		});
		caller = std::thread([&] {
#ifndef _WIN32
			if (debugger) std::raise(SIGUSR1); // GDB injects saveServer on this non-dispatcher thread.
#endif
			result = g_game.saveCrashStateAndWait(timeout ? 20 : 5000);
			returned = true;
		});
		g_scheduler.addEvent(250, [&] {
			CHECK(returned == timeout);
			held.unblock();
		});
	});
	caller.join();
	CHECK(returned && result == (!fail && !timeout) && drained == !fail);
	CHECK(!g_saveManager.hasPendingPlayerSave(7));
	CHECK(number("SELECT save_generation AS value FROM players WHERE id=7") == (fail ? 1 : 2));
	CHECK(number("SELECT COUNT(*) AS value FROM player_save_journal") == (fail ? 1 : 0));
	std::cout << "Emergency save wait/drain regression passed: " << mode << '\n';
}

int main(int argc, char** argv)
{
	const char* name = std::getenv("TFS_SAVE_TEST_DB");
	if (!name) {
		std::cout << "[skip] Real save database tests require a disposable TFS_SAVE_TEST_DB.\n";
		return 0;
	}
	// Never run DELETE fixtures against a game database, including if misconfigured.
	if (!std::string_view(name).starts_with("tfs_save_audit_")) return EXIT_FAILURE;
#ifndef _WIN32
	std::signal(SIGPIPE, SIG_IGN); // Match server startup when injecting connection loss.
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
	if (!db().connect()) {
		const auto error = db().getLastConnectionError();
		std::cerr << "Disposable save-test database connection failed (" << error.code << "): " << error.message
		          << '\n';
		return EXIT_FAILURE;
	}
	try {
		std::filesystem::current_path(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path());
		sql("CREATE TABLE IF NOT EXISTS `players` (`id` INT PRIMARY KEY, `save_generation` BIGINT UNSIGNED NOT NULL DEFAULT 0, `balance` BIGINT NOT NULL DEFAULT 0, `onlinetime` BIGINT NOT NULL DEFAULT 0, `lastlogin` BIGINT NOT NULL DEFAULT 0, `lastip` INT UNSIGNED NOT NULL DEFAULT 0) ENGINE=InnoDB");
		// Allow reusing a disposable fixture created by an earlier test revision.
		sql("ALTER TABLE `players` ADD COLUMN IF NOT EXISTS `lastlogin` BIGINT NOT NULL DEFAULT 0, ADD COLUMN IF NOT EXISTS `lastip` INT UNSIGNED NOT NULL DEFAULT 0");
		sql("CREATE TABLE IF NOT EXISTS `player_save_journal` (`guid` INT PRIMARY KEY, `generation` BIGINT UNSIGNED NOT NULL, `payload` LONGBLOB NOT NULL, `payload_hash` BINARY(32) NOT NULL, `created_at` BIGINT NOT NULL) ENGINE=InnoDB");
		sql("CREATE TABLE IF NOT EXISTS `player_save_async_pending` (`guid` INT, `query_index` INT, `query_text` LONGBLOB, `created_at` BIGINT, PRIMARY KEY (`guid`,`query_index`)) ENGINE=InnoDB");
		sql("CREATE TABLE IF NOT EXISTS `tile_store` (`house_id` INT, `data` LONGBLOB) ENGINE=InnoDB");
		sql("CREATE TABLE IF NOT EXISTS `game_storage` (`key` INT, `value` BIGINT) ENGINE=InnoDB");
		sql("CREATE TABLE IF NOT EXISTS `account_storage` (`account_id` INT, `key` INT, `value` BIGINT) ENGINE=InnoDB");
		sql("CREATE TABLE IF NOT EXISTS `house_lists` (`house_id` INT, `listid` INT, `list` TEXT) ENGINE=InnoDB");
		sql("CREATE TABLE IF NOT EXISTS `kv_store` (`key_name` VARCHAR(255) PRIMARY KEY, `timestamp` BIGINT, `value` LONGBLOB) ENGINE=InnoDB");
		g_threadPool.start(4);
		int result = 0;
		if (argc == 2) {
			const std::string_view mode = argv[1];
			CHECK(mode == "--crash-save-wait" || mode == "--crash-save-failure" || mode == "--crash-save-timeout" ||
			      mode == "--gdb-save-wait");
			runCrashSaveWait(mode);
		} else {
			result = tfs::tests::run();
		}
		g_threadPool.shutdown();
		Database::shutdown();
		return result;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		g_threadPool.shutdown();
		return EXIT_FAILURE;
	}
}
