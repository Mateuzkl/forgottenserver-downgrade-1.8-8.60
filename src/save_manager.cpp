// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// SaveManager - Async save coordination using ThreadPool

#include "otpch.h"

#include "save_manager.h"

#include <thread>

#include "database.h"
#include "game.h"
#include "iomapserialize.h"
#include "logger.h"
#include "thread_pool.h"
#include "tasks.h"
#include "kv/kv.h"

extern Game g_game;

SaveManager g_saveManager;

void SaveManager::saveAll()
{
	if (isSaving() || saving.exchange(true)) {
		LOG_INFO(fmt::format(">> {}: {}",
			fmt::format(fg(fmt::color::magenta), "SaveManager"),
			fmt::format(fg(fmt::color::yellow), "Save already in progress, skipping.")));
		return;
	}

	auto now = std::chrono::steady_clock::now().time_since_epoch();
	int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
	int64_t lastSave = lastSaveTimestamp.load(std::memory_order_relaxed);

	if (lastSave > 0 && (nowMs - lastSave) < MIN_SAVE_INTERVAL_MS) {
		LOG_INFO(fmt::format(">> {}: {}",
			fmt::format(fg(fmt::color::magenta), "SaveManager"),
			fmt::format(fg(fmt::color::yellow), "Save throttled (min {}ms interval).", MIN_SAVE_INTERVAL_MS)));
		saving.store(false);
		return;
	}

	lastSaveTimestamp.store(nowMs, std::memory_order_relaxed);
	auto startTime = std::chrono::high_resolution_clock::now();

	LOG_INFO(fmt::format(">> {}: {}",
		fmt::format(fg(fmt::color::magenta), "SaveManager"),
		fmt::format(fg(fmt::color::cyan), "Saving server state...")));

	// Save game storage values (on dispatcher thread - fast)
	if (!g_game.saveGameStorageValues()) {
		LOG_ERROR("[SaveManager] Failed to save game storage values.");
	}

	if (!g_game.saveAccountStorageValues()) {
		LOG_ERROR("[SaveManager] Failed to save account storage values.");
	}

	// Save KV store
	if (!KVStore::getInstance().saveAll()) {
		LOG_ERROR("[SaveManager] Failed to save KV store.");
	}

	// Build all online players on dispatcher and flush SQL on the thread pool.
	uint32_t playerCount = 0;
	const auto& players = g_game.getPlayers();

	for (const auto& player : players) {
		if (schedulePlayerFlush(player.get(), true)) {
			playerCount++;
		}
	}

	// Save map ASYNC on ThreadPool (house info + house items = pure SQL, no game state access)
	beginTrackedFlush();
	g_threadPool.detach_task([this]() {
		bool mapSaved = false;
		for (uint32_t tries = 0; tries < 3; tries++) {
			if (IOMapSerialize::saveHouseInfo() && IOMapSerialize::saveHouseItems()) {
				mapSaved = true;
				break;
			}
		}
		if (!mapSaved) {
			LOG_ERROR("[SaveManager] Failed to save map data after 3 retries.");
		}
		completeTrackedFlush();
	});

	auto endTime = std::chrono::high_resolution_clock::now();
	auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

	lastSaveDurationMs.store(static_cast<uint64_t>(durationMs), std::memory_order_relaxed);
	lastPlayersSaved.store(playerCount, std::memory_order_relaxed);

	LOG_INFO(fmt::format(">> {}: Queued {} player save(s) in {} (map/player SQL flushing async)",
		fmt::format(fg(fmt::color::magenta), "SaveManager"),
		fmt::format(fg(fmt::color::lime_green), "{}", playerCount),
		fmt::format(fg(fmt::color::cyan), "{}ms", durationMs)));

}

bool SaveManager::savePlayer(Player* player)
{
	if (!player) {
		return false;
	}

	if (!g_dispatcher.isDispatcherThread()) {
		LOG_ERROR("[SaveManager] savePlayer must be called on the dispatcher thread.");
		return false;
	}

	auto startTime = std::chrono::high_resolution_clock::now();
	const bool queued = schedulePlayerFlush(player);
	auto endTime = std::chrono::high_resolution_clock::now();
	auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

	if (queued) {
		LOG_INFO(fmt::format(">> {}: Player {} save queued in {}",
			fmt::format(fg(fmt::color::magenta), "SaveManager"),
			fmt::format(fg(fmt::color::lime_green), "{}", player->getName()),
			fmt::format(fg(fmt::color::cyan), "{}ms", durationMs)));
	}

	return queued;
}

