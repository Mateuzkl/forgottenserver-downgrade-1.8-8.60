// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// Modern C++20 Dispatcher with practical improvements

#include "otpch.h"

#include "tasks.h"

#include "game.h"
#include "logger.h"

extern Game g_game;

Task* createTaskWithStats(TaskFunc&& f, const std::string& description, const std::string& extraDescription)
{
	if (g_stats.isEnabled()) {
		return new Task(std::move(f), description, extraDescription);
	}
	return new Task(std::move(f), "", "");
}

Task* createTaskWithStats(uint32_t expiration, TaskFunc&& f, const std::string& description,
                          const std::string& extraDescription)
{
	if (g_stats.isEnabled()) {
		return new Task(expiration, std::move(f), description, extraDescription);
	}
	return new Task(expiration, std::move(f), "", "");
}

void Dispatcher::threadMain()
{
	// Capture thread ID for isDispatcherThread() checks
	threadId = std::this_thread::get_id();

	std::vector<Task*> tmpTaskList;
	tmpTaskList.reserve(128);

#ifdef STATS_ENABLED
	std::chrono::high_resolution_clock::time_point time_point;
#endif

	while (getState() != THREAD_STATE_TERMINATED) {
#ifdef STATS_ENABLED
		if (g_stats.isEnabled()) {
			time_point = std::chrono::high_resolution_clock::now();
		}
		taskSignal.acquire();
		if (g_stats.isEnabled()) {
			g_stats.dispatcherWaitTime(dispatcherId) += std::chrono::duration_cast<std::chrono::nanoseconds>(
			                                                std::chrono::high_resolution_clock::now() - time_point)
			                                                .count();
		}
#else
		taskSignal.acquire();
#endif

		if (getState() == THREAD_STATE_TERMINATED) {
			break;
		}

		// Drain all pending signals to coalesce multiple wakeups into one pass
		while (taskSignal.try_acquire()) {
			// consume extra signals
		}

		// Critical section: move tasks to the temporary list
		{
			std::lock_guard<std::mutex> lockGuard(taskLock);
			if (!taskList.empty()) {
				tmpTaskList.swap(taskList);
			}
		}

		// Process all available tasks
		for (Task* task : tmpTaskList) {
#if defined(STATS_ENABLED) || defined(SLOW_TASK_DETECTION)
			auto taskStart = std::chrono::high_resolution_clock::now();
#endif

#ifdef STATS_ENABLED
			if (g_stats.isEnabled()) {
				time_point = taskStart;
			}
#endif
			if (!task->hasExpired()) {
				++dispatcherCycle;
				++totalTasksProcessed;
				(*task)();

#ifdef SLOW_TASK_DETECTION
				// Slow task detection (disabled in production with -DENABLE_SLOW_TASK_DETECTION=OFF)
				auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
				                   std::chrono::high_resolution_clock::now() - taskStart)
				                   .count();

				if (static_cast<uint64_t>(elapsed) > SLOW_TASK_THRESHOLD_NS) {
					++slowTaskCount;
					auto elapsedMs = elapsed / 1'000'000;
					if (!task->description.empty()) {
						LOG_WARN(">> Slow task detected: {}ms [{}] {}", elapsedMs, task->description,
						         task->extraDescription);
					} else {
						LOG_WARN(">> Slow task detected: {}ms [unknown]", elapsedMs);
					}
				}
#endif
			}

#ifdef STATS_ENABLED
			if (g_stats.isEnabled()) {
				task->executionTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
				                          std::chrono::high_resolution_clock::now() - time_point)
				                          .count();
				g_stats.addDispatcherTask(dispatcherId, task);
			} else {
				delete task;
			}
#else
			delete task;
#endif
		}
		tmpTaskList.clear();
	}
}

void Dispatcher::addTask(Task* task)
{
	bool do_signal = false;

	{
		std::lock_guard<std::mutex> lockGuard(taskLock);

		if (getState() == THREAD_STATE_RUNNING) {
			do_signal = taskList.empty();
			taskList.push_back(task);
		} else {
			delete task;
		}
	}

	if (do_signal) {
		taskSignal.release();
	}
}

void Dispatcher::shutdown()
{
	Task* task = createTaskWithStats([this]() { setState(THREAD_STATE_TERMINATED); }, "Dispatcher::shutdown", "");

	{
		std::lock_guard<std::mutex> lockGuard(taskLock);
		taskList.push_back(task);
	}

	taskSignal.release();
}
