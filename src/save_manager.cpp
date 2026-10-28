// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// SaveManager - Async save coordination using ThreadPool

#include "otpch.h"

#include "save_manager.h"

#include <thread>
#include <fstream>
#include <charconv>

#include "database.h"
#include "game.h"
#include "iomapserialize.h"
#include "logger.h"
#include "thread_pool.h"
#include "tasks.h"
#include "tools.h"
#include "kv/kv.h"

extern Game g_game;

SaveManager g_saveManager;

void SaveManager::saveAll(std::string_view reason, bool mandatory)
{
	if (mandatory && !drainForShutdown() && isSaving()) {
		// A timeout cannot justify skipping the final state. Register each live
		// player's latest generation in WAL before the dispatcher stops.
		for (const auto& player : g_game.getPlayers()) { (void)schedulePlayerFlush(player.get(), true); }
		LOG_CRITICAL("[SaveManager] mandatory save: latest player snapshots queued; earlier workers still unresolved");
		return;
	}
	if (isSaving() || saving.exchange(true)) {
		followupSaveRequested |= reason != "periodic";
		LOG_INFO(fmt::format(">> {}: {}",
			fmt::format(fg(fmt::color::magenta), "SaveManager"),
			fmt::format(fg(fmt::color::yellow), "Save already in progress, skipping.")));
		return;
	}

	auto now = std::chrono::steady_clock::now().time_since_epoch();
	int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
	int64_t lastSave = lastSaveTimestamp.load(std::memory_order_relaxed);

	if (!mandatory && reason == "periodic" && lastSave > 0 && (nowMs - lastSave) < MIN_SAVE_INTERVAL_MS) {
		LOG_INFO(fmt::format(">> {}: {}",
			fmt::format(fg(fmt::color::magenta), "SaveManager"),
			fmt::format(fg(fmt::color::yellow), "Save throttled (min {}ms interval).", MIN_SAVE_INTERVAL_MS)));
		saving.store(false);
		return;
	}

	lastSaveTimestamp.store(nowMs, std::memory_order_relaxed);
	auto startTime = std::chrono::high_resolution_clock::now();
	cycleStarted = std::chrono::steady_clock::now();
	cycleSnapshotUs = cycleWalUs = 0;
	cycleFailures = 0;
	beginTrackedFlush(); // sentinel: a fast worker cannot finish this cycle during snapshot construction
	LOG_INFO(fmt::format("[SaveManager] cycle_begin reason={} mandatory={} timestamp_ms={}", reason, mandatory, nowMs));

	LOG_INFO(fmt::format(">> {}: {}",
		fmt::format(fg(fmt::color::magenta), "SaveManager"),
		fmt::format(fg(fmt::color::cyan), "Saving server state...")));

	// Save game storage values (on dispatcher thread - fast)
	if (!g_game.saveGameStorageValues()) {
		++cycleFailures;
		LOG_ERROR("[SaveManager] Failed to save game storage values.");
	}

	if (!g_game.saveAccountStorageValues()) {
		++cycleFailures;
		LOG_ERROR("[SaveManager] Failed to save account storage values.");
	}

	// Save KV store
	if (!KVStore::getInstance().saveAll()) {
		++cycleFailures;
		LOG_ERROR("[SaveManager] Failed to save KV store.");
	}

	// Build all online players on dispatcher and flush SQL on the thread pool.
	uint32_t playerCount = 0;
	uint32_t rejectedCount = 0;
	const auto& players = g_game.getPlayers();

	for (const auto& player : players) {
		if (schedulePlayerFlush(player.get(), true)) {
			playerCount++;
		} else {
			++rejectedCount;
		}
	}

	// Save map synchronously on the dispatcher thread (reads g_game.map.houses — not thread-safe).
	beginTrackedFlush();
	const auto mapStart = std::chrono::steady_clock::now();
	bool mapSaved = false;
	for (uint32_t tries = 0; tries < 3; tries++) {
		if (IOMapSerialize::saveHouseInfo() && IOMapSerialize::saveHouseItems()) {
			mapSaved = true;
			break;
		}
	}
	if (!mapSaved) {
		++cycleFailures;
		LOG_ERROR("[SaveManager] Failed to save map data after 3 retries.");
	}
	LOG_INFO(fmt::format("[SaveManager] stage=map elapsed_ms={} result={}",
	    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-mapStart).count(), mapSaved));
	completeTrackedFlush();

	auto endTime = std::chrono::high_resolution_clock::now();
	auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

	cycleFailures += rejectedCount;
	lastSaveDurationMs.store(static_cast<uint64_t>(durationMs), std::memory_order_relaxed);
	lastPlayersSaved.store(playerCount, std::memory_order_relaxed);
	LOG_INFO(fmt::format("[SaveManager] cycle_end_dispatcher reason={} queued={} rejected={} dispatcher_ms={} active={} pending={}",
	    reason, playerCount, rejectedCount, durationMs, activeFlushes.size(), pendingFlushes.size()));
	completeTrackedFlush();

	LOG_INFO(fmt::format(">> {}: Queued {} player save(s) in {} (player SQL flushes may still be pending)",
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
	if (!player || !g_dispatcher.isDispatcherThread() || hasFailedRecovery(player->getGUID())) {
		return false;
	}
	const uint32_t guid = player->getGUID();
	if (flushInFlight.contains(guid)) {
		(void)schedulePlayerFlush(player); // latest state is durable in the ordered WAL queue
		return false;
	}
	auto save = IOLoginData::buildPlayerSave(player);
	if (!save) {
		quarantinePlayer(guid, "sync-build", false);
		return false;
	}
	if (!savePendingFlushToDB(guid, *save)) {
		preserveRejectedSnapshot(guid, std::move(*save));
		quarantinePlayer(guid, "sync-wal", true);
		return false;
	}
	const auto result = IOLoginData::flushPlayerSaveChecked(*save);
	if (result != IOLoginData::FlushResult::Committed) {
		retainedFailures[guid] = std::move(*save);
		quarantinePlayer(guid, "sync-flush", result == IOLoginData::FlushResult::Indeterminate);
		return false;
	}
	acknowledgePlayerSave(guid, *save);
	return true;
}

bool SaveManager::savePlayersSync(const std::vector<Player*>& players)
{
	return savePlayersAtomic(players, [] { return true; }) == AtomicSaveResult::Committed;
}

void SaveManager::quarantinePlayer(uint32_t guid, std::string_view stage, bool commitUnknown)
{
	failedRecoveryGuids.insert(guid);
	LOG_CRITICAL(fmt::format("[SaveManager] guid={} stage={} state=quarantined commit={} wal=preserved-or-unknown action=disconnect recovery=restart-required",
	    guid, stage, commitUnknown ? "unknown" : "rolled-back"));
	if (auto player = g_game.getPlayerByGUID(guid)) {
		player->disconnectForPersistenceFailure();
	}
}

bool SaveManager::drainForShutdown()
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (!activeFlushes.empty()) {
		const uint32_t guid = activeFlushes.begin()->first;
		const auto active = activeFlushes.begin()->second;
		if (active.completion.wait_until(deadline) != std::future_status::ready) {
			LOG_CRITICAL("[SaveManager] Shutdown drain timed out; durable WAL retained. No flush success is reported.");
			return false;
		}
		onPlayerFlushed(guid, active.completion.get(), active.save);
	}
	const bool complete = pendingFlushes.empty() && failedRecoveryGuids.empty();
	LOG_INFO(fmt::format("[SaveManager] shutdown_drain={} pending={} quarantined={}",
	    complete ? "complete" : "incomplete", pendingFlushes.size(), failedRecoveryGuids.size()));
	return complete;
}

