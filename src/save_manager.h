// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// SaveManager - Async save coordination using ThreadPool

#ifndef FS_SAVE_MANAGER_H
#define FS_SAVE_MANAGER_H

#include <atomic>
#include <chrono>
#include <cstdint>

class Player;

class SaveManager
{
public:
	SaveManager() = default;

	void saveAll();
	bool savePlayer(Player* player);
	void saveMapAsync();

	[[nodiscard]] bool isSaving() const noexcept { return saving.load(std::memory_order_relaxed); }
	[[nodiscard]] uint64_t getLastSaveTime() const noexcept { return lastSaveDurationMs.load(std::memory_order_relaxed); }
	[[nodiscard]] uint32_t getLastPlayerCount() const noexcept { return lastPlayersSaved.load(std::memory_order_relaxed); }

private:
	std::atomic<bool> saving{false};
	std::atomic<uint64_t> lastSaveDurationMs{0};
	std::atomic<uint32_t> lastPlayersSaved{0};
	std::atomic<int64_t> lastSaveTimestamp{0};

	static constexpr int64_t MIN_SAVE_INTERVAL_MS = 5000;
};

extern SaveManager g_saveManager;

#endif // FS_SAVE_MANAGER_H
