// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "reactor.h"

#include "logger.h"
#include "performance_metrics.h"
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
		if (maxInboxSize > 0 && sendInbox.size() + scheduleInbox.size() >= maxInboxSize) {
			g_performanceMetrics.recordTaskDropped();
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
		if (maxInboxSize > 0 && sendInbox.size() + scheduleInbox.size() >= maxInboxSize) {
			g_performanceMetrics.recordTaskDropped();
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

	uint32_t identifier = 0;
	{
		std::scoped_lock lock(mutex);
		if (maxInboxSize > 0 && sendInbox.size() + scheduleInbox.size() >= maxInboxSize) {
			g_performanceMetrics.recordTaskDropped();
			LOG_WARN("[TaskReactor] scheduleInbox overflow ({}), dropping scheduled task", scheduleInbox.size());
			return 0;
		}
		do {
			identifier = nextIdentifier.fetch_add(1, std::memory_order_relaxed) + 1;
		} while (identifier == 0 || activeIdentifiers.contains(identifier));

		Task task{
		    .fireAt = std::chrono::steady_clock::now() + delay,
		    .deadline = distantFuture(),
		    .identifier = identifier,
		    .sequence = nextSequence.fetch_add(1, std::memory_order_relaxed),
		    .function = std::move(callback),
		};
		activeIdentifiers.insert(identifier);
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
		if (activeIdentifiers.contains(taskIdentifier) && pendingCancellations.insert(taskIdentifier).second) {
			cancelInbox.push_back(taskIdentifier);
		}
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
	const auto cycleStart = std::chrono::steady_clock::now();
	PerformanceScope cycleScope(PerformanceMetric::ReactorCycle);
	std::vector<Task> readyTasks;
	readyTasks.reserve(128);

	{
		PerformanceScope scope(PerformanceMetric::ReactorDrainInbox);
		drainInbox(readyTasks, cycleStart);
	}
	{
		PerformanceScope scope(PerformanceMetric::ReactorDrainReady);
		drainReadyTasks(readyTasks, cycleStart);
	}
	{
		PerformanceScope scope(PerformanceMetric::ReactorCallbacks);
		executeReadyTasks(readyTasks, cycleStart);
	}
	{
		std::scoped_lock lock(mutex);
		g_performanceMetrics.recordQueueSize(sendInbox.size() + scheduleInbox.size() + cancelInbox.size() + taskHeap.size());
	}
	g_performanceMetrics.maybeReport();
}

void TaskReactor::shutdown() noexcept
{
	{
		std::scoped_lock lock(mutex);
		threadState.store(THREAD_STATE_TERMINATED, std::memory_order_release);
	}
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

bool TaskReactor::timeBudgetReached(std::chrono::steady_clock::time_point cycleStart) const noexcept
{
	return timeBudget.count() > 0 && std::chrono::steady_clock::now() - cycleStart >= timeBudget;
}

size_t TaskReactor::preprocessingLimit() const noexcept
{
	if (maxTasksPerCycle > 0) {
		return maxTasksPerCycle;
	}
	return timeBudget.count() > 0 ? 1024 : std::numeric_limits<size_t>::max();
}

void TaskReactor::drainInbox(std::vector<Task>& readyTasks, std::chrono::steady_clock::time_point cycleStart)
{
	std::deque<Task> sentTasks;
	std::deque<Task> scheduledTasks;
	std::deque<uint32_t> cancellations;
	const size_t limit = preprocessingLimit();
	size_t drained = 0;

	{
		std::scoped_lock lock(mutex);
		while (drained < limit && !cancelInbox.empty()) {
			cancellations.push_back(cancelInbox.front());
			cancelInbox.pop_front();
			++drained;
		}
		while (drained < limit && (!sendInbox.empty() || !scheduleInbox.empty())) {
			const bool takeScheduled = !scheduleInbox.empty() &&
				(sendInbox.empty() || scheduleInbox.front().sequence < sendInbox.front().sequence);
			if (takeScheduled) {
				scheduledTasks.push_back(std::move(scheduleInbox.front()));
				scheduleInbox.pop_front();
			} else {
				sentTasks.push_back(std::move(sendInbox.front()));
				sendInbox.pop_front();
			}
			++drained;
		}
	}

	for (auto& task : scheduledTasks) {
		taskHeap.push_back(std::move(task));
		std::push_heap(taskHeap.begin(), taskHeap.end(), taskComesAfter);
	}

	for (uint32_t identifier : cancellations) {
		std::scoped_lock lock(mutex);
		if (activeIdentifiers.contains(identifier)) {
			cancelled.insert(identifier);
		} else {
			pendingCancellations.erase(identifier);
		}
	}

	const auto now = std::chrono::steady_clock::now();
	for (auto& task : sentTasks) {
		if (timeBudgetReached(cycleStart)) {
			std::scoped_lock lock(mutex);
			for (auto it = sentTasks.rbegin(); it != sentTasks.rend(); ++it) {
				if (it->function) sendInbox.push_front(std::move(*it));
			}
			break;
		}
		if (!task.hasExpired(now)) {
			readyTasks.push_back(std::move(task));
		} else {
			task.function = nullptr;
			g_performanceMetrics.recordTaskExpired();
		}
	}
}

void TaskReactor::drainReadyTasks(std::vector<Task>& readyTasks, std::chrono::steady_clock::time_point cycleStart)
{
	const auto now = std::chrono::steady_clock::now();
	const size_t limit = maxTasksPerCycle > 0 ? maxTasksPerCycle : std::numeric_limits<size_t>::max();

	while (readyTasks.size() < limit && !taskHeap.empty() && taskHeap.front().fireAt <= now &&
	       !timeBudgetReached(cycleStart)) {
		std::pop_heap(taskHeap.begin(), taskHeap.end(), taskComesAfter);
		auto readyTask = std::move(taskHeap.back());
		taskHeap.pop_back();

		if (cancelled.erase(readyTask.identifier) > 0 || readyTask.hasExpired(now)) {
			std::scoped_lock lock(mutex);
			activeIdentifiers.erase(readyTask.identifier);
			pendingCancellations.erase(readyTask.identifier);
			if (readyTask.hasExpired(now)) {
				g_performanceMetrics.recordTaskExpired();
			}
			continue;
		}

		readyTasks.push_back(std::move(readyTask));
	}
}

void TaskReactor::executeReadyTasks(std::vector<Task>& readyTasks, std::chrono::steady_clock::time_point cycleStart)
{
	{
		PerformanceScope scope(PerformanceMetric::ReactorSort);
		std::sort(readyTasks.begin(), readyTasks.end(), [](const Task& lhs, const Task& rhs) {
			if (lhs.fireAt != rhs.fireAt) {
				return lhs.fireAt < rhs.fireAt;
			}
			return lhs.sequence < rhs.sequence;
		});
	}

	uint32_t tasksExecuted = 0;
	size_t nextTaskIndex = 0;

	for (; nextTaskIndex < readyTasks.size(); ++nextTaskIndex) {
		auto& task = readyTasks[nextTaskIndex];
		if (!task.function) {
			continue;
		}

		if (maxTasksPerCycle > 0 && tasksExecuted >= maxTasksPerCycle) {
			LOG_WARN("[TaskReactor] fairness limit reached ({} tasks/cycle), deferring {} tasks",
			         maxTasksPerCycle, readyTasks.size() - nextTaskIndex);
			break;
		}

		if (timeBudgetReached(cycleStart)) {
			LOG_REACTOR("time budget exceeded ({} ms), deferring {} tasks",
			            timeBudget.count(), readyTasks.size() - nextTaskIndex);
			break;
		}
		if (task.identifier != 0) {
			std::scoped_lock lock(mutex);
			activeIdentifiers.erase(task.identifier);
			pendingCancellations.erase(task.identifier);
		}

		try {
			if (g_performanceMetrics.isEnabled()) {
				const auto callbackStart = std::chrono::steady_clock::now();
				const auto queueLatency = callbackStart > task.fireAt ? callbackStart - task.fireAt : std::chrono::steady_clock::duration::zero();
				g_performanceMetrics.record(PerformanceMetric::ReactorQueueLatency,
					std::chrono::duration_cast<std::chrono::nanoseconds>(queueLatency).count());
			}
			PerformanceScope callbackScope(PerformanceMetric::ReactorCallback);
			task.function();
		} catch (const std::exception& exception) {
			LOG_ERROR("[TaskReactor] Unhandled task exception: {}", exception.what());
		} catch (...) {
			LOG_ERROR("[TaskReactor] Unhandled non-standard task exception");
		}

		++tasksExecuted;
	}

	if (nextTaskIndex < readyTasks.size()) {
		g_performanceMetrics.recordTaskDeferred(readyTasks.size() - nextTaskIndex);
		std::scoped_lock lock(mutex);
		for (size_t i = readyTasks.size(); i-- > nextTaskIndex;) {
			auto& deferred = readyTasks[i];
			if (!deferred.function) {
				continue;
			}
			if (deferred.identifier != 0) {
				taskHeap.push_back(std::move(deferred));
				std::push_heap(taskHeap.begin(), taskHeap.end(), taskComesAfter);
			} else {
				sendInbox.push_front(std::move(deferred));
			}
		}
		conditionVariable.notify_one();
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
