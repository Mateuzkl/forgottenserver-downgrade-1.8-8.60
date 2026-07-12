#include "../otpch.h"

#include "../reactor.h"
#include "../scheduler.h"
#include "../tasks.h"

#include "test_support.h"

namespace {

void startReactor(TaskReactor& reactor)
{
	reactor.start();
}

} // namespace

static_assert(!std::copy_constructible<ReactorCallback>);
static_assert(!std::copy_constructible<TaskFunc>);

TEST_CASE(test_reactor_send_executes)
{
	TaskReactor reactor;
	startReactor(reactor);
	bool executed = false;

	reactor.send([&executed] { executed = true; });
	reactor.runOnce();

	CHECK(executed);
}

TEST_CASE(test_reactor_accepts_move_only_callback)
{
	TaskReactor reactor;
	startReactor(reactor);
	auto value = std::make_unique<int>(42);
	int result = 0;

	reactor.send([value = std::move(value), &result] { result = *value; });
	reactor.runOnce();

	CHECK(!value);
	CHECK(result == 42);
}

TEST_CASE(test_reactor_schedule_immediate_executes)
{
	TaskReactor reactor;
	startReactor(reactor);
	bool executed = false;

	reactor.schedule(0, [&executed] { executed = true; });
	reactor.runOnce();

	CHECK(executed);
}

TEST_CASE(test_reactor_preserves_send_and_schedule_order)
{
	TaskReactor reactor;
	startReactor(reactor);
	std::vector<int> order;

	reactor.send([&order] { order.push_back(1); });
	reactor.schedule(0, [&order] { order.push_back(2); });
	reactor.send([&order] { order.push_back(3); });
	reactor.schedule(0, [&order] { order.push_back(4); });
	reactor.runOnce();

	CHECK(order == std::vector<int>({1, 2, 3, 4}));
}

TEST_CASE(test_reactor_preserves_multiple_send_order)
{
	TaskReactor reactor;
	startReactor(reactor);
	std::vector<int> order;

	reactor.send([&order] { order.push_back(1); });
	reactor.send([&order] { order.push_back(2); });
	reactor.send([&order] { order.push_back(3); });
	reactor.runOnce();

	CHECK(order == std::vector<int>({1, 2, 3}));
}

TEST_CASE(test_reactor_preserves_multiple_schedule_order)
{
	TaskReactor reactor;
	startReactor(reactor);
	std::vector<int> order;

	reactor.schedule(0, [&order] { order.push_back(1); });
	reactor.schedule(0, [&order] { order.push_back(2); });
	reactor.schedule(0, [&order] { order.push_back(3); });
	reactor.runOnce();

	CHECK(order == std::vector<int>({1, 2, 3}));
}

TEST_CASE(test_reactor_cancel_prevents_execution)
{
	TaskReactor reactor;
	startReactor(reactor);
	bool executed = false;

	const uint32_t identifier = reactor.schedule(0, [&executed] { executed = true; });
	reactor.cancel(identifier);
	reactor.runOnce();

	CHECK(!executed);
}

TEST_CASE(test_reactor_cancel_zero_is_noop)
{
	TaskReactor reactor;
	startReactor(reactor);
	bool executed = false;

	reactor.send([&executed] { executed = true; });
	reactor.cancel(0);
	reactor.runOnce();

	CHECK(executed);
}

TEST_CASE(test_reactor_expired_send_is_discarded)
{
	TaskReactor reactor;
	startReactor(reactor);
	bool executed = false;

	reactor.send(std::chrono::milliseconds(1), [&executed] { executed = true; });
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	reactor.runOnce();

	CHECK(!executed);
}

TEST_CASE(test_reactor_future_schedule_waits)
{
	TaskReactor reactor;
	startReactor(reactor);
	bool executed = false;

	reactor.schedule(std::chrono::hours(1), [&executed] { executed = true; });
	reactor.runOnce();

	CHECK(!executed);
}

