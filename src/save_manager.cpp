// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in the LICENSE file.

#include "otpch.h"

#include "save_manager.h"

#include "game.h"
#include "iomapserialize.h"
#include "kv/kv.h"
#include "logger.h"
#include "save_journal.h"
#include "scheduler.h"
#include "tasks.h"
#include "thread_pool.h"

#include <openssl/sha.h>

SaveManager g_saveManager;

namespace {
int64_t saveClock()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
	    .count();
}
} // namespace

bool SaveManager::saveAll(Completion completion)
{
	if (!g_dispatcher.isDispatcherThread() || !accepting) {
		if (completion) completion(false);
		return false;
	}
	if (isSaving()) {
		// One intentional follow-up generation snapshots the latest state. No time
		// throttle can discard requests, and all callers receive their completion.
		saveAgain = true;
		if (completion) nextSaveCallbacks.push_back(std::move(completion));
		return true;
	}
	if (completion) saveCallbacks.push_back(std::move(completion));
	saving.store(true, std::memory_order_release);
	++saveGenerationId;
	saveStartedMs = saveClock();
	generationSucceeded = true;
	pendingSaveFlushes = 1; // Sentinel: completion cannot fire while snapshotting/map save runs.
	generationSucceeded &= g_game.saveGameStorageValues();
	generationSucceeded &= g_game.saveAccountStorageValues();
	generationSucceeded &= KVStore::getInstance().saveAll();
	uint32_t count = 0;
	for (const auto& player : g_game.getPlayers()) {
		if (schedulePlayerFlush(player.get(), true) != SaveResult::Failed) {
			++count;
		} else {
			generationSucceeded = false;
		}
	}
	lastPlayersSaved.store(count);
	generationSucceeded &= saveMap();
	LOG_INFO("[SaveManager] Generation {}: scheduled {} player snapshots in {}ms; waiting for durable completion.",
	         saveGenerationId, count, saveClock() - saveStartedMs);
	completeTrackedFlush(true);
	return true;
}

std::optional<IOLoginData::PlayerSaveSnapshot> SaveManager::snapshot(Player* player, const ItemBlockList& inboxCredit)
{
	if (!player || player->isRemoved() || !accepting || hasFailedRecovery(player->getGUID())) return {};
	auto save = IOLoginData::buildPlayerSave(player, inboxCredit);
	if (!save) return {};
	auto& generation = generations[player->getGUID()];
	generation = std::max(generation, player->getSaveGeneration());
	if (generation == std::numeric_limits<uint64_t>::max()) return {};
	save->generation = ++generation;
	return save;
}

SaveResult SaveManager::savePlayer(Player* player)
{
	if (!g_dispatcher.isDispatcherThread() || Database::getInstance().isInTransaction()) {
		return SaveResult::Failed;
	}
	auto save = snapshot(player);
	if (!save) return SaveResult::Failed;
	const uint32_t guid = player->getGUID();
	// Public per-player async saves retain WAL-durable acceptance. Global saves
	// write their journals on workers and claim durability only on completion.
	if (!IOLoginData::writePlayerJournal(*save)) {
		// Nothing durable was written, so there is nothing to reconcile. Blocking
		// the GUID here would reject every later save of a live player for the
		// rest of the session. Report the failure and let the next save retry.
		LOG_ERROR("[SaveManager] Journal write failed for guid={}; the next save will retry.", guid);
		return SaveResult::Failed;
	}
	queueSnapshot(guid, {player->getName(), std::move(*save), false, true});
	return hasFailedRecovery(guid) ? SaveResult::Failed : SaveResult::Queued;
}

SaveResult SaveManager::schedulePlayerFlush(Player* player, bool tracked)
{
	if (!g_dispatcher.isDispatcherThread()) return SaveResult::Failed;
	auto save = snapshot(player);
	if (!save) return SaveResult::Failed;
	queueSnapshot(player->getGUID(), {player->getName(), std::move(*save), tracked, false});
	return hasFailedRecovery(player->getGUID()) ? SaveResult::Failed : SaveResult::Scheduled;
}

