// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "scheduler.h"

uint32_t Scheduler::addEvent(SchedulerTask* task)
{
	// check if the event has a valid id
	if (task->getEventId() == 0) {
		uint32_t id = lastEventId.fetch_add(1, std::memory_order_relaxed) + 1;
		task->setEventId(id);
	}

	struct TaskGuard {
		SchedulerTask* task;
		bool released = false;
		explicit TaskGuard(SchedulerTask* t) : task(t) {}
		TaskGuard(const TaskGuard&) = delete;
		TaskGuard& operator=(const TaskGuard&) = delete;
		~TaskGuard() { if (!released) delete task; }
	};
	auto guard = std::make_shared<TaskGuard>(task);

	boost::asio::post(io_context, [this, guard]() {
		// insert the event id in the list of active events
		auto [it, inserted] = eventIdTimerMap.emplace(guard->task->getEventId(), boost::asio::steady_timer{ io_context });
		auto& timer = it->second;

		timer.expires_after(std::chrono::milliseconds(guard->task->getDelay()));
		timer.async_wait([this, guard](const boost::system::error_code& error) {
			eventIdTimerMap.erase(guard->task->getEventId());

			if (error == boost::asio::error::operation_aborted || getState() == THREAD_STATE_TERMINATED) {
				// the timer has been manually canceled(timer->cancel()) or Scheduler::shutdown has been called.
				// guard destructor will delete the task.
				return;
			}

			// Transfer ownership to the dispatcher; guard must not delete.
			guard->released = true;
			g_dispatcher.addTask(guard->task);
		});
	});

	return task->getEventId();
}

void Scheduler::stopEvent(uint32_t eventId) noexcept
{
	if (eventId == 0) {
		return;
	}

	boost::asio::post(io_context, [this, eventId]() {
		// search the event id
		if (auto it = eventIdTimerMap.find(eventId); it != eventIdTimerMap.end()) {
			it->second.cancel();
		}
	});
}

void Scheduler::shutdown() noexcept
{
	setState(THREAD_STATE_TERMINATED);
	boost::asio::post(io_context, [this]() {
		// cancel all active timers
		for (auto& [eventId, timer] : eventIdTimerMap) {
			timer.cancel();
		}

		io_context.stop();
	});
}

SchedulerTask* createSchedulerTaskWithStats(uint32_t delay, TaskFunc&& f, const std::string& description,
                                            const std::string& extraDescription)
{
	return new SchedulerTask(delay, std::move(f), description, extraDescription);
}
