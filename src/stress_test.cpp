#include "otpch.h"

#include "stress_test.h"

#include "configmanager.h"
#include "reactor.h"
#include "scheduler.h"
#include "tasks.h"
#include "logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace {

inline std::string tag() { return fmt::format(fg(fmt::color::cyan), "[Stress Reactor]"); }
inline std::string passTag() { return fmt::format(fg(fmt::color::green), "[PASS]"); }
inline std::string failTag() { return fmt::format(fg(fmt::color::red), "[FAIL]"); }
inline std::string skipTag() { return fmt::format(fg(fmt::color::yellow), "[SKIP]"); }

#define STRESS_CHECK(cond, msg) \
	do { \
		if (!(cond)) { \
			LOG_ERROR("{} {} {} - {}", tag(), failTag(), msg, #cond); \
			return false; \
		} \
	} while (false)

int getCount() { return static_cast<int>(std::max<int64_t>(100, ConfigManager::getInteger(ConfigManager::STRESS_TEST_COUNT))); }
int getThreads() { return static_cast<int>(std::max<int64_t>(1, ConfigManager::getInteger(ConfigManager::STRESS_TEST_THREADS))); }
int getCountFor(ConfigManager::Integer key) { int64_t v = ConfigManager::getInteger(key); return static_cast<int>(v > 0 ? v : getCount()); }
int getThreadsFor(ConfigManager::Integer key) { int64_t v = ConfigManager::getInteger(key); return static_cast<int>(v > 0 ? v : getThreads()); }

void startReactor(TaskReactor& reactor)
{
	reactor.start();
}

bool testSendBulk()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_SEND_COUNT);
	std::atomic<int> counter{0};

	for (int i = 0; i < COUNT; ++i) {
		reactor.send([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
	}
	reactor.runOnce();

	STRESS_CHECK(counter.load() == COUNT, "send all should execute");
	return true;
}

bool testScheduleBulk()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_SCHEDULE_COUNT);
	std::atomic<int> counter{0};

	for (int i = 0; i < COUNT; ++i) {
		reactor.schedule(0, [&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
	}
	reactor.runOnce();

	STRESS_CHECK(counter.load() == COUNT, "schedule all should execute");
	return true;
}

bool testScheduleStaggered()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_STAGGERED_COUNT);
	std::atomic<int> counter{0};

	for (int i = 0; i < COUNT; ++i) {
		reactor.schedule(static_cast<uint32_t>(i % 50), [&counter] {
			counter.fetch_add(1, std::memory_order_relaxed);
		});
	}

	for (int i = 0; i < 100; ++i) {
		reactor.runOnce();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	STRESS_CHECK(counter.load() == COUNT, "staggered all should execute");
	return true;
}

bool testMixedSendSchedule()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_MIXED_COUNT);
	int sendCount = 0;
	int schedCount = 0;

	for (int i = 0; i < COUNT; ++i) {
		if (i % 2 == 0) {
			reactor.send([&sendCount] { ++sendCount; });
		} else {
			reactor.schedule(0, [&schedCount] { ++schedCount; });
		}
	}
	reactor.runOnce();

	STRESS_CHECK(sendCount == COUNT / 2, "half via send");
	STRESS_CHECK(schedCount == COUNT / 2, "half via schedule");
	return true;
}

bool testCancelHalf()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_CANCEL_COUNT);
	std::atomic<int> executed{0};
	std::vector<uint32_t> ids;
	ids.reserve(COUNT);

	for (int i = 0; i < COUNT; ++i) {
		uint32_t id = reactor.schedule(0, [&executed] {
			executed.fetch_add(1, std::memory_order_relaxed);
		});
		ids.push_back(id);
	}

	for (size_t i = 0; i < ids.size(); i += 2) {
		reactor.cancel(ids[i]);
	}

	reactor.runOnce();

	STRESS_CHECK(executed.load() == COUNT / 2, "cancel half should leave half");
	return true;
}

