// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// SaveManager - Async save coordination using ThreadPool

#include "otpch.h"

#include "save_manager.h"

#include "game.h"
#include "iologindata.h"
#include "iomapserialize.h"
#include "logger.h"
#include "thread_pool.h"

extern Game g_game;

SaveManager g_saveManager;

void SaveManager::saveAll()
{
	if (saving.exchange(true)) {
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

	// Save all online players (on dispatcher thread - must be serial for thread safety)
	uint32_t playerCount = 0;
	uint32_t failCount = 0;
	const auto& players = g_game.getPlayers();

	for (const auto& it : players) {
		if (IOLoginData::savePlayer(it.second)) {
			playerCount++;
		} else {
			failCount++;
			LOG_ERROR(fmt::format("[SaveManager] Failed to save player: {}", it.second->getName()));
		}
	}

	// Save map ASYNC on ThreadPool (house info + house items = pure SQL, no game state access)
	g_threadPool.detach_task([]() {
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
	});

	auto endTime = std::chrono::high_resolution_clock::now();
	auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

	lastSaveDurationMs.store(static_cast<uint64_t>(durationMs), std::memory_order_relaxed);
	lastPlayersSaved.store(playerCount, std::memory_order_relaxed);

	if (failCount > 0) {
		LOG_INFO(fmt::format(">> {}: Saved {} players ({} failed) in {}",
			fmt::format(fg(fmt::color::magenta), "SaveManager"),
			fmt::format(fg(fmt::color::lime_green), "{}", playerCount),
			fmt::format(fg(fmt::color::red), "{}", failCount),
			fmt::format(fg(fmt::color::cyan), "{}ms", durationMs)));
	} else {
		LOG_INFO(fmt::format(">> {}: Saved {} players in {} (map saving async)",
			fmt::format(fg(fmt::color::magenta), "SaveManager"),
			fmt::format(fg(fmt::color::lime_green), "{}", playerCount),
			fmt::format(fg(fmt::color::cyan), "{}ms", durationMs)));
	}

	saving.store(false);
}

bool SaveManager::savePlayer(Player* player)
{
	if (!player) {
		return false;
	}

	auto startTime = std::chrono::high_resolution_clock::now();
	bool success = IOLoginData::savePlayer(player);
	auto endTime = std::chrono::high_resolution_clock::now();
	auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

	if (success) {
		LOG_INFO(fmt::format(">> {}: Player {} saved in {}",
			fmt::format(fg(fmt::color::magenta), "SaveManager"),
			fmt::format(fg(fmt::color::lime_green), "{}", player->getName()),
			fmt::format(fg(fmt::color::cyan), "{}ms", durationMs)));
	} else {
		LOG_ERROR(fmt::format("[SaveManager] Failed to save player: {}", player->getName()));
	}

	return success;
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
