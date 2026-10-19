#include "../otpch.h"

#include "../reactor.h"
#include "../reactor_heap.h"
#include "../scheduler.h"
#include "../tasks.h"
#include "test_support.h"

#include <limits>

struct TaskReactorTestAccess
{
	using Clock = std::chrono::steady_clock;
	static void setScheduledTimes(TaskReactor& reactor,
	                              const std::vector<std::pair<uint32_t, Clock::time_point>>& times)
	{
		std::scoped_lock lock(reactor.mutex);
		for (auto& task : reactor.scheduleInbox) {
			for (const auto& [identifier, fireAt] : times) {
				if (task.identifier == identifier) task.fireAt = fireAt;
			}
		}
		std::make_heap(reactor.scheduleInbox.begin(), reactor.scheduleInbox.end(), TaskReactor::taskComesAfter);
	}
	static void ingestWithoutExecuting(TaskReactor& reactor) { reactor.drainInbox(); }
	static bool validHeap(const TaskReactor& reactor)
	{
		return std::is_heap(reactor.taskHeap.begin(), reactor.taskHeap.end(), TaskReactor::taskComesAfter);
	}
	static void expectIncrementalSendIngestion(TaskReactor& reactor)
	{
		// Check the actual algorithm choice without wall-clock thresholds or a
		// production counter: rebuilding this heap produces a different layout.
		CHECK(reactor.scheduleInbox.empty());
		using Key = std::pair<Clock::time_point, uint64_t>;
		std::vector<Key> expected;
		for (const auto& task : reactor.taskHeap) expected.emplace_back(task.fireAt, task.sequence);
		for (const auto& task : reactor.sendInbox) {
			expected.emplace_back(task.fireAt, task.sequence);
			std::push_heap(expected.begin(), expected.end(), std::greater<Key>{});
		}
		reactor.drainInbox();
		CHECK(reactor.taskHeap.size() == expected.size());
		for (size_t i = 0; i < expected.size(); ++i) {
			CHECK((Key{reactor.taskHeap[i].fireAt, reactor.taskHeap[i].sequence} == expected[i]));
		}
	}
	static size_t largestScratchCapacity(const TaskReactor& reactor)
	{
		CHECK(reactor.sentTasksBuffer.empty());
		CHECK(reactor.scheduledTasksBuffer.empty());
		CHECK(reactor.cancellationsBuffer.empty());
		return std::max({reactor.sentTasksBuffer.capacity(), reactor.scheduledTasksBuffer.capacity(),
		                 reactor.cancellationsBuffer.capacity()});
	}
	static void makeHeapDue(TaskReactor& reactor)
	{
		for (auto& task : reactor.taskHeap) task.fireAt = Clock::now() - std::chrono::seconds(1);
		std::make_heap(reactor.taskHeap.begin(), reactor.taskHeap.end(), TaskReactor::taskComesAfter);
	}
	static void setLastIdentifier(TaskReactor& reactor, uint32_t identifier)
	{
		std::scoped_lock lock(reactor.mutex);
		reactor.nextIdentifier = identifier;
	}
};

