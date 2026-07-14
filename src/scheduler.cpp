// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "scheduler.h"

SchedulerTask::SchedulerTask(uint32_t delay, TaskFunc&& f, const std::string& description, const std::string& extraDescription) :
	Task(std::move(f), description, extraDescription), delay(delay)
{
}

std::unique_ptr<SchedulerTask> createSchedulerTaskWithStats(uint32_t delay, TaskFunc&& f, const std::string& description, const std::string& extraDescription)
{
	return std::make_unique<SchedulerTask>(delay, std::move(f), description, extraDescription);
}

void Scheduler::start() noexcept
{
	state.store(THREAD_STATE_RUNNING, std::memory_order_release);
}

void Scheduler::stop() noexcept
{
	state.store(THREAD_STATE_CLOSING, std::memory_order_release);
}

void Scheduler::shutdown() noexcept
{
	state.store(THREAD_STATE_TERMINATED, std::memory_order_release);
}

uint32_t Scheduler::addEvent(std::unique_ptr<SchedulerTask>&& task)
{
	if (!task || state.load(std::memory_order_acquire) != THREAD_STATE_RUNNING) {
		return 0;
	}

	const uint32_t delay = task->getDelay();
	std::string description = task->description;
	std::string origin = task->extraDescription;
	const uint32_t eventId = g_reactor.schedule(
	    delay, [task = std::move(task)]() mutable { g_dispatcher.executeTask(std::move(task)); },
	    std::move(description), std::move(origin));

	return eventId;
}

void Scheduler::stopEvent(uint32_t eventId) noexcept
{
	g_reactor.cancel(eventId);
}