bool SaveManager::savePlayerSync(Player* player)
{
	if (!player) {
		return false;
	}

	if (!g_dispatcher.isDispatcherThread()) {
		LOG_ERROR("[SaveManager] savePlayerSync must be called on the dispatcher thread.");
		return false;
	}

	const uint32_t guid = player->getGUID();

	if (flushInFlight.contains(guid)) {
		auto save = IOLoginData::buildPlayerSave(player);
		if (!save) {
			return false;
		}
		bool tracked = false;
		if (auto it = pendingFlushes.find(guid); it != pendingFlushes.end()) {
			tracked = it->second.trackedBySaveAll;
		}
		PendingPlayerFlush pending{player->getName(), std::move(*save), tracked};
		if (!savePendingFlushToDB(guid, pending.save)) {
			LOG_ERROR(fmt::format("[SaveManager] WAL write failed for pending flush guid={}, save may be lost", guid));
		}
		pendingFlushes[guid] = std::move(pending);
		return false; // enqueued behind in-flight flush; save will complete via onPlayerFlushed
	}

	auto save = IOLoginData::buildPlayerSave(player);
	if (!save) {
		return false;
	}

	flushInFlight.insert(guid);
	bool success = false;
	for (uint32_t tries = 0; tries < 3; ++tries) {
		if (IOLoginData::flushPlayerSave(*save)) {
			player->acknowledgeStorageDirty(Player::StorageDirtySnapshot{
				save->storageSnapshotId,
				save->snapshotModifiedKeys,
				save->snapshotRemovedKeys
			});
			success = true;
			break;
		}
	}
	flushInFlight.erase(guid);

	auto it = pendingFlushes.find(guid);
	if (it != pendingFlushes.end()) {
		PendingPlayerFlush pending = std::move(it->second);
		pendingFlushes.erase(it);
		flushInFlight.insert(guid);
		dispatchPlayerFlush(guid, std::move(pending));
	}

	return success;
}

void SaveManager::drainPlayerFlushAsync(uint32_t guid, std::function<void(bool)> callback)
{
	g_dispatcher.addTask([this, guid, callback = std::move(callback)]() mutable {
		if (flushInFlight.contains(guid) || pendingFlushes.contains(guid)) {
			flushChainCallbacks[guid].push_back(std::move(callback));
		} else {
			// No flush pending, deliver callback on dispatcher
			g_dispatcher.addTask([callback = std::move(callback)]() mutable {
				callback(true);
			});
		}
	});
}

bool SaveManager::schedulePlayerFlush(Player* player, bool trackSaveAll /* = false */)
{
	if (!player) {
		return false;
	}

	if (!g_dispatcher.isDispatcherThread()) {
		LOG_ERROR("[SaveManager] schedulePlayerFlush must run on the dispatcher thread.");
		return false;
	}

	auto save = IOLoginData::buildPlayerSave(player);
	if (!save) {
		LOG_ERROR(fmt::format("[SaveManager] Failed to build save for player: {}", player->getName()));
		return false;
	}

	const uint32_t guid = player->getGUID();
	const std::string name = player->getName();
	if (flushInFlight.contains(guid)) {
		bool oldTracked = false;
		if (auto it = pendingFlushes.find(guid); it != pendingFlushes.end()) {
			oldTracked = it->second.trackedBySaveAll;
		}
		bool newTracked = oldTracked | trackSaveAll;
		if (trackSaveAll && !oldTracked) {
			beginTrackedFlush();
		}
		PendingPlayerFlush pending{name, std::move(*save), newTracked};
		if (!savePendingFlushToDB(guid, pending.save)) {
			LOG_ERROR(fmt::format("[SaveManager] WAL write failed for pending flush guid={}, save may be lost", guid));
		}
		pendingFlushes[guid] = std::move(pending);
		return true;
	}

	flushInFlight.insert(guid);
	if (trackSaveAll) {
		beginTrackedFlush();
	}
	dispatchPlayerFlush(guid, PendingPlayerFlush{name, std::move(*save), trackSaveAll});
	return true;
}

