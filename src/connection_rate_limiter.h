// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CONNECTION_RATE_LIMITER_H
#define FS_CONNECTION_RATE_LIMITER_H

#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>

namespace tfs::net {

struct ConnectionRateLimitResult
{
	bool allowed = true;
	bool blockStarted = false;
	uint64_t blockDuration = 0;
	uint32_t totalBlocks = 0;
};

class ConnectionRateLimiter final
{
public:
	ConnectionRateLimitResult check(uint32_t clientIp, uint64_t currentTime, uint32_t allowedConnections,
	                                uint64_t minimumInterval);

private:
	using ExpiryIndex = std::multimap<uint64_t, uint32_t>;

	struct Entry
	{
		Entry(uint64_t lastAttempt, ExpiryIndex::iterator expiry) : lastAttempt(lastAttempt), expiry(expiry) {}

		uint64_t lastAttempt;
		uint64_t blockUntil = 0;
		uint32_t count = 1;
		uint32_t totalBlocks = 0;
		ExpiryIndex::iterator expiry;
	};

	void cleanup(uint64_t currentTime);
	void refreshExpiry(uint32_t clientIp, Entry& entry);
	// Drops the entry at the front of expiryIndex. Caller must check that the
	// index is not empty.
	void eraseOldestEntry();

	std::unordered_map<uint32_t, Entry> entries;
	ExpiryIndex expiryIndex;
	std::mutex mutex;
	uint64_t lastCleanup = 0;
};

} // namespace tfs::net

#endif // FS_CONNECTION_RATE_LIMITER_H
