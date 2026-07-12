// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_WALK_EVENT_H
#define FS_WALK_EVENT_H

#include "creature_handle.h"

#include <cstdint>
#include <optional>

struct WalkEventTicket
{
	CreatureHandle creature;
	uint64_t generation = 0;
	int64_t deadlineMilliseconds = 0;
};

struct WalkEventCancellation
{
	uint32_t eventId = 0;
	bool wasQueued = false;
	bool wasExecuting = false;

	[[nodiscard]] constexpr bool wasActive() const noexcept { return wasQueued || wasExecuting; }
};

class WalkEventState
{
public:
	explicit constexpr WalkEventState(uint64_t initialGeneration = 0) noexcept : generation(initialGeneration) {}

	[[nodiscard]] std::optional<uint64_t> beginScheduling() noexcept
	{
		if (scheduling || queued || executing) {
			return std::nullopt;
		}
		advanceGeneration();
		scheduling = true;
		return generation;
	}

	[[nodiscard]] bool commitSchedule(uint64_t expectedGeneration, uint32_t scheduledEventId) noexcept
	{
		if (!scheduling || queued || executing || generation != expectedGeneration) {
			return false;
		}
		scheduling = false;
		if (scheduledEventId == 0) {
			return false;
		}
		eventId = scheduledEventId;
		queued = true;
		return true;
	}

	[[nodiscard]] bool beginExecution(uint64_t expectedGeneration) noexcept
	{
		if (scheduling || !queued || executing || eventId == 0 || generation != expectedGeneration) {
			return false;
		}
		eventId = 0;
		queued = false;
		executing = true;
		return true;
	}

	[[nodiscard]] bool finishExecution(uint64_t expectedGeneration) noexcept
	{
		if (!executing || generation != expectedGeneration) {
			return false;
		}
		executing = false;
		return true;
	}

	[[nodiscard]] WalkEventCancellation cancel() noexcept
	{
		WalkEventCancellation result{eventId, queued, executing};
		advanceGeneration();
		eventId = 0;
		scheduling = false;
		queued = false;
		executing = false;
		return result;
	}

	[[nodiscard]] constexpr uint64_t getGeneration() const noexcept { return generation; }
	[[nodiscard]] constexpr uint32_t getEventId() const noexcept { return eventId; }
	[[nodiscard]] constexpr bool isQueued() const noexcept { return queued; }
	[[nodiscard]] constexpr bool isExecuting() const noexcept { return executing; }

private:
	constexpr void advanceGeneration() noexcept
	{
		++generation;
		if (generation == 0) {
			++generation;
		}
	}

	uint64_t generation = 0;
	uint32_t eventId = 0;
	bool scheduling = false;
	bool queued = false;
	bool executing = false;
};

#endif // FS_WALK_EVENT_H
