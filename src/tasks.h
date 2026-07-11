// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// Modern C++20 Dispatcher with practical improvements

#ifndef FS_TASKS_H
#define FS_TASKS_H

#include "thread_holder_base.h"
#include "thread_pool.h"
#include "stats.h"

using TaskFunc = std::function<void(void)>;
inline constexpr int DISPATCHER_TASK_EXPIRATION = 2000;
inline constexpr uint64_t SLOW_TASK_THRESHOLD_NS = 50'000'000; // 50ms in nanoseconds

// C++20: Use steady_clock for monotonic time
const auto SYSTEM_TIME_ZERO = std::chrono::steady_clock::time_point(std::chrono::milliseconds(0));

class Task
{
public:
	// DO NOT allocate this class on the stack
	explicit Task(TaskFunc&& f, const std::string& _description, const std::string& _extraDescription) :
	description(_description), extraDescription(_extraDescription), func(std::move(f)) {}

	Task(uint32_t ms, TaskFunc&& f, const std::string& _description, const std::string& _extraDescription) :
		description(_description), 
		extraDescription(_extraDescription),
		expiration(std::chrono::steady_clock::now() + std::chrono::milliseconds(ms)),
		func(std::move(f))
	{}

	virtual ~Task() = default;

	void operator()() { func(); }

	void setDontExpire() { expiration = SYSTEM_TIME_ZERO; }

	[[nodiscard]] bool hasExpired() const
	{
		if (expiration == SYSTEM_TIME_ZERO) {
			return false;
		}
		return expiration < std::chrono::steady_clock::now();
	}

	const std::string description;
	const std::string extraDescription;

	bool trackInStats = true;
	bool skipSlowDetection = false;

protected:
	std::chrono::steady_clock::time_point expiration = SYSTEM_TIME_ZERO;

private:
	TaskFunc func;
};

std::unique_ptr<Task> createTaskWithStats(TaskFunc&& f, const std::string& description, const std::string& extraDescription);
std::unique_ptr<Task> createTaskWithStats(uint32_t expiration, TaskFunc&& f, const std::string& description, const std::string& extraDescription);

class Dispatcher : public ThreadHolder<Dispatcher>
{
public:
	Dispatcher() : ThreadHolder()
	{
		static int id = 0;
		dispatcherId = id;
		id += 1;

		taskList.reserve(32);
	}

	void addTask(std::unique_ptr<Task> task);
	void addTask(TaskFunc&& f) { addTask(createTask(std::move(f))); }
	void addTask(uint32_t expiration, TaskFunc&& f) { addTask(createTimedTask(expiration, std::move(f))); }

	// Async dispatch: run func on ThreadPool, then dispatch callback with result on Dispatcher thread
	template<typename Func, typename Callback>
	void asyncTask(Func&& func, Callback&& callback) {
		g_threadPool.detach_task([this, f = std::forward<Func>(func), cb = std::forward<Callback>(callback)]() {
			auto result = f();
			addTask([cb = std::move(cb), result = std::move(result)]() mutable {
				cb(std::move(result));
			});
		});
	}

	// Fire-and-forget async: run func on ThreadPool with no callback
	void asyncTask(TaskFunc&& func) {
		g_threadPool.detach_task(std::move(func));
	}

	void shutdown();

	// Thread context verification
	[[nodiscard]] bool isDispatcherThread() const noexcept { return threadId == std::this_thread::get_id(); }

	[[nodiscard]] uint64_t getDispatcherCycle() const { return dispatcherCycle; }
	[[nodiscard]] int getDispatcherId() const { return dispatcherId; }

	// Performance metrics
	[[nodiscard]] uint64_t getTotalTasksProcessed() const noexcept { return totalTasksProcessed; }
	[[nodiscard]] uint64_t getSlowTaskCount() const noexcept { return slowTaskCount; }

	void threadMain();

private:
	std::mutex taskLock;

	// C++20: binary_semaphore is more efficient than condition_variable
	std::binary_semaphore taskSignal{0};

	std::vector<std::unique_ptr<Task>> taskList;
	uint64_t dispatcherCycle = 0;
	int dispatcherId = 0;

	// Thread identity for context checks
	std::thread::id threadId;

	// Performance counters
	uint64_t totalTasksProcessed = 0;
	uint64_t slowTaskCount = 0;
};

extern Dispatcher g_dispatcher;

#endif // FS_TASKS_H
