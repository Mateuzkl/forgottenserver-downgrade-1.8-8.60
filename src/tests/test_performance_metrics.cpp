// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"
#include "../performance_metrics.h"
#include "test_support.h"

struct PerformanceMetricsTestAccess
{
	static uint64_t calls(const PerformanceMetrics& metrics, PerformanceMetric metric)
	{
		return metrics.metrics[static_cast<size_t>(metric)].calls.load();
	}
	static uint64_t targetSamples(const PerformanceMetrics& metrics) { return metrics.targetSizes.samples.load(); }
	static uint64_t targetMaximum(const PerformanceMetrics& metrics) { return metrics.targetSizes.maximum.load(); }
	static uint64_t targetBucket(const PerformanceMetrics& metrics, size_t index)
	{
		return metrics.targetSizes.histogram[index].load();
	}
	static void reportNow(PerformanceMetrics& metrics)
	{
		metrics.nextReportNanoseconds.store(0);
		metrics.maybeReport();
	}
};

TEST_CASE(ai_profiling_disabled_records_nothing)
{
	PerformanceMetrics metrics;
	metrics.recordMonsterAi(MonsterAiMetric::SearchCandidates, 400);
	metrics.recordMonsterTargetCount(400);
	metrics.record(PerformanceMetric::PlayerOnThink, 1000);
	for (auto count : metrics.takeMonsterAiCounters()) {
		CHECK(count == 0);
	}
	CHECK(PerformanceMetricsTestAccess::targetSamples(metrics) == 0);
	CHECK(PerformanceMetricsTestAccess::calls(metrics, PerformanceMetric::PlayerOnThink) == 0);
}

TEST_CASE(ai_profiling_fixed_counters_saturate_and_reset)
{
	PerformanceMetrics metrics;
	metrics.setEnabled(true);
	metrics.recordMonsterAi(MonsterAiMetric::SearchCandidates, std::numeric_limits<uint64_t>::max() - 1);
	metrics.recordMonsterAi(MonsterAiMetric::SearchCandidates, 400);
	metrics.recordMonsterAi(MonsterAiMetric::Count, 10); // Invalid sentinel must not index the array.
	metrics.recordMonsterAi(static_cast<MonsterAiMetric>(255), 10);
	const auto counters = metrics.takeMonsterAiCounters();
	CHECK(counters[static_cast<size_t>(MonsterAiMetric::SearchCandidates)] == std::numeric_limits<uint64_t>::max());
	for (auto count : metrics.takeMonsterAiCounters()) {
		CHECK(count == 0);
	}
	metrics.setEnabled(false);
	metrics.recordMonsterAi(MonsterAiMetric::AttackCasts);
	CHECK(metrics.takeMonsterAiCounters()[static_cast<size_t>(MonsterAiMetric::AttackCasts)] == 0);
}

TEST_CASE(ai_target_histogram_and_timing_reset_after_report)
{
	PerformanceMetrics metrics;
	metrics.setEnabled(true);
	metrics.recordMonsterTargetCount(0);
	metrics.recordMonsterTargetCount(1);
	metrics.recordMonsterTargetCount(400);
	metrics.record(PerformanceMetric::MonsterFullRefresh, 1000);
	CHECK(PerformanceMetricsTestAccess::targetSamples(metrics) == 3);
	CHECK(PerformanceMetricsTestAccess::targetMaximum(metrics) == 400);
	CHECK(PerformanceMetricsTestAccess::targetBucket(metrics, 0) == 1);
	CHECK(PerformanceMetricsTestAccess::targetBucket(metrics, 1) == 1);
	CHECK(PerformanceMetricsTestAccess::targetBucket(metrics, 9) == 1);
	PerformanceMetricsTestAccess::reportNow(metrics);
	CHECK(PerformanceMetricsTestAccess::targetSamples(metrics) == 0);
	CHECK(PerformanceMetricsTestAccess::targetMaximum(metrics) == 0);
	CHECK(PerformanceMetricsTestAccess::targetBucket(metrics, 9) == 0);
	CHECK(PerformanceMetricsTestAccess::calls(metrics, PerformanceMetric::MonsterFullRefresh) == 0);
	metrics.recordMonsterTargetCount(10);
	CHECK(PerformanceMetricsTestAccess::targetMaximum(metrics) == 10);
}

TFS_TEST_MAIN()
