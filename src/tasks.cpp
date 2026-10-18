// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "tasks.h"

#include "configmanager.h"
#include "logger.h"
#include "performance_metrics.h"
#include "tools.h"

bool shouldCaptureTaskMetadata() noexcept
{
	if (g_performanceMetrics.isEnabled() || getBoolean(ConfigManager::SLOW_TASK_WARNING)) {
		return true;
	}
#ifdef STATS_ENABLED
	return g_stats.isEnabled();
#else
	return false;
#endif
}

Task::Task(TaskFunc&& f, std::string_view description, std::string_view extraDescription) :
    description(description), extraDescription(extraDescription), func(std::move(f))
{}

Task::Task(uint32_t ms, TaskFunc&& f, std::string_view description, std::string_view extraDescription) :
    description(description),
    extraDescription(extraDescription),
    expiration(std::chrono::steady_clock::now() + std::chrono::milliseconds(ms)),
    func(std::move(f))
{}

bool Task::hasExpired() const
{
	if (expiration == SYSTEM_TIME_ZERO) {
		return false;
	}
	return expiration < std::chrono::steady_clock::now();
}

std::unique_ptr<Task> createTaskWithStats(TaskFunc&& f, std::string_view description, std::string_view extraDescription)
{
	if (shouldCaptureTaskMetadata()) {
		return std::make_unique<Task>(std::move(f), description, extraDescription);
	}
	return std::make_unique<Task>(std::move(f), "", "");
}

std::unique_ptr<Task> createTaskWithStats(uint32_t expiration, TaskFunc&& f, std::string_view description,
                                          std::string_view extraDescription)
{
	if (shouldCaptureTaskMetadata()) {
		return std::make_unique<Task>(expiration, std::move(f), description, extraDescription);
	}
	return std::make_unique<Task>(expiration, std::move(f), "", "");
}

Dispatcher::Dispatcher()
{
	static int id = 0;
	dispatcherId = id++;
}

void Dispatcher::start() noexcept
{
	state.store(THREAD_STATE_RUNNING, std::memory_order_release);
	g_reactor.start();
}

void Dispatcher::stop() noexcept { state.store(THREAD_STATE_CLOSING, std::memory_order_release); }

void Dispatcher::shutdown() noexcept
{
	state.store(THREAD_STATE_TERMINATED, std::memory_order_release);
	g_reactor.shutdown();
}

bool Dispatcher::addTask(std::unique_ptr<Task>&& task)
{
	if (!task || state.load(std::memory_order_acquire) != THREAD_STATE_RUNNING) {
		return false;
	}

	std::string description = task->description;
	std::string origin = task->extraDescription;
	const auto expiration = task->getExpiration();
	auto callback = [this, task = std::move(task)]() mutable { executeTask(std::move(task)); };
	if (expiration != SYSTEM_TIME_ZERO) {
		const auto now = std::chrono::steady_clock::now();
		if (expiration <= now) {
			return false;
		}
		const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(expiration - now);
		return g_reactor.send(remaining, std::move(callback), std::move(description), std::move(origin));
	}

	return g_reactor.send(std::move(callback), std::move(description), std::move(origin));
}

bool Dispatcher::addTask(TaskFunc&& f, const std::source_location location)
{
	if (shouldCaptureTaskMetadata()) {
		return addTask(createTaskWithStats(std::move(f), location.function_name(),
		                                   std::string(location.file_name()) + ":" + std::to_string(location.line())));
	}
	return addTask(std::make_unique<Task>(std::move(f), "", ""));
}

bool Dispatcher::addTask(uint32_t expiration, TaskFunc&& f, const std::source_location location)
{
	if (shouldCaptureTaskMetadata()) {
		return addTask(createTaskWithStats(expiration, std::move(f), location.function_name(),
		                                   std::string(location.file_name()) + ":" + std::to_string(location.line())));
	}
	return addTask(std::make_unique<Task>(expiration, std::move(f), "", ""));
}

void Dispatcher::executeTask(std::unique_ptr<Task> task)
{
	if (!task || state.load(std::memory_order_acquire) != THREAD_STATE_RUNNING || task->hasExpired()) {
		return;
	}

	UPDATE_OTSYS_TIME();

	const bool slowTaskWarning = getBoolean(ConfigManager::SLOW_TASK_WARNING);
	uint64_t slowThresholdNs = SLOW_TASK_THRESHOLD_NS;
	if (slowTaskWarning) {
		const int64_t reactorBudgetMs = getInteger(ConfigManager::REACTOR_TIME_BUDGET_MS);
		if (reactorBudgetMs > 0) {
			slowThresholdNs = static_cast<uint64_t>(reactorBudgetMs) * 1'000'000;
		}
	}
#ifdef STATS_ENABLED
	const bool shouldMeasure = true;
#else
	const bool shouldMeasure = slowTaskWarning;
#endif
	const auto taskStart = shouldMeasure ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

	dispatcherCycle.fetch_add(1, std::memory_order_relaxed);
	totalTasksProcessed.fetch_add(1, std::memory_order_relaxed);
	(*task)();

	if (shouldMeasure) {
		const auto elapsed =
		    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - taskStart).count();
		const uint64_t elapsedNs = elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0;

		if (slowTaskWarning && elapsedNs > slowThresholdNs && !task->skipSlowDetection) {
			slowTaskCount.fetch_add(1, std::memory_order_relaxed);
			const auto elapsedMs = elapsedNs / 1'000'000;
			if (!task->description.empty()) {
				LOG_WARN(">> Slow task detected: {}ms [{}] {}", elapsedMs, task->description, task->extraDescription);
			} else {
				LOG_WARN(">> Slow task detected: {}ms [unknown]", elapsedMs);
			}
		}

#ifdef STATS_ENABLED
		if (g_stats.isEnabled() && g_stats.isRunning() && task->trackInStats) {
			g_stats.addDispatcherStat(0, std::make_unique<Stat>(elapsedNs, task->description, task->extraDescription));
		}
#endif
	}
}
