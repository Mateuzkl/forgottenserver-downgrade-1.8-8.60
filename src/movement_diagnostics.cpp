// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "movement_diagnostics.h"

#include "logger.h"
#include "reactor.h"
#include "scheduler.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

inline double toMilliseconds(std::chrono::steady_clock::duration duration) noexcept
{
	return std::chrono::duration<double, std::milli>(duration).count();
}

MetricPercentiles calculatePercentiles(std::vector<double>& values)
{
	MetricPercentiles result;
	if (values.empty()) {
		return result;
	}

	result.count = values.size();
	std::sort(values.begin(), values.end());

	result.min = values.front();
	result.max = values.back();

	const double sum = std::accumulate(values.begin(), values.end(), 0.0);
	result.avg = sum / static_cast<double>(result.count);

	auto percentileAt = [&values](double p) -> double {
		if (values.empty()) {
			return 0.0;
		}
		const size_t index = static_cast<size_t>(std::ceil(p * static_cast<double>(values.size()))) - 1;
		return values[std::min(index, values.size() - 1)];
	};

	result.p50 = percentileAt(0.50);
	result.p95 = percentileAt(0.95);
	result.p99 = percentileAt(0.99);

	return result;
}

} // namespace

void MovementDiagnostics::setEnabled(bool value) noexcept
{
	enabled.store(value, std::memory_order_release);
	if (value) {
		const auto now = std::chrono::steady_clock::now().time_since_epoch();
		const auto interval = std::chrono::seconds(reportIntervalSeconds.load(std::memory_order_relaxed));
		nextReportNanoseconds.store((now + interval).count(), std::memory_order_relaxed);
	}
}

void MovementDiagnostics::recordSample(const WalkTimingContext& context) noexcept
{
	if (!isEnabled()) {
		return;
	}

	MovementLatencySample sample;

	// Overshoot: actual reactor callback start vs expected fire time
	if (context.reactorStartTime != std::chrono::steady_clock::time_point{} &&
	    context.fireAt != std::chrono::steady_clock::time_point{}) {
		sample.schedulerOvershootMs = std::max(0.0, toMilliseconds(context.reactorStartTime - context.fireAt));
	}

	// Reactor queue / execution delay: from task start in reactor to checkCreatureWalk
	if (context.checkWalkStartTime != std::chrono::steady_clock::time_point{} &&
	    context.reactorStartTime != std::chrono::steady_clock::time_point{}) {
		sample.reactorQueueMs = std::max(0.0, toMilliseconds(context.checkWalkStartTime - context.reactorStartTime));
	}

	// Total movement latency: from request receipt to physical movement
	if (context.physicalMoveTime != std::chrono::steady_clock::time_point{} &&
	    context.requestTime != std::chrono::steady_clock::time_point{}) {
		sample.totalLatencyMs = std::max(0.0, toMilliseconds(context.physicalMoveTime - context.requestTime));
	}

	// Request to schedule
	if (context.scheduleTime != std::chrono::steady_clock::time_point{} &&
	    context.requestTime != std::chrono::steady_clock::time_point{}) {
		sample.requestToScheduleMs = std::max(0.0, toMilliseconds(context.scheduleTime - context.requestTime));
	}

	// Dispatcher delay
	if (context.checkWalkStartTime != std::chrono::steady_clock::time_point{} &&
	    context.dispatcherStartTime != std::chrono::steady_clock::time_point{}) {
		sample.dispatcherDelayMs = std::max(0.0, toMilliseconds(context.checkWalkStartTime - context.dispatcherStartTime));
	}

	{
		std::lock_guard lock(sampleMutex);
		samples[sampleHead] = sample;
		sampleHead = (sampleHead + 1) % BUFFER_SIZE;
		if (sampleCount < BUFFER_SIZE) {
			++sampleCount;
		}
		++totalSamplesRecorded;
	}

	const auto queueSnapshot = g_reactor.getQueueSnapshot();
	const size_t pending = queueSnapshot.totalPending;
	size_t currentMax = maxQueueObserved.load(std::memory_order_relaxed);
	while (pending > currentMax && !maxQueueObserved.compare_exchange_weak(currentMax, pending, std::memory_order_relaxed)) {
		// retry until updated or eclipsed
	}
}