bool SaveManager::canSavePlayersSync(const std::vector<Player*>& players) const
{
	if (!g_dispatcher.isDispatcherThread() || Database::getInstance().isInTransaction()) {
		return false;
	}
	for (const Player* player : players) {
		if (!player || !player->getSaveFlag() || hasPendingPlayerSave(player->getGUID()) ||
		    hasFailedRecovery(player->getGUID())) {
			return false;
		}
		// A failed flush can leave durable WAL even after its in-memory queue drained.
		auto result = Database::getInstance().storeQuery(fmt::format(
		    "SELECT COUNT(*) AS `total` FROM `player_save_async_pending` WHERE `guid` = {}", player->getGUID()));
		if (!result || result->getNumber<uint64_t>("total") != 0) {
			return false;
		}
	}
	return true;
}

SaveManager::AtomicSaveResult SaveManager::savePlayersAtomic(
    const std::vector<Player*>& players, const std::function<bool()>& transactionQueries)
{
	if (!g_dispatcher.isDispatcherThread()) {
		LOG_ERROR("[SaveManager] savePlayersAtomic must be called on the dispatcher thread.");
		return AtomicSaveResult::Failed;
	}
	Database& db = Database::getInstance();
	if (!canSavePlayersSync(players)) {
		return AtomicSaveResult::Failed;
	}

	std::unordered_set<uint32_t> guids;
	std::vector<IOLoginData::PlayerSaveSnapshot> saves;
	saves.reserve(players.size());
	for (Player* player : players) {
		if (!player || hasPendingPlayerSave(player->getGUID()) || hasFailedRecovery(player->getGUID()) ||
		    !guids.insert(player->getGUID()).second) {
			return AtomicSaveResult::Failed;
		}
		auto save = IOLoginData::buildPlayerSave(player);
		if (!save) {
			return AtomicSaveResult::Failed;
		}
		saves.push_back(std::move(*save));
	}

	struct FlushGuard {
		std::unordered_set<uint32_t>& inFlight;
		const std::unordered_set<uint32_t>& guids;
		~FlushGuard()
		{
			for (uint32_t guid : guids) {
				inFlight.erase(guid);
			}
		}
	} guard{flushInFlight, guids};
	flushInFlight.insert(guids.begin(), guids.end());

	// Do not nest flushPlayerSave: snapshots and the Market outbox must commit together.
	DBTransaction transaction;
	if (!transaction.begin()) {
		return AtomicSaveResult::Failed;
	}
	const auto quarantine = [&] {
		failedRecoveryGuids.insert(guids.begin(), guids.end());
		for (uint32_t guid : guids) {
			quarantinePlayer(guid, "atomic-commit", true);
		}
		LOG_CRITICAL("[SaveManager] Atomic commit outcome unknown; affected players must reconnect after restart.");
		return AtomicSaveResult::Indeterminate;
	};
	const auto execute = [&]() {
		if (!transactionQueries()) {
			return false;
		}
		for (const auto& save : saves) {
			for (const auto& query : save.queries) {
				if (!db.executeQuery(query)) {
					return false;
				}
			}
		}
		return true;
	};
	try {
		if (!execute()) {
			return transaction.rollback() ? AtomicSaveResult::Failed : quarantine();
		}
	} catch (const std::exception& e) {
		LOG_ERROR(fmt::format("[SaveManager] Atomic save failed: {}", e.what()));
		return transaction.rollback() ? AtomicSaveResult::Failed : quarantine();
	}
	if (!transaction.commit()) {
		// A lost COMMIT acknowledgement does not prove rollback. Never refund RAM
		// or let a later snapshot overwrite the database in this state.
		return quarantine();
	}
	for (size_t i = 0; i < players.size(); ++i) {
		const auto& save = saves[i];
		players[i]->acknowledgeStorageDirty(Player::StorageDirtySnapshot{
		    save.storageSnapshotId, save.snapshotModifiedKeys, save.snapshotRemovedKeys});
		players[i]->acknowledgeBestiaryDirty(Player::BestiaryDirtySnapshot{
		    save.bestiarySnapshotId, save.snapshotModifiedBestiaryRaceIds});
	}
	return AtomicSaveResult::Committed;
}