TEST_CASE(test_reactor_identifiers_are_unique)
{
	TaskReactor reactor;
	startReactor(reactor);

	const uint32_t first = reactor.schedule(0, [] {});
	const uint32_t second = reactor.schedule(0, [] {});
	const uint32_t third = reactor.schedule(0, [] {});

	CHECK(first != 0);
	CHECK(first != second);
	CHECK(first != third);
	CHECK(second != third);
}

TEST_CASE(test_reactor_cancel_after_execution_is_safe)
{
	TaskReactor reactor;
	startReactor(reactor);
	int executions = 0;

	const uint32_t identifier = reactor.schedule(0, [&executions] { ++executions; });
	reactor.runOnce();
	reactor.cancel(identifier);
	reactor.runOnce();

	CHECK(executions == 1);
}

TEST_CASE(test_reactor_rejects_new_work_at_backpressure_limit)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxInboxSize(1);

	CHECK(reactor.send([] {}));
	CHECK(!reactor.send([] {}));
}

TEST_CASE(test_reactor_preserves_deferred_send_order)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxTasksPerCycle(1);
	std::vector<int> order;

	reactor.send([&order] { order.push_back(1); });
	reactor.send([&order] { order.push_back(2); });
	reactor.send([&order] { order.push_back(3); });

	reactor.runOnce();
	reactor.runOnce();
	reactor.runOnce();

	CHECK(order == std::vector<int>({1, 2, 3}));
}

TEST_CASE(test_reactor_deferred_scheduled_task_remains_cancellable)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxTasksPerCycle(1);
	int executions = 0;

	reactor.schedule(0, [&executions] { ++executions; });
	const uint32_t deferredId = reactor.schedule(0, [&executions] { executions += 100; });
	reactor.runOnce();
	CHECK(executions == 1);

	reactor.cancel(deferredId);
	reactor.runOnce();
	reactor.runOnce();

	CHECK(executions == 1);
}

TEST_CASE(test_reactor_cancellation_is_not_dropped_when_inbox_is_full)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxInboxSize(1);
	bool executed = false;

	const uint32_t identifier = reactor.schedule(0, [&executed] { executed = true; });
	CHECK(identifier != 0);
	reactor.cancel(identifier);
	reactor.runOnce();

	CHECK(!executed);
}

TEST_CASE(test_reactor_time_budget_defers_remaining_callbacks)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setTimeBudget(std::chrono::milliseconds(1));
	std::vector<int> order;

	reactor.send([&order] {
		order.push_back(1);
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
	});
	reactor.send([&order] { order.push_back(2); });
	reactor.runOnce();
	CHECK(order == std::vector<int>({1}));

	reactor.runOnce();
	CHECK(order == std::vector<int>({1, 2}));
}

TEST_CASE(test_reactor_shutdown_wakes_run_loop)
{
	TaskReactor reactor;
	startReactor(reactor);
	std::atomic_bool enteredLoop = false;

	reactor.send([&enteredLoop] { enteredLoop.store(true, std::memory_order_release); });
	std::jthread reactorThread([&reactor] { reactor.runLoop(); });

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	while (!enteredLoop.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::yield();
	}

	CHECK(enteredLoop.load(std::memory_order_acquire));
	reactor.shutdown();
	reactorThread.join();
	CHECK(reactor.getState() == THREAD_STATE_TERMINATED);
}

TEST_CASE(test_reactor_exception_does_not_stop_other_callbacks)
{
	TaskReactor reactor;
	startReactor(reactor);
	bool executedAfterException = false;

	reactor.send([] { throw std::runtime_error("expected test exception"); });
	reactor.send([&executedAfterException] { executedAfterException = true; });
	reactor.runOnce();

	CHECK(executedAfterException);
}

TEST_CASE(test_reactor_cancel_from_earlier_task_in_same_batch)
{
	// A cancellation issued by a task must be honoured for tasks later in
	// the SAME ready batch, not only for tasks still sitting in the heap.
	TaskReactor reactor;
	startReactor(reactor);
	bool victimExecuted = false;
	uint32_t victimId = 0;

	reactor.schedule(0, [&reactor, &victimId] { reactor.cancel(victimId); });
	victimId = reactor.schedule(0, [&victimExecuted] { victimExecuted = true; });
	reactor.runOnce();

	CHECK(!victimExecuted);
}

