// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "connection_rate_limiter.h"

namespace {
constexpr uint64_t CLEANUP_INTERVAL = 60'000;
constexpr uint64_t ENTRY_LIFETIME = 300'000;
constexpr uint64_t ATTEMPT_WINDOW = 5'000;
constexpr size_t CLEANUP_BATCH_SIZE = 256;

uint64_t getBlockDuration(uint32_t totalBlocks)
{
	if (totalBlocks >= 10) {
		return 300'000;
	}
	if (totalBlocks >= 5) {
		return 60'000;
	}
	if (totalBlocks >= 3) {
		return 15'000;
	}
	return 3'000;
}
} // namespace

namespace tfs::net {

void ConnectionRateLimiter::refreshExpiry(uint32_t clientIp, Entry& entry)
{
	if (entry.expiry != expiryIndex.end()) {
		expiryIndex.erase(entry.expiry);
	}

	const uint64_t expiresAt = std::max(entry.blockUntil, entry.lastAttempt + ENTRY_LIFETIME + 1);
	entry.expiry = expiryIndex.emplace(expiresAt, clientIp);
}

void ConnectionRateLimiter::cleanup(uint64_t currentTime)
{
	if (lastCleanup != 0 && currentTime - lastCleanup < CLEANUP_INTERVAL) {
		return;
	}
	lastCleanup = currentTime;

	for (size_t processed = 0; processed < CLEANUP_BATCH_SIZE && !expiryIndex.empty(); ++processed) {
		const auto expiryIt = expiryIndex.begin();
		if (expiryIt->first > currentTime) {
			break;
		}

		const auto entryIt = entries.find(expiryIt->second);
		if (entryIt != entries.end() && entryIt->second.expiry == expiryIt) {
			entries.erase(entryIt);
		}
		expiryIndex.erase(expiryIt);
	}
}

ConnectionRateLimitResult ConnectionRateLimiter::check(uint32_t clientIp, uint64_t currentTime,
	                                                     uint32_t allowedConnections,
	                                                     uint64_t minimumInterval)
{
	std::scoped_lock lock(mutex);
	cleanup(currentTime);

	auto [it, inserted] = entries.try_emplace(clientIp, currentTime, expiryIndex.end());
	if (inserted) {
		refreshExpiry(clientIp, it->second);
		return {};
	}

	Entry& entry = it->second;
	if (entry.blockUntil > currentTime) {
		return {.allowed = false, .totalBlocks = entry.totalBlocks};
	}

	const uint64_t timeDifference = currentTime - entry.lastAttempt;
	entry.lastAttempt = currentTime;
	entry.blockUntil = 0;

	if (timeDifference <= ATTEMPT_WINDOW) {
		if (++entry.count > allowedConnections) {
			entry.count = 0;
			if (timeDifference <= minimumInterval) {
				++entry.totalBlocks;
				const uint64_t blockDuration = getBlockDuration(entry.totalBlocks);
				entry.blockUntil = currentTime + blockDuration;
				refreshExpiry(clientIp, entry);
				return {
					.allowed = false,
					.blockStarted = true,
					.blockDuration = blockDuration,
					.totalBlocks = entry.totalBlocks,
				};
			}
		}
	} else {
		entry.count = 1;
		if (timeDifference > CLEANUP_INTERVAL && entry.totalBlocks > 0) {
			--entry.totalBlocks;
		}
	}

	refreshExpiry(clientIp, entry);
	return {};
}

} // namespace tfs::net