bool testConcurrentPushes()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int THREADS = getThreadsFor(ConfigManager::STRESS_TEST_CONCURRENT_PUSH_THREADS);
	const int TASKS_PER_THREAD = std::max(100, getCountFor(ConfigManager::STRESS_TEST_CONCURRENT_PUSH_COUNT) / THREADS);
	const int TOTAL = THREADS * TASKS_PER_THREAD;
	std::atomic<int> counter{0};
	{
		std::vector<std::jthread> pushers;
		for (int t = 0; t < THREADS; ++t) {
			pushers.emplace_back([&reactor, &counter, TASKS_PER_THREAD]() {
				for (int i = 0; i < TASKS_PER_THREAD; ++i) {
					reactor.send([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
				}
			});
		}
	}

	reactor.runOnce();

	STRESS_CHECK(counter.load() == TOTAL, "concurrent pushes should sum correctly");
	return true;
}

bool testConcurrentSchedule()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int THREADS = getThreadsFor(ConfigManager::STRESS_TEST_CONCURRENT_SCHEDULE_THREADS);
	const int TASKS_PER_THREAD = std::max(100, getCountFor(ConfigManager::STRESS_TEST_CONCURRENT_SCHEDULE_COUNT) / THREADS);
	const int TOTAL = THREADS * TASKS_PER_THREAD;
	std::atomic<int> counter{0};
	{
		std::vector<std::jthread> pushers;
		for (int t = 0; t < THREADS; ++t) {
			pushers.emplace_back([&reactor, &counter, TASKS_PER_THREAD]() {
				for (int i = 0; i < TASKS_PER_THREAD; ++i) {
					reactor.schedule(0, [&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
				}
			});
		}
	}

	reactor.runOnce();

	STRESS_CHECK(counter.load() == TOTAL, "concurrent schedule should sum correctly");
	return true;
}

bool testHeapOrderIntegrity()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = std::min(5000, getCountFor(ConfigManager::STRESS_TEST_HEAP_ORDER_COUNT));
	std::vector<int64_t> fireOrder;
	std::mutex orderMutex;

	for (int i = 0; i < COUNT; ++i) {
		reactor.schedule(static_cast<uint32_t>(i), [i, &fireOrder, &orderMutex] {
			std::scoped_lock lock(orderMutex);
			fireOrder.push_back(i);
		});
	}

	for (int step = 0; step < COUNT + 10; ++step) {
		reactor.runOnce();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}

	STRESS_CHECK(fireOrder.size() == static_cast<size_t>(COUNT), "all should fire");

	bool ordered = true;
	for (size_t i = 1; i < fireOrder.size(); ++i) {
		if (fireOrder[i] < fireOrder[i - 1]) {
			ordered = false;
			break;
		}
	}
	STRESS_CHECK(ordered, "should fire in order");
	return true;
}

bool testUniqueIdentifiers()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int THREADS = getThreadsFor(ConfigManager::STRESS_TEST_UNIQUE_IDS_THREADS);
	const int TASKS_PER_THREAD = std::max(100, getCountFor(ConfigManager::STRESS_TEST_UNIQUE_IDS_COUNT) / THREADS);
	const int TOTAL = THREADS * TASKS_PER_THREAD;
	std::set<uint32_t> allIds;
	std::mutex mutex;

	{
		std::vector<std::jthread> pushers;
		for (int t = 0; t < THREADS; ++t) {
			pushers.emplace_back([&reactor, &mutex, &allIds, TASKS_PER_THREAD]() {
				for (int i = 0; i < TASKS_PER_THREAD; ++i) {
					uint32_t id = reactor.schedule(0, [] {});
					std::scoped_lock lock(mutex);
					allIds.insert(id);
				}
			});
		}
	}

	reactor.shutdown();

	STRESS_CHECK(allIds.size() == static_cast<size_t>(TOTAL), "all IDs unique");
	return true;
}

bool testMoveOnlyPipeline()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = std::min(1000, getCountFor(ConfigManager::STRESS_TEST_MOVE_ONLY_COUNT));
	std::atomic<int> sum{0};

	for (int i = 0; i < COUNT; ++i) {
		auto value = std::make_unique<int>(i + 1);
		reactor.schedule(0, [value = std::move(value), &sum] {
			sum.fetch_add(*value, std::memory_order_relaxed);
		});
	}
	reactor.runOnce();

	STRESS_CHECK(sum.load() == COUNT * (COUNT + 1) / 2, "pipeline sum should match");
	return true;
}