namespace {

void startReactor(TaskReactor& reactor) { reactor.start(); }

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

TEST_CASE(test_reactor_cancel_from_earlier_task_in_same_batch)
{
	TaskReactor reactor;
	startReactor(reactor);
	uint32_t victimId = 0;
	std::vector<int> order;

	reactor.schedule(0, [&] {
		order.push_back(1);
		reactor.cancel(victimId);
		reactor.cancel(victimId); // repeated cancellation must also be consumed
	});
	victimId = reactor.schedule(0, [&] { order.push_back(2); });
	reactor.send([&] { order.push_back(3); });
	reactor.runOnce();

	CHECK(order == std::vector<int>({1, 3}));
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_cancel_and_replace_within_batch_keeps_single_lineage)
{
	TaskReactor reactor;
	startReactor(reactor);
	uint32_t victimId = 0;
	int victimRuns = 0;
	int replacementRuns = 0;

	reactor.send([&] {
		reactor.cancel(victimId);
		reactor.schedule(0, [&] { ++replacementRuns; });
	});
	victimId = reactor.schedule(0, [&] { ++victimRuns; });
	reactor.runOnce();
	CHECK(victimRuns == 0);
	CHECK(replacementRuns == 0); // replacement belongs to the next batch
	CHECK(reactor.hasPendingTasks());

	reactor.runOnce();
	reactor.runOnce();
	CHECK(victimRuns == 0);
	CHECK(replacementRuns == 1);
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_same_batch_check_preserves_cancel_of_new_schedule)
{
	TaskReactor reactor;
	startReactor(reactor);
	bool newTaskExecuted = false;
	bool tailExecuted = false;

	reactor.schedule(0, [&] {
		const auto newId = reactor.schedule(0, [&] { newTaskExecuted = true; });
		reactor.cancel(newId);
	});
	// Checking this ready task must preserve newId's cancellation while the
	// newly scheduled task is still waiting for the next inbox drain.
	reactor.schedule(0, [&] { tailExecuted = true; });
	reactor.runOnce();
	CHECK(tailExecuted);
	reactor.runOnce();
	CHECK(!newTaskExecuted);
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_cancel_in_same_batch_preserves_fairness_deferral)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxTasksPerCycle(2);
	uint32_t victimId = 0;
	std::vector<int> order;

	reactor.schedule(0, [&] {
		order.push_back(1);
		reactor.cancel(victimId);
	});
	victimId = reactor.schedule(0, [&] { order.push_back(2); });
	reactor.schedule(0, [&] { order.push_back(3); });
	reactor.runOnce();
	CHECK(order == std::vector<int>({1}));
	CHECK(reactor.hasPendingTasks());