void SaveManager::drainPlayerFlushAsync(uint32_t guid, std::function<void(bool)> callback)
{
	g_dispatcher.addTask([this, guid, callback = std::move(callback)]() mutable {
		if (hasFailedRecovery(guid)) {
			callback(false);
			return;
		}
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
	if (!player || !g_dispatcher.isDispatcherThread() || hasFailedRecovery(player->getGUID())) {
		return false;
	}
	const uint32_t guid = player->getGUID();
	const auto snapshotStart = std::chrono::steady_clock::now();
	auto save = IOLoginData::buildPlayerSave(player);
	cycleSnapshotUs += std::chrono::duration_cast<std::chrono::microseconds>(
	    std::chrono::steady_clock::now() - snapshotStart).count();
	if (!save) {
		quarantinePlayer(guid, "snapshot-build", false);
		return false;
	}
	if (flushInFlight.contains(guid)) {
		const auto old = pendingFlushes.find(guid);
		const bool oldTracked = old != pendingFlushes.end() && old->second.trackedBySaveAll;
		const auto active = activeFlushes.find(guid);
		const bool trackNext = oldTracked || trackSaveAll ||
		    (active != activeFlushes.end() && active->second.trackedBySaveAll);
		if (old != pendingFlushes.end() && old->second.save.queries == save->queries) {
			if (trackNext && !oldTracked) {
				old->second.trackedBySaveAll = true;
				beginTrackedFlush();
			}
			return true;
		}
		if (!savePendingFlushToDB(guid, *save)) {
			preserveRejectedSnapshot(guid, std::move(*save));
			quarantinePlayer(guid, "pending-wal", true);
			return false;
		}
		if (trackNext && !oldTracked) {
			beginTrackedFlush();
		}
		pendingFlushes[guid] = PendingPlayerFlush{player->getName(), std::move(*save), trackNext};
		return true;
	}
	flushInFlight.insert(guid);
	if (trackSaveAll) {
		beginTrackedFlush();
	}
	dispatchPlayerFlush(guid, PendingPlayerFlush{player->getName(), std::move(*save), trackSaveAll});
	return !hasFailedRecovery(guid);
}

void SaveManager::onPlayerFlushed(uint32_t guid, IOLoginData::FlushResult result,
    const std::shared_ptr<IOLoginData::PlayerSaveSnapshot>& save)
{
	auto active = activeFlushes.find(guid);
	if (active == activeFlushes.end() || active->second.save != save) {
		return; // already consumed by the shutdown barrier, never acknowledge twice
	}
	const bool tracked = active->second.trackedBySaveAll;
	activeFlushes.erase(active);
	if (tracked) {
		if (result != IOLoginData::FlushResult::Committed) { ++cycleFailures; }
		completeTrackedFlush();
	}
	if (result == IOLoginData::FlushResult::Committed) {
		acknowledgePlayerSave(guid, *save);
	}
	auto pending = pendingFlushes.find(guid);
	const bool unknown = result == IOLoginData::FlushResult::Indeterminate;
	if (!unknown && !hasFailedRecovery(guid) && pending != pendingFlushes.end()) {
		auto next = std::move(pending->second);
		pendingFlushes.erase(pending);
		dispatchPlayerFlush(guid, std::move(next));
		return;
	}
	flushInFlight.erase(guid);
	if (pending != pendingFlushes.end()) {
		if (!retainedFailures.contains(guid)) { retainedFailures[guid] = std::move(pending->second.save); }
		if (pending->second.trackedBySaveAll) {
			completeTrackedFlush();
		}
		pendingFlushes.erase(pending);
	}
	const bool success = result == IOLoginData::FlushResult::Committed && !hasFailedRecovery(guid);
	if (!success) {
		if (!retainedFailures.contains(guid)) {
			retainedFailures[guid] = *save;
		}
		quarantinePlayer(guid, "async-flush", unknown);
	}
	if (auto callbacks = flushChainCallbacks.find(guid); callbacks != flushChainCallbacks.end()) {
		for (auto& callback : callbacks->second) {
			g_dispatcher.addTask([callback = std::move(callback), success]() mutable { callback(success); });
		}
		flushChainCallbacks.erase(callbacks);
	}
}

void SaveManager::dispatchPlayerFlush(uint32_t guid, PendingPlayerFlush pending)
{
	if (!savePendingFlushToDB(guid, pending.save)) {
		preserveRejectedSnapshot(guid, std::move(pending.save));
		flushInFlight.erase(guid);
		if (pending.trackedBySaveAll) {
			completeTrackedFlush();
		}
		quarantinePlayer(guid, "dispatch-wal", true);
		if (auto callbacks = flushChainCallbacks.find(guid); callbacks != flushChainCallbacks.end()) {
			for (auto& callback : callbacks->second) {
				g_dispatcher.addTask([callback = std::move(callback)]() mutable { callback(false); });
			}
			flushChainCallbacks.erase(callbacks);
		}
		return;
	}
	auto save = std::make_shared<IOLoginData::PlayerSaveSnapshot>(std::move(pending.save));
	try {
		auto future = g_threadPool.submit_task([this, guid, save] {
			const auto start = std::chrono::steady_clock::now();
			auto result = IOLoginData::FlushResult::Indeterminate;
			try { result = IOLoginData::flushPlayerSaveChecked(*save); }
			catch (const std::exception&) { LOG_ERROR("[SaveManager] Worker exception; commit outcome treated as unknown"); }
			LOG_INFO(fmt::format("[SaveManager] guid={} stage=worker-flush result={} elapsed_ms={}", guid,
			    static_cast<int>(result), std::chrono::duration_cast<std::chrono::milliseconds>(
			    std::chrono::steady_clock::now() - start).count()));
			g_dispatcher.addTask([this, guid, result, save] { onPlayerFlushed(guid, result, save); });
			return result;
		}).share();
		activeFlushes.emplace(guid, ActiveFlush{save, std::move(future), pending.trackedBySaveAll});
	} catch (const std::exception&) {
		activeFlushes.emplace(guid, ActiveFlush{save, {}, pending.trackedBySaveAll});
		onPlayerFlushed(guid, IOLoginData::FlushResult::Failed, save); // WAL remains; no job started
	}
}

void SaveManager::acknowledgePlayerSave(uint32_t guid, const IOLoginData::PlayerSaveSnapshot& save)
{
	if (auto player = g_game.getPlayerByGUID(guid)) {
		player->acknowledgeStorageDirty(Player::StorageDirtySnapshot{
			save.storageSnapshotId,
			save.snapshotModifiedKeys,
			save.snapshotRemovedKeys
		});
		player->acknowledgeBestiaryDirty(Player::BestiaryDirtySnapshot{
			save.bestiarySnapshotId,
			save.snapshotModifiedBestiaryRaceIds
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
		LOG_INFO(fmt::format("[SaveManager] cycle_flush_complete status={} elapsed_ms={} snapshot_us={} wal_us={} failures={}",
		    cycleFailures ? "partial" : "complete", std::chrono::duration_cast<std::chrono::milliseconds>(
		    std::chrono::steady_clock::now() - cycleStarted).count(), cycleSnapshotUs, cycleWalUs, cycleFailures));
		if (std::exchange(followupSaveRequested, false) && g_game.getGameState() != GAME_STATE_SHUTDOWN) {
			g_dispatcher.addTask([this] { saveAll("coalesced-manual"); });
		}
	}
}

void SaveManager::preserveRejectedSnapshot(uint32_t guid, IOLoginData::PlayerSaveSnapshot save)
{
	retainedFailures[guid] = std::move(save);
	// A DB outage may prevent recording the latest WAL generation. Keep an
	// emergency SQL snapshot on disk and block startup replay/login for this GUID
	// until an operator reconciles it. Never automatically execute this file.
	try {
		const auto directory = std::filesystem::path("data/save-quarantine");
		std::filesystem::create_directories(directory);
#ifndef _WIN32
		// Restrict traversal before creating a file containing private player SQL.
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
		    std::filesystem::perm_options::replace);
#endif
		const auto destination = directory / (std::to_string(guid) + ".sql");
		const auto temporary = directory / (std::to_string(guid) + ".tmp");
		if (std::filesystem::exists(destination)) { throw std::runtime_error("quarantine file already exists"); }
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		output.exceptions(std::ios::failbit | std::ios::badbit);
#ifndef _WIN32
		std::filesystem::permissions(temporary,
		    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
		    std::filesystem::perm_options::replace);
#endif
		output << "-- Emergency player snapshot; reconcile offline with SQL WAL before deleting.\n";
		for (const auto& query : retainedFailures.at(guid).queries) { output << query << ";\n"; }
		output.flush();
		output.close();
		std::filesystem::rename(temporary, destination);
		LOG_CRITICAL(fmt::format("[SaveManager] guid={} emergency_snapshot={} action=manual-reconciliation-required", guid, destination.string()));
	} catch (const std::exception& error) {
		LOG_CRITICAL(fmt::format("[SaveManager] guid={} emergency_snapshot_failed={} latest_state=RAM-only do_not_restart_until_reconciled=true", guid, error.what()));
	}
}

bool SaveManager::savePendingFlushToDB(uint32_t guid, IOLoginData::PlayerSaveSnapshot& save)
{
	const auto walStart = std::chrono::steady_clock::now();
	struct WalTimer {
		uint64_t& elapsed;
		std::chrono::steady_clock::time_point start;
		~WalTimer() { elapsed += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count(); }
	} timer{cycleWalUs, walStart};
	if (hasFailedRecovery(guid)) {
		return false;
	}
	Database& db = Database::getInstance();
	std::string fingerprint;
	for (const auto& query : save.queries) {
		fingerprint += std::to_string(query.size()) + ":" + query;
	}
	const auto header = db.escapeString("-- tfs-save:" + transformToSHA1Hex(fingerprint));
	// A worker for an older generation must never delete a newer queued WAL.
	save.walClearQuery = fmt::format(
	    "DELETE w FROM `player_save_async_pending` w INNER JOIN `player_save_async_pending` h ON h.`guid`=w.`guid` "
	    "AND h.`query_index`=0 AND h.`query_text`={} WHERE w.`guid`={}", header, guid);
	DBTransaction transaction;
	if (!transaction.begin() || !db.executeQuery(fmt::format(
	    "DELETE FROM `player_save_async_pending` WHERE `guid`={}", guid))) {
		return false;
	}
	DBInsert insert("INSERT INTO `player_save_async_pending` (`guid`,`query_index`,`query_text`,`created_at`) VALUES ");
	if (!insert.addRow(fmt::format("{},0,{},UNIX_TIMESTAMP()", guid, header))) {
		return false;
	}
	for (size_t i = 0; i < save.queries.size(); ++i) {
		const auto row = fmt::format("{},{},{},UNIX_TIMESTAMP()", guid, i + 1, db.escapeString(save.queries[i]));
		if (row.size() + 1024 >= db.getMaxPacketSize() || !insert.addRow(row)) {
			return false;
		}
	}
	return insert.execute() && transaction.commit();
}

bool SaveManager::normalizeLegacyOnlineTimeQuery(std::string& query)
{
	constexpr std::string_view prefix = "UPDATE `players` SET ";
	constexpr std::string_view additive = "`onlinetime` = `onlinetime` + ";
	if (!query.starts_with(prefix)) { return false; }
	char quote = 0;
	for (size_t i = prefix.size(); i < query.size(); ++i) {
		const char c = query[i];
		if (quote) {
			if (c == '\\') { ++i; }
			else if (c == quote) {
				if (i + 1 < query.size() && query[i + 1] == quote) { ++i; }
				else { quote = 0; }
			}
			continue;
		}
		if (std::string_view(query).substr(i).starts_with(additive)) {
			const size_t digits = i + additive.size();
			const size_t end = query.find_first_not_of("0123456789", digits);
			// Captured player snapshots always separate this assignment with a comma.
			if (end != digits && end != std::string::npos && query[end] == ',') {
				query.replace(i, end - i, "`onlinetime` = `onlinetime`");
				return true;
			}
		}
		if (c == '\'' || c == '"' || c == '`') { quote = c; }
	}
	return false;
}

void SaveManager::recoverPendingFlushes()
{
	std::unordered_set<uint32_t> diskQuarantine;
	try {
		const auto directory = std::filesystem::path("data/save-quarantine");
		if (std::filesystem::exists(directory)) {
			for (const auto& entry : std::filesystem::directory_iterator(directory)) {
				if (entry.path().extension() != ".sql" && entry.path().extension() != ".tmp") { continue; }
				const auto stem = entry.path().stem().string();
				uint32_t guid = 0;
				const auto parsed = std::from_chars(stem.data(), stem.data() + stem.size(), guid);
				if (parsed.ec == std::errc{} && parsed.ptr == stem.data() + stem.size() && guid) {
					diskQuarantine.insert(guid);
					quarantinePlayer(guid, "emergency-file-requires-reconciliation", true);
				}
			}
		}
	} catch (const std::exception& error) {
		throw std::runtime_error(fmt::format("Cannot inspect save quarantine: {}", error.what()));
	}
	// Controlled recovery only. Never replay into a live session or while workers
	// can still apply an earlier generation for that GUID.
	Database& db = Database::getInstance();
	auto rows = db.storeQuery("SELECT `guid`,`query_index`,`query_text` FROM `player_save_async_pending` ORDER BY `guid`,`query_index`");
	if (!rows) {
		return;
	}
	std::unordered_map<uint32_t, IOLoginData::PlayerSaveSnapshot> saves;
	do {
		const uint32_t guid = rows->getNumber<uint32_t>("guid");
		std::string query{rows->getString("query_text")};
		if (normalizeLegacyOnlineTimeQuery(query)) {
			LOG_WARN(fmt::format("[SaveManager] guid={} legacy online-time increment omitted during idempotent recovery", guid));
		}
		if (rows->getNumber<uint32_t>("query_index") != 0 || !query.starts_with("-- tfs-save:")) {
			saves[guid].queries.push_back(std::move(query));
		} else {
			saves.try_emplace(guid);
		}
	} while (rows->next());
	for (auto& [guid, save] : saves) {
		if (diskQuarantine.contains(guid)) { continue; }
		if (g_game.getPlayerByGUID(guid) || hasPendingPlayerSave(guid) || save.queries.empty()) {
			quarantinePlayer(guid, "recovery-live-or-empty", true);
			continue;
		}
		save.walClearQuery = fmt::format("DELETE FROM `player_save_async_pending` WHERE `guid`={}", guid);
		const auto result = IOLoginData::flushPlayerSaveChecked(save);
		if (result == IOLoginData::FlushResult::Committed) {
			failedRecoveryGuids.erase(guid);
			retainedFailures.erase(guid);
			LOG_INFO(fmt::format("[SaveManager] guid={} stage=recovery state=healthy queries={}", guid, save.queries.size()));
		} else {
			quarantinePlayer(guid, "recovery", result == IOLoginData::FlushResult::Indeterminate);
		}
	}
}

void SaveManager::saveMapAsync()
{
	LOG_INFO(fmt::format(">> {}: {}",
		fmt::format(fg(fmt::color::magenta), "SaveManager"),
		fmt::format(fg(fmt::color::cyan), "Saving map...")));

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
}
