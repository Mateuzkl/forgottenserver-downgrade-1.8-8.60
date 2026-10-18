// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_SCHEDULER_H
#define FS_SCHEDULER_H

#include "tasks.h"

inline constexpr int32_t MIN_TASK_INTERVAL = 50;
inline constexpr int32_t SCHEDULER_MINTICKS = MIN_TASK_INTERVAL;

class SchedulerTask : public Task
{
public:
	SchedulerTask(uint32_t delay, TaskFunc&& f, std::string_view description, std::string_view extraDescription);

	void setEventId(uint32_t id) noexcept { eventId = id; }
	[[nodiscard]] constexpr uint32_t getEventId() const noexcept { return eventId; }
	[[nodiscard]] constexpr uint32_t getDelay() const noexcept { return delay; }

private:
	uint32_t eventId = 0;
	uint32_t delay = 0;

	friend std::unique_ptr<SchedulerTask> createSchedulerTaskWithStats(uint32_t, TaskFunc&&, std::string_view,
	                                                                   std::string_view);
};

std::unique_ptr<SchedulerTask> createSchedulerTaskWithStats(uint32_t delay, TaskFunc&& f, std::string_view description,
                                                            std::string_view extraDescription);

class Scheduler
{
public:
	void start() noexcept;
	void stop() noexcept;
	void join() noexcept {}
	void shutdown() noexcept;

	uint32_t addEvent(std::unique_ptr<SchedulerTask>&& task);
	uint32_t addEvent(uint32_t delay, TaskFunc&& f) { return addEvent(createSchedulerTask(delay, std::move(f))); }
	void stopEvent(uint32_t eventId) noexcept;

	[[nodiscard]] ThreadState getState() const noexcept { return state.load(std::memory_order_acquire); }

private:
	std::atomic<ThreadState> state{THREAD_STATE_TERMINATED};
};

extern Scheduler g_scheduler;

#endif // FS_SCHEDULER_H
