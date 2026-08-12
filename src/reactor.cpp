// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "reactor.h"

#include "configmanager.h"
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

std::string taskLabel(const std::string& description, const std::string& origin)
{
	if (description.empty()) {
		return origin.empty() ? "unknown" : origin;
	}
	if (origin.empty()) {
		return description;
	}
	return fmt::format("{} @ {}", description, origin);
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

bool TaskReactor::send(ReactorCallback&& callback, std::string description, std::string origin)
{
	if (!callback || threadState.load(std::memory_order_acquire) != THREAD_STATE_RUNNING) {
		return false;
	}

	const auto now = std::chrono::steady_clock::now();
	Task task{
	    .fireAt = now,
	    .deadline = distantFuture(),
	    .sequence = nextSequence.fetch_add(1, std::memory_order_relaxed),
	    .description = std::move(description),
	    .origin = std::move(origin),
	    .function = std::move(callback),
	};

	{
		std::scoped_lock lock(mutex);
		if (maxInboxSize > 0 && sendInbox.size() >= maxInboxSize) {
			g_performanceMetrics.recordTaskDropped();
			LOG_WARN("[TaskReactor] sendInbox overflow ({}), dropping task", sendInbox.size());
			return false;
		}
		sendInbox.push_back(std::move(task));
	}

	conditionVariable.notify_one();
	return true;
}

bool TaskReactor::send(std::chrono::milliseconds expirationTime, ReactorCallback&& callback, std::string description,
                       std::string origin)
{
	if (!callback || threadState.load(std::memory_order_acquire) != THREAD_STATE_RUNNING) {
		return false;
	}

	const auto now = std::chrono::steady_clock::now();
	Task task{
	    .fireAt = now,
	    .deadline = now + expirationTime,
	    .sequence = nextSequence.fetch_add(1, std::memory_order_relaxed),
	    .description = std::move(description),
	    .origin = std::move(origin),
	    .function = std::move(callback),
	};

	{
		std::scoped_lock lock(mutex);
		if (maxInboxSize > 0 && sendInbox.size() >= maxInboxSize) {
			g_performanceMetrics.recordTaskDropped();
			LOG_WARN("[TaskReactor] sendInbox overflow ({}), dropping timed task", sendInbox.size());
			return false;
		}
		sendInbox.push_back(std::move(task));
	}

	conditionVariable.notify_one();
	return true;
}

bool TaskReactor::send(uint32_t expirationTime, ReactorCallback&& callback, std::string description, std::string origin)
{
	if (expirationTime == 0) {
		return send(std::move(callback), std::move(description), std::move(origin));
	}

	return send(std::chrono::milliseconds(expirationTime), std::move(callback), std::move(description),
	            std::move(origin));
}

uint32_t TaskReactor::schedule(std::chrono::milliseconds delay, ReactorCallback&& callback, std::string description,
                               std::string origin)
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
	    .description = std::move(description),
	    .origin = std::move(origin),
	    .function = std::move(callback),
	};

	{
		std::scoped_lock lock(mutex);
		if (maxInboxSize > 0 && scheduleInbox.size() >= maxInboxSize) {
			g_performanceMetrics.recordTaskDropped();
			LOG_WARN("[TaskReactor] scheduleInbox overflow ({}), dropping scheduled task", scheduleInbox.size());
			return 0;
		}
		scheduleInbox.push_back(std::move(task));
	}

	conditionVariable.notify_one();
	return identifier;
}

uint32_t TaskReactor::schedule(uint32_t delay, ReactorCallback&& callback, std::string description, std::string origin)
{
	return schedule(std::chrono::milliseconds(delay), std::move(callback), std::move(description), std::move(origin));
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
#ifndef NDEBUG
	struct SingleDriverGuard
	{
		std::atomic<int>& depth;
		explicit SingleDriverGuard(std::atomic<int>& depth) : depth{depth}
		{
			assert(depth.fetch_add(1, std::memory_order_acq_rel) == 0 &&
			       "TaskReactor::runOnce() must be driven by one thread at a time");
		}
		~SingleDriverGuard() { depth.fetch_sub(1, std::memory_order_acq_rel); }
	} singleDriverGuard{runOnceDepth};
#endif

	PerformanceScope cycleScope(PerformanceMetric::ReactorCycle);
	std::vector<Task> readyTasks;
	readyTasks.reserve(128);

	{
		PerformanceScope scope(PerformanceMetric::ReactorDrainInbox);
		drainInbox(readyTasks);
	}
	{
		PerformanceScope scope(PerformanceMetric::ReactorDrainReady);
		drainReadyTasks(readyTasks);
	}
	{
		PerformanceScope scope(PerformanceMetric::ReactorCallbacks);
		executeReadyTasks(readyTasks);
	}
	{
		std::scoped_lock lock(mutex);
		g_performanceMetrics.recordQueueSize(sendInbox.size() + scheduleInbox.size() + cancelInbox.size() + taskHeap.size());
	}
	g_performanceMetrics.maybeReport();
}