	reactor.runOnce();
	CHECK(order == std::vector<int>({1, 3}));
	CHECK(!reactor.hasPendingTasks());
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

TEST_CASE(test_reactor_identifier_wrap_skips_zero_and_ids_in_schedule_inbox)
{
	TaskReactor reactor;
	startReactor(reactor);
	int oldRuns = 0;
	int lastRuns = 0;
	int wrappedRuns = 0;
	const auto oldId = reactor.schedule(0, [&] { ++oldRuns; });
	const auto maxIdentifier = (std::numeric_limits<uint32_t>::max)();
	TaskReactorTestAccess::setLastIdentifier(reactor, maxIdentifier - 1);
	const auto lastId = reactor.schedule(0, [&] { ++lastRuns; });
	const auto wrappedId = reactor.schedule(0, [&] { ++wrappedRuns; });

	CHECK(lastId == maxIdentifier);
	CHECK(wrappedId != 0);
	CHECK(wrappedId != oldId);
	CHECK(wrappedId != lastId);
	reactor.cancel(wrappedId);
	reactor.runOnce();
	CHECK(oldRuns == 1);
	CHECK(lastRuns == 1);
	CHECK(wrappedRuns == 0);
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_identifier_wrap_preserves_heap_reservation)
{
	TaskReactor reactor;
	startReactor(reactor);
	bool oldExecuted = false;
	bool newExecuted = false;
	const auto oldId = reactor.schedule(std::chrono::hours(1), [&] { oldExecuted = true; });
	reactor.runOnce(); // move the old task from scheduleInbox into taskHeap
	TaskReactorTestAccess::setLastIdentifier(reactor, (std::numeric_limits<uint32_t>::max)());
	const auto newId = reactor.schedule(0, [&] { newExecuted = true; });

	CHECK(newId != 0);
	CHECK(newId != oldId);
	reactor.cancel(newId);
	reactor.runOnce();
	CHECK(!newExecuted);
	CHECK(!oldExecuted);
	CHECK(reactor.hasPendingTasks()); // the old, future task is still pending
}

TEST_CASE(test_reactor_identifier_wrap_preserves_cancelled_bounded_inbox_reservation)
{
	TaskReactor reactor;
	startReactor(reactor);
	// DragonKeep ingests large bursts over several cycles. The cancellation
	// must stay attached to an undrained task even when new ids wrap around.
	reactor.setMaxInboxDrainPerCycle(1);
	reactor.send([] {});
	int cancelledRuns = 0;
	int survivorRuns = 0;
	int replacementRuns = 0;
	const auto cancelledId = reactor.schedule(0, [&] { ++cancelledRuns; });
	const auto survivorId = reactor.schedule(0, [&] { ++survivorRuns; });
	reactor.cancel(cancelledId);
	reactor.runOnce(); // only the immediate task is ingested

	TaskReactorTestAccess::setLastIdentifier(reactor, (std::numeric_limits<uint32_t>::max)());
	const auto replacementId = reactor.schedule(0, [&] { ++replacementRuns; });
	CHECK(replacementId != 0);
	CHECK(replacementId != cancelledId);
	CHECK(replacementId != survivorId);
	for (size_t cycle = 0; cycle < 3; ++cycle) {
		reactor.runOnce();
	}

	CHECK(cancelledRuns == 0);
	CHECK(survivorRuns == 1);
	CHECK(replacementRuns == 1);
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_same_batch_cancellation_releases_captures_before_next_cycle)
{
	TaskReactor reactor;
	startReactor(reactor);
	uint32_t victimId = 0;
	reactor.send([&] { reactor.cancel(victimId); });
	auto ownership = std::make_shared<int>(42);
	std::weak_ptr<int> observer = ownership;
	victimId = reactor.schedule(0, [ownership = std::move(ownership)] {});
	CHECK(!observer.expired());

	reactor.runOnce();
	CHECK(observer.expired());
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_identifier_wrap_during_ready_batch_cancels_only_new_task)
{
	TaskReactor reactor;
	startReactor(reactor);
	int oldRuns = 0;
	int newRuns = 0;
	uint32_t newId = 0;
	reactor.send([&] {
		TaskReactorTestAccess::setLastIdentifier(reactor, (std::numeric_limits<uint32_t>::max)());
		newId = reactor.schedule(0, [&] { ++newRuns; });
		reactor.cancel(newId);
	});
	const auto oldId = reactor.schedule(0, [&] { ++oldRuns; });
	reactor.runOnce();

	CHECK(newId != 0);
	CHECK(newId != oldId);
	CHECK(oldRuns == 1);
	CHECK(newRuns == 0);
	reactor.runOnce();
	CHECK(newRuns == 0);
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_identifier_stays_reserved_while_callback_runs)
{
	TaskReactor reactor;
	startReactor(reactor);
	std::atomic_bool callbackStarted = false;
	std::atomic_bool releaseCallback = false;
	std::atomic_int replacementRuns = 0;

	const auto oldId = reactor.schedule(0, [&] {
		callbackStarted.store(true, std::memory_order_release);
		while (!releaseCallback.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
	});
	std::jthread driver([&] { reactor.runOnce(); });

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	while (!callbackStarted.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::yield();
	}

	const bool started = callbackStarted.load(std::memory_order_acquire);
	uint32_t replacementId = 0;
	if (started) {
		TaskReactorTestAccess::setLastIdentifier(reactor, (std::numeric_limits<uint32_t>::max)());
		replacementId = reactor.schedule(0, [&] { replacementRuns.fetch_add(1, std::memory_order_relaxed); });
		reactor.cancel(oldId);
	}

	releaseCallback.store(true, std::memory_order_release);
	driver.join();
	CHECK(started);
	CHECK(replacementId != 0);
	CHECK(replacementId != oldId);

	reactor.runOnce();
	CHECK(replacementRuns.load(std::memory_order_relaxed) == 1);
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_identifier_wrap_keeps_deferred_and_new_tasks_distinct)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxTasksPerCycle(1);
	int deferredRuns = 0;
	int newRuns = 0;
	const auto firstId = reactor.schedule(0, [] {});
	const auto deferredId = reactor.schedule(0, [&] { ++deferredRuns; });
	reactor.runOnce();
	CHECK(deferredRuns == 0);

	TaskReactorTestAccess::setLastIdentifier(reactor, (std::numeric_limits<uint32_t>::max)());
	const auto reusedId = reactor.schedule(0, [] {});
	const auto newId = reactor.schedule(0, [&] { ++newRuns; });
	CHECK(reusedId == firstId); // the executed task's reservation was released
	CHECK(newId != 0);
	CHECK(newId != deferredId);
	CHECK(newId != reusedId);
	reactor.cancel(newId);
	reactor.setMaxTasksPerCycle(0);
	reactor.runOnce();
	CHECK(deferredRuns == 1);
	CHECK(newRuns == 0);
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_reused_identifier_does_not_inherit_consumed_cancellations)
{
	TaskReactor reactor;
	startReactor(reactor);
	int oldRuns = 0;
	int newRuns = 0;
	const auto oldId = reactor.schedule(0, [&] { ++oldRuns; });
	reactor.cancel(oldId);
	reactor.cancel(oldId);
	reactor.runOnce();
	CHECK(oldRuns == 0);
	CHECK(!reactor.hasPendingTasks());

	// A cancellation issued after retirement must not wait around for reuse.
	reactor.cancel(oldId);
	CHECK(!reactor.hasPendingTasks());
	TaskReactorTestAccess::setLastIdentifier(reactor, (std::numeric_limits<uint32_t>::max)());
	const auto newId = reactor.schedule(0, [&] { ++newRuns; });
	CHECK(newId == oldId);
	reactor.runOnce();
	CHECK(newRuns == 1);
	CHECK(!reactor.hasPendingTasks());
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
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_shutdown_wakes_run_loop)
{
	TaskReactor reactor;
	startReactor(reactor);
	std::atomic_bool enteredLoop = false;
	std::atomic_bool loopExited = false;

	reactor.send([&enteredLoop] { enteredLoop.store(true, std::memory_order_release); });
	std::jthread reactorThread([&reactor, &loopExited] {
		reactor.runLoop();
		loopExited.store(true, std::memory_order_release);
	});

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	while (!enteredLoop.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::yield();
	}

	CHECK(enteredLoop.load(std::memory_order_acquire));
	reactor.shutdown();

	// join() has no deadline of its own, so a missed wakeup hangs the whole suite
	// instead of failing it. Bound the wait; if it expires, notify once more — by
	// then the loop is definitely registered on the condition variable — so the
	// test can report the failure rather than block ctest until CI times out.
	const auto exitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!loopExited.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < exitDeadline) {
		std::this_thread::yield();
	}
	const bool exitedInTime = loopExited.load(std::memory_order_acquire);
	if (!exitedInTime) {
		reactor.shutdown();
	}

