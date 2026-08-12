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
