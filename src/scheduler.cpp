// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "scheduler.h"

uint32_t Scheduler::addEvent(std::unique_ptr<SchedulerTask> task)
{
	// check if the event has a valid id
	if (task->getEventId() == 0) {
		uint32_t id = lastEventId.fetch_add(1,
			std::memory_order_relaxed) + 1;
		task->setEventId(id);
	}

	const uint32_t eventId = task->getEventId();

	struct TaskHolder {
		std::unique_ptr<SchedulerTask> task;
		explicit TaskHolder(std::unique_ptr<SchedulerTask> t) : task(std::move(t)) {}
	};
	auto holder = std::make_shared<TaskHolder>(std::move(task));

	asio::post(io_context, [this, holder]() {
		// insert the event id in the list of active events
		auto [it, inserted] = eventIdTimerMap.emplace(holder->task->getEventId(), asio::steady_timer{ io_context });
			if (!inserted) {
      			return;
    		}

		auto& timer = it->second;

		timer.expires_after(std::chrono::milliseconds(holder->task->getDelay()));
		timer.async_wait([this, holder](const asio::error_code& error) {
			eventIdTimerMap.erase(holder->task->getEventId());

			if (error == asio::error::operation_aborted || getState() == THREAD_STATE_TERMINATED) {
				// the timer has been manually canceled(timer->cancel()) or Scheduler::shutdown has been called.
				// holder destructor will clean up the task via unique_ptr.
				return;
			}

			// Transfer ownership to the dispatcher.
			g_dispatcher.addTask(std::move(holder->task));
			});
		});

	return eventId;
}

void Scheduler::stopEvent(uint32_t eventId) noexcept
{
	if (eventId == 0) {
		return;
	}

	asio::post(io_context, [this, eventId]() {
		// search the event id
		if (auto it = eventIdTimerMap.find(eventId); it != eventIdTimerMap.end()) {
			it->second.cancel();
		}
	});
}

void Scheduler::shutdown() noexcept
{
	setState(THREAD_STATE_TERMINATED);
	asio::post(io_context, [this]() {
		// cancel all active timers
		for (auto& [eventId, timer] : eventIdTimerMap) {
			timer.cancel();
		}

		io_context.stop();
	});
}

std::unique_ptr<SchedulerTask> createSchedulerTaskWithStats(uint32_t delay, TaskFunc&& f, const std::string& description, const std::string& extraDescription)
{
	return std::make_unique<SchedulerTask>(delay, std::move(f), description, extraDescription);
}
