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
#include "tasks.h"
#include "kv/kv.h"

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

	// Save KV store
	if (!KVStore::getInstance().saveAll()) {
		LOG_ERROR("[SaveManager] Failed to save KV store.");
	}

	// Build all online players on dispatcher and flush SQL on the thread pool.
	uint32_t playerCount = 0;
	const auto& players = g_game.getPlayers();

	for (const auto& player : players) {
		if (schedulePlayerFlush(player.get())) {
			playerCount++;
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

	LOG_INFO(fmt::format(">> {}: Queued {} player save(s) in {} (map/player SQL flushing async)",
		fmt::format(fg(fmt::color::magenta), "SaveManager"),
		fmt::format(fg(fmt::color::lime_green), "{}", playerCount),
		fmt::format(fg(fmt::color::cyan), "{}ms", durationMs)));

	saving.store(false);
}

bool SaveManager::savePlayer(Player* player)
{
	if (!player) {
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

bool SaveManager::schedulePlayerFlush(Player* player)
{
	if (!player) {
		return false;
	}

	auto queries = IOLoginData::buildPlayerSave(player);
	if (!queries) {
		LOG_ERROR(fmt::format("[SaveManager] Failed to build save for player: {}", player->getName()));
		return false;
	}

	const uint32_t guid = player->getGUID();
	const std::string name = player->getName();
	if (flushInFlight.contains(guid)) {
		pendingFlushes[guid] = std::make_pair(name, std::move(*queries));
		return true;
	}

	flushInFlight.insert(guid);
	g_threadPool.detach_task([this, guid, name, q = std::move(*queries)]() {
		if (!IOLoginData::flushPlayerSave(q)) {
			LOG_ERROR(fmt::format("[SaveManager] Failed to flush save for player: {}", name));
		}
		g_dispatcher.addTask([this, guid]() { onPlayerFlushed(guid); });
	});
	return true;
}

void SaveManager::onPlayerFlushed(uint32_t guid)
{
	auto it = pendingFlushes.find(guid);
	if (it == pendingFlushes.end()) {
		flushInFlight.erase(guid);
		return;
	}

	auto [name, queries] = std::move(it->second);
	pendingFlushes.erase(it);
	g_threadPool.detach_task([this, guid, name, q = std::move(queries)]() {
		if (!IOLoginData::flushPlayerSave(q)) {
			LOG_ERROR(fmt::format("[SaveManager] Failed to flush queued save for player: {}", name));
		}
		g_dispatcher.addTask([this, guid]() { onPlayerFlushed(guid); });
	});
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
