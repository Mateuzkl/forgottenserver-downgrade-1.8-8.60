// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// SaveManager - Async save coordination using ThreadPool

#ifndef FS_SAVE_MANAGER_H
#define FS_SAVE_MANAGER_H

#include "iologindata.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;

enum class SaveResult : uint8_t
{
	Failed,
	Scheduled, // In memory only: drain/completion must succeed before claiming durability.
	Queued,    // Latest snapshot committed to WAL behind an older in-flight save.
	Persisted  // Player data, generation and WAL cleanup committed atomically.
};

class SaveManager
{
public:
	using Completion = std::function<void(bool)>;
	// Scheduling and all bookkeeping are dispatcher-only. Workers own value snapshots.
	bool saveAll(Completion completion = {});
	SaveResult savePlayer(Player* player);
	bool saveMap(); // Synchronous: traverses mutable house state on the dispatcher.
	// Queued is permitted only for logout, whose contract accepts durable WAL.
	SaveResult savePlayerSync(Player* player, bool allowDurableQueue = false);
	bool savePlayersSync(const std::vector<Player*>& players);
	// Side changes are SQL-only and execute in the same transaction as the inbox credit.
	bool savePlayerTransfer(Player* player, const ItemBlockList& inboxCredit,
	                        const std::function<bool()>& sideChanges,
	                        const std::vector<Player*>& participants = {});
	[[nodiscard]] bool isPersistenceBlocked() const noexcept { return persistenceBlocked; }
	void shutdownAsync(Completion completion);

	[[nodiscard]] bool hasPendingPlayerSave(uint32_t guid) const noexcept
	{
		return flushInFlight.contains(guid) || pendingFlushes.contains(guid);
	}
	// Exactly once on the dispatcher. This API owns the sole timeout (default 10s),
	// releases timed-out callback captures, and never cancels the actual save.
	void drainPlayerFlushAsync(uint32_t guid, Completion callback, uint32_t timeoutMs = 10000);
	bool recoverPendingFlushes();
	[[nodiscard]] bool isSaving() const noexcept { return saving.load(std::memory_order_acquire); }
	[[nodiscard]] uint64_t getLastSaveTime() const noexcept { return lastSaveDurationMs.load(); }
	[[nodiscard]] uint32_t getLastPlayerCount() const noexcept { return lastPlayersSaved.load(); }
	[[nodiscard]] bool hasFailedRecovery(uint32_t guid) const noexcept
	{
		return persistenceBlocked || failedRecoveryGuids.contains(guid);
	}

private:
	friend class House;
	friend struct SaveManagerTestAccess;
	bool commitTransfer(std::string_view receiptQuery, uint64_t expected, uint64_t committedValue,
	                    const std::function<bool()>& apply);
	struct PendingPlayerFlush
	{
		std::string name;
		IOLoginData::PlayerSaveSnapshot save;
		bool trackedBySaveAll = false;
		bool journalDurable = false;
	};
	struct Waiter
	{
		uint64_t id;
		uint32_t timeoutEvent;
		Completion callback;
	};
	std::optional<IOLoginData::PlayerSaveSnapshot> snapshot(Player* player, const ItemBlockList& inboxCredit = {});
	SaveResult schedulePlayerFlush(Player* player, bool trackSaveAll = false);
	void queueSnapshot(uint32_t guid, PendingPlayerFlush pending);
	void onPlayerFlushed(uint32_t guid, bool tracked, bool success, IOLoginData::PlayerSaveSnapshot save);
	void acknowledgePlayerSave(uint32_t guid, const IOLoginData::PlayerSaveSnapshot& save);
	void dispatchPlayerFlush(uint32_t guid, PendingPlayerFlush pending);
	void completeTrackedFlush(bool success);
	void finishSaveGeneration();
	void finishWaiters(uint32_t guid, bool success);
	void timeoutWaiter(uint32_t guid, uint64_t id);
	void finishShutdown();

	std::atomic<bool> saving{false};
	std::atomic<uint64_t> lastSaveDurationMs{0};
	std::atomic<uint32_t> lastPlayersSaved{0};
	uint32_t pendingSaveFlushes = 0;
	uint32_t completingGenerations = 0;
	uint64_t saveGenerationId = 0;
	int64_t saveStartedMs = 0;
	bool generationSucceeded = true;
	// Startup recovery gates logins; only runtime durability failures block shutdown.
	bool sessionSaveFailed = false;
	bool saveAgain = false;
	bool accepting = true;
	bool persistenceBlocked = false;
	bool shutdownRequested = false;
	std::vector<Completion> saveCallbacks;
	std::vector<Completion> nextSaveCallbacks;
	std::vector<Completion> shutdownCallbacks;
	std::unordered_set<uint32_t> flushInFlight;
	std::unordered_map<uint32_t, PendingPlayerFlush> pendingFlushes;
	// Retain per-GUID high water marks across chains: two offline temporary
	// Player objects may have loaded the same older database generation.
	std::unordered_map<uint32_t, uint64_t> generations;
	std::unordered_map<uint32_t, std::vector<Waiter>> flushChainCallbacks;
	uint64_t nextWaiterId = 0;
	std::unordered_set<uint32_t> failedRecoveryGuids;
};

extern SaveManager g_saveManager;

#endif // FS_SAVE_MANAGER_H
