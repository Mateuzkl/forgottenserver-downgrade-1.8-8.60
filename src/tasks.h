// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_TASKS_H
#define FS_TASKS_H

#include "reactor.h"
#include "stats.h"
#include "thread_pool.h"

#include <source_location>

using TaskFunc = ReactorCallback;

inline constexpr int DISPATCHER_TASK_EXPIRATION = 2000;
inline constexpr uint64_t SLOW_TASK_THRESHOLD_NS = 50'000'000; // 50ms in nanoseconds

const auto SYSTEM_TIME_ZERO = std::chrono::steady_clock::time_point(std::chrono::milliseconds(0));

class Task
{
public:
	explicit Task(TaskFunc&& f, const std::string& description, const std::string& extraDescription);
	Task(uint32_t ms, TaskFunc&& f, const std::string& description, const std::string& extraDescription);
	virtual ~Task() = default;

	void operator()() { func(); }

	void setDontExpire() { expiration = SYSTEM_TIME_ZERO; }
	[[nodiscard]] bool hasExpired() const;

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

class Dispatcher
{
public:
	Dispatcher();

	void start() noexcept;
	void stop() noexcept;
	void join() noexcept {}
	void shutdown() noexcept;

	void addTask(std::unique_ptr<Task>&& task);
	void addTask(TaskFunc&& f, const std::source_location location = std::source_location::current())
	{
		addTask(createTaskWithStats(std::move(f), location.function_name(),
		                            std::string(location.file_name()) + ":" + std::to_string(location.line())));
	}
	void addTask(uint32_t expiration, TaskFunc&& f,
	             const std::source_location location = std::source_location::current())
	{
		addTask(createTaskWithStats(expiration, std::move(f), location.function_name(),
		                            std::string(location.file_name()) + ":" + std::to_string(location.line())));
	}
	void executeTask(std::unique_ptr<Task> task);

	template <typename Func, typename Callback>
	void asyncTask(Func&& func, Callback&& callback)
	{
		g_threadPool.detach_task([this, f = std::forward<Func>(func), cb = std::forward<Callback>(callback)]() mutable {
			auto result = f();
			addTask([cb = std::move(cb), result = std::move(result)]() mutable { cb(std::move(result)); });
		});
	}

	void asyncTask(TaskFunc&& func) { g_threadPool.detach_task(std::move(func)); }

	[[nodiscard]] bool isDispatcherThread() const noexcept { return g_reactor.isReactorThread(); }
	[[nodiscard]] uint64_t getDispatcherCycle() const noexcept { return dispatcherCycle; }
	[[nodiscard]] int getDispatcherId() const noexcept { return dispatcherId; }
	[[nodiscard]] uint64_t getTotalTasksProcessed() const noexcept { return totalTasksProcessed; }
	[[nodiscard]] uint64_t getSlowTaskCount() const noexcept { return slowTaskCount; }
	[[nodiscard]] ThreadState getState() const noexcept { return state.load(std::memory_order_acquire); }

private:
	std::atomic<ThreadState> state{THREAD_STATE_TERMINATED};
	std::atomic<uint64_t> dispatcherCycle{0};
	std::atomic<uint64_t> totalTasksProcessed{0};
	std::atomic<uint64_t> slowTaskCount{0};
	int dispatcherId = 0;
};

extern Dispatcher g_dispatcher;

#endif // FS_TASKS_H