bool testSendWithExpiration()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = std::max(100, getCountFor(ConfigManager::STRESS_TEST_EXPIRATION_COUNT) / 2);
	std::atomic<int> alive{0};
	std::atomic<int> expired{0};

	for (int i = 0; i < COUNT; ++i) {
		reactor.send(std::chrono::milliseconds(1), [&expired] {
			expired.fetch_add(1, std::memory_order_relaxed);
		});
	}
	for (int i = 0; i < COUNT; ++i) {
		reactor.send([&alive] {
			alive.fetch_add(1, std::memory_order_relaxed);
		});
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	reactor.runOnce();

	STRESS_CHECK(alive.load() == COUNT, "all alive should execute");
	STRESS_CHECK(expired.load() == COUNT, "all expired should execute");
	return true;
}

bool testRunLoopBurst()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int BURSTS = 20;
	const int TASKS_PER_BURST = std::max(100, getCountFor(ConfigManager::STRESS_TEST_BURST_COUNT) / BURSTS);
	const int TOTAL = BURSTS * TASKS_PER_BURST;
	std::atomic<int> counter{0};

	{
		std::jthread feeder([&reactor, &counter, TASKS_PER_BURST]() {
			for (int b = 0; b < BURSTS; ++b) {
				for (int i = 0; i < TASKS_PER_BURST; ++i) {
					reactor.send([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
				}
				std::this_thread::sleep_for(std::chrono::microseconds(500));
			}
		});

		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (counter.load() < TOTAL && std::chrono::steady_clock::now() < deadline) {
			reactor.runOnce();
			std::this_thread::yield();
		}
	}

	for (int i = 0; i < 10 && counter.load() < TOTAL; ++i) {
		reactor.runOnce();
		std::this_thread::yield();
	}

	STRESS_CHECK(counter.load() == TOTAL, "burst should sum correctly");
	return true;
}

bool testReentrancy()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_REENTRANCY_COUNT);
	std::atomic<int> counter{0};

	struct ReentrantTask
	{
		TaskReactor& reactor;
		std::atomic<int>& counter;
		const int limit;
		void operator()() const
		{
			int prev = counter.fetch_add(1, std::memory_order_relaxed);
			if (prev + 1 < limit) {
				reactor.send(ReentrantTask{reactor, counter, limit});
			}
		}
	};

	reactor.send(ReentrantTask{reactor, counter, COUNT});

	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (counter.load(std::memory_order_acquire) < COUNT &&
	       std::chrono::steady_clock::now() < deadline) {
		reactor.runOnce();
		std::this_thread::yield();
	}

	STRESS_CHECK(counter.load() == COUNT, "reentrancy chain should complete");
	return true;
}

bool testShutdownPending()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_SHUTDOWN_PENDING_COUNT);
	std::atomic<int> counter{0};

	for (int i = 0; i < COUNT; ++i) {
		reactor.send([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
	}

	reactor.shutdown();
	reactor.runOnce();

	STRESS_CHECK(counter.load() < COUNT, "not all should execute after shutdown");
	return true;
}

bool testCancelExternal()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_CANCEL_EXTERNAL_COUNT);
	const int THREADS = getThreadsFor(ConfigManager::STRESS_TEST_CANCEL_EXTERNAL_THREADS);
	std::atomic<int> executed{0};
	std::mutex idMutex;
	std::vector<uint32_t> ids;

	for (int i = 0; i < COUNT; ++i) {
		uint32_t id = reactor.schedule(0, [&executed] {
			executed.fetch_add(1, std::memory_order_relaxed);
		});
		std::scoped_lock lock(idMutex);
		ids.push_back(id);
	}

	{
		std::vector<std::jthread> cancelers;
		for (int t = 0; t < THREADS; ++t) {
			cancelers.emplace_back([&reactor, &ids, &idMutex, t, THREADS] {
				for (size_t i = t; i < ids.size(); i += THREADS) {
					reactor.cancel(ids[i]);
				}
			});
		}
	}

	reactor.runOnce();

	STRESS_CHECK(executed.load() < COUNT, "some should be cancelled by external threads");
	return true;
}

bool testException()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_EXCEPTION_COUNT);
	std::atomic<int> goodCounter{0};

	// Suppress expected exception logging from TaskReactor
	const auto oldLogLevel = g_logger().getLevel();
	g_logger().setLevel(LogLevel::CRITICAL);

	for (int i = 0; i < COUNT; ++i) {
		if (i % 3 == 0) {
			reactor.send([&goodCounter] {
				goodCounter.fetch_add(1, std::memory_order_relaxed);
			});
		} else if (i % 3 == 1) {
			reactor.send([] {
				throw std::runtime_error("test exception");
			});
		} else {
			reactor.send([] {
				throw 42;
			});
		}
	}
	reactor.runOnce();

	g_logger().setLevel(oldLogLevel);

	int expectedGood = (COUNT + 2) / 3;
	STRESS_CHECK(goodCounter.load() == expectedGood,
	             "exception tasks should not prevent others from executing");
	return true;
}

