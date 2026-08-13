// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

// Regression tests for DatabaseTasks::addTask() rejection reporting.
//
// The bug these cover: addTask() returned void, so a caller could not tell that the
// worker was not running and its callback would never run. Callers that own a
// resource released *by* that callback — luaDatabaseAsyncExecute() and
// luaDatabaseAsyncStoreQuery() hold a Lua registry reference — leaked it silently.

#include "../otpch.h"

#include "../databasetasks.h"

#include "test_support.h"

namespace {

// Stands in for a caller-owned resource whose release is the callback's job, which is
// exactly the shape of the Lua registry reference the real call sites hold.
struct CallbackOwnedResource
{
	bool released = false;
};

} // namespace

// The core regression: a rejected task must report false so the caller can react.
TEST_CASE(test_rejected_task_reports_failure)
{
	DatabaseTasks tasks;
	CHECK(!tasks.isRunning());

	CHECK(!tasks.addTask("SELECT 1"));
	CHECK(!tasks.addTask("SELECT 1", nullptr, true));
	CHECK(!tasks.addTask("SELECT 1", [](DBResult_ptr, bool, uint64_t) {}));
}

// The pattern the Lua bindings now follow: on rejection the caller releases what the
// callback would have released.
TEST_CASE(test_caller_can_release_callback_owned_resource_on_rejection)
{
	DatabaseTasks tasks;
	CallbackOwnedResource resource;

	const bool accepted = tasks.addTask(
	    "UPDATE `players` SET `level` = 2 WHERE `id` = 1",
	    [&resource](DBResult_ptr, bool, uint64_t) { resource.released = true; });

	CHECK(!accepted);
	CHECK(!resource.released); // the callback never ran

	if (!accepted) {
		resource.released = true; // caller-side cleanup, mirroring luaL_unref()
	}
	CHECK(resource.released);
}

// An accepted task must still report true, so the caller does not free a resource the
// callback is about to use.
TEST_CASE(test_accepted_task_reports_success)
{
	DatabaseTasks tasks;
	tasks.start();
	CHECK(tasks.isRunning());

	CHECK(tasks.addTask("SELECT 1"));

	// shutdown() stops the worker but deliberately does not join it — otserv.cpp and
	// signals.cpp join explicitly afterwards. Without that join here the worker is
	// still returning from threadMain() while ~DatabaseTasks() destroys the deque,
	// mutex and condition variables it is using, which ThreadSanitizer reports as a
	// data race in the destructor. Follow the same contract the server does.
	tasks.shutdown();
	tasks.join();
	CHECK(!tasks.isRunning());

	// Once the worker is down, rejection resumes.
	CHECK(!tasks.addTask("SELECT 1"));
}

// addTask() must stay usable in a boolean context without a callback; the ban
// bookkeeping call sites rely on that.
TEST_CASE(test_rejection_is_reported_for_callbackless_tasks)
{
	DatabaseTasks tasks;
	CHECK(!tasks.addTask("DELETE FROM `ip_bans` WHERE `ip` = 1"));
	CHECK(!tasks.addTask(std::string(4096, 'x')));
}

TFS_TEST_MAIN()
