// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in LICENSE.
#include "../otpch.h"

#include "../configmanager.h"
#include "../database.h"
#include "../logger.h"
#include "../performance_metrics.h"
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
thread_local bool active = false;
thread_local uint64_t calls = 0;
thread_local uint64_t bytes = 0;

void record(size_t size) noexcept
{
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

namespace {
using Clock = std::chrono::steady_clock;
const std::string description = "stats gate operation with description exceeding the small string optimization";
const std::string origin = "data/events/stats_gate_origin_with_a_long_enough_name.lua:42";

std::string readFile(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

class ProbeStat : public AutoStat
{
public:
	using AutoStat::AutoStat;
	bool timed() const noexcept { return time_point != Clock::time_point{}; }
};

class ProbeRecursive : public AutoStatRecursive
{
public:
	using AutoStatRecursive::AutoStatRecursive;
	bool timed() const noexcept { return time_point != Clock::time_point{}; }
	uint64_t excluded() const noexcept { return minusTime; }
	uint64_t elapsed() const
	{
		return static_cast<uint64_t>(
		    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - time_point).count());
	}
};

struct Fixture
{
	std::filesystem::path previousDirectory = std::filesystem::current_path();
	std::filesystem::path directory =
	    std::filesystem::temp_directory_path() /
	    ("tfs-stats-work-gate-" + std::to_string(Clock::now().time_since_epoch().count()));
	bool previousEnabled = g_stats.isEnabled();
	bool previousMetrics = g_performanceMetrics.isEnabled();
	bool previousFileLog = ConfigManager::getBoolean(ConfigManager::LOG_TO_FILE);
	bool previousWarning = ConfigManager::getBoolean(ConfigManager::SLOW_TASK_WARNING);
	int64_t previousDump = ConfigManager::getInteger(ConfigManager::STATS_DUMP_INTERVAL);
	int64_t previousSlow = ConfigManager::getInteger(ConfigManager::STATS_SLOW_LOG_TIME);
	int64_t previousVerySlow = ConfigManager::getInteger(ConfigManager::STATS_VERY_SLOW_LOG_TIME);
	int64_t previousBudget = ConfigManager::getInteger(ConfigManager::REACTOR_TIME_BUDGET_MS);
	Dispatcher dispatcher;

	Fixture(bool enabled, bool running, bool warnings = false)
	{
		CHECK(!g_stats.isRunning());
		CHECK(!isLoggerInitialized());
		CHECK(std::filesystem::create_directory(directory));
		std::filesystem::current_path(directory);
		g_performanceMetrics.setEnabled(false);
		ConfigManager::setBoolean(ConfigManager::LOG_TO_FILE, true);
		ConfigManager::setBoolean(ConfigManager::SLOW_TASK_WARNING, warnings);
		ConfigManager::setInteger(ConfigManager::STATS_DUMP_INTERVAL, 60);
		ConfigManager::setInteger(ConfigManager::STATS_SLOW_LOG_TIME, 0);
		ConfigManager::setInteger(ConfigManager::STATS_VERY_SLOW_LOG_TIME, 0);
		ConfigManager::setInteger(ConfigManager::REACTOR_TIME_BUDGET_MS, 1);
		g_stats.setEnabled(enabled);
		g_stats.prepareFileLogging();
		CHECK(g_stats.getFileLoggingStatus().available);
		if (warnings) CHECK(initLogger(LogLevel::WARNING, (directory / "warning.log").string(), 1024 * 1024, 1, true));
		if (running) g_stats.start();
		CHECK(g_stats.isRunning() == running);
		dispatcher.start();
	}

	void stopStats()
	{
		if (g_stats.isRunning()) g_stats.shutdown();
		g_stats.join();
	}

	~Fixture()
	{
		stopStats();
		dispatcher.shutdown();
		shutdownLogger();
		g_stats.setEnabled(previousEnabled);
		g_performanceMetrics.setEnabled(previousMetrics);
		ConfigManager::setBoolean(ConfigManager::LOG_TO_FILE, previousFileLog);
		ConfigManager::setBoolean(ConfigManager::SLOW_TASK_WARNING, previousWarning);
		ConfigManager::setInteger(ConfigManager::STATS_DUMP_INTERVAL, previousDump);
		ConfigManager::setInteger(ConfigManager::STATS_SLOW_LOG_TIME, previousSlow);
		ConfigManager::setInteger(ConfigManager::STATS_VERY_SLOW_LOG_TIME, previousVerySlow);
		ConfigManager::setInteger(ConfigManager::REACTOR_TIME_BUDGET_MS, previousBudget);
		std::filesystem::current_path(previousDirectory);
		std::error_code ignored;
		std::filesystem::remove_all(directory, ignored);
	}

	std::string statsText(std::string_view file)
	{
		stopStats();
		return readFile(directory / "data/logs/stats" / file);
	}

	std::string warningText()
	{
		shutdownLogger();
		std::string text;
		for (const auto& entry : std::filesystem::directory_iterator(directory)) {
			if (entry.is_regular_file() && entry.path().extension() == ".log") text += readFile(entry.path());
		}
		return text;
	}
};

void checkInactive(bool enabled, bool running)
{
	Fixture fixture(enabled, running);
	uint64_t allocations = 0;
	{
		allocation_probe::Window window;
		ProbeStat simple(description, origin);
		ProbeRecursive outer(description, origin);
		ProbeRecursive child(description, origin);
		CHECK(!simple.timed());
		CHECK(!outer.timed());
		CHECK(!child.timed());
		allocations = allocation_probe::calls;
	}
	CHECK(allocations == 0);
	int calls = 0;
	fixture.dispatcher.executeTask(createTaskWithStats([&calls] { ++calls; }, description, origin));
	CHECK(calls == 1);
}
} // namespace

TEST_CASE(disabled_stats_do_not_allocate_or_time_unobserved_scopes) { checkInactive(false, false); }
TEST_CASE(disabled_running_stats_do_not_allocate_or_time_unobserved_scopes) { checkInactive(false, true); }
TEST_CASE(enabled_stopped_stats_do_not_allocate_or_time_unobserved_scopes) { checkInactive(true, false); }

TEST_CASE(active_monitor_keeps_samples_descriptions_origins_and_execution)
{
	Fixture fixture(true, true);
	ConfigManager::setInteger(ConfigManager::STATS_SLOW_LOG_TIME, 1);
	{
		ProbeStat scope(description, origin);
		CHECK(scope.timed());
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	int calls = 0;
	auto task = createTaskWithStats(
	    [&calls] {
		    ++calls;
		    std::this_thread::sleep_for(std::chrono::milliseconds(2));
	    },
	    description, origin);
#ifdef STATS_ENABLED
	CHECK(task->description == description);
	CHECK(task->extraDescription == origin);
#endif
	fixture.dispatcher.executeTask(std::move(task));
	CHECK(calls == 1);
	CHECK(fixture.statsText("special.log").find(description) != std::string::npos);
#ifdef STATS_ENABLED
	const auto dispatcherStats = fixture.statsText("dispatcher.log");
	CHECK(dispatcherStats.find(description) != std::string::npos);
	CHECK(fixture.statsText("dispatcher_slow.log").find(origin) != std::string::npos);
#endif
}

TEST_CASE(nested_exclusive_time_survives_disabling_and_reenabling_collection)
{
	Fixture fixture(true, true);
	{
		ProbeRecursive outer("gate_active_outer");
		CHECK(outer.timed());
		g_stats.setEnabled(false);
		{
			ProbeRecursive child("gate_disabled_child");
			CHECK(child.timed());
			{
				ProbeRecursive leaf("gate_disabled_leaf");
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			CHECK(child.excluded() > 0);
			CHECK(child.excluded() <= child.elapsed());
		}
		CHECK(outer.excluded() > 0);
		CHECK(outer.excluded() <= outer.elapsed());
		g_stats.setEnabled(true);
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	const auto text = fixture.statsText("special.log");
	CHECK(text.find("gate_active_outer") != std::string::npos);
	CHECK(text.find("gate_disabled_child") == std::string::npos);
	CHECK(text.find("gate_disabled_leaf") == std::string::npos);
}

TEST_CASE(enabling_inside_an_unobserved_outer_does_not_subtract_an_unset_timestamp)
{
	Fixture fixture(false, true);
	{
		ProbeRecursive outer("gate_unobserved_outer");
		CHECK(!outer.timed());
		g_stats.setEnabled(true);
		{
			ProbeRecursive child("gate_newly_enabled_child");
			CHECK(child.timed());
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		CHECK(outer.excluded() == 0);
	}
	const auto text = fixture.statsText("special.log");
	CHECK(text.find("gate_unobserved_outer") == std::string::npos);
	CHECK(text.find("gate_newly_enabled_child") != std::string::npos);
}

TEST_CASE(stopping_monitor_during_a_scope_discards_sample_and_keeps_nested_time_valid)
{
	Fixture fixture(true, true);
	{
		ProbeRecursive outer("gate_stopped_outer");
		fixture.stopStats();
		{
			ProbeRecursive child("gate_stopped_child");
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		CHECK(outer.excluded() > 0);
		CHECK(outer.excluded() <= outer.elapsed());
	}
	CHECK(fixture.statsText("special.log").find("gate_stopped_outer") == std::string::npos);
}

TEST_CASE(slow_task_warning_keeps_origin_and_executes_with_stats_off)
{
	Fixture fixture(false, false, true);
	int calls = 0;
	auto task = createTaskWithStats(
	    [&calls] {
		    ++calls;
		    std::this_thread::sleep_for(std::chrono::milliseconds(5));
	    },
	    description, origin);
	task->trackInStats = false;
	CHECK(task->description == description);
	const auto before = fixture.dispatcher.getSlowTaskCount();
	fixture.dispatcher.executeTask(std::move(task));
	CHECK(calls == 1);
	CHECK(fixture.dispatcher.getSlowTaskCount() == before + 1);
	const auto warnings = fixture.warningText();
	CHECK(warnings.find("Slow task detected") != std::string::npos);
	CHECK(warnings.find(description) != std::string::npos);
	CHECK(warnings.find(origin) != std::string::npos);
}

TEST_CASE(task_exclusion_and_expiration_remain_effective_with_monitor_active)
{
	Fixture fixture(true, true);
	int calls = 0;
	auto excluded = createTaskWithStats([&calls] { ++calls; }, "gate_excluded_task", origin);
	excluded->trackInStats = false;
	fixture.dispatcher.executeTask(std::move(excluded));
	auto expired = std::make_unique<Task>(1, [&calls] { ++calls; }, "gate_expired_task", origin);
	std::this_thread::sleep_for(std::chrono::milliseconds(3));
	fixture.dispatcher.executeTask(std::move(expired));
	CHECK(calls == 1);
	CHECK(fixture.dispatcher.getTotalTasksProcessed() == 1);
	const auto text = fixture.statsText("dispatcher.log");
	CHECK(text.find("gate_excluded_task") == std::string::npos);
	CHECK(text.find("gate_expired_task") == std::string::npos);
}

TFS_TEST_MAIN()