bool testInspection()
{
	TaskReactor reactor;
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_INSPECTION_COUNT);

	STRESS_CHECK(reactor.getState() == THREAD_STATE_TERMINATED, "initial state should be terminated");

	reactor.start();
	STRESS_CHECK(reactor.getState() == THREAD_STATE_RUNNING, "after start should be running");
	STRESS_CHECK(!reactor.isReactorThread(), "main thread is not reactor thread");

	for (int i = 0; i < COUNT; ++i) {
		reactor.schedule(0, [] {});
	}

	reactor.runOnce();
	reactor.shutdown();
	STRESS_CHECK(reactor.getState() == THREAD_STATE_TERMINATED, "after shutdown should be terminated");

	return true;
}

bool testMixedDelays()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_MIXED_DELAYS_COUNT);
	std::vector<std::pair<uint32_t, uint64_t>> fired;
	std::mutex orderMutex;

	for (int i = 0; i < COUNT; ++i) {
		if (i % 3 == 0) {
			reactor.send([i, &fired, &orderMutex] {
				std::scoped_lock lock(orderMutex);
				fired.emplace_back(0, i);
			});
		} else if (i % 3 == 1) {
			reactor.schedule(0, [i, &fired, &orderMutex] {
				std::scoped_lock lock(orderMutex);
				fired.emplace_back(0, i);
			});
		} else {
			reactor.schedule(static_cast<uint32_t>(i % 20), [i, &fired, &orderMutex] {
				std::scoped_lock lock(orderMutex);
				fired.emplace_back(i % 20, i);
			});
		}
	}

	for (int step = 0; step < 100; ++step) {
		reactor.runOnce();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}

	STRESS_CHECK(fired.size() == static_cast<size_t>(COUNT), "all mixed-delay tasks should fire");

	fired.clear();
	return true;
}

struct LeakTracker
{
	std::atomic<int>& alive;
	LeakTracker(std::atomic<int>& ref) : alive(ref) { alive.fetch_add(1, std::memory_order_relaxed); }
	LeakTracker(const LeakTracker& other) : alive(other.alive) { alive.fetch_add(1, std::memory_order_relaxed); }
	LeakTracker(LeakTracker&& other) noexcept : alive(other.alive) { alive.fetch_add(1, std::memory_order_relaxed); }
	~LeakTracker() { alive.fetch_sub(1, std::memory_order_relaxed); }
};

bool testLeak()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_LEAK_COUNT);
	std::atomic<int> aliveCount{0};

	for (int i = 0; i < COUNT; ++i) {
		auto tracker = std::make_shared<LeakTracker>(aliveCount);
		reactor.send([t = std::move(tracker)] {});
	}

	reactor.runOnce();
	reactor.shutdown();

	STRESS_CHECK(aliveCount.load() == 0, "all trackers should be destroyed");
	return true;
}

