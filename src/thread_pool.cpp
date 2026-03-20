// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// ThreadPool - C++20 thread pool for async task execution

#include "otpch.h"

#include "thread_pool.h"

#include "logger.h"

ThreadPool g_threadPool;

static constexpr uint32_t DEFAULT_MIN_THREADS = 4;

void ThreadPool::start(uint32_t requestedThreads)
{
	if (requestedThreads == 0) {
		threadCount = std::max<uint32_t>(std::thread::hardware_concurrency(), DEFAULT_MIN_THREADS);
	} else {
		threadCount = std::max<uint32_t>(requestedThreads, 1);
	}

	workers.reserve(threadCount);
	for (uint32_t i = 0; i < threadCount; ++i) {
		workers.emplace_back([this]() { workerMain(); });
	}

	LOG_INFO(fmt::format(">> {}: Running with {} threads.", fmt::format(fg(fmt::color::cyan), "ThreadPool"),
	                     fmt::format(fg(fmt::color::lime_green), "{}", threadCount)));
}

void ThreadPool::shutdown()
{
	{
		std::scoped_lock lock(queueMutex);
		if (stopped.exchange(true)) {
			return; // already stopped
		}
	}

	LOG_INFO(fmt::format(">> {}: {}", fmt::format(fg(fmt::color::cyan), "ThreadPool"),
	                     fmt::format(fg(fmt::color::yellow), "Shutting down...")));
	condition.notify_all();

	for (auto& worker : workers) {
		if (worker.joinable()) {
			worker.join();
		}
	}
	workers.clear();
	LOG_INFO(fmt::format(">> {}: {}", fmt::format(fg(fmt::color::cyan), "ThreadPool"),
	                     fmt::format(fg(fmt::color::lime_green), "All {} workers stopped.", threadCount)));
}

ThreadPool::~ThreadPool()
{
	if (!stopped.load()) {
		shutdown();
	}
}

void ThreadPool::detach_task(std::function<void()>&& task)
{
	{
		std::scoped_lock lock(queueMutex);
		if (stopped) {
			return;
		}
		taskQueue.emplace(std::move(task));
	}
	condition.notify_one();
}

void ThreadPool::workerMain()
{
	while (true) {
		std::function<void()> task;

		{
			std::unique_lock lock(queueMutex);
			condition.wait(lock, [this]() { return stopped.load(std::memory_order_relaxed) || !taskQueue.empty(); });

			if (stopped.load(std::memory_order_relaxed) && taskQueue.empty()) {
				return;
			}

			task = std::move(taskQueue.front());
			taskQueue.pop();
		}

		try {
			task();
		} catch (const std::exception& e) {
			LOG_ERROR(fmt::format("ThreadPool: Exception in worker: {}", e.what()));
		} catch (...) {
			LOG_ERROR("ThreadPool: Unknown exception in worker");
		}
	}
}