void TaskReactor::shutdown() noexcept
{
	// The state has to change under the mutex. waitForWork() evaluates its predicate
	// holding the lock and only then blocks; a store from outside can land in that
	// window, and notify_all() reaches nobody because the waiter is not registered yet.
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
		// Scheduled tasks deferred by the fairness or time-budget limit come back
		// through sendInbox, so this path must honour cancellations too — otherwise
		// stopEvent() silently does nothing for any task that got deferred and the
		// callback runs after the caller retired it. Plain send() tasks carry
		// identifier 0 and are never cancellable.
		if (task.identifier != 0 && cancelled.erase(task.identifier) > 0) {
			activeIdentifiers.erase(task.identifier);
			continue;
		}

		if (!task.hasExpired(now)) {
			readyTasks.push_back(std::move(task));
		} else {
			activeIdentifiers.erase(task.identifier);
			g_performanceMetrics.recordTaskExpired();
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

		if (cancelled.erase(readyTask.identifier) > 0 || readyTask.hasExpired(now)) {
			activeIdentifiers.erase(readyTask.identifier);
			if (readyTask.hasExpired(now)) {
				g_performanceMetrics.recordTaskExpired();
			}
			continue;
		}

		// The identifier deliberately stays active until the task actually runs.
		// Retiring it here would make a later cancel() a no-op, because cancel()
		// only records cancellations for identifiers still in activeIdentifiers.
		readyTasks.push_back(std::move(readyTask));
	}
}

void TaskReactor::executeReadyTasks(std::vector<Task>& readyTasks)
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

	const auto cycleStart = std::chrono::steady_clock::now();
	uint32_t tasksExecuted = 0;
	std::chrono::steady_clock::duration slowestDuration{};
	std::optional<size_t> slowestTaskIndex;

	for (size_t index = 0; index < readyTasks.size(); ++index) {
		auto& task = readyTasks[index];
		if (!task.function) {
			++tasksExecuted;
			continue;
		}

		if (maxTasksPerCycle > 0 && tasksExecuted >= maxTasksPerCycle) {
			LOG_WARN("[TaskReactor] fairness limit reached ({} tasks/cycle), deferring {} tasks; next: {}",
			         maxTasksPerCycle, readyTasks.size() - tasksExecuted, taskLabel(task.description, task.origin));
			break;
		}

		// Past every deferral point, so this task is committed to running now.
		// Deferred tasks keep their identifier registered precisely so a cancel()
		// that landed while they waited is still honoured here; retire it now and
		// drop the task if it was cancelled. tasksExecuted doubles as the index of
		// the first unprocessed task in the deferral loop below, so a skipped task
		// still has to advance it.
		if (task.identifier != 0) {
			activeIdentifiers.erase(task.identifier);
			if (cancelled.erase(task.identifier) > 0) {
				++tasksExecuted;
				continue;
			}
		}

		const auto taskStart = std::chrono::steady_clock::now();
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
			LOG_ERROR("[TaskReactor] Unhandled task exception in {}: {}", taskLabel(task.description, task.origin),
			          exception.what());
		} catch (...) {
			LOG_ERROR("[TaskReactor] Unhandled non-standard task exception in {}",
			          taskLabel(task.description, task.origin));
		}

		++tasksExecuted;
		const auto taskEnd = std::chrono::steady_clock::now();
		const auto taskDuration = taskEnd - taskStart;
		if (g_performanceMetrics.isEnabled()) {
			const auto taskNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(taskDuration).count();
			g_performanceMetrics.recordReactorCallbackSource(
				taskNanoseconds > 0 ? static_cast<uint64_t>(taskNanoseconds) : 0, task.description, task.origin);
		}
		if (taskDuration > slowestDuration) {
			slowestDuration = taskDuration;
			slowestTaskIndex = index;
		}

		if (timeBudget.count() > 0 && taskEnd - cycleStart >= timeBudget) {
			if (getBoolean(ConfigManager::SLOW_TASK_WARNING)) {
				const auto cycleMicros =
				    std::chrono::duration_cast<std::chrono::microseconds>(taskEnd - cycleStart).count();
				const auto slowestMicros =
				    std::chrono::duration_cast<std::chrono::microseconds>(slowestDuration).count();
				LOG_REACTOR(
				    "time budget exceeded (budget={} ms, cycle={:.3f} ms, executed={}), "
				    "deferring {} tasks; slowest: {} ({:.3f} ms); last: {} ({:.3f} ms)",
				    timeBudget.count(), cycleMicros / 1000.0, tasksExecuted, readyTasks.size() - tasksExecuted,
				    slowestTaskIndex ? taskLabel(readyTasks[*slowestTaskIndex].description,
				                                readyTasks[*slowestTaskIndex].origin) : "unknown",
				    slowestMicros / 1000.0, taskLabel(task.description, task.origin),
				    std::chrono::duration_cast<std::chrono::microseconds>(taskDuration).count() / 1000.0);
			}
			break;
		}
	}

	if (tasksExecuted < readyTasks.size()) {
		g_performanceMetrics.recordTaskDeferred(readyTasks.size() - tasksExecuted);
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
