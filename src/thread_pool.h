// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// ThreadPool - C++20 thread pool for async task execution

#ifndef FS_THREAD_POOL_H
#define FS_THREAD_POOL_H

#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <condition_variable>
#include <atomic>

class ThreadPool
{
public:
	ThreadPool() = default;
	~ThreadPool();

	// Non-copyable
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	/**
	 * @brief Start the thread pool with the given number of threads.
	 * If threadCount is 0, uses max(hardware_concurrency, 4).
	 */
	void start(uint32_t threadCount = 0);

	/**
	 * @brief Graceful shutdown - finishes pending tasks, then stops all threads.
	 */
	void shutdown();

	/**
	 * @brief Submit a fire-and-forget task to the pool.
	 * Thread-safe - can be called from any thread.
	 */
	void detach_task(std::function<void()>&& task);

	/**
	 * @brief Submit a task and get a future for its result.
	 * Thread-safe - can be called from any thread.
	 *
	 * @tparam F Callable type
	 * @param f Callable to execute
	 * @return std::future with the result of the callable
	 */
	template <typename F>
	auto submit_task(F&& f) -> std::future<decltype(f())>
	{
		using ReturnType = decltype(f());
		auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<F>(f));
		auto future = task->get_future();

		{
			std::scoped_lock lock(queueMutex);
			if (stopped) {
				throw std::runtime_error("ThreadPool: cannot submit task after shutdown");
			}
			taskQueue.emplace([task]() { (*task)(); });
		}
		condition.notify_one();

		return future;
	}

	/**
	 * @brief Get the number of worker threads.
	 */
	[[nodiscard]] uint32_t get_thread_count() const noexcept { return threadCount; }

	/**
	 * @brief Check if the pool has been stopped.
	 */
	[[nodiscard]] bool isStopped() const noexcept { return stopped.load(std::memory_order_relaxed); }

private:
	void workerMain();

	std::vector<std::jthread> workers;
	std::queue<std::function<void()>> taskQueue;

	std::mutex queueMutex;
	std::condition_variable condition;
	std::atomic<bool> stopped{false};
	uint32_t threadCount = 0;
};

extern ThreadPool g_threadPool;

#endif // FS_THREAD_POOL_H
