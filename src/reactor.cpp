// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "reactor.h"

#include "logger.h"
#include "stats.h"

TaskReactor g_reactor;
thread_local const TaskReactor* TaskReactor::currentReactor = nullptr;

TaskReactor::TaskReactor() :
	maxTasksPerCycle(0),
	timeBudget(0),
	maxInboxSize(REACTOR_MAX_INBOX_SIZE)
{}

namespace {
auto distantFuture() noexcept
{
	return std::chrono::steady_clock::time_point::max();
}
} // namespace

bool TaskReactor::Task::hasExpired(std::chrono::steady_clock::time_point now) const noexcept
{
	return deadline != distantFuture() && deadline <= now;
}

void TaskReactor::start() noexcept
{
	threadState.store(THREAD_STATE_RUNNING, std::memory_order_release);
}

bool TaskReactor::send(ReactorCallback&& callback)
{
	if (!callback || threadState.load(std::memory_order_acquire) != THREAD_STATE_RUNNING) {
		return false;
	}

	const auto now = std::chrono::steady_clock::now();
	Task task{
	    .fireAt = now,
	    .deadline = distantFuture(),
	    .sequence = nextSequence.fetch_add(1, std::memory_order_relaxed),
	    .function = std::move(callback),
	};

	{
		std::scoped_lock lock(mutex);
		if (maxInboxSize > 0 && sendInbox.size() >= maxInboxSize) {
			LOG_WARN("[TaskReactor] sendInbox overflow ({}), dropping task", sendInbox.size());
			return false;
		}
		sendInbox.push_back(std::move(task));
	}

	conditionVariable.notify_one();
	return true;
}

bool TaskReactor::send(std::chrono::milliseconds expirationTime, ReactorCallback&& callback)
{
	if (!callback || threadState.load(std::memory_order_acquire) != THREAD_STATE_RUNNING) {
		return false;
	}

	const auto now = std::chrono::steady_clock::now();
	Task task{
	    .fireAt = now,
	    .deadline = now + expirationTime,
	    .sequence = nextSequence.fetch_add(1, std::memory_order_relaxed),
	    .function = std::move(callback),
	};

	{
		std::scoped_lock lock(mutex);
		if (maxInboxSize > 0 && sendInbox.size() >= maxInboxSize) {
			LOG_WARN("[TaskReactor] sendInbox overflow ({}), dropping timed task", sendInbox.size());
			return false;
		}
		sendInbox.push_back(std::move(task));
	}

	conditionVariable.notify_one();
	return true;
}

bool TaskReactor::send(uint32_t expirationTime, ReactorCallback&& callback)
{
	if (expirationTime == 0) {
		return send(std::move(callback));
	}

	return send(std::chrono::milliseconds(expirationTime), std::move(callback));
}