void SaveManager::queueSnapshot(uint32_t guid, PendingPlayerFlush pending)
{
	if (flushInFlight.contains(guid)) {
		auto old = pendingFlushes.find(guid);
		if (old != pendingFlushes.end()) {
			pending.trackedBySaveAll |= old->second.trackedBySaveAll;
		}
		if (pending.trackedBySaveAll && (old == pendingFlushes.end() || !old->second.trackedBySaveAll)) {
			++pendingSaveFlushes;
		}
		pendingFlushes.insert_or_assign(guid, std::move(pending));
		return;
	}
	flushInFlight.insert(guid);
	if (pending.trackedBySaveAll) ++pendingSaveFlushes;
	dispatchPlayerFlush(guid, std::move(pending));
}

SaveResult SaveManager::savePlayerSync(Player* player, bool allowDurableQueue)
{
	if (!g_dispatcher.isDispatcherThread() || !player || !accepting || hasFailedRecovery(player->getGUID()) ||
	    Database::getInstance().isInTransaction()) {
		return SaveResult::Failed;
	}
	const uint32_t guid = player->getGUID();
	// A synchronous API does not mutate/queue a snapshot it cannot persist now.
	if (hasPendingPlayerSave(guid) && !allowDurableQueue) return SaveResult::Failed;
	auto save = snapshot(player);
	if (!save) {
		if (allowDurableQueue) {
			sessionSaveFailed = true;
			failedRecoveryGuids.insert(guid);
		}
		return SaveResult::Failed;
	}
	// Offline transfer callers such as mail restore their mutation on sync failure.
	// Do not leave a replayable snapshot of that rolled-back transfer behind.
	// Logout cannot restore its removed Player, so it explicitly requires WAL.
	if (allowDurableQueue && !IOLoginData::writePlayerJournal(*save)) {
		sessionSaveFailed = true;
		failedRecoveryGuids.insert(guid);
		LOG_ERROR("[SaveManager] Journal write failed for guid={}; latest save was not accepted as durable.", guid);
		return SaveResult::Failed;
	}
	if (flushInFlight.contains(guid)) {
		queueSnapshot(guid, {player->getName(), std::move(*save), false, true});
		return SaveResult::Queued;
	}
	const bool success = IOLoginData::flushPlayerSave(*save);
	if (success) {
		player->acknowledgeSaveGeneration(save->generation);
		player->acknowledgeStorageDirty(
		    {save->storageSnapshotId, save->snapshotModifiedKeys, save->snapshotRemovedKeys});
		player->acknowledgeBestiaryDirty({save->bestiarySnapshotId, save->snapshotModifiedBestiaryRaceIds});
		return SaveResult::Persisted;
	}
	if (allowDurableQueue) {
		sessionSaveFailed = true;
		failedRecoveryGuids.insert(guid);
	}
	LOG_ERROR("[SaveManager] Player commit failed for guid={}. {}", guid,
	          allowDurableQueue ? "Login blocked; logout journal retained."
	                            : "Synchronous mutation was not accepted as durable; caller must restore it.");
	return SaveResult::Failed;
}

bool SaveManager::savePlayersSync(const std::vector<Player*>& players)
{
	if (!g_dispatcher.isDispatcherThread() || !accepting || Database::getInstance().isInTransaction()) return false;
	std::unordered_set<uint32_t> guids;
	std::vector<IOLoginData::PlayerSaveSnapshot> saves;
	for (Player* player : players) {
		if (!player || hasPendingPlayerSave(player->getGUID()) || !guids.insert(player->getGUID()).second) return false;
		auto save = snapshot(player);
		if (!save) return false;
		saves.push_back(std::move(*save));
	}
	if (!DBTransaction::executeWithinTransactionRollbackOnFailure([&saves]() {
		    for (const auto& save : saves) {
			    if (!IOLoginData::applyPlayerSave(save)) return false;
		    }
		    return true;
	    }))
		return false;
	for (size_t i = 0; i < players.size(); ++i) {
		const auto& save = saves[i];
		players[i]->acknowledgeSaveGeneration(save.generation);
		players[i]->acknowledgeStorageDirty(
		    {save.storageSnapshotId, save.snapshotModifiedKeys, save.snapshotRemovedKeys});
		players[i]->acknowledgeBestiaryDirty({save.bestiarySnapshotId, save.snapshotModifiedBestiaryRaceIds});
	}
	return true;
}

