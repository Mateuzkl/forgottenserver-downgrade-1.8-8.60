#include "../otpch.h"

#include "../reactor.h"
#include "test_support.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {

constexpr int ProducerThreads = 16;
constexpr int TasksPerProducer = 12500;
constexpr int TotalTasks = ProducerThreads * TasksPerProducer;

void drainUntil(TaskReactor& reactor, const std::atomic<int>& completed, int expected)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (completed.load(std::memory_order_acquire) < expected && std::chrono::steady_clock::now() < deadline) {
		reactor.runOnce();
		std::this_thread::yield();
	}
	CHECK(completed.load(std::memory_order_acquire) == expected);
}

} // namespace

TEST_CASE(test_reactor_200k_concurrent_producer_stress)
{
	TaskReactor reactor;
	reactor.setMaxInboxSize(TotalTasks + 1024);
	reactor.start();

	std::atomic<int> completed{0};
	std::atomic<int> accepted{0};
	const auto started = std::chrono::steady_clock::now();
	{
		std::vector<std::jthread> producers;
		producers.reserve(ProducerThreads);
		for (int thread = 0; thread < ProducerThreads; ++thread) {
			producers.emplace_back([&reactor, &completed, &accepted] {
				for (int task = 0; task < TasksPerProducer; ++task) {
					if (reactor.send([&completed] { completed.fetch_add(1, std::memory_order_release); })) {
						accepted.fetch_add(1, std::memory_order_relaxed);
					}
				}
			});
		}
	}

	CHECK(accepted.load(std::memory_order_relaxed) == TotalTasks);
	drainUntil(reactor, completed, TotalTasks);
	const auto elapsed =
	    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
	std::cout << "Concurrent reactor stress: " << TotalTasks << " tasks in " << elapsed.count() << " ms\n";
}

TEST_CASE(test_reactor_250k_fairness_cycles_stress)
{
	TaskReactor reactor;
	reactor.setMaxInboxSize(260000);
	reactor.setMaxTasksPerCycle(1024);
	reactor.setMaxInboxDrainPerCycle(1024);
	reactor.start();

	constexpr int scheduledTasks = 250000;
	std::atomic<int> completed{0};
	for (int task = 0; task < scheduledTasks; ++task) {
		CHECK(reactor.schedule(0, [&completed] { completed.fetch_add(1, std::memory_order_release); }) != 0);
	}

	const auto started = std::chrono::steady_clock::now();
	int cycles = 0;
	auto maximumCycle = std::chrono::steady_clock::duration::zero();
	while (completed.load(std::memory_order_acquire) < scheduledTasks && cycles < 1000) {
		const auto cycleStarted = std::chrono::steady_clock::now();
		reactor.runOnce();
		maximumCycle = std::max(maximumCycle, std::chrono::steady_clock::now() - cycleStarted);
		++cycles;
	}
	CHECK(completed.load(std::memory_order_acquire) == scheduledTasks);
	CHECK(cycles >= scheduledTasks / 1024);
	const auto elapsed =
	    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
	const auto maximumCycleMicros = std::chrono::duration_cast<std::chrono::microseconds>(maximumCycle);
	std::cout << "Fairness reactor stress: " << scheduledTasks << " tasks, " << cycles << " cycles, " << elapsed.count()
	          << " ms total, " << maximumCycleMicros.count() << " us max cycle\n";
}

TFS_TEST_MAIN()