void SaveManager::onPlayerFlushed(uint32_t guid, bool trackedBySaveAll, bool success, IOLoginData::PlayerSaveSnapshot save)
{
	if (success) {
		acknowledgePlayerSave(guid, save);
	}

	if (trackedBySaveAll) {
		completeTrackedFlush();
	}

	auto it = pendingFlushes.find(guid);
	if (it == pendingFlushes.end()) {
		flushInFlight.erase(guid);

		// Chain complete: delete WAL on success, keep on failure for recovery
		if (success) {
			deletePendingFlushFromDB(guid);
		} else {
			LOG_ERROR(fmt::format("[SaveManager] Flush failed for guid={}, WAL entry preserved for recovery", guid));
		}

		// Invoke all registered callbacks for this flush chain
		auto cbIt = flushChainCallbacks.find(guid);
		if (cbIt != flushChainCallbacks.end()) {
			for (auto& cb : cbIt->second) {
				g_dispatcher.addTask([cb = std::move(cb), success]() mutable {
					cb(success);
				});
			}
			flushChainCallbacks.erase(cbIt);
		}

		return;
	}

	PendingPlayerFlush pending = std::move(it->second);
	pendingFlushes.erase(it);
	dispatchPlayerFlush(guid, std::move(pending));
}

void SaveManager::dispatchPlayerFlush(uint32_t guid, PendingPlayerFlush pending)
{
	// Persist to WAL before dispatch so crash recovery can replay
	if (!savePendingFlushToDB(guid, pending.save)) {
		LOG_ERROR(fmt::format("[SaveManager] WAL write failed for guid={}, aborting flush chain", guid));
		flushInFlight.erase(guid);
		pendingFlushes.erase(guid);

		// Notify all waiting callbacks with failure
		auto cbIt = flushChainCallbacks.find(guid);
		if (cbIt != flushChainCallbacks.end()) {
			for (auto& cb : cbIt->second) {
				g_dispatcher.addTask([cb = std::move(cb)]() mutable {
					cb(false);
				});
			}
			flushChainCallbacks.erase(cbIt);
		}

		if (pending.trackedBySaveAll) {
			completeTrackedFlush();
		}
		return;
	}

	g_threadPool.detach_task([this, guid, pending = std::move(pending)]() mutable {
		std::string name = std::move(pending.name);
		IOLoginData::PlayerSaveSnapshot save = std::move(pending.save);
		const bool trackSaveAll = pending.trackedBySaveAll;

		const bool success = IOLoginData::flushPlayerSave(save);
		if (!success) {
			LOG_ERROR(fmt::format("[SaveManager] Failed to flush save for player: {}", name));
		}

		g_dispatcher.addTask([this, guid, trackSaveAll, success, save = std::move(save)]() mutable {
			onPlayerFlushed(guid, trackSaveAll, success, std::move(save));
		});
	});
}

void SaveManager::acknowledgePlayerSave(uint32_t guid, const IOLoginData::PlayerSaveSnapshot& save)
{
	if (auto player = g_game.getPlayerByGUID(guid)) {
		player->acknowledgeStorageDirty(Player::StorageDirtySnapshot{
			save.storageSnapshotId,
			save.snapshotModifiedKeys,
			save.snapshotRemovedKeys
		});
	}
}

void SaveManager::beginTrackedFlush() noexcept
{
	pendingSaveFlushes.fetch_add(1, std::memory_order_release);
}

void SaveManager::completeTrackedFlush() noexcept
{
	uint32_t prev = pendingSaveFlushes.fetch_sub(1, std::memory_order_acq_rel);
	if (prev == 0) {
		pendingSaveFlushes.fetch_add(1, std::memory_order_relaxed);
		LOG_ERROR("[SaveManager] completeTrackedFlush underflow detected! begin/complete call mismatch.");
		saving.store(false, std::memory_order_release);
		return;
	}
	if (prev == 1) {
		saving.store(false, std::memory_order_release);
	}
}