uint32_t TaskReactor::schedule(std::chrono::milliseconds delay, ReactorCallback&& callback)
{
	if (!callback || threadState.load(std::memory_order_acquire) != THREAD_STATE_RUNNING) {
		return 0;
	}

	uint32_t identifier = nextIdentifier.fetch_add(1, std::memory_order_relaxed) + 1;
	if (identifier == 0) {
		identifier = nextIdentifier.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	Task task{
	    .fireAt = std::chrono::steady_clock::now() + delay,
	    .deadline = distantFuture(),
	    .identifier = identifier,
	    .sequence = nextSequence.fetch_add(1, std::memory_order_relaxed),
	    .function = std::move(callback),
	};

	{
		std::scoped_lock lock(mutex);
		if (maxInboxSize > 0 && scheduleInbox.size() >= maxInboxSize) {
			LOG_WARN("[TaskReactor] scheduleInbox overflow ({}), dropping scheduled task", scheduleInbox.size());
			return 0;
		}
		scheduleInbox.push_back(std::move(task));
	}

	conditionVariable.notify_one();
	return identifier;
}

uint32_t TaskReactor::schedule(uint32_t delay, ReactorCallback&& callback)
{
	return schedule(std::chrono::milliseconds(delay), std::move(callback));
}

void TaskReactor::cancel(uint32_t taskIdentifier)
{
	if (taskIdentifier == 0 || threadState.load(std::memory_order_acquire) != THREAD_STATE_RUNNING) {
		return;
	}

	{
		std::scoped_lock lock(mutex);
		if (maxInboxSize > 0 && cancelInbox.size() >= maxInboxSize) {
			LOG_WARN("[TaskReactor] cancelInbox overflow ({}), dropping cancellation", cancelInbox.size());
			return;
		}
		cancelInbox.push_back(taskIdentifier);
	}

	conditionVariable.notify_one();
}

void TaskReactor::runLoop()
{
	currentReactor = this;

	while (threadState.load(std::memory_order_acquire) == THREAD_STATE_RUNNING) {
		runOnce();

		if (threadState.load(std::memory_order_acquire) != THREAD_STATE_RUNNING) {
			break;
		}

		waitForWork();
	}

	currentReactor = nullptr;
}

void TaskReactor::runOnce()
{
	std::vector<Task> readyTasks;
	readyTasks.reserve(128);

	drainInbox(readyTasks);
	drainReadyTasks(readyTasks);
	executeReadyTasks(readyTasks);
}

void TaskReactor::shutdown() noexcept
{
	threadState.store(THREAD_STATE_TERMINATED, std::memory_order_release);
	conditionVariable.notify_all();
}

void TaskReactor::drain()
{
	const auto deadline = std::chrono::steady_clock::now() + REACTOR_DRAIN_TIMEOUT;
	while (std::chrono::steady_clock::now() < deadline) {
		{
			std::scoped_lock lock(mutex);
			if (sendInbox.empty() && scheduleInbox.empty() && taskHeap.empty() && cancelInbox.empty()) {
				return;
			}
		}
		runOnce();
		std::this_thread::yield();
	}
	LOG_WARN("[TaskReactor] drain timed out after {} ms",
	         REACTOR_DRAIN_TIMEOUT.count());
}

bool TaskReactor::hasPendingTasks() const
{
	std::scoped_lock lock(mutex);
	return !sendInbox.empty() || !scheduleInbox.empty() || !taskHeap.empty() || !cancelInbox.empty();
}

bool TaskReactor::isReactorThread() const noexcept
{
	return currentReactor == this;
}

ThreadState TaskReactor::getState() const noexcept
{
	return threadState.load(std::memory_order_acquire);
}

bool TaskReactor::taskComesAfter(const Task& lhs, const Task& rhs) noexcept
{
	if (lhs.fireAt != rhs.fireAt) {
		return lhs.fireAt > rhs.fireAt;
	}
	return lhs.sequence > rhs.sequence;
}

void TaskReactor::drainInbox(std::vector<Task>& readyTasks)
{
	std::vector<Task> sentTasks;
	std::vector<Task> scheduledTasks;
	std::vector<uint32_t> cancellations;

	{
		std::scoped_lock lock(mutex);
		sentTasks.swap(sendInbox);
		scheduledTasks.swap(scheduleInbox);
		cancellations.swap(cancelInbox);
	}

	for (auto& task : scheduledTasks) {
		activeIdentifiers.insert(task.identifier);
		taskHeap.push_back(std::move(task));
		std::push_heap(taskHeap.begin(), taskHeap.end(), taskComesAfter);
	}

	for (uint32_t identifier : cancellations) {
		if (activeIdentifiers.contains(identifier)) {
			cancelled.insert(identifier);
		}
	}

	const auto now = std::chrono::steady_clock::now();
	for (auto& task : sentTasks) {
		if (!task.hasExpired(now)) {
			readyTasks.push_back(std::move(task));
		}
	}
}

void TaskReactor::drainReadyTasks(std::vector<Task>& readyTasks)
{
	const auto now = std::chrono::steady_clock::now();

	while (!taskHeap.empty() && taskHeap.front().fireAt <= now) {
		std::pop_heap(taskHeap.begin(), taskHeap.end(), taskComesAfter);
		auto readyTask = std::move(taskHeap.back());
		taskHeap.pop_back();

		activeIdentifiers.erase(readyTask.identifier);
		if (cancelled.erase(readyTask.identifier) > 0 || readyTask.hasExpired(now)) {
			continue;
		}

		readyTasks.push_back(std::move(readyTask));
	}
}

void TaskReactor::executeReadyTasks(std::vector<Task>& readyTasks)
{
	std::sort(readyTasks.begin(), readyTasks.end(), [](const Task& lhs, const Task& rhs) {
		if (lhs.fireAt != rhs.fireAt) {
			return lhs.fireAt < rhs.fireAt;
		}
		return lhs.sequence < rhs.sequence;
	});

	const auto cycleStart = std::chrono::steady_clock::now();
	uint32_t tasksExecuted = 0;

	for (auto& task : readyTasks) {
		if (!task.function) {
			continue;
		}

		if (maxTasksPerCycle > 0 && tasksExecuted >= maxTasksPerCycle) {
			LOG_WARN("[TaskReactor] fairness limit reached ({} tasks/cycle), deferring {} tasks",
			         maxTasksPerCycle, readyTasks.size() - tasksExecuted);
			break;
		}

		if (timeBudget.count() > 0 && std::chrono::steady_clock::now() - cycleStart >= timeBudget) {
		LOG_REACTOR("time budget exceeded ({} ms), deferring {} tasks",
		            timeBudget.count(), readyTasks.size() - tasksExecuted);
			break;
		}

		try {
			task.function();
		} catch (const std::exception& exception) {
			LOG_ERROR("[TaskReactor] Unhandled task exception: {}", exception.what());
		} catch (...) {
			LOG_ERROR("[TaskReactor] Unhandled non-standard task exception");
		}

		++tasksExecuted;
	}

	if (tasksExecuted < readyTasks.size()) {
		std::scoped_lock lock(mutex);
		for (size_t i = tasksExecuted; i < readyTasks.size(); ++i) {
			sendInbox.push_back(std::move(readyTasks[i]));
		}
	}
}

void TaskReactor::waitForWork()
{
#ifdef STATS_ENABLED
	const auto waitStart = std::chrono::steady_clock::now();
#endif
	auto wakePredicate = [this]() {
		return threadState.load(std::memory_order_acquire) != THREAD_STATE_RUNNING || !sendInbox.empty() ||
		       !scheduleInbox.empty() || !cancelInbox.empty();
	};

	std::unique_lock lock(mutex);
	if (!wakePredicate()) {
		if (taskHeap.empty()) {
			conditionVariable.wait(lock, wakePredicate);
		} else {
			conditionVariable.wait_until(lock, taskHeap.front().fireAt, wakePredicate);
		}
	}
	lock.unlock();

#ifdef STATS_ENABLED
	if (g_stats.isEnabled() && g_stats.isRunning()) {
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
		    std::chrono::steady_clock::now() - waitStart).count();
		g_stats.addDispatcherWaitTime(0, elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0);
	}
#endif
}
