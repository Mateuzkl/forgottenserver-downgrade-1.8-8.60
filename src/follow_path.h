// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_FOLLOW_PATH_H
#define FS_FOLLOW_PATH_H

#include "position.h"

#include <algorithm>
#include <cstdint>

struct FollowPathKey
{
	Position start;
	Position target;
	uint32_t targetId = 0;

	constexpr bool sameTarget(const FollowPathKey& other) const noexcept
	{
		return targetId == other.targetId && target == other.target;
	}

	constexpr bool operator==(const FollowPathKey&) const noexcept = default;
};

class FollowPathState
{
public:
	[[nodiscard]] bool canReuse(const FollowPathKey& key, bool hasRemainingSteps, bool forced) const noexcept
	{
		if (forced || !snapshotValid || !lastSucceeded || !snapshot.sameTarget(key)) {
			return false;
		}
		return hasRemainingSteps || snapshot.start == key.start;
	}

	[[nodiscard]] bool retryAllowed(const FollowPathKey& key, uint64_t nowMilliseconds) const noexcept
	{
		return !snapshotValid || !(snapshot == key) || nowMilliseconds >= nextRetryMilliseconds;
	}

	[[nodiscard]] bool beginRequest(uint32_t& token) noexcept
	{
		if (pending) {
			return false;
		}
		pending = true;
		pendingGeneration = generation;
		token = pendingGeneration;
		return true;
	}

	[[nodiscard]] bool acceptResult(uint32_t token) noexcept
	{
		if (!pending || token != pendingGeneration) {
			return false;
		}
		pending = false;
		return token == generation;
	}

	void recordSuccess(const FollowPathKey& key) noexcept
	{
		snapshot = key;
		snapshotValid = true;
		lastSucceeded = true;
		consecutiveFailures = 0;
		nextRetryMilliseconds = 0;
	}

	uint32_t recordFailure(const FollowPathKey& key, uint64_t nowMilliseconds) noexcept
	{
		snapshot = key;
		snapshotValid = true;
		lastSucceeded = false;
		consecutiveFailures = std::min<uint8_t>(consecutiveFailures + 1, 4);
		const uint32_t delay = 250u << (consecutiveFailures - 1);
		nextRetryMilliseconds = nowMilliseconds + delay;
		return delay;
	}

	void invalidate(bool resetFailures) noexcept
	{
		++generation;
		snapshotValid = false;
		lastSucceeded = false;
		if (resetFailures) {
			consecutiveFailures = 0;
			nextRetryMilliseconds = 0;
		}
	}

	void cancel() noexcept
	{
		++generation;
		pending = false;
		snapshotValid = false;
		lastSucceeded = false;
		consecutiveFailures = 0;
		nextRetryMilliseconds = 0;
	}

	[[nodiscard]] bool isPending() const noexcept { return pending; }
	[[nodiscard]] uint32_t getGeneration() const noexcept { return generation; }
	[[nodiscard]] uint8_t getConsecutiveFailures() const noexcept { return consecutiveFailures; }
	[[nodiscard]] uint64_t getNextRetryMilliseconds() const noexcept { return nextRetryMilliseconds; }

private:
	FollowPathKey snapshot;
	uint64_t nextRetryMilliseconds = 0;
	uint32_t generation = 1;
	uint32_t pendingGeneration = 0;
	uint8_t consecutiveFailures = 0;
	bool snapshotValid = false;
	bool lastSucceeded = false;
	bool pending = false;
};

#endif // FS_FOLLOW_PATH_H
