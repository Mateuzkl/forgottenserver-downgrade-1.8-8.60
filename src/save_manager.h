// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// SaveManager - Async save coordination using ThreadPool

#ifndef FS_SAVE_MANAGER_H
#define FS_SAVE_MANAGER_H

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "iologindata.h"

class Player;

class SaveManager
{
public:
	SaveManager() = default;

	void saveAll();
	bool savePlayer(Player* player);
	void saveMapAsync();
	bool savePlayerSync(Player* player);
	// All-or-nothing synchronous batch. Never queues behind another save.
	bool savePlayersSync(const std::vector<Player*>& players);

	// Dispatcher-thread only, like the save queue bookkeeping itself.
	[[nodiscard]] bool hasPendingPlayerSave(uint32_t guid) const noexcept
	{
		return flushInFlight.contains(guid) || pendingFlushes.contains(guid);
	}

	/**
	 * @brief Non-blocking: invokes callback(bool) when pending save operations for
	 * the given GUID have been fully persisted to the database (or on timeout).
	 *
	 * Called from the login flow (thread pool worker) before loadPlayerById.
	 * The callback is delivered on the dispatcher thread. The callback should
	 * schedule any heavy work (e.g. loadPlayerById) on the thread pool to
	 * avoid blocking the dispatcher.
	 */
	void drainPlayerFlushAsync(uint32_t guid, std::function<void(bool)> callback);

	/**
	 * @brief Replay any pending async saves that were lost due to a server crash.
	 * Must be called once during server startup after database is connected.
	 */
	void recoverPendingFlushes();

	[[nodiscard]] bool isSaving() const noexcept
	{
		return saving.load(std::memory_order_acquire) || pendingSaveFlushes.load(std::memory_order_acquire) != 0;
	}
	[[nodiscard]] uint64_t getLastSaveTime() const noexcept { return lastSaveDurationMs.load(std::memory_order_acquire); }
	[[nodiscard]] uint32_t getLastPlayerCount() const noexcept { return lastPlayersSaved.load(std::memory_order_acquire); }

	// Returns true if the given GUID's WAL recovery failed at startup;
	// saves will be rejected and login should be blocked pending manual resolution.
	[[nodiscard]] bool hasFailedRecovery(uint32_t guid) const noexcept
	{
		return failedRecoveryGuids.contains(guid);
	}

private:
	struct PendingPlayerFlush
	{
		std::string name;
		IOLoginData::PlayerSaveSnapshot save;
		bool trackedBySaveAll = false;
	};

	using FlushCallback = std::function<void(bool)>;

	// Player state snapshots and flush queue bookkeeping must run on the dispatcher thread.
	bool schedulePlayerFlush(Player* player, bool trackSaveAll = false);
	void onPlayerFlushed(uint32_t guid, bool trackedBySaveAll, bool success, IOLoginData::PlayerSaveSnapshot save);
	void acknowledgePlayerSave(uint32_t guid, const IOLoginData::PlayerSaveSnapshot& save);
	void beginTrackedFlush() noexcept;
	void completeTrackedFlush() noexcept;
	void dispatchPlayerFlush(uint32_t guid, PendingPlayerFlush pending);

	// WAL helpers for crash-safe async saves
	bool savePendingFlushToDB(uint32_t guid, const IOLoginData::PlayerSaveSnapshot& save);
	void deletePendingFlushFromDB(uint32_t guid);

	std::atomic<bool> saving{false};
	std::atomic<uint32_t> pendingSaveFlushes{0};
	std::atomic<uint64_t> lastSaveDurationMs{0};
	std::atomic<uint32_t> lastPlayersSaved{0};
	std::atomic<int64_t> lastSaveTimestamp{0};
	std::unordered_set<uint32_t> flushInFlight;
	std::unordered_map<uint32_t, PendingPlayerFlush> pendingFlushes;

	// Non-blocking callback registry for login barrier
	std::unordered_map<uint32_t, std::vector<FlushCallback>> flushChainCallbacks;

	// GUIDs whose WAL recovery failed at startup; savePendingFlushToDB will refuse to
	// overwrite their WAL entries until manually resolved (prevents silent data loss)
	std::unordered_set<uint32_t> failedRecoveryGuids;

	static constexpr int64_t MIN_SAVE_INTERVAL_MS = 2000;
};

extern SaveManager g_saveManager;

#endif // FS_SAVE_MANAGER_H
