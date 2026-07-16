// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "connection_rate_limiter.h"

namespace {
constexpr uint64_t CLEANUP_INTERVAL = 60'000;
constexpr uint64_t ENTRY_LIFETIME = 300'000;
constexpr uint64_t ATTEMPT_WINDOW = 5'000;

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

void ConnectionRateLimiter::cleanup(uint64_t currentTime)
{
	if (lastCleanup != 0 && currentTime - lastCleanup < CLEANUP_INTERVAL) {
		return;
	}
	lastCleanup = currentTime;

	std::erase_if(entries, [currentTime](const auto& pair) {
		const Entry& entry = pair.second;
		return entry.blockUntil <= currentTime && currentTime - entry.lastAttempt > ENTRY_LIFETIME;
	});
}

ConnectionRateLimitResult ConnectionRateLimiter::check(uint32_t clientIp, uint64_t currentTime,
	                                                     uint32_t allowedConnections,
	                                                     uint64_t minimumInterval)
{
	std::scoped_lock lock(mutex);
	cleanup(currentTime);

	auto [it, inserted] = entries.try_emplace(clientIp, Entry{.lastAttempt = currentTime});
	if (inserted) {
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
			++entry.totalBlocks;
			if (timeDifference <= minimumInterval) {
				const uint64_t blockDuration = getBlockDuration(entry.totalBlocks);
				entry.blockUntil = currentTime + blockDuration;
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

	return {};
}

} // namespace tfs::net