bool SaveManager::savePlayerTransfer(Player* player, const ItemBlockList& inboxCredit,
                                     const std::function<bool()>& sideChanges, const std::vector<Player*>& participants)
{
	Database& db = Database::getInstance();
	if (!g_dispatcher.isDispatcherThread() || !accepting || !player || !player->getSaveFlag() ||
	    hasPendingPlayerSave(player->getGUID()) || db.isInTransaction())
		return false;
	const uint64_t expectedGeneration = player->getSaveGeneration();
	const auto save = snapshot(player, inboxCredit);
	if (!save) return false;
	std::vector<std::pair<Player*, IOLoginData::PlayerSaveSnapshot>> peers;
	std::unordered_set<uint32_t> guids{player->getGUID()};
	for (Player* peer : participants) {
		if (!peer || !peer->getSaveFlag() || hasPendingPlayerSave(peer->getGUID())) return false;
		if (!guids.insert(peer->getGUID()).second) continue;
		auto image = snapshot(peer);
		if (!image) return false;
		peers.emplace_back(peer, std::move(*image));
	}
	if (!commitTransfer(
	        fmt::format("SELECT `save_generation` AS `receipt` FROM `players` WHERE `id` = {} FOR UPDATE", save->guid),
	        expectedGeneration, save->generation, [&] {
		        if (!IOLoginData::applyPlayerSave(*save)) return false;
		        for (const auto& [peer, image] : peers) {
			        const auto row = db.storeQuery(
			            fmt::format("SELECT `save_generation` FROM `players` WHERE `id` = {} FOR UPDATE", image.guid));
			        if (!row || row->getNumber<uint64_t>("save_generation") != peer->getSaveGeneration() ||
			            !IOLoginData::applyPlayerSave(image))
				        return false;
		        }
		        return sideChanges();
	        }))
		return false;
	player->acknowledgeSaveGeneration(save->generation);
	player->acknowledgeStorageDirty({save->storageSnapshotId, save->snapshotModifiedKeys, save->snapshotRemovedKeys});
	player->acknowledgeBestiaryDirty({save->bestiarySnapshotId, save->snapshotModifiedBestiaryRaceIds});
	for (const auto& [peer, image] : peers) {
		peer->acknowledgeSaveGeneration(image.generation);
		peer->acknowledgeStorageDirty({image.storageSnapshotId, image.snapshotModifiedKeys, image.snapshotRemovedKeys});
		peer->acknowledgeBestiaryDirty({image.bestiarySnapshotId, image.snapshotModifiedBestiaryRaceIds});
	}
	return true;
}

bool SaveManager::commitTransfer(std::string_view receiptQuery, uint64_t expected, uint64_t committedValue,
                                 const std::function<bool()>& apply)
{
	Database& db = Database::getInstance();
	if (!g_dispatcher.isDispatcherThread() || !accepting || db.isInTransaction()) return false;
	const auto blockPersistence = [&] {
		// Never let a lost COMMIT reply turn into a rollback of the live world.
		persistenceBlocked = true;
		accepting = false;
		LOG_CRITICAL(
		    "[SaveManager] Transfer outcome could not be established. Persistence is blocked; "
		    "reconnect the database and restart before saving again.");
	};
	for (uint8_t attempt = 0; attempt < DBTransaction::TRANSACTION_MAX_ATTEMPTS; ++attempt) {
		bool commitAttempted = false;
		bool retryable = false;
		{
			DBTransaction transaction;
			if (!transaction.begin()) return false;
			const auto row = db.storeQuery(receiptQuery);
			// A stale Player must not let applyPlayerSave skip the inbox credit
			// while still committing removal of its source house items.
			if (!row || row->getNumber<uint64_t>("receipt") != expected) return false;
			if (apply()) {
				commitAttempted = true;
				if (transaction.commit()) return true;
			}
			retryable = db.lastQueryWasDeadlock();
			if (!transaction.rollback() && !commitAttempted) {
				blockPersistence();
				return false;
			}
		}
		if (commitAttempted) {
			// A fresh locking read waits for the old transaction to finish, even
			// after reconnecting. A committed generation/owner is its receipt.
			DBTransaction verification;
			if (!verification.begin()) {
				blockPersistence();
				return false;
			}
			const auto row = db.storeQuery(receiptQuery);
			if (!row) {
				blockPersistence();
				return false;
			}
			const uint64_t receipt = row->getNumber<uint64_t>("receipt");
			if (receipt != committedValue && receipt != expected) {
				blockPersistence();
				return false;
			}
			if (!verification.rollback()) {
				blockPersistence();
				return false;
			}
			if (receipt == committedValue) return true;
		}
		if (!retryable) return false;
	}
	return false;
}