	reactorThread.join();
	CHECK(exitedInTime);
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

// A task kept in the persistent ready heap by the fairness limit must remain
// cancellable before the next cycle.
TEST_CASE(test_reactor_cancel_while_deferred_by_fairness_limit_prevents_execution)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxTasksPerCycle(1);

	bool firstExecuted = false;
	bool deferredExecuted = false;

	reactor.schedule(0, [&firstExecuted] { firstExecuted = true; });
	const uint32_t deferredIdentifier = reactor.schedule(0, [&deferredExecuted] { deferredExecuted = true; });

	// Only one task may run this cycle; the second is deferred, still registered.
	reactor.runOnce();
	CHECK(firstExecuted);
	CHECK(!deferredExecuted);

	reactor.cancel(deferredIdentifier);
	reactor.setMaxTasksPerCycle(0);

	reactor.runOnce();
	reactor.runOnce();
	CHECK(!deferredExecuted);
}

// Same contract under the time budget, which is the other deferral path.
TEST_CASE(test_reactor_cancel_while_deferred_by_time_budget_prevents_execution)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setTimeBudget(std::chrono::milliseconds(1));

	bool deferredExecuted = false;

	// The first callback alone overruns the budget, so everything after it defers.
	reactor.schedule(0, [] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); });
	const uint32_t deferredIdentifier = reactor.schedule(0, [&deferredExecuted] { deferredExecuted = true; });

	reactor.runOnce();
	CHECK(!deferredExecuted);

	reactor.cancel(deferredIdentifier);
	reactor.setTimeBudget(std::chrono::milliseconds(0));

	reactor.runOnce();
	reactor.runOnce();
	CHECK(!deferredExecuted);
}

