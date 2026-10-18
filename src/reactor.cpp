// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "reactor.h"

#include "configmanager.h"
#include "logger.h"
#include "performance_metrics.h"
#include "stats.h"

#include <limits>

TaskReactor g_reactor;
thread_local const TaskReactor* TaskReactor::currentReactor = nullptr;

TaskReactor::TaskReactor() : maxTasksPerCycle(0), timeBudget(0), maxInboxSize(REACTOR_MAX_INBOX_SIZE)
{
	readyTasksBuffer.reserve(1024);
}

namespace {
auto distantFuture() noexcept { return std::chrono::steady_clock::time_point::max(); }

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

void TaskReactor::start() noexcept { threadState.store(THREAD_STATE_RUNNING, std::memory_order_release); }

bool TaskReactor::send(ReactorCallback&& callback, std::string description, std::string origin)
{
	if (!callback || threadState.load(std::memory_order_acquire) != THREAD_STATE_RUNNING) {
		return false;
	}

	Task task{
	    .fireAt = {},
	    .deadline = distantFuture(),
	    .sequence = 0,
	    .description = std::move(description),
	    .origin = std::move(origin),
	    .function = std::move(callback),
	};

	bool crossedSoftLimit = false;
	size_t pendingInbox = 0;
	{
		std::scoped_lock lock(mutex);
		task.fireAt = std::chrono::steady_clock::now();
		task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
		sendInbox.push_back(std::move(task));
		pendingInbox = sendInbox.size() + scheduleInbox.size() + cancelInbox.size();
		if (maxInboxSize > 0 && pendingInbox >= maxInboxSize && !inboxAboveSoftLimit) {
			inboxAboveSoftLimit = true;
			crossedSoftLimit = true;
		}
	}
	if (crossedSoftLimit) {
		LOG_WARN("[TaskReactor] inbound backlog reached soft limit ({} tasks); preserving critical work", pendingInbox);
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
	    .fireAt = {},
	    .deadline = now + expirationTime,
	    .sequence = 0,
	    .description = std::move(description),
	    .origin = std::move(origin),
	    .function = std::move(callback),
	};

	bool crossedLimit = false;
	bool rejected = false;
	size_t pendingTasks = 0;
	{
		std::scoped_lock lock(mutex);
		pendingTasks = pendingTaskCountUnsafe();
		if (maxInboxSize > 0 && pendingTasks >= maxInboxSize) {
			rejected = true;
		} else {
			task.fireAt = std::chrono::steady_clock::now();
			task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
			sendInbox.push_back(std::move(task));
			++pendingTasks;
		}
		if (maxInboxSize > 0 && pendingTasks >= maxInboxSize && !inboxAboveSoftLimit) {
			inboxAboveSoftLimit = true;
			crossedLimit = true;
		}
	}
	if (rejected) {
		g_performanceMetrics.recordTaskDropped();
		if (crossedLimit) {
			LOG_WARN("[TaskReactor] backlog reached {} tasks; rejecting expiring work while preserving critical work",
			         pendingTasks);
		}
		return false;
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

	uint32_t identifier = 0;
	Task task{
	    .fireAt = {},
	    .deadline = distantFuture(),
	    .sequence = 0,
	    .description = std::move(description),
	    .origin = std::move(origin),
	    .function = std::move(callback),
	};

	bool crossedSoftLimit = false;
	size_t pendingInbox = 0;
	{
		std::scoped_lock lock(mutex);
		if (activeIdentifiers.size() >= (std::numeric_limits<uint32_t>::max)()) {
			g_performanceMetrics.recordTaskDropped();
			LOG_WARN("[TaskReactor] task identifier space exhausted, dropping scheduled task");
			return 0;
		}

		// Reserve before publishing, including while bounded ingestion leaves the
		// task in scheduleInbox. A wrapped counter must skip every live owner.
		do {
			identifier = ++nextIdentifier;
		} while (identifier == 0 || activeIdentifiers.contains(identifier));
		activeIdentifiers.insert(identifier);
		task.identifier = identifier;
		try {
			task.fireAt = std::chrono::steady_clock::now() + delay;
			task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
			scheduleInbox.push_back(std::move(task));
			std::push_heap(scheduleInbox.begin(), scheduleInbox.end(), taskComesAfter);
		} catch (...) {
			activeIdentifiers.erase(identifier);
			throw;
		}
		pendingInbox = sendInbox.size() + scheduleInbox.size() + cancelInbox.size();
		if (maxInboxSize > 0 && pendingInbox >= maxInboxSize && !inboxAboveSoftLimit) {
			inboxAboveSoftLimit = true;
			crossedSoftLimit = true;
		}
	}
	if (crossedSoftLimit) {
		LOG_WARN("[TaskReactor] inbound backlog reached soft limit ({} tasks); preserving scheduled work",
		         pendingInbox);
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

	bool crossedSoftLimit = false;
	size_t pendingInbox = 0;
	{
		std::scoped_lock lock(mutex);
		if (!activeIdentifiers.contains(taskIdentifier) || !cancellationRequests.insert(taskIdentifier).second) {
			return;
		}
		try {
			cancelInbox.insert(taskIdentifier);
		} catch (...) {
			cancellationRequests.erase(taskIdentifier);
			throw;
		}
		pendingInbox = sendInbox.size() + scheduleInbox.size() + cancelInbox.size();
		if (maxInboxSize > 0 && pendingInbox >= maxInboxSize && !inboxAboveSoftLimit) {
			inboxAboveSoftLimit = true;
			crossedSoftLimit = true;
		}
	}
	if (crossedSoftLimit) {
		LOG_WARN("[TaskReactor] inbound backlog reached soft limit ({} tasks); preserving cancellation", pendingInbox);
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
	const auto cycleStart = std::chrono::steady_clock::now();
	readyTasksBuffer.clear();
	auto& readyTasks = readyTasksBuffer;

	{
		PerformanceScope scope(PerformanceMetric::ReactorDrainInbox);
		drainInbox();
	}
	{
		PerformanceScope scope(PerformanceMetric::ReactorDrainReady);
		drainReadyTasks(readyTasks);
	}
	{
		PerformanceScope scope(PerformanceMetric::ReactorCallbacks);
		executeReadyTasks(readyTasks, cycleStart);
	}
	{
		std::scoped_lock lock(mutex);
		g_performanceMetrics.recordQueueSize(sendInbox.size() + scheduleInbox.size() + cancelInbox.size() +
		                                     taskHeapSize.load(std::memory_order_relaxed));
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
			if (sendInbox.empty() && scheduleInbox.empty() && cancelInbox.empty() &&
			    taskHeapSize.load(std::memory_order_relaxed) == 0) {
				return;
			}
		}
		runOnce();
		std::this_thread::yield();
	}
	LOG_WARN("[TaskReactor] drain timed out after {} ms", REACTOR_DRAIN_TIMEOUT.count());
}

bool TaskReactor::hasPendingTasks() const
{
	std::scoped_lock lock(mutex);
	return !sendInbox.empty() || !scheduleInbox.empty() || !cancelInbox.empty() ||
	       taskHeapSize.load(std::memory_order_relaxed) != 0;
}

bool TaskReactor::canAcceptExpiringTask() const
{
	if (threadState.load(std::memory_order_acquire) != THREAD_STATE_RUNNING) {
		return false;
	}

	std::scoped_lock lock(mutex);
	return maxInboxSize == 0 || pendingTaskCountUnsafe() < maxInboxSize;
}

size_t TaskReactor::getPendingTaskCount() const
{
	std::scoped_lock lock(mutex);
	return pendingTaskCountUnsafe();
}

size_t TaskReactor::pendingTaskCountUnsafe() const noexcept
{
	return sendInbox.size() + scheduleInbox.size() + cancelInbox.size() + inboxTasksInFlight +
	       taskHeapSize.load(std::memory_order_relaxed);
}

bool TaskReactor::isReactorThread() const noexcept { return currentReactor == this; }

ThreadState TaskReactor::getState() const noexcept { return threadState.load(std::memory_order_acquire); }

bool TaskReactor::taskComesAfter(const Task& lhs, const Task& rhs) noexcept
{
	if (lhs.fireAt != rhs.fireAt) {
		return lhs.fireAt > rhs.fireAt;
	}
	return lhs.sequence > rhs.sequence;
}

void TaskReactor::drainInbox()
{
	auto& sentTasks = sentTasksBuffer;
	auto& scheduledTasks = scheduledTasksBuffer;
	auto& cancellations = cancellationsBuffer;

	{
		std::scoped_lock lock(mutex);
		const size_t drainLimit =
		    maxInboxDrainPerCycle > 0 ? maxInboxDrainPerCycle : std::numeric_limits<size_t>::max();
		sentTasks.reserve(std::min(drainLimit, sendInbox.size()));
		scheduledTasks.reserve(std::min(drainLimit, scheduleInbox.size()));
		cancellations.reserve(cancelInbox.size());

		size_t drained = 0;
		while (drained < drainLimit && (!sendInbox.empty() || !scheduleInbox.empty())) {
			if (sendInbox.empty() ||
			    (!scheduleInbox.empty() && taskComesAfter(sendInbox.front(), scheduleInbox.front()))) {
				std::pop_heap(scheduleInbox.begin(), scheduleInbox.end(), taskComesAfter);
				scheduledTasks.push_back(std::move(scheduleInbox.back()));
				scheduleInbox.pop_back();
			} else {
				sentTasks.push_back(std::move(sendInbox.front()));
				sendInbox.pop_front();
			}
			++drained;
		}
		// Cancellation is control traffic: drain it every cycle so a large task
		// burst cannot cause a stopped event to run before its cancellation.
		for (uint32_t identifier : cancelInbox) cancellations.push_back(identifier);
		cancelInbox.clear();
		inboxTasksInFlight = sentTasks.size() + scheduledTasks.size() + cancellations.size();

		undrainedTaskFrontier.reset();
		if (!sendInbox.empty() || !scheduleInbox.empty()) {
			const auto& next = sendInbox.empty() || (!scheduleInbox.empty() &&
			                                         taskComesAfter(sendInbox.front(), scheduleInbox.front()))
			                       ? scheduleInbox.front()
			                       : sendInbox.front();
			undrainedTaskFrontier.emplace(next.fireAt, next.sequence);
		}

		const size_t pendingTasks = pendingTaskCountUnsafe();
		if (maxInboxSize == 0 || pendingTasks < std::max<size_t>(1, maxInboxSize / 2)) {
			inboxAboveSoftLimit = false;
		}
	}

	const size_t previousHeapSize = taskHeap.size();
	const size_t incomingTasks = sentTasks.size() + scheduledTasks.size();
	if (incomingTasks > 0) {
		const size_t requiredCapacity = previousHeapSize + incomingTasks;
		if (requiredCapacity > taskHeap.capacity()) {
			// Reserve for the whole batch with geometric spare capacity, so
			// small later batches retain amortized vector growth.
			const size_t capacity = taskHeap.capacity();
			const size_t growth = std::min(capacity / 2, taskHeap.max_size() - capacity);
			taskHeap.reserve(std::max(requiredCapacity, capacity + growth));
		}
	}

	for (auto& task : scheduledTasks) {
		taskHeap.push_back(std::move(task));
	}
	for (auto& task : sentTasks) {
		taskHeap.push_back(std::move(task));
	}

	// A burst is heapified once in linear time. With only a few arrivals and a
	// large future-event heap, incremental insertion avoids rebuilding it all.
	if (incomingTasks > 0) {
		const bool rebuildHeap = previousHeapSize == 0 || incomingTasks >= 64 || incomingTasks * 4 >= previousHeapSize;
		if (rebuildHeap) {
			std::make_heap(taskHeap.begin(), taskHeap.end(), taskComesAfter);
		} else {
			for (size_t heapSize = previousHeapSize + 1; heapSize <= taskHeap.size(); ++heapSize) {
				std::push_heap(taskHeap.begin(), taskHeap.begin() + heapSize, taskComesAfter);
			}
		}
	}

	for (uint32_t identifier : cancellations) {
		// cancel() only queues live ids; retirement consumes queued cancellation
		// before releasing their reservation. This also covers undrained tasks.
		cancelled.insert(identifier);
	}
	{
		// Publication and removal of the in-flight count are one producer-visible
		// operation: admission must never observe neither representation.
		std::scoped_lock lock(mutex);
		taskHeapSize.store(taskHeap.size(), std::memory_order_relaxed);
		inboxTasksInFlight = 0;
	}

	// Retain only storage, never task captures. An exceptional unbounded control
	// burst must not leave arbitrarily large scratch buffers resident forever.
	cancellations.clear();
	scheduledTasks.clear();
	sentTasks.clear();
	if (cancellations.capacity() > REACTOR_MAX_RETAINED_SCRATCH) std::vector<uint32_t>{}.swap(cancellations);
	if (scheduledTasks.capacity() > REACTOR_MAX_RETAINED_SCRATCH) std::vector<Task>{}.swap(scheduledTasks);
	if (sentTasks.capacity() > REACTOR_MAX_RETAINED_SCRATCH) std::vector<Task>{}.swap(sentTasks);
}

void TaskReactor::drainReadyTasks(std::vector<Task>& readyTasks)
{
	const auto now = std::chrono::steady_clock::now();

	const size_t readyLimit = maxTasksPerCycle > 0 ? maxTasksPerCycle : std::numeric_limits<size_t>::max();
	size_t processed = 0;
	while (processed < readyLimit && !taskHeap.empty() && taskHeap.front().fireAt <= now) {
		if (undrainedTaskFrontier &&
		    *undrainedTaskFrontier < std::pair{taskHeap.front().fireAt, taskHeap.front().sequence}) {
			break;
		}
		std::pop_heap(taskHeap.begin(), taskHeap.end(), taskComesAfter);
		auto readyTask = std::move(taskHeap.back());
		taskHeap.pop_back();
		++processed; // Retired due entries consume the cycle limit too.

		if (cancelled.contains(readyTask.identifier) || readyTask.hasExpired(now)) {
			retireIdentifier(readyTask.identifier);
			if (readyTask.hasExpired(now)) {
				g_performanceMetrics.recordTaskExpired();
			}
			continue;
		}

		// Keep the identifier reserved until execution or discard, so cancellation
		// remains valid and a wrapped counter cannot assign it to another task.
		readyTasks.push_back(std::move(readyTask));
	}
	taskHeapSize.store(taskHeap.size(), std::memory_order_relaxed);
}

bool TaskReactor::consumeCancellation(uint32_t identifier)
{
	if (identifier == 0) {
		return false;
	}

	std::scoped_lock lock(mutex);
	const bool cancelledInBatch = cancelInbox.erase(identifier) > 0;
	const bool cancelledPreviously = cancelled.erase(identifier) > 0;
	const bool wasCancelled = cancelledInBatch || cancelledPreviously;
	if (wasCancelled) {
		cancellationRequests.erase(identifier);
		activeIdentifiers.erase(identifier);
	}
	return wasCancelled;
}

bool TaskReactor::retireIdentifier(uint32_t identifier)
{
	if (identifier == 0) {
		return false;
	}

	// Consume cancellations received after execution was committed before
	// releasing the reservation, so they cannot target a reused identifier.
	std::scoped_lock lock(mutex);
	const bool cancelledInBatch = cancelInbox.erase(identifier) > 0;
	const bool cancelledPreviously = cancelled.erase(identifier) > 0;
	cancellationRequests.erase(identifier);
	activeIdentifiers.erase(identifier);
	return cancelledInBatch || cancelledPreviously;
}

void TaskReactor::executeReadyTasks(std::vector<Task>& readyTasks, std::chrono::steady_clock::time_point cycleStart)
{
	uint32_t tasksExecuted = 0;
	std::chrono::steady_clock::duration slowestDuration{};
	std::optional<size_t> slowestTaskIndex;

	for (size_t index = 0; index < readyTasks.size(); ++index) {
		auto& task = readyTasks[index];
		if (!task.function) {
			retireIdentifier(task.identifier);
			++tasksExecuted;
			continue;
		}

		// Past every deferral point, so this task is committed to running now.
		// Honour cancellations that landed while it waited, but keep its identifier
		// reserved until the callback returns so a wrapped counter cannot reuse it.
		// tasksExecuted is also the index of the first task in the deferral loop.
		if (consumeCancellation(task.identifier)) {
			task.function = {};
			++tasksExecuted;
			continue;
		}

		const auto taskStart = std::chrono::steady_clock::now();
		try {
			if (g_performanceMetrics.isEnabled()) {
				const auto callbackStart = std::chrono::steady_clock::now();
				const auto queueLatency = callbackStart > task.fireAt ? callbackStart - task.fireAt
				                                                      : std::chrono::steady_clock::duration::zero();
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
		// Reusing readyTasksBuffer must not extend the lifetime of captures until
		// the next cycle. Network backlog tickets and shared ownership are expected
		// to be released as soon as the callback finishes.
		task.function = {};

		const auto taskEnd = std::chrono::steady_clock::now();
		retireIdentifier(task.identifier);
		++tasksExecuted;
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
				    slowestTaskIndex
				        ? taskLabel(readyTasks[*slowestTaskIndex].description, readyTasks[*slowestTaskIndex].origin)
				        : "unknown",
				    slowestMicros / 1000.0, taskLabel(task.description, task.origin),
				    std::chrono::duration_cast<std::chrono::microseconds>(taskDuration).count() / 1000.0);
			}
			break;
		}
	}

	if (tasksExecuted < readyTasks.size()) {
		g_performanceMetrics.recordTaskDeferred(readyTasks.size() - tasksExecuted);
		for (size_t i = tasksExecuted; i < readyTasks.size(); ++i) {
			taskHeap.push_back(std::move(readyTasks[i]));
			std::push_heap(taskHeap.begin(), taskHeap.end(), taskComesAfter);
		}
		taskHeapSize.store(taskHeap.size(), std::memory_order_relaxed);
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
		const auto elapsed =
		    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - waitStart).count();
		g_stats.addDispatcherWaitTime(0, elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0);
	}
#endif
}
