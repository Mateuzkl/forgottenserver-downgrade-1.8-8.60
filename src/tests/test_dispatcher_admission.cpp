// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in LICENSE.
#include "../otpch.h"

#include "../configmanager.h"
#include "../database.h"
#include "../logger.h"
#include "../performance_metrics.h"
#include "../scheduler.h"
#include "../stats.h"
#include "../tasks.h"
#include "test_support.h"

#include <cstdlib>
#include <fstream>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace allocation_probe {
// Only the calling test thread is counted. Stats' worker allocations cannot
// contaminate the scope. Latency and allocation windows are separate passes.
// Test-only fault injection into a real allocation during runOnce ingestion.
thread_local void (*nextAllocationHook)() = nullptr;
thread_local bool active = false;
thread_local uint64_t calls = 0;
thread_local uint64_t bytes = 0;

void record(size_t size) noexcept
{
	if (auto hook = std::exchange(nextAllocationHook, nullptr)) hook();
	if (active) {
		++calls;
		bytes += size;
	}
}

void* allocate(size_t size)
{
	if (void* pointer = std::malloc(size == 0 ? 1 : size)) {
		record(size);
		return pointer;
	}
	throw std::bad_alloc{};
}

void* allocateAligned(size_t size, size_t alignment)
{
	void* pointer = nullptr;
#ifdef _WIN32
	pointer = _aligned_malloc(size == 0 ? 1 : size, alignment);
#else
	if (posix_memalign(&pointer, alignment, size == 0 ? 1 : size) != 0) pointer = nullptr;
#endif
	if (!pointer) throw std::bad_alloc{};
	record(size);
	return pointer;
}

void releaseAligned(void* pointer) noexcept
{
#ifdef _WIN32
	_aligned_free(pointer);
#else
	std::free(pointer);
#endif
}

struct Window
{
	Window()
	{
		calls = bytes = 0;
		active = true;
	}
	~Window() { active = false; }
};
} // namespace allocation_probe

