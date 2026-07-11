// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// Modern C++20 Dispatcher with practical improvements

#include "otpch.h"

#include "tasks.h"

#include "game.h"
#include "logger.h"
#include "configmanager.h"
#include "tools.h"

extern Game g_game;

std::unique_ptr<Task> createTaskWithStats(TaskFunc&& f, const std::string& description, const std::string& extraDescription)
{
	if (g_stats.isEnabled()) {
		return std::make_unique<Task>(std::move(f), description, extraDescription);
	}
	return std::make_unique<Task>(std::move(f), "", "");
}

std::unique_ptr<Task> createTaskWithStats(uint32_t expiration, TaskFunc&& f, const std::string& description, const std::string& extraDescription)
{
	if (g_stats.isEnabled()) {
		return std::make_unique<Task>(expiration, std::move(f), description, extraDescription);
	}
	return std::make_unique<Task>(expiration, std::move(f), "", "");
}

void Dispatcher::threadMain()
{
    // Capture thread ID for isDispatcherThread() checks
    threadId = std::this_thread::get_id();
    UPDATE_OTSYS_TIME();

    std::vector<std::unique_ptr<Task>> tmpTaskList;
    tmpTaskList.reserve(128);

#ifdef STATS_ENABLED
    std::chrono::steady_clock::time_point waitStart;
#endif

    while (getState() != THREAD_STATE_TERMINATED) {
#ifdef STATS_ENABLED
        const bool statsEnabled = g_stats.isEnabled();
        if (statsEnabled) {
            waitStart = std::chrono::steady_clock::now();
        }
        taskSignal.acquire();
        if (statsEnabled) {
            const auto waitElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - waitStart
            ).count();
            g_stats.addDispatcherWaitTime(static_cast<std::size_t>(dispatcherId),
                                          waitElapsed > 0 ? static_cast<uint64_t>(waitElapsed) : 0);
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

        UPDATE_OTSYS_TIME();

        // Critical section: move tasks to the temporary list
        {
            std::scoped_lock lockGuard(taskLock);
            if (!taskList.empty()) {
                tmpTaskList.swap(taskList);
            }
        }

        // Process all available tasks
        for (auto& task : tmpTaskList) {
            UPDATE_OTSYS_TIME();
#if defined(STATS_ENABLED) || defined(SLOW_TASK_DETECTION)
            auto taskStart = std::chrono::steady_clock::now();
#endif
#ifdef STATS_ENABLED
            bool executed = false;
#endif
            if (!task->hasExpired()) {
                ++dispatcherCycle;
                ++totalTasksProcessed;
                (*task)();
#ifdef STATS_ENABLED
                executed = true;
#endif

#ifdef SLOW_TASK_DETECTION
                // Slow task detection (disabled in production with -DENABLE_SLOW_TASK_DETECTION=OFF)
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - taskStart
                ).count();
                const uint64_t elapsedNs = elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0;

                if (elapsedNs > SLOW_TASK_THRESHOLD_NS && !task->skipSlowDetection) {
                    ++slowTaskCount;
                    if (getBoolean(ConfigManager::SLOW_TASK_WARNING)) {
                        auto elapsedMs = elapsedNs / 1'000'000;
                        // Log slow task warning (optional, can be disabled)
                        if (!task->description.empty()) {
                            LOG_WARN(">> Slow task detected: {}ms [{}] {}", elapsedMs, task->description, task->extraDescription);
                        } else {
                            LOG_WARN(">> Slow task detected: {}ms [unknown]", elapsedMs);
                        }
                    }
                }
#endif
            }

#ifdef STATS_ENABLED
            if (executed && g_stats.isEnabled() && task->trackInStats) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - taskStart
                ).count();
                const uint64_t executionTime = elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0;
                g_stats.addDispatcherStat(static_cast<std::size_t>(dispatcherId),
                                          std::make_unique<Stat>(executionTime, task->description, task->extraDescription));
            }
#endif
            task.reset();
        }
        tmpTaskList.clear();
    }

    tmpTaskList.clear();

    {
        std::scoped_lock lockGuard(taskLock);
        taskList.clear();
    }
}

void Dispatcher::addTask(std::unique_ptr<Task> task)
{
	bool do_signal = false;

	{
		std::scoped_lock lockGuard(taskLock);

		if (getState() == THREAD_STATE_RUNNING) {
			do_signal = taskList.empty();
			taskList.push_back(std::move(task));
		}
	}

	if (do_signal) {
		taskSignal.release();
	}
}

void Dispatcher::shutdown()
{
	auto task = createTaskWithStats([this]() { setState(THREAD_STATE_TERMINATED); }, "Dispatcher::shutdown", "");

    task->trackInStats = false; // sentinel must always be freed by threadMain

	{
		std::scoped_lock lockGuard(taskLock);
		taskList.push_back(std::move(task));
	}

	taskSignal.release();
}