bool SaveManager::savePendingFlushToDB(uint32_t guid, const IOLoginData::PlayerSaveSnapshot& save)
{
	// Se recovery falhou para este GUID, nao sobrescrever WAL - exigir intervencao manual
	if (failedRecoveryGuids.contains(guid)) {
		LOG_CRITICAL(fmt::format("[SaveManager] Cannot overwrite WAL for guid={}: recovery previously failed. "
			"Manual intervention required to clear player_save_async_pending entries.", guid));
		return false;
	}

	Database& db = Database::getInstance();

	DBTransaction transaction;
	if (!transaction.begin()) {
		LOG_ERROR(fmt::format("[SaveManager] Failed to begin WAL transaction for guid={}", guid));
		return false;
	}

	if (!db.executeQuery(fmt::format("DELETE FROM `player_save_async_pending` WHERE `guid` = {}", guid))) {
		LOG_ERROR(fmt::format("[SaveManager] Failed to clear old WAL entries for guid={}", guid));
		transaction.rollback();
		return false;
	}

	for (size_t i = 0; i < save.queries.size(); ++i) {
		const std::string escaped = db.escapeString(save.queries[i]);
		if (!db.executeQuery(fmt::format(
			"INSERT INTO `player_save_async_pending` (`guid`, `query_index`, `query_text`, `created_at`) "
			"VALUES ({}, {}, {}, UNIX_TIMESTAMP())",
			guid, i, escaped)))
		{
			LOG_ERROR(fmt::format("[SaveManager] Failed to insert WAL entry for guid={}, query_index={}", guid, i));
			transaction.rollback();
			return false;
		}
	}

	if (!transaction.commit()) {
		LOG_ERROR(fmt::format("[SaveManager] Failed to commit WAL transaction for guid={}", guid));
		return false;
	}

	return true;
}

void SaveManager::deletePendingFlushFromDB(uint32_t guid)
{
	Database::getInstance().executeQuery(
		fmt::format("DELETE FROM `player_save_async_pending` WHERE `guid` = {}", guid));
}

void SaveManager::recoverPendingFlushes()
{
	Database& db = Database::getInstance();
	DBResult_ptr result = db.storeQuery(
		"SELECT `guid`, `query_index`, `query_text` FROM `player_save_async_pending` ORDER BY `guid`, `query_index`");
	if (!result) {
		LOG_INFO(">> [SaveManager] No pending async saves to recover.");
		return;
	}

	std::unordered_map<uint32_t, std::vector<std::string>> pendingByGuid;
	do {
		const uint32_t guid = result->getNumber<uint32_t>("guid");
		const std::string query{result->getString("query_text")};
		pendingByGuid[guid].push_back(std::move(query));
	} while (result->next());

	for (auto& [guid, queries] : pendingByGuid) {
		LOG_INFO(fmt::format(">> [SaveManager] Recovering {} pending query(es) for guid={}", queries.size(), guid));
		bool allOk = true;
		DBTransaction transaction;
		if (!transaction.begin()) {
			LOG_ERROR(fmt::format("[SaveManager] Failed to begin recovery transaction for guid={}", guid));
			continue;
		}
		for (const auto& q : queries) {
			if (!db.executeQuery(q)) {
				allOk = false;
				LOG_ERROR(fmt::format("[SaveManager] Recovery query failed for guid={}: {}", guid, q));
				break;
			}
		}
		if (allOk) {
			if (transaction.commit()) {
				db.executeQuery(fmt::format("DELETE FROM `player_save_async_pending` WHERE `guid` = {}", guid));
				LOG_INFO(fmt::format(">> [SaveManager] Successfully recovered save for guid={}", guid));
			} else {
				transaction.rollback();
				LOG_ERROR(fmt::format("[SaveManager] Recovery commit failed for guid={}", guid));
			}
		} else {
			transaction.rollback();
			failedRecoveryGuids.insert(guid);
			LOG_ERROR(fmt::format("[SaveManager] Recovery failed for guid={}, manual intervention required. "
				"WAL entries preserved; savePendingFlushToDB will skip this GUID to prevent silent data loss.", guid));
		}
	}
}

void SaveManager::saveMapAsync()
{
	LOG_INFO(fmt::format(">> {}: {}",
		fmt::format(fg(fmt::color::magenta), "SaveManager"),
		fmt::format(fg(fmt::color::cyan), "Saving map async on ThreadPool...")));

	g_threadPool.detach_task([]() {
		auto startTime = std::chrono::high_resolution_clock::now();

		bool mapSaved = false;
		for (uint32_t tries = 0; tries < 3; tries++) {
			if (IOMapSerialize::saveHouseInfo() && IOMapSerialize::saveHouseItems()) {
				mapSaved = true;
				break;
			}
		}

		auto endTime = std::chrono::high_resolution_clock::now();
		auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

		if (mapSaved) {
			LOG_INFO(fmt::format(">> {}: Map saved in {}",
				fmt::format(fg(fmt::color::magenta), "SaveManager"),
				fmt::format(fg(fmt::color::lime_green), "{}ms", durationMs)));
		} else {
			LOG_ERROR("[SaveManager] Failed to save map after 3 retries.");
		}
	});
}