// A deferred task that is *not* cancelled must still run on a later cycle, so the
// cancellation fix does not silently drop legitimate work.
TEST_CASE(test_reactor_deferred_task_still_runs_when_not_cancelled)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxTasksPerCycle(1);

	int executions = 0;
	reactor.schedule(0, [] {});
	reactor.schedule(0, [&executions] { ++executions; });

	reactor.runOnce();
	CHECK(executions == 0);

	reactor.runOnce();
	CHECK(executions == 1);

	reactor.runOnce();
	CHECK(executions == 1); // runs exactly once
}

TEST_CASE(test_reactor_soft_inbox_limit_never_drops_gameplay_tasks)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxInboxSize(1);

	int executions = 0;
	CHECK(reactor.send([&executions] { ++executions; }));
	CHECK(reactor.send([&executions] { ++executions; }));
	CHECK(reactor.send([&executions] { ++executions; }));

	const uint32_t scheduled = reactor.schedule(0, [&executions] { ++executions; });
	CHECK(scheduled != 0);
	reactor.runOnce();
	CHECK(executions == 4);
}

TEST_CASE(test_reactor_heap_preserves_order_across_fairness_cycles)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxTasksPerCycle(2);

	std::vector<int> order;
	for (int value = 0; value < 7; ++value) {
		CHECK(reactor.schedule(0, [&order, value] { order.push_back(value); }) != 0);
	}

	while (order.size() < 7) {
		reactor.runOnce();
	}
	const std::vector<int> expected{0, 1, 2, 3, 4, 5, 6};
	CHECK(order == expected);
}

TEST_CASE(test_reactor_cancels_event_still_waiting_in_bounded_inbox)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxInboxDrainPerCycle(1);

	int executions = 0;
	CHECK(reactor.schedule(0, [&executions] { ++executions; }) != 0);
	CHECK(reactor.schedule(0, [&executions] { ++executions; }) != 0);
	const uint32_t cancelledId = reactor.schedule(0, [&executions] { executions += 100; });
	CHECK(cancelledId != 0);
	reactor.cancel(cancelledId);

	for (int cycle = 0; cycle < 5; ++cycle) {
		reactor.runOnce();
	}
	CHECK(executions == 2);
}

TEST_CASE(test_reactor_bounds_expiring_work_but_preserves_critical_work)
{
	TaskReactor reactor;
	startReactor(reactor);
	reactor.setMaxInboxSize(2);

	int executed = 0;
	CHECK(reactor.canAcceptExpiringTask());
	CHECK(reactor.send(std::chrono::seconds(1), [&executed] { ++executed; }));
	CHECK(reactor.send(std::chrono::seconds(1), [&executed] { ++executed; }));
	CHECK(!reactor.canAcceptExpiringTask());
	CHECK(!reactor.send(std::chrono::seconds(1), [&executed] { executed += 100; }));
	CHECK(reactor.getPendingTaskCount() == 2);

	// Lossless work is accepted even while expiring work is shedding.
	CHECK(reactor.send([&executed] { executed += 10; }));
	CHECK(reactor.getPendingTaskCount() == 3);
	reactor.runOnce();
	CHECK(executed == 12);
	CHECK(reactor.getPendingTaskCount() == 0);
	reactor.shutdown();
}