size_t MovementDiagnostics::getSampleCount() const noexcept
{
	std::lock_guard lock(sampleMutex);
	return sampleCount;
}

MetricPercentiles MovementDiagnostics::getTotalLatencyPercentiles() const
{
	std::vector<double> values;
	{
		std::lock_guard lock(sampleMutex);
		values.reserve(sampleCount);
		for (size_t i = 0; i < sampleCount; ++i) {
			values.push_back(samples[i].totalLatencyMs);
		}
	}
	return calculatePercentiles(values);
}

MetricPercentiles MovementDiagnostics::getSchedulerOvershootPercentiles() const
{
	std::vector<double> values;
	{
		std::lock_guard lock(sampleMutex);
		values.reserve(sampleCount);
		for (size_t i = 0; i < sampleCount; ++i) {
			values.push_back(samples[i].schedulerOvershootMs);
		}
	}
	return calculatePercentiles(values);
}

MetricPercentiles MovementDiagnostics::getReactorQueuePercentiles() const
{
	std::vector<double> values;
	{
		std::lock_guard lock(sampleMutex);
		values.reserve(sampleCount);
		for (size_t i = 0; i < sampleCount; ++i) {
			values.push_back(samples[i].reactorQueueMs);
		}
	}
	return calculatePercentiles(values);
}

void MovementDiagnostics::maybeReport()
{
	if (!isEnabled()) {
		return;
	}

	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto next = nextReportNanoseconds.load(std::memory_order_relaxed);
	if (now < next) {
		return;
	}

	const auto intervalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
	                            std::chrono::seconds(reportIntervalSeconds.load(std::memory_order_relaxed)))
	                            .count();
	nextReportNanoseconds.store(now + intervalNs, std::memory_order_relaxed);

	const auto totalPerc = getTotalLatencyPercentiles();
	const auto overshootPerc = getSchedulerOvershootPercentiles();
	const auto reactorPerc = getReactorQueuePercentiles();

	const auto queueSnapshot = g_reactor.getQueueSnapshot();
	const auto statsSnapshot = g_reactor.getStatsSnapshot();

	const uint64_t callbacks = walkCallbacksExecuted.load(std::memory_order_relaxed);
	const uint64_t stale = walkCallbacksStale.load(std::memory_order_relaxed);
	const uint64_t cancelled = walkCallbacksCancelled.load(std::memory_order_relaxed);
	const uint64_t deferred = statsSnapshot.deferredByMaxTasks + statsSnapshot.deferredByTimeBudget;
	const uint64_t dropped = statsSnapshot.tasksDropped;
	const size_t maxQueue = maxQueueObserved.load(std::memory_order_relaxed);

	LOG_INFO("[MovementLatency]\n"
	         "samples={}\n"
	         "total: avg={:.2f}ms p50={:.2f}ms p95={:.2f}ms p99={:.2f}ms max={:.2f}ms\n"
	         "scheduler_overshoot: avg={:.2f}ms p95={:.2f}ms p99={:.2f}ms max={:.2f}ms\n"
	         "reactor_queue: avg={:.2f}ms p95={:.2f}ms p99={:.2f}ms\n"
	         "reactor_queue_size: current={} max={}\n"
	         "walk_callbacks={} stale={} cancelled={} deferred={} dropped={}",
	         totalPerc.count,
	         totalPerc.avg, totalPerc.p50, totalPerc.p95, totalPerc.p99, totalPerc.max,
	         overshootPerc.avg, overshootPerc.p95, overshootPerc.p99, overshootPerc.max,
	         reactorPerc.avg, reactorPerc.p95, reactorPerc.p99,
	         queueSnapshot.totalPending, maxQueue,
	         callbacks, stale, cancelled, deferred, dropped);
}

