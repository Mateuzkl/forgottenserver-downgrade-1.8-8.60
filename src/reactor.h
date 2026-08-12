// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_REACTOR_H
#define FS_REACTOR_H

#include "enums.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#if !defined(__cpp_lib_move_only_function) || __cpp_lib_move_only_function < 202110L
#error "TaskReactor requires C++23 std::move_only_function support"
#endif

using ReactorCallback = std::move_only_function<void()>;

inline constexpr size_t REACTOR_MAX_INBOX_SIZE = 100000;
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

	[[nodiscard]] bool isReactorThread() const noexcept;
	[[nodiscard]] ThreadState getState() const noexcept;
	[[nodiscard]] bool hasPendingTasks() const;

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

	void drainInbox(std::vector<Task>& readyTasks);
	void drainReadyTasks(std::vector<Task>& readyTasks);
	void executeReadyTasks(std::vector<Task>& readyTasks);
	void waitForWork();
	static bool taskComesAfter(const Task& lhs, const Task& rhs) noexcept;

	mutable std::mutex mutex;
	std::condition_variable conditionVariable;

#ifndef NDEBUG
	// taskHeap, activeIdentifiers and cancelled are deliberately touched without
	// the mutex, because they belong to whichever single thread drives runOnce().
	// Nothing enforced that, and when it was broken the symptom was heap
	// corruption far from the cause. Counts concurrent entries so the next
	// mistake of that shape aborts immediately instead.
	std::atomic<int> runOnceDepth{0};
#endif

	std::vector<Task> sendInbox;
	std::vector<Task> scheduleInbox;
	std::vector<uint32_t> cancelInbox;

	std::unordered_set<uint32_t> cancelled;
	std::unordered_set<uint32_t> activeIdentifiers;
	std::vector<Task> taskHeap;

	std::atomic<uint32_t> nextIdentifier{0};
	std::atomic<uint64_t> nextSequence{0};
	std::atomic<ThreadState> threadState{THREAD_STATE_TERMINATED};

	static thread_local const TaskReactor* currentReactor;

	uint32_t maxTasksPerCycle = 0;
	std::chrono::milliseconds timeBudget{0};
	size_t maxInboxSize = REACTOR_MAX_INBOX_SIZE;
};

extern TaskReactor g_reactor;

#endif // FS_REACTOR_H