TEST_CASE(test_scheduler_dispatcher_move_only_pipeline)
{
	g_dispatcher.start();
	g_scheduler.start();

	auto payload = std::make_unique<int>(42);
	int result = 0;
	const uint32_t eventId = g_scheduler.addEvent(0, [payload = std::move(payload), &result] { result = *payload; });

	CHECK(eventId != 0);
	CHECK(!payload);
	g_reactor.runOnce();
	CHECK(result == 42);

	g_scheduler.stop();
	CHECK(g_scheduler.addEvent(0, [] {}) == 0);

	bool dispatcherAcceptedAfterStop = false;
	g_dispatcher.stop();
	CHECK(!g_dispatcher.addTask([&dispatcherAcceptedAfterStop] { dispatcherAcceptedAfterStop = true; }));
	g_reactor.runOnce();
	CHECK(!dispatcherAcceptedAfterStop);

	g_scheduler.shutdown();
	g_dispatcher.shutdown();
}

TEST_CASE(test_reactor_reused_ingestion_storage_releases_completed_captures)
{
	TaskReactor reactor;
	reactor.start();
	for (int cycle = 0; cycle < 3; ++cycle) {
		auto value = std::make_shared<int>(cycle);
		std::weak_ptr<int> observer = value;
		CHECK(reactor.send([value] {}));
		value.reset();
		reactor.runOnce();
		CHECK(observer.expired());
		CHECK(TaskReactorTestAccess::largestScratchCapacity(reactor) <= REACTOR_MAX_RETAINED_SCRATCH);
	}
	reactor.shutdown();
}

TEST_CASE(test_reactor_ingestion_scratch_does_not_retain_unbounded_burst)
{
	TaskReactor reactor;
	reactor.start();
	reactor.setMaxInboxDrainPerCycle(0);
	int executed = 0;
	for (size_t i = 0; i < REACTOR_MAX_RETAINED_SCRATCH + 1; ++i) {
		CHECK(reactor.send([&executed] { ++executed; }));
	}
	reactor.runOnce();
	CHECK(executed == REACTOR_MAX_RETAINED_SCRATCH + 1);
	CHECK(!reactor.hasPendingTasks());
	CHECK(TaskReactorTestAccess::largestScratchCapacity(reactor) <= REACTOR_MAX_RETAINED_SCRATCH);
	reactor.shutdown();
}

TEST_CASE(test_bounded_ingestion_selects_earliest_timer_before_insertion_order)
{
	TaskReactor reactor;
	reactor.start();
	reactor.setMaxInboxDrainPerCycle(1);
	std::vector<int> order;
	const auto latest = reactor.schedule(0, [&] { order.push_back(3); });
	const auto earliest = reactor.schedule(0, [&] { order.push_back(1); });
	const auto middle = reactor.schedule(0, [&] { order.push_back(2); });
	const auto now = std::chrono::steady_clock::now();
	TaskReactorTestAccess::setScheduledTimes(reactor, {{latest, now - std::chrono::milliseconds(100)},
	                                                   {earliest, now - std::chrono::milliseconds(300)},
	                                                   {middle, now - std::chrono::milliseconds(200)}});
	for (int cycle = 0; cycle < 3; ++cycle) reactor.runOnce();
	CHECK(order == std::vector<int>({1, 2, 3}));
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_bounded_ingestion_does_not_overtake_an_older_undrained_timer)
{
	TaskReactor reactor;
	reactor.start();
	std::vector<int> order;
	const auto now = std::chrono::steady_clock::now();
	const auto alreadyInHeap = reactor.schedule(0, [&] { order.push_back(3); });
	TaskReactorTestAccess::setScheduledTimes(reactor, {{alreadyInHeap, now - std::chrono::milliseconds(100)}});
	TaskReactorTestAccess::ingestWithoutExecuting(reactor);
	const auto first = reactor.schedule(0, [&] { order.push_back(1); });
	const auto second = reactor.schedule(0, [&] { order.push_back(2); });
	TaskReactorTestAccess::setScheduledTimes(
	    reactor, {{first, now - std::chrono::milliseconds(300)}, {second, now - std::chrono::milliseconds(200)}});
	reactor.setMaxInboxDrainPerCycle(1);
	reactor.runOnce();
	CHECK(order == std::vector<int>({1}));
	reactor.runOnce();
	CHECK(order == std::vector<int>({1, 2, 3}));
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_bounded_ingestion_keeps_sequence_order_for_equal_timer_deadlines)
{
	TaskReactor reactor;
	reactor.start();
	reactor.setMaxInboxDrainPerCycle(1);
	std::vector<int> order;
	const auto now = std::chrono::steady_clock::now();
	std::vector<std::pair<uint32_t, std::chrono::steady_clock::time_point>> times;
	for (int value = 0; value < 12; ++value) {
		times.emplace_back(reactor.schedule(0, [&, value] { order.push_back(value); }), now);
	}
	TaskReactorTestAccess::setScheduledTimes(reactor, times);
	for (int cycle = 0; cycle < 12; ++cycle) reactor.runOnce();
	CHECK(order.size() == 12);
	for (int index = 0; index < 12; ++index) CHECK(order[index] == index);
}

