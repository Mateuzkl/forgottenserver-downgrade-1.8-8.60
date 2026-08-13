// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_DATABASETASKS_H
#define FS_DATABASETASKS_H

#include "database.h"
#include "enums.h"
#include "thread_holder_base.h"

struct DatabaseTask
{
	DatabaseTask(std::string_view query, std::function<void(DBResult_ptr, bool, uint64_t)>&& callback, bool store) :
	    query{query}, callback{std::move(callback)}, store{store}
	{}

	std::string query;
	std::function<void(DBResult_ptr, bool, uint64_t)> callback;
	bool store;
};

class DatabaseTasks : public ThreadHolder<DatabaseTasks>
{
public:
	DatabaseTasks() = default;
	void start();
	void flush();
	void shutdown();

	// Returns true only when the task was actually queued. A false return means the
	// worker is not running and the callback will NEVER be invoked, so any resource
	// whose release is owned by that callback (a Lua registry reference, for example)
	// must be freed by the caller instead. Discarding the result silently leaks those.
	[[nodiscard]] bool addTask(std::string query,
	                           std::function<void(DBResult_ptr, bool, uint64_t)> callback = nullptr,
	                           bool store = false);

	void threadMain();
	[[nodiscard]] bool isRunning() const { return getState() == THREAD_STATE_RUNNING; }

private:
	void runTask(const DatabaseTask& task);

	std::deque<DatabaseTask> tasks;
	std::mutex taskLock;
	std::condition_variable taskSignal;
	std::condition_variable taskDoneSignal;
	size_t activeTasks = 0;
};

extern DatabaseTasks g_databaseTasks;

#endif