std::string MovementDiagnostics::formatReportSnapshot() const
{
	const auto totalPerc = getTotalLatencyPercentiles();
	const auto overshootPerc = getSchedulerOvershootPercentiles();
	const auto queueSnapshot = g_reactor.getQueueSnapshot();
	const auto statsSnapshot = g_reactor.getStatsSnapshot();

	const uint64_t stale = walkCallbacksStale.load(std::memory_order_relaxed);
	const uint64_t cancelled = walkCallbacksCancelled.load(std::memory_order_relaxed);
	const uint64_t deferred = statsSnapshot.deferredByMaxTasks + statsSnapshot.deferredByTimeBudget;
	const uint64_t dropped = statsSnapshot.tasksDropped;
	const size_t maxQueue = maxQueueObserved.load(std::memory_order_relaxed);

	return fmt::format("Movement latency diagnostics:\n"
	                   "Samples: {}\n"
	                   "Average: {:.1f} ms\n"
	                   "P50: {:.1f} ms\n"
	                   "P95: {:.1f} ms\n"
	                   "P99: {:.1f} ms\n"
	                   "Max: {:.1f} ms\n"
	                   "Scheduler overshoot avg: {:.2f} ms (p95: {:.2f} ms, max: {:.2f} ms)\n"
	                   "Reactor queue: {}\n"
	                   "Max queue observed: {}\n"
	                   "Deferred tasks: {}\n"
	                   "Dropped tasks: {}\n"
	                   "Cancelled walk callbacks: {}\n"
	                   "Stale walk callbacks: {}",
	                   totalPerc.count,
	                   totalPerc.avg,
	                   totalPerc.p50,
	                   totalPerc.p95,
	                   totalPerc.p99,
	                   totalPerc.max,
	                   overshootPerc.avg, overshootPerc.p95, overshootPerc.max,
	                   queueSnapshot.totalPending,
	                   maxQueue,
	                   deferred,
	                   dropped,
	                   cancelled,
	                   stale);
}

void MovementDiagnostics::reset()
{
	{
		std::lock_guard lock(sampleMutex);
		sampleHead = 0;
		sampleCount = 0;
		totalSamplesRecorded = 0;
		samples.fill(MovementLatencySample{});
	}

	walkCallbacksExecuted.store(0, std::memory_order_relaxed);
	walkCallbacksStale.store(0, std::memory_order_relaxed);
	walkCallbacksCancelled.store(0, std::memory_order_relaxed);
	maxQueueObserved.store(0, std::memory_order_relaxed);
	g_reactor.resetStatsSnapshot();

	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	const auto interval = std::chrono::seconds(reportIntervalSeconds.load(std::memory_order_relaxed));
	nextReportNanoseconds.store((now + interval).count(), std::memory_order_relaxed);
}

void MovementDiagnostics::stepStressTest()
{
	if (!stressRunning.load(std::memory_order_relaxed)) {
		return;
	}

	if (stressTicksRemaining == 0) {
		stressRunning.store(false, std::memory_order_release);
		LOG_INFO("[MovementLag] Reactor stress test completed.");
		return;
	}

	--stressTicksRemaining;
	const uint32_t count = stressTasksPerTick;
	for (uint32_t i = 0; i < count; ++i) {
		g_reactor.send([]() {
			volatile uint32_t dummy = 0;
			for (uint32_t j = 0; j < 100; ++j) {
				dummy += j;
			}
		}, "movement_stress_synthetic_task", "MovementDiagnostics");
	}

	if (stressTicksRemaining > 0) {
		g_scheduler.addEvent(createSchedulerTask(50, [this]() {
			stepStressTest();
		}));
	} else {
		stressRunning.store(false, std::memory_order_release);
		LOG_INFO("[MovementLag] Reactor stress test completed.");
	}
}

bool MovementDiagnostics::startStressTest(uint32_t level)
{
	if (!isEnabled() || level < 1 || level > 3) {
		return false;
	}

	bool expected = false;
	if (!stressRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
		return false;
	}

	stressActiveLevel = level;
	stressTicksRemaining = 200;
	if (level == 1) {
		stressTasksPerTick = 5;
	} else if (level == 2) {
		stressTasksPerTick = 25;
	} else {
		stressTasksPerTick = 75;
	}

	LOG_WARN("[MovementLag] Starting controlled Reactor stress test level {} (~{} tasks/s for 10s). Local/test only!",
	         level, stressTasksPerTick * 20);

	g_scheduler.addEvent(createSchedulerTask(50, [this]() {
		stepStressTest();
	}));
	return true;
}

void MovementDiagnostics::stopStressTest()
{
	stressRunning.store(false, std::memory_order_release);
	stressTicksRemaining = 0;
}
