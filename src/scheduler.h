// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_SCHEDULER_H
#define FS_SCHEDULER_H

#include "tasks.h"
#include "thread_holder_base.h"

inline constexpr int32_t SCHEDULER_MINTICKS = 50;

class SchedulerTask : public Task
{
public:
	void setEventId(uint32_t id) noexcept { eventId = id; }
	[[nodiscard]] constexpr uint32_t getEventId() const noexcept { return eventId; }

	[[nodiscard]] constexpr uint32_t getDelay() const noexcept { return delay; }

private:
	SchedulerTask(uint32_t delay, TaskFunc&& f, const std::string& description, const std::string& extraDescription) :
	    Task(std::move(f), description, extraDescription), delay(delay)
	{}

	uint32_t eventId = 0;
	uint32_t delay = 0;

	friend SchedulerTask* createSchedulerTaskWithStats(uint32_t, TaskFunc&&, const std::string&, const std::string&);
};

SchedulerTask* createSchedulerTaskWithStats(uint32_t delay, TaskFunc&& f, const std::string& description,
                                            const std::string& extraDescription);

class Scheduler : public ThreadHolder<Scheduler>
{
public:
	uint32_t addEvent(SchedulerTask* task);
	uint32_t addEvent(uint32_t delay, TaskFunc&& f) { return addEvent(createSchedulerTask(delay, std::move(f))); }
	void stopEvent(uint32_t eventId) noexcept;

	void shutdown() noexcept;

	void threadMain() { io_context.run(); }

private:
	std::atomic<uint32_t> lastEventId{0};
	std::unordered_map<uint32_t, boost::asio::steady_timer> eventIdTimerMap;
	boost::asio::io_context io_context;
	boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work{io_context.get_executor()};
};

extern Scheduler g_scheduler;

#endif // FS_SCHEDULER_H