bool testShutdownSend()
{
	TaskReactor reactor;
	startReactor(reactor);
	const int COUNT = getCountFor(ConfigManager::STRESS_TEST_SHUTDOWN_SEND_COUNT);

	reactor.shutdown();

	std::atomic<int> counter{0};
	for (int i = 0; i < COUNT; ++i) {
		reactor.send([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
		reactor.schedule(0, [&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
		reactor.cancel(42);
	}

	reactor.runOnce();

	STRESS_CHECK(counter.load() == 0, "no tasks should execute after shutdown");
	return true;
}

struct TestEntry
{
	const char* name;
	ConfigManager::Boolean toggle;
	bool (*function)();
};

const TestEntry TESTS[] = {
	{"send bulk",               ConfigManager::STRESS_TEST_SEND,               testSendBulk},
	{"schedule bulk",           ConfigManager::STRESS_TEST_SCHEDULE,           testScheduleBulk},
	{"schedule staggered",      ConfigManager::STRESS_TEST_STAGGERED,          testScheduleStaggered},
	{"mixed send+schedule",     ConfigManager::STRESS_TEST_MIXED,              testMixedSendSchedule},
	{"cancel half",             ConfigManager::STRESS_TEST_CANCEL,             testCancelHalf},
	{"concurrent pushes",       ConfigManager::STRESS_TEST_CONCURRENT_PUSH,    testConcurrentPushes},
	{"concurrent schedule",     ConfigManager::STRESS_TEST_CONCURRENT_SCHEDULE,testConcurrentSchedule},
	{"heap order integrity",    ConfigManager::STRESS_TEST_HEAP_ORDER,         testHeapOrderIntegrity},
	{"unique identifiers",      ConfigManager::STRESS_TEST_UNIQUE_IDS,         testUniqueIdentifiers},
	{"move-only pipeline",      ConfigManager::STRESS_TEST_MOVE_ONLY,          testMoveOnlyPipeline},
	{"send with expiration",    ConfigManager::STRESS_TEST_EXPIRATION,         testSendWithExpiration},
	{"runLoop burst",           ConfigManager::STRESS_TEST_BURST,              testRunLoopBurst},
	{"reentrancy",              ConfigManager::STRESS_TEST_REENTRANCY,         testReentrancy},
	{"shutdown pending",        ConfigManager::STRESS_TEST_SHUTDOWN_PENDING,   testShutdownPending},
	{"cancel external",         ConfigManager::STRESS_TEST_CANCEL_EXTERNAL,    testCancelExternal},
	{"exception resilience",    ConfigManager::STRESS_TEST_EXCEPTION,          testException},
	{"inspection",              ConfigManager::STRESS_TEST_INSPECTION,         testInspection},
	{"mixed delays",            ConfigManager::STRESS_TEST_MIXED_DELAYS,       testMixedDelays},
	{"leak check",              ConfigManager::STRESS_TEST_LEAK,               testLeak},
	{"shutdown send",           ConfigManager::STRESS_TEST_SHUTDOWN_SEND,      testShutdownSend},
};

constexpr int TEST_COUNT = sizeof(TESTS) / sizeof(TESTS[0]);

} // namespace

void runStressTests()
{
	static std::atomic<bool> testRunning{false};
	bool expected = false;
	if (!testRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
		LOG_ERROR("{} {} Tests already running — concurrent execution denied", tag(), failTag());
		return;
	}

	auto runTests = []() {
		const int count = getCount();
		const int threads = getThreads();
		const bool benchmark = ConfigManager::getBoolean(ConfigManager::STRESS_TEST_BENCHMARK);
		LOG_INFO("{} Running (count={}, threads={})...", tag(),
		    fmt::format(fg(fmt::color::lime_green), "{}", count),
		    fmt::format(fg(fmt::color::lime_green), "{}", threads));

		int passed = 0;
		int failed = 0;

		for (int i = 0; i < TEST_COUNT; ++i) {
			if (!ConfigManager::getBoolean(TESTS[i].toggle)) {
				LOG_INFO("{}   {} {}", tag(), skipTag(), TESTS[i].name);
				continue;
			}

			auto start = std::chrono::steady_clock::now();
			bool ok = TESTS[i].function();
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			    std::chrono::steady_clock::now() - start).count();

			if (ok) {
				++passed;
				if (benchmark) {
					LOG_INFO("{}   {} {} ({} ms)", tag(), passTag(), TESTS[i].name, elapsed);
				} else {
					LOG_INFO("{}   {} {}", tag(), passTag(), TESTS[i].name);
				}
			} else {
				++failed;
				LOG_ERROR("{}   {} {}", tag(), failTag(), TESTS[i].name);
			}
		}

		if (failed == 0) {
			LOG_INFO("{} {} passed, {} skipped", tag(),
			    fmt::format(fg(fmt::color::lime_green), "{}", passed),
			    TEST_COUNT - passed - failed);
		} else {
			LOG_ERROR("{} {} passed, {} {}, {} skipped", tag(),
			    fmt::format(fg(fmt::color::lime_green), "{}", passed),
			    fmt::format(fg(fmt::color::red), "{}", failed),
			    fmt::format(fg(fmt::color::red), "FAILED"),
			    TEST_COUNT - passed - failed);
		}

		testRunning.store(false, std::memory_order_release);
	};

	g_threadPool.detach_task(std::move(runTests));
}