void* operator new(size_t size) { return allocation_probe::allocate(size); }
void* operator new[](size_t size) { return allocation_probe::allocate(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, size_t) noexcept { std::free(pointer); }
void* operator new(size_t size, std::align_val_t alignment)
{
	return allocation_probe::allocateAligned(size, static_cast<size_t>(alignment));
}
void* operator new[](size_t size, std::align_val_t alignment)
{
	return allocation_probe::allocateAligned(size, static_cast<size_t>(alignment));
}
void operator delete(void* pointer, std::align_val_t) noexcept { allocation_probe::releaseAligned(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { allocation_probe::releaseAligned(pointer); }
void operator delete(void* pointer, size_t, std::align_val_t) noexcept { allocation_probe::releaseAligned(pointer); }
void operator delete[](void* pointer, size_t, std::align_val_t) noexcept { allocation_probe::releaseAligned(pointer); }
void* operator new(size_t size, const std::nothrow_t&) noexcept
{
	try {
		return ::operator new(size);
	} catch (...) {
		return nullptr;
	}
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept
{
	try {
		return ::operator new[](size);
	} catch (...) {
		return nullptr;
	}
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void* operator new(size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	try {
		return ::operator new(size, alignment);
	} catch (...) {
		return nullptr;
	}
}
void* operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	try {
		return ::operator new[](size, alignment);
	} catch (...) {
		return nullptr;
	}
}
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
	allocation_probe::releaseAligned(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
	allocation_probe::releaseAligned(pointer);
}

struct TaskReactorTestAccess
{
	static std::pair<std::string, std::string> nextMetadata(const TaskReactor& reactor)
	{
		std::scoped_lock lock(reactor.mutex);
		CHECK(!reactor.sendInbox.empty());
		return {reactor.sendInbox.front().description, reactor.sendInbox.front().origin};
	}
	static void reserveIngestionBuffers(TaskReactor& reactor, size_t count)
	{
		reactor.sentTasksBuffer.reserve(count);
		reactor.scheduledTasksBuffer.reserve(count);
		reactor.cancellationsBuffer.reserve(count);
	}
	static std::chrono::steady_clock::time_point nextDeadline(const TaskReactor& reactor)
	{
		std::scoped_lock lock(reactor.mutex);
		CHECK(!reactor.sendInbox.empty());
		return reactor.sendInbox.front().deadline;
	}
};

namespace {
const std::string description = "dispatcher metadata description exceeding small string optimization";
const std::string origin = "data/events/dispatcher_metadata_origin_with_a_long_name.lua:42";

struct Fixture
{
	bool oldStats = g_stats.isEnabled();
	bool oldMetrics = g_performanceMetrics.isEnabled();
	bool oldWarnings = getBoolean(ConfigManager::SLOW_TASK_WARNING);
	Fixture()
	{
		CHECK(!g_stats.isRunning());
		CHECK(!g_reactor.hasPendingTasks());
		g_stats.setEnabled(false);
		g_performanceMetrics.setEnabled(false);
		ConfigManager::setBoolean(ConfigManager::SLOW_TASK_WARNING, false);
		g_dispatcher.start();
		g_reactor.setMaxInboxSize(REACTOR_MAX_INBOX_SIZE);
		g_reactor.setMaxInboxDrainPerCycle(REACTOR_MAX_INBOX_DRAIN_PER_CYCLE);
		g_reactor.setMaxTasksPerCycle(0);
		g_reactor.setTimeBudget(std::chrono::milliseconds(0));
	}
	~Fixture()
	{
		// Each case owns this process's global reactor. Drain before captures
		// referring to its fixture can leave scope.
		g_dispatcher.stop();
		g_reactor.setMaxInboxDrainPerCycle(0);
		g_reactor.setMaxTasksPerCycle(0);
		g_reactor.setTimeBudget(std::chrono::milliseconds(0));
		g_reactor.drain();
		g_dispatcher.shutdown();
		g_stats.setEnabled(oldStats);
		g_performanceMetrics.setEnabled(oldMetrics);
		ConfigManager::setBoolean(ConfigManager::SLOW_TASK_WARNING, oldWarnings);
		g_reactor.setMaxInboxSize(REACTOR_MAX_INBOX_SIZE);
		g_reactor.setMaxInboxDrainPerCycle(REACTOR_MAX_INBOX_DRAIN_PER_CYCLE);
	}
};

uint64_t countCreatorAllocations(bool original, size_t iterations)
{
	uint64_t calls = 0;
	{
		allocation_probe::Window window;
		for (size_t index = 0; index < iterations; ++index) {
			// This is the complete original factory body from upstream ecd8ff9.
			// Both paths construct the real Task in the same native executable.
			auto task = original ? std::make_unique<Task>([] {}, description, origin)
			                     : createTaskWithStats([] {}, description, origin);
			if (!original) {
				CHECK(task->description.empty());
				CHECK(task->extraDescription.empty());
			}
		}
		calls = allocation_probe::calls;
	}
	return calls;
}

uint64_t countIdleCycleAllocations(size_t iterations)
{
	TaskReactor reactor;
	reactor.start();
	for (int warmup = 0; warmup < 16; ++warmup) reactor.runOnce();
	uint64_t calls = 0;
	{
		allocation_probe::Window window;
		for (size_t cycle = 0; cycle < iterations; ++cycle) reactor.runOnce();
		calls = allocation_probe::calls;
	}
	reactor.shutdown();
	return calls;
}
} // namespace

TEST_CASE(dispatcher_disabled_metadata_avoids_description_and_origin_allocations)
{
	Fixture fixture;
	const auto original = countCreatorAllocations(true, 128);
	const auto optimized = countCreatorAllocations(false, 128);
	CHECK(original >= optimized + 2 * 128);
	auto timed = createTaskWithStats(1000, [] {}, description, origin);
	CHECK(timed->description.empty());
	CHECK(timed->extraDescription.empty());
	CHECK(timed->hasExpiration());
}

TEST_CASE(dispatcher_metadata_is_preserved_for_each_independent_consumer)
{
	Fixture fixture;
	for (int consumer = 0; consumer < 3; ++consumer) {
		g_stats.setEnabled(consumer == 0);
		g_performanceMetrics.setEnabled(consumer == 1);
		ConfigManager::setBoolean(ConfigManager::SLOW_TASK_WARNING, consumer == 2);
		auto task = createTaskWithStats([] {}, description, origin);
		auto timed = createTaskWithStats(1000, [] {}, description, origin);
#ifndef STATS_ENABLED
		if (consumer == 0) {
			CHECK(task->description.empty());
			CHECK(task->extraDescription.empty());
			CHECK(timed->description.empty());
			CHECK(timed->extraDescription.empty());
			continue;
		}
#endif
		CHECK(task->description == description);
		CHECK(task->extraDescription == origin);
		CHECK(timed->description == description);
		CHECK(timed->extraDescription == origin);
	}
}

TEST_CASE(reactor_idle_cycles_reuse_ready_storage_without_allocations)
{
	Fixture fixture;
	CHECK(countIdleCycleAllocations(1024) == 0);
}

TEST_CASE(dispatcher_source_metadata_is_created_only_when_observed)
{
	Fixture fixture;
	int calls = 0;
	CHECK(g_dispatcher.addTask([&] { ++calls; }));
	auto [label, source] = TaskReactorTestAccess::nextMetadata(g_reactor);
	CHECK(label.empty());
	CHECK(source.empty());
	g_reactor.runOnce();
	CHECK(calls == 1);
	ConfigManager::setBoolean(ConfigManager::SLOW_TASK_WARNING, true);
	CHECK(g_dispatcher.addTask([&] { ++calls; }));
	std::tie(label, source) = TaskReactorTestAccess::nextMetadata(g_reactor);
	CHECK(label.find("dispatcher_source_metadata") != std::string::npos);
	CHECK(source.find("test_dispatcher_admission.cpp:") != std::string::npos);
	g_reactor.runOnce();
	CHECK(calls == 2);
}

TEST_CASE(dispatcher_reports_overload_rejection_and_releases_owned_captures)
{
	Fixture fixture;
	g_reactor.setMaxInboxSize(1);
	int calls = 0;
	CHECK(g_dispatcher.addTask([&] { ++calls; }));
	auto owner = std::make_shared<int>(42);
	std::weak_ptr<int> observer = owner;
	CHECK(!g_dispatcher.addTask(1000, [owner = std::move(owner), &calls] { calls += *owner; }));
	CHECK(observer.expired());
	CHECK(g_dispatcher.addTask([&] { ++calls; }));
	g_reactor.runOnce();
	CHECK(calls == 2);
	CHECK(!g_reactor.hasPendingTasks());
}

TEST_CASE(dispatcher_stopped_and_expired_submissions_report_failure)
{
	Fixture fixture;
	int calls = 0;
	auto expired = std::make_unique<Task>(1, [&] { ++calls; }, description, origin);
	std::this_thread::sleep_for(std::chrono::milliseconds(3));
	CHECK(!g_dispatcher.addTask(std::move(expired)));
	CHECK(!g_reactor.hasPendingTasks());
	g_dispatcher.stop();
	CHECK(!g_dispatcher.addTask([&] { ++calls; }));
	CHECK(!g_dispatcher.addTask(1000, [&] { ++calls; }));
	g_reactor.runOnce();
	CHECK(calls == 0);
}

TEST_CASE(dispatcher_forwards_expiration_without_running_expired_callbacks)
{
	Fixture fixture;
	int calls = 0;
	auto task = std::make_unique<Task>(20, [&] { ++calls; }, description, origin);
	const auto expected = task->getExpiration();
	const auto beforeSubmission = std::chrono::steady_clock::now();
	CHECK(g_dispatcher.addTask(std::move(task)));
	const auto afterSubmission = std::chrono::steady_clock::now();
	const auto forwarded = TaskReactorTestAccess::nextDeadline(g_reactor);
	CHECK(forwarded >= expected);
	CHECK(forwarded - expected <= afterSubmission - beforeSubmission + std::chrono::milliseconds(1));
	std::this_thread::sleep_until(expected + std::chrono::milliseconds(3));
	g_reactor.runOnce();
	CHECK(calls == 0);
	CHECK(!g_reactor.hasPendingTasks());
}

TEST_CASE(dispatcher_set_dont_expire_keeps_authoritative_work_lossless)
{
	Fixture fixture;
	g_reactor.setMaxInboxSize(1);
	int calls = 0;
	CHECK(g_dispatcher.addTask([&] { ++calls; }));
	auto task = std::make_unique<Task>(1000, [&] { ++calls; }, description, origin);
	task->setDontExpire();
	CHECK(!task->hasExpiration());
	CHECK(g_dispatcher.addTask(std::move(task)));
	g_reactor.runOnce();
	CHECK(calls == 2);
}

TEST_CASE(task_macros_and_scheduled_factories_do_not_allocate_unobserved_metadata)
{
	Fixture fixture;
	static_assert(std::string_view(TASK_SOURCE_LOCATION).size() > 32);
	TaskFunc plainCallback = [] {};
	TaskFunc timedCallback = [] {};
	TaskFunc scheduledCallback = [] {};
	uint64_t allocations = 0;
	{
		allocation_probe::Window window;
		auto plain = createTask(std::move(plainCallback));
		auto timed = createTimedTask(1000, std::move(timedCallback));
		auto scheduled = createSchedulerTask(50, std::move(scheduledCallback));
		CHECK(plain->description.empty() && plain->extraDescription.empty());
		CHECK(timed->description.empty() && timed->extraDescription.empty());
		CHECK(scheduled->description.empty() && scheduled->extraDescription.empty());
		CHECK(timed->hasExpiration());
		CHECK(scheduled->getDelay() == 50);
		allocations = allocation_probe::calls;
	}
	CHECK(allocations == 3); // Only the three owned Task objects.
}

TEST_CASE(scheduler_metadata_is_owned_and_preserved_for_each_consumer)
{
	Fixture fixture;
	for (int consumer = 0; consumer < 3; ++consumer) {
		g_stats.setEnabled(consumer == 0);
		g_performanceMetrics.setEnabled(consumer == 1);
		ConfigManager::setBoolean(ConfigManager::SLOW_TASK_WARNING, consumer == 2);
		std::string label = description + " trailing bytes";
		std::string location = origin + " trailing bytes";
		auto task = createSchedulerTaskWithStats(
		    75, [] {}, std::string_view(label.data(), description.size()),
		    std::string_view(location.data(), origin.size()));
		label.clear();
		location.clear();
#ifndef STATS_ENABLED
		if (consumer == 0) {
			CHECK(task->description.empty() && task->extraDescription.empty());
			continue;
		}
#endif
		CHECK(task->description == description);
		CHECK(task->extraDescription == origin);
		CHECK(task->getDelay() == 75);
	}
}

namespace {
bool preprocessingDelayed = false;
void slowPreprocessingAllocation()
{
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	preprocessingDelayed = true;
}

TaskReactor* transferringReactor = nullptr;
size_t producerPendingCount = 0;
bool producerAccepted = true;
void submitDuringHeapAllocation()
{
	std::thread producer([] {
		producerPendingCount = transferringReactor->getPendingTaskCount();
		producerAccepted = transferringReactor->send(std::chrono::seconds(1), [] {});
	});
	producer.join();
}
} // namespace

TEST_CASE(test_reactor_budget_includes_preprocessing_and_still_makes_progress)
{
	Fixture fixture;
	TaskReactor reactor;
	reactor.start();
	reactor.setTimeBudget(std::chrono::milliseconds(1));
	std::vector<int> order;
	order.reserve(3);
	for (int i = 1; i <= 3; ++i) CHECK(reactor.send([&, i] { order.push_back(i); }));
	preprocessingDelayed = false;
	allocation_probe::nextAllocationHook = slowPreprocessingAllocation;
	reactor.runOnce(); // The first ingestion reserve consumes this real cycle's budget.
	allocation_probe::nextAllocationHook = nullptr;
	CHECK(preprocessingDelayed);
	CHECK(order == std::vector<int>({1}));
	CHECK(reactor.hasPendingTasks());
	reactor.setTimeBudget(std::chrono::milliseconds(0));
	reactor.runOnce();
	CHECK(order == std::vector<int>({1, 2, 3}));
}

TEST_CASE(expiring_admission_counts_tasks_during_inbox_to_heap_transfer)
{
	Fixture fixture;
	TaskReactor reactor;
	reactor.start();
	reactor.setMaxInboxSize(3);
	for (int i = 0; i < 3; ++i) CHECK(reactor.send([] {}));
	TaskReactorTestAccess::reserveIngestionBuffers(reactor, 3);
	transferringReactor = &reactor;
	producerPendingCount = 0;
	producerAccepted = true;
	// With ingestion already reserved, the next allocation grows the owner heap
	// after releasing the producer mutex and before publishing its new size.
	allocation_probe::nextAllocationHook = submitDuringHeapAllocation;
	reactor.runOnce();
	allocation_probe::nextAllocationHook = nullptr;
	transferringReactor = nullptr;
	CHECK(producerPendingCount == 3);
	CHECK(!producerAccepted);
	CHECK(!reactor.hasPendingTasks());
}

int main(int argc, char** argv)
{
	if (argc == 2 && std::string_view(argv[1]) == "--allocation-probe") {
		Fixture fixture;
		constexpr size_t iterations = 10000;
		const auto original = countCreatorAllocations(true, iterations);
		const auto optimized = countCreatorAllocations(false, iterations);
		const auto idle = countIdleCycleAllocations(iterations);
		std::cout << "{\"iterations\":" << iterations << ",\"original_factory_allocations\":" << original
		          << ",\"optimized_factory_allocations\":" << optimized
		          << ",\"optimized_idle_reactor_cycle_allocations\":" << idle
		          << ",\"baseline\":\"original_factory_body_same_native_Task\",\"gameplay_measured\":false}\n";
		return original >= optimized + 2 * iterations && idle == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	return tfs::tests::run();
}