void SaveManager::dispatchPlayerFlush(uint32_t guid, PendingPlayerFlush pending)
{
	// Values only: no live Player*, Lua reference or dispatcher-owned map is read by the worker.
	const bool tracked = pending.trackedBySaveAll;
	// Shared ownership is only of a value snapshot, never of a live game object.
	// It also preserves the rejection result without duplicating the SQL payload.
	auto work = std::make_shared<PendingPlayerFlush>(std::move(pending));
	if (!g_threadPool.try_detach_task([this, guid, work]() mutable {
		    bool success = false;
		    try {
			    success = (work->journalDurable || IOLoginData::writePlayerJournal(work->save)) &&
			              IOLoginData::flushPlayerSave(work->save);
		    } catch (const std::exception& e) {
			    LOG_ERROR("[SaveManager] Save worker failed for guid={}: {}", guid, e.what());
		    } catch (...) {
			    LOG_ERROR("[SaveManager] Save worker failed for guid={}: unknown exception", guid);
		    }
		    // Retry completion delivery, never SQL, if a load spike fills the inbox.
		    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		    while (g_dispatcher.getState() == THREAD_STATE_RUNNING) {
			    if (g_dispatcher.addTask([this, guid, work, success]() mutable {
				        onPlayerFlushed(guid, work->trackedBySaveAll, success, std::move(work->save));
			        }))
				    return;
			    if (std::chrono::steady_clock::now() >= deadline) break;
			    std::this_thread::sleep_for(std::chrono::milliseconds(1));
		    }
		    // Leave the in-flight barrier blocked, rather than report false success.
		    LOG_CRITICAL("[SaveManager] Completion delivery failed for guid={}; persistence barrier remains blocked.",
		                 guid);
	    })) {
		onPlayerFlushed(guid, tracked, false, std::move(work->save));
	}
}

void SaveManager::onPlayerFlushed(uint32_t guid, bool tracked, bool success, IOLoginData::PlayerSaveSnapshot save)
{
	if (success) acknowledgePlayerSave(guid, save);
	auto next = pendingFlushes.find(guid);
	if (next != pendingFlushes.end()) {
		PendingPlayerFlush pending = std::move(next->second);
		pendingFlushes.erase(next);
		dispatchPlayerFlush(guid, std::move(pending));
	} else {
		flushInFlight.erase(guid);
		if (!success) {
			if (g_game.getPlayerByGUID(guid)) {
				// The live Player supersedes whatever this chain left behind: the
				// next snapshot takes a higher generation, the journal upsert keeps
				// the newest payload and applyPlayerSave skips committed ones. So a
				// retry is safe whether the journal write failed, the apply rolled
				// back or the COMMIT reply was lost. Blocking here instead would
				// reject every later save for the session and lose the progress at
				// logout, because logout refuses a blocked GUID.
				LOG_ERROR("[SaveManager] Save chain failed for online guid={}; the next save will retry.", guid);
			} else {
				// Offline: the journal may hold the only copy of the latest state.
				// Keep login blocked until startup recovery replays it.
				sessionSaveFailed = true;
				failedRecoveryGuids.insert(guid);
				LOG_ERROR("[SaveManager] Save chain failed for guid={}; login and new saves are blocked.", guid);
			}
		}
		finishWaiters(guid, success && !hasFailedRecovery(guid));
	}
	if (tracked) completeTrackedFlush(success);
	finishShutdown();
}