TEST_CASE(test_reactor_repeated_cancellation_is_deduplicated_until_retirement)
{
	TaskReactor reactor;
	reactor.start();
	bool executed = false;
	const auto id = reactor.schedule(std::chrono::hours(1), [&] { executed = true; });
	CHECK(id != 0);
	for (int i = 0; i < 10000; ++i) reactor.cancel(id);
	CHECK(reactor.getPendingTaskCount() == 2); // One timer and one control request.
	reactor.runOnce();
	CHECK(reactor.getPendingTaskCount() == 1);
	for (int i = 0; i < 10000; ++i) reactor.cancel(id);
	CHECK(reactor.getPendingTaskCount() == 1); // Already ingested cancellation.
	TaskReactorTestAccess::makeHeapDue(reactor);
	reactor.runOnce();
	CHECK(!executed);
	CHECK(!reactor.hasPendingTasks());
	TaskReactorTestAccess::setLastIdentifier(reactor, id - 1);
	CHECK(reactor.schedule(0, [&] { executed = true; }) == id);
	reactor.runOnce();
	CHECK(executed); // No stale cancellation can target a reused identifier.
}

TEST_CASE(test_reactor_cancelled_due_tasks_consume_the_cycle_limit)
{
	TaskReactor reactor;
	reactor.start();
	reactor.setMaxInboxDrainPerCycle(0);
	reactor.setMaxTasksPerCycle(3);
	int executed = 0;
	for (int i = 0; i < 50; ++i) reactor.cancel(reactor.schedule(0, [&] { ++executed; }));
	reactor.runOnce();
	CHECK(executed == 0);
	CHECK(reactor.getPendingTaskCount() == 47);
	reactor.setMaxTasksPerCycle(0);
	reactor.runOnce();
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_expired_due_tasks_consume_the_cycle_limit)
{
	TaskReactor reactor;
	reactor.start();
	reactor.setMaxInboxDrainPerCycle(0);
	reactor.setMaxTasksPerCycle(3);
	int executed = 0;
	for (int i = 0; i < 50; ++i) CHECK(reactor.send(std::chrono::milliseconds(-1), [&] { ++executed; }));
	reactor.runOnce();
	CHECK(executed == 0);
	CHECK(reactor.getPendingTaskCount() == 47);
	reactor.setMaxTasksPerCycle(0);
	reactor.runOnce();
	CHECK(!reactor.hasPendingTasks());
}

TEST_CASE(test_reactor_heap_policy_is_proportional_and_overflow_safe)
{
	using reactor_detail::shouldRebuildHeap;
	CHECK(!shouldRebuildHeap(0, 0));
	CHECK(shouldRebuildHeap(0, 1024));
	CHECK(shouldRebuildHeap(64, 64));
	CHECK(!shouldRebuildHeap(100000, 64));
	CHECK(!shouldRebuildHeap(100000, 1024));
	CHECK(!shouldRebuildHeap(200000, 1024));
	CHECK(shouldRebuildHeap(100000, 32768));
	const auto maximum = std::numeric_limits<size_t>::max();
	CHECK(!shouldRebuildHeap(maximum, 0));
	CHECK(!shouldRebuildHeap(maximum, 1024));
	CHECK(shouldRebuildHeap(maximum, maximum));
}

