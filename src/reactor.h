// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_REACTOR_H
#define FS_REACTOR_H

#include "enums.h"
#include "move_only_function.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

using ReactorCallback = MoveOnlyFunction;

// Expiring work is rejected at this queued-work threshold. Authoritative work
// without a deadline, scheduled events and cancellation remain lossless.
// This is not a hard memory bound for a sustained critical-work producer.
inline constexpr size_t REACTOR_MAX_INBOX_SIZE = 100000;
inline constexpr size_t REACTOR_MAX_INBOX_DRAIN_PER_CYCLE = 1024;
inline constexpr size_t REACTOR_MAX_RETAINED_SCRATCH = 8192;
inline constexpr std::chrono::milliseconds REACTOR_DRAIN_TIMEOUT{5000};

class TaskReactor
{
public:
	TaskReactor();

	void start() noexcept;
	bool send(ReactorCallback&& callback, std::string description = {}, std::string origin = {});
	bool send(std::chrono::milliseconds expirationTime, ReactorCallback&& callback, std::string description = {},
	          std::string origin = {});
	bool send(uint32_t expirationTime, ReactorCallback&& callback, std::string description = {},
	          std::string origin = {});
	uint32_t schedule(std::chrono::milliseconds delay, ReactorCallback&& callback, std::string description = {},
	                  std::string origin = {});
	uint32_t schedule(uint32_t delay, ReactorCallback&& callback, std::string description = {},
	                  std::string origin = {});
	void cancel(uint32_t taskIdentifier);

	void runLoop();
	void runOnce();
	void shutdown() noexcept;
	void drain();

	void setMaxTasksPerCycle(uint32_t maxTasks) noexcept { maxTasksPerCycle = maxTasks; }
	void setTimeBudget(std::chrono::milliseconds budget) noexcept { timeBudget = budget; }
	void setMaxInboxSize(size_t maxSize) noexcept { maxInboxSize = maxSize; }
	void setMaxInboxDrainPerCycle(size_t maxTasks) noexcept { maxInboxDrainPerCycle = maxTasks; }

	[[nodiscard]] bool isReactorThread() const noexcept;
	[[nodiscard]] ThreadState getState() const noexcept;
	[[nodiscard]] bool hasPendingTasks() const;
	[[nodiscard]] bool canAcceptExpiringTask() const;
	[[nodiscard]] size_t getPendingTaskCount() const;

private:
	struct Task
	{
		std::chrono::steady_clock::time_point fireAt;
		std::chrono::steady_clock::time_point deadline;
		uint32_t identifier = 0;
		uint64_t sequence = 0;
		std::string description;
		std::string origin;
		ReactorCallback function;

		[[nodiscard]] bool hasExpired(std::chrono::steady_clock::time_point now) const noexcept;
	};

	void drainInbox();
	void drainReadyTasks(std::vector<Task>& readyTasks);
	void executeReadyTasks(std::vector<Task>& readyTasks, std::chrono::steady_clock::time_point cycleStart);
	bool consumeCancellation(uint32_t identifier);
	bool retireIdentifier(uint32_t identifier);
	void waitForWork();
	static bool taskComesAfter(const Task& lhs, const Task& rhs) noexcept;
	[[nodiscard]] size_t pendingTaskCountUnsafe() const noexcept;

	mutable std::mutex mutex;
	std::condition_variable conditionVariable;

#ifndef NDEBUG
	// taskHeap and cancelled are deliberately touched without
	// the mutex, because they belong to whichever single thread drives runOnce().
	// Nothing enforced that, and when it was broken the symptom was heap
	// corruption far from the cause. Counts concurrent entries so the next
	// mistake of that shape aborts immediately instead.
	std::atomic<int> runOnceDepth{0};
#endif

	std::deque<Task> sendInbox;
	// A producer-side heap exposes the earliest undrained timer.
	std::vector<Task> scheduleInbox;
	std::deque<uint32_t> cancelInbox;
	// Protected by mutex, from acceptance into scheduleInbox until retirement.
	// Keep ids reserved across bounded ingestion, heap, ready-batch and deferral.
	std::unordered_set<uint32_t> activeIdentifiers;
	uint32_t nextIdentifier = 0;
	bool inboxAboveSoftLimit = false;

	std::unordered_set<uint32_t> cancelled;
	std::vector<Task> taskHeap;
	std::atomic<size_t> taskHeapSize{0};
	// Reused by the single reactor driver. Keeping the buffer alive avoids a
	// heap allocation on every world cycle.
	std::vector<Task> readyTasksBuffer;
	// Scratch storage belongs to the same single driver. Clear moved captures
	// after ingestion; capacity alone survives between cycles.
	std::vector<Task> sentTasksBuffer;
	std::vector<Task> scheduledTasksBuffer;
	std::vector<uint32_t> cancellationsBuffer;
	// Snapshot boundary: ready work must not overtake an older task
	// that remains in the bounded inbox after this cycle's ingestion.
	std::optional<std::pair<std::chrono::steady_clock::time_point, uint64_t>> undrainedTaskFrontier;

	std::atomic<uint64_t> nextSequence{0};
	std::atomic<ThreadState> threadState{THREAD_STATE_TERMINATED};

	static thread_local const TaskReactor* currentReactor;

	uint32_t maxTasksPerCycle = 0;
	std::chrono::milliseconds timeBudget{0};
	size_t maxInboxSize = REACTOR_MAX_INBOX_SIZE;
	size_t maxInboxDrainPerCycle = REACTOR_MAX_INBOX_DRAIN_PER_CYCLE;

	friend struct TaskReactorTestAccess;
};

extern TaskReactor g_reactor;

#endif // FS_REACTOR_H