TEST_CASE(test_reactor_cancel_and_replace_within_batch_keeps_single_lineage)
{
	// The walk-event pattern: an earlier task cancels the pending event and
	// schedules a replacement. Only the replacement may run; if the cancelled
	// victim also runs, the caller ends up with two event lineages.
	TaskReactor reactor;
	startReactor(reactor);
	int victimRuns = 0;
	int replacementRuns = 0;
	uint32_t victimId = 0;

	reactor.schedule(0, [&reactor, &victimId, &replacementRuns] {
		reactor.cancel(victimId);
		reactor.schedule(0, [&replacementRuns] { ++replacementRuns; });
	});
	victimId = reactor.schedule(0, [&victimRuns] { ++victimRuns; });

	reactor.runOnce();
	reactor.runOnce();

	CHECK(victimRuns == 0);
	CHECK(replacementRuns == 1);
}

TEST_CASE(test_reactor_time_budget_deferred_scheduled_task_remains_cancellable)
{
	// A scheduled task deferred by the time budget is pushed back into the
	// heap; a cancellation issued afterwards must still be honoured.
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setTimeBudget(std::chrono::milliseconds(1));
	int victimRuns = 0;

	reactor.schedule(0, [] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); });
	const uint32_t victimId = reactor.schedule(0, [&victimRuns] { ++victimRuns; });
	reactor.runOnce();
	CHECK(victimRuns == 0);

	CHECK(reactor.cancel(victimId));
	reactor.runOnce();
	reactor.runOnce();
	CHECK(victimRuns == 0);
}

TEST_CASE(test_reactor_cancel_inside_own_callback_reports_not_pending)
{
	// stopEventWalk from inside the fired walk callback targets the event
	// that is currently executing: it must be a no-op consume, reported as
	// not pending, and must not suppress future events.
	TaskReactor reactor;
	startReactor(reactor);
	int executions = 0;
	uint32_t selfId = 0;
	bool selfCancelReportedPending = true;

	selfId = reactor.schedule(0, [&reactor, &selfId, &selfCancelReportedPending, &executions] {
		++executions;
		selfCancelReportedPending = reactor.cancel(selfId);
	});
	reactor.runOnce();
	reactor.runOnce();

	CHECK(executions == 1);
	CHECK(!selfCancelReportedPending);

	bool laterExecuted = false;
	reactor.schedule(0, [&laterExecuted] { laterExecuted = true; });
	reactor.runOnce();
	CHECK(laterExecuted);
}

TEST_CASE(test_reactor_cancel_reports_whether_task_was_pending)
{
	TaskReactor reactor;
	startReactor(reactor);
	int executions = 0;

	const uint32_t executedId = reactor.schedule(0, [&executions] { ++executions; });
	reactor.runOnce();
	CHECK(executions == 1);
	CHECK(!reactor.cancel(executedId));

	const uint32_t pendingId = reactor.schedule(0, [&executions] { ++executions; });
	CHECK(reactor.cancel(pendingId));
	reactor.runOnce();
	CHECK(executions == 1);
	CHECK(!reactor.cancel(pendingId));
}

TEST_CASE(test_scheduler_dispatcher_move_only_pipeline)
{
	g_dispatcher.start();
	g_scheduler.start();

	auto payload = std::make_unique<int>(42);
	int result = 0;
	const uint32_t eventId =
	    g_scheduler.addEvent(0, [payload = std::move(payload), &result] { result = *payload; });

	CHECK(eventId != 0);
	CHECK(!payload);
	g_reactor.runOnce();
	CHECK(result == 42);

	g_scheduler.stop();
	CHECK(g_scheduler.addEvent(0, [] {}) == 0);

	bool dispatcherAcceptedAfterStop = false;
	g_dispatcher.stop();
	g_dispatcher.addTask([&dispatcherAcceptedAfterStop] { dispatcherAcceptedAfterStop = true; });
	g_reactor.runOnce();
	CHECK(!dispatcherAcceptedAfterStop);

	g_scheduler.shutdown();
	g_dispatcher.shutdown();
}

TFS_TEST_MAIN()