TEST_CASE(test_reactor_large_future_heap_accepts_small_earlier_batch)
{
	TaskReactor reactor;
	reactor.start();
	reactor.setMaxInboxSize(0);
	reactor.setMaxInboxDrainPerCycle(0);
	for (int i = 0; i < 100000; ++i) {
		CHECK(reactor.schedule(std::chrono::hours(1), [] {}) != 0);
	}
	TaskReactorTestAccess::ingestWithoutExecuting(reactor);
	CHECK(TaskReactorTestAccess::validHeap(reactor));
	std::vector<int> order;
	for (int i = 0; i < 64; ++i) CHECK(reactor.send([&, i] { order.push_back(i); }));
	TaskReactorTestAccess::expectIncrementalSendIngestion(reactor);
	reactor.runOnce();
	CHECK(TaskReactorTestAccess::validHeap(reactor));
	CHECK(reactor.getPendingTaskCount() == 100000);
	CHECK(order.size() == 64);
	for (int i = 0; i < 64; ++i) CHECK(order[i] == i);
}

TEST_CASE(test_reactor_heap_mixed_insertion_cancellation_deferral_and_equal_deadlines)
{
	TaskReactor reactor;
	reactor.start();
	std::vector<int> order;
	std::vector<uint32_t> future;
	for (int i = 0; i < 20; ++i) {
		future.push_back(reactor.schedule(std::chrono::hours(1), [&] { order.push_back(-1); }));
	}
	TaskReactorTestAccess::ingestWithoutExecuting(reactor);
	const auto due = TaskReactorTestAccess::Clock::now() - std::chrono::seconds(2);
	uint32_t victim = 0;
	const auto first = reactor.schedule(0, [&] {
		order.push_back(1);
		reactor.cancel(victim);
		reactor.cancel(victim);
		const auto earlier = reactor.schedule(0, [&] { order.push_back(2); });
		const auto equal = reactor.schedule(0, [&] { order.push_back(4); });
		TaskReactorTestAccess::setScheduledTimes(reactor, {{earlier, due - std::chrono::seconds(1)}, {equal, due}});
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
	});
	victim = reactor.schedule(0, [&] { order.push_back(-2); });
	const auto tail = reactor.schedule(0, [&] { order.push_back(3); });
	TaskReactorTestAccess::setScheduledTimes(reactor, {{first, due}, {victim, due}, {tail, due}});
	reactor.setTimeBudget(std::chrono::milliseconds(1));
	reactor.runOnce(); // First callback consumes the budget, deferring victim and tail.
	CHECK(order == std::vector<int>({1}));
	CHECK(TaskReactorTestAccess::validHeap(reactor));
	reactor.setTimeBudget(std::chrono::milliseconds(0));
	reactor.setMaxInboxDrainPerCycle(1);
	reactor.setMaxTasksPerCycle(3);
	reactor.runOnce(); // Earlier new timer precedes deferred work; equal new timer remains in inbox.
	CHECK(order == std::vector<int>({1, 2, 3}));
	CHECK(TaskReactorTestAccess::validHeap(reactor));
	reactor.runOnce();
	CHECK(order == std::vector<int>({1, 2, 3, 4})); // Sequence breaks the tie with the older tail.
	for (auto id : future) reactor.cancel(id);
	TaskReactorTestAccess::makeHeapDue(reactor);
	for (int i = 0; i < 10 && reactor.hasPendingTasks(); ++i) reactor.runOnce();
	CHECK(!reactor.hasPendingTasks());
	CHECK(order == std::vector<int>({1, 2, 3, 4}));
}

TFS_TEST_MAIN()