void SaveManager::acknowledgePlayerSave(uint32_t guid, const IOLoginData::PlayerSaveSnapshot& save)
{
	if (auto player = g_game.getPlayerByGUID(guid)) {
		player->acknowledgeSaveGeneration(save.generation);
		player->acknowledgeStorageDirty({save.storageSnapshotId, save.snapshotModifiedKeys, save.snapshotRemovedKeys});
		player->acknowledgeBestiaryDirty({save.bestiarySnapshotId, save.snapshotModifiedBestiaryRaceIds});
	}
}

void SaveManager::completeTrackedFlush(bool success)
{
	generationSucceeded &= success;
	if (pendingSaveFlushes == 0) {
		LOG_CRITICAL("[SaveManager] Save generation counter mismatch.");
		return;
	}
	if (--pendingSaveFlushes == 0) finishSaveGeneration();
}

void SaveManager::finishSaveGeneration()
{
	lastSaveDurationMs.store(static_cast<uint64_t>(saveClock() - saveStartedMs));
	const bool success = generationSucceeded;
	sessionSaveFailed |= !success;
	LOG_INFO("[SaveManager] Generation {}: {} after {}ms.", saveGenerationId, success ? "durably completed" : "FAILED",
	         getLastSaveTime());
	saving.store(false, std::memory_order_release);
	auto callbacks = std::move(saveCallbacks);
	const bool repeat = std::exchange(saveAgain, false);
	auto nextCallbacks = std::move(nextSaveCallbacks);
	{
		// A no-player follow-up may finish synchronously and request shutdown.
		// Keep teardown blocked until callbacks of all completing generations ran.
		struct CompletionScope
		{
			uint32_t& count;
			explicit CompletionScope(uint32_t& count) : count(count) { ++count; }
			~CompletionScope() { --count; }
		} scope(completingGenerations);
		// Queue the already-accepted follow-up before callbacks (e.g. shutdown) can
		// stop admission. Reentrant calls see this new generation and coalesce.
		if (repeat) {
			const bool previousAccepting = accepting;
			accepting = !persistenceBlocked;
			saveCallbacks = std::move(nextCallbacks);
			const bool scheduled = saveAll();
			accepting = previousAccepting && !shutdownRequested && !persistenceBlocked;
			if (!scheduled) {
				auto rejected = std::exchange(saveCallbacks, {});
				for (auto& callback : rejected) callback(false);
			}
		}
		for (auto& callback : callbacks) callback(success);
	}
	finishShutdown();
}

void SaveManager::drainPlayerFlushAsync(uint32_t guid, Completion callback, uint32_t timeoutMs)
{
	if (!g_dispatcher.isDispatcherThread()) {
		g_dispatcher.addTask([this, guid, callback = std::move(callback), timeoutMs]() mutable {
			drainPlayerFlushAsync(guid, std::move(callback), timeoutMs);
		});
		return;
	}
	if (!callback) return;
	if (!hasPendingPlayerSave(guid)) {
		callback(!hasFailedRecovery(guid));
		return;
	}
	const uint64_t id = ++nextWaiterId;
	const uint32_t event = g_scheduler.addEvent(timeoutMs, [this, guid, id] { timeoutWaiter(guid, id); });
	if (event == 0) {
		callback(false);
		return;
	}
	flushChainCallbacks[guid].push_back({id, event, std::move(callback)});
}

void SaveManager::timeoutWaiter(uint32_t guid, uint64_t id)
{
	auto chain = flushChainCallbacks.find(guid);
	if (chain == flushChainCallbacks.end()) return;
	auto waiter = std::ranges::find(chain->second, id, &Waiter::id);
	if (waiter == chain->second.end()) return;
	auto callback = std::move(waiter->callback);
	chain->second.erase(waiter);
	if (chain->second.empty()) flushChainCallbacks.erase(chain);
	callback(false);
}

void SaveManager::finishWaiters(uint32_t guid, bool success)
{
	auto chain = flushChainCallbacks.find(guid);
	if (chain == flushChainCallbacks.end()) return;
	auto waiters = std::move(chain->second);
	flushChainCallbacks.erase(chain);
	for (auto& waiter : waiters) {
		g_scheduler.stopEvent(waiter.timeoutEvent);
		waiter.callback(success);
	}
}

