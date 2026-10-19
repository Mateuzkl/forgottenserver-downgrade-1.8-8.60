// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../reactor.h"
#include "../reactor_heap.h"

#include <algorithm>
#include <iostream>
#include <random>
#include <string_view>

// Standalone CSV probe, deliberately not registered in CTest: timings are not
// pass/fail assertions. Setup, allocation and verification are outside samples.
struct TaskReactorTestAccess
{
	using Task = TaskReactor::Task;
	using Clock = std::chrono::steady_clock;
	static bool compare(const Task& lhs, const Task& rhs) { return TaskReactor::taskComesAfter(lhs, rhs); }
	static void ingest(TaskReactor& reactor) { reactor.drainInbox(); }
	static void seed(TaskReactor& reactor, std::vector<Task> heap, std::vector<Task> incoming)
	{
		reactor.taskHeap = std::move(heap);
		reactor.taskHeapSize.store(reactor.taskHeap.size());
		reactor.scheduleInbox = std::move(incoming);
		std::make_heap(reactor.scheduleInbox.begin(), reactor.scheduleInbox.end(), compare);
		reactor.setMaxInboxDrainPerCycle(0);
	}
	static bool valid(const TaskReactor& reactor)
	{
		return std::is_heap(reactor.taskHeap.begin(), reactor.taskHeap.end(), compare);
	}
};

namespace {
using Access = TaskReactorTestAccess;
using Task = Access::Task;
using Clock = Access::Clock;
enum class Shape { Random, Increasing, Decreasing, Equal };
enum class Strategy { Rebuild, Incremental, Previous, Adaptive };
volatile uint64_t observedSequence = 0;

std::vector<Task> tasks(size_t count, size_t offset, Shape shape)
{
	std::mt19937 random{12345};
	std::vector<Task> result;
	result.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		const auto index = offset + i;
		int64_t ticks = 0;
		switch (shape) {
			case Shape::Random: ticks = random(); break;
			case Shape::Increasing: ticks = static_cast<int64_t>(index); break;
			case Shape::Decreasing: ticks = -static_cast<int64_t>(index); break;
			case Shape::Equal: break;
		}
		result.push_back(Task{.fireAt = Clock::time_point{std::chrono::hours(100000)} +
		                                   std::chrono::nanoseconds(ticks),
		                      .deadline = Clock::time_point::max(),
		                      .sequence = index,
		                      .description = {}, .origin = {}, .function = [] {}});
	}
	return result;
}

void heapify(std::vector<Task>& heap, size_t existing, size_t incoming, Strategy strategy)
{
	const bool rebuild = strategy == Strategy::Rebuild ||
	                     (strategy == Strategy::Adaptive && reactor_detail::shouldRebuildHeap(existing, incoming)) ||
	                     (strategy == Strategy::Previous &&
	                      (existing == 0 || incoming >= 64 || incoming * 4 >= existing));
	if (rebuild) {
		std::make_heap(heap.begin(), heap.end(), Access::compare);
	} else {
		for (size_t size = existing + 1; size <= heap.size(); ++size) {
			std::push_heap(heap.begin(), heap.begin() + size, Access::compare);
		}
	}
}

void report(std::string_view path, std::string_view shape, size_t existing, size_t incoming,
            std::string_view strategy, std::vector<double> samples)
{
	std::sort(samples.begin(), samples.end());
	std::cout << path << ',' << shape << ',' << existing << ',' << incoming << ',' << strategy << ','
	          << samples[samples.size() / 2] << ',' << samples[(samples.size() - 1) * 95 / 100] << '\n';
}
} // namespace

int main(int argc, char** argv)
{
	const bool integration = argc == 2 && std::string_view(argv[1]) == "--integration";
	if (argc > 2 || (argc == 2 && !integration)) {
		std::cerr << "Usage: bench_reactor_heap [--integration]\n";
		return 2;
	}
#ifdef REACTOR_HEAP_MICRO_ONLY
	if (integration) {
		std::cerr << "The micro-only build does not include reactor integration measurements.\n";
		return 2;
	}
#endif
	std::cout << "path,deadlines,existing,incoming,strategy,median_ns,p95_ns\n";
	for (const auto& [shape, name] : {std::pair{Shape::Random, "random"}, {Shape::Increasing, "increasing"},
	                                 {Shape::Decreasing, "decreasing"}, {Shape::Equal, "equal"}}) {
		for (const size_t existing : {0, 64, 1000, 10000, 100000, 200000}) {
			for (const size_t incoming : {1, 16, 32, 64, 128, 1024, 32768, 200000}) {
				if (incoming > 1024 && incoming > std::max<size_t>(1024, existing)) continue;
				for (const auto strategy : {Strategy::Rebuild, Strategy::Incremental, Strategy::Previous, Strategy::Adaptive}) {
					if (integration && strategy != Strategy::Previous) continue;
					for (const bool runOnce : {false, true}) {
						if (!integration && runOnce) continue;
						std::vector<double> samples;
						for (int repetition = -3; repetition < 21; ++repetition) {
							auto heap = tasks(existing, 0, shape);
							std::make_heap(heap.begin(), heap.end(), Access::compare);
							heap.reserve(existing + incoming);
							auto batch = tasks(incoming, existing, shape);
							double elapsed = 0;
#ifndef REACTOR_HEAP_MICRO_ONLY
							if (integration) {
								TaskReactor reactor;
								Access::seed(reactor, std::move(heap), std::move(batch));
								const auto start = Clock::now();
								if (runOnce) reactor.runOnce();
								else Access::ingest(reactor);
								elapsed = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
								if (!Access::valid(reactor)) return 1;
							} else
#endif
							{
								for (auto& task : batch) heap.push_back(std::move(task));
								const auto start = Clock::now();
								heapify(heap, existing, incoming, strategy);
								elapsed = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
								if (!std::is_heap(heap.begin(), heap.end(), Access::compare)) return 1;
								observedSequence = heap.front().sequence;
							}
							if (repetition >= 0) samples.push_back(elapsed);
						}
						const auto label = strategy == Strategy::Rebuild ? "make_heap" :
						                   strategy == Strategy::Incremental ? "push_heap" :
						                   strategy == Strategy::Adaptive ? "adaptive" : "policy";
						report(integration ? (runOnce ? "runOnce" : "drainInbox") : "heap", name, existing, incoming,
						       label, std::move(samples));
					}
				}
			}
		}
	}
}
