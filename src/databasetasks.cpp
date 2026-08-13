// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "databasetasks.h"

#include "tasks.h"

extern Dispatcher g_dispatcher;

void DatabaseTasks::start()
{
	ThreadHolder::start();
}

void DatabaseTasks::threadMain()
{
	while (true) {
		std::unique_lock<std::mutex> guard{taskLock};
		taskSignal.wait(guard, [this] { return getState() == THREAD_STATE_TERMINATED || !tasks.empty(); });

		if (tasks.empty()) {
			break;
		}

		DatabaseTask task = std::move(tasks.front());
		tasks.pop_front();
		++activeTasks;
		guard.unlock();

		runTask(task);

		guard.lock();
		--activeTasks;
		if (tasks.empty() && activeTasks == 0) {
			taskDoneSignal.notify_all();
		}
	}

	taskDoneSignal.notify_all();
}

bool DatabaseTasks::addTask(std::string query, std::function<void(DBResult_ptr, bool, uint64_t)> callback /* = nullptr*/,
                            bool store /* = false*/)
{
	bool signal = false;
	bool accepted = false;
	const size_t queryLength = query.size();
	{
		std::scoped_lock guard(taskLock);
		if (getState() == THREAD_STATE_RUNNING) {
			signal = tasks.empty();
			tasks.emplace_back(std::move(query), std::move(callback), store);
			accepted = true;
		}
	}

	if (!accepted) {
		// Dropping the task used to be completely silent: no log, no return value,
		// nothing for the caller to react to. Since logout and shutdown saves come
		// through here, that turned into player progress vanishing with no trace to
		// correlate against the report.
		//
		// Only metadata is logged. Queries carry account names, passwords, player
		// positions and other user data, so echoing even a truncated prefix into the
		// log would leak it to anyone who can read the log file.
		LOG_ERROR("[DatabaseTasks::addTask] Task rejected because the database worker is not running; "
		          "the query was NOT executed; query_length={}",
		          queryLength);
		return false;
	}

	if (signal) {
		taskSignal.notify_one();
	}
	return true;
}

void DatabaseTasks::runTask(const DatabaseTask& task)
{
	Database& database = Database::getInstance();
	bool success;
	DBResult_ptr result;
	if (task.store) {
		result = database.storeQuery(task.query);
		success = (database.getLastErrno() == 0);
	} else {
		result = nullptr;
		success = database.executeQuery(task.query);
	}

	uint64_t affectedRows = database.getAffectedRows();

	if (task.callback) {
		g_dispatcher.addTask([=, callback = task.callback]() { callback(result, success, affectedRows); });
	}
}

void DatabaseTasks::flush()
{
	std::unique_lock<std::mutex> guard{taskLock};
	taskDoneSignal.wait(guard, [this] { return tasks.empty() && activeTasks == 0; });
}

void DatabaseTasks::shutdown()
{
	{
		std::scoped_lock guard(taskLock);
		setState(THREAD_STATE_TERMINATED);
	}
	taskSignal.notify_all();
	flush();
}