void SaveManager::shutdownAsync(Completion completion)
{
	shutdownRequested = true;
	accepting = false;
	if (completion) shutdownCallbacks.push_back(std::move(completion));
	finishShutdown();
}

void SaveManager::finishShutdown()
{
	if (!shutdownRequested || isSaving() || saveAgain || completingGenerations != 0 || !flushInFlight.empty() ||
	    !pendingFlushes.empty())
		return;
	auto callbacks = std::move(shutdownCallbacks);
	for (auto& callback : callbacks) callback(!persistenceBlocked && !sessionSaveFailed);
}

bool SaveManager::recoverPendingFlushes()
{
	Database& db = Database::getInstance();
	std::unordered_set<uint32_t> legacyGuids;
	// COUNT distinguishes an empty journal from failed SELECT/schema/connection.
	const auto counts = db.storeQuery(
	    "SELECT (SELECT COUNT(*) FROM `player_save_journal`) AS `journal_count`, "
	    "(SELECT COUNT(*) FROM `player_save_async_pending`) AS `legacy_count`, "
	    "(SELECT MAX(`save_generation`) FROM `players`) AS `latest_generation`");
	if (!counts) return false;
	if (counts->getNumber<uint64_t>("legacy_count") != 0) {
		auto legacy = db.storeQuery("SELECT DISTINCT `guid` FROM `player_save_async_pending`");
		if (!legacy) return false;
		do {
			const auto guid = legacy->getNumber<uint32_t>("guid");
			legacyGuids.insert(guid);
			failedRecoveryGuids.insert(guid);
			LOG_CRITICAL(
			    "[SaveManager] Legacy WAL for guid={} has no generation. Evidence retained; manual reconciliation required.",
			    guid);
		} while (legacy->next());
	}
	if (counts->getNumber<uint64_t>("journal_count") == 0) return true;
	auto result = db.storeQuery(
	    "SELECT `guid`, `generation`, `payload`, `payload_hash` FROM `player_save_journal` ORDER BY `guid`");
	if (!result) return false;
	do {
		const auto guid = result->getNumber<uint32_t>("guid");
		const auto generation = result->getNumber<uint64_t>("generation");
		generations[guid] = std::max(generations[guid], generation);
		// Poison before any decoding/begin/query/commit. Clear only on proven success.
		const bool legacyFailed = legacyGuids.contains(guid);
		failedRecoveryGuids.insert(guid);
		const auto payload = result->getString("payload");
		const auto hash = result->getString("payload_hash");
		unsigned char digest[SHA256_DIGEST_LENGTH];
		if (legacyFailed || hash.size() != sizeof(digest) ||
		    !SHA256(reinterpret_cast<const unsigned char*>(payload.data()), payload.size(), digest) ||
		    std::memcmp(hash.data(), digest, sizeof(digest)) != 0) {
			LOG_CRITICAL("[SaveManager] Unresolved or corrupt journal for guid={}; preserved and blocked.", guid);
			continue;
		}
		auto queries = tfs::save::decode(payload, guid, generation);
		if (!queries) {
			LOG_CRITICAL("[SaveManager] Malformed journal for guid={}; preserved and blocked.", guid);
			continue;
		}
		IOLoginData::PlayerSaveSnapshot save;
		save.guid = guid;
		save.generation = generation;
		save.queries = std::move(*queries);
		if (IOLoginData::flushPlayerSave(save)) {
			failedRecoveryGuids.erase(guid);
			LOG_INFO("[SaveManager] Recovered guid={} generation={}.", guid, generation);
		} else {
			LOG_CRITICAL("[SaveManager] Recovery failed for guid={}; journal preserved and login blocked.", guid);
		}
	} while (result->next());
	return true;
}

bool SaveManager::saveMap()
{
	if (persistenceBlocked) return false;
	// Never inspect mutable map/house state from a database worker.
	const bool success = IOMapSerialize::saveHouseInfo() && IOMapSerialize::saveHouseItems();
	if (!success) LOG_ERROR("[SaveManager] Map save failed.");
	return success;
}
