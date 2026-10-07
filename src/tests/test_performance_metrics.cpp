#include "../otpch.h"

#include "../performance_metrics.h"
#include "test_support.h"

struct PerformanceMetricsTestAccess
{
	static uint64_t work(const PerformanceMetrics& m, CombatWork kind)
	{
		return m.combatWork[static_cast<size_t>(kind)].load();
	}
	static uint64_t distributionCalls(const PerformanceMetrics& m, CombatDistribution kind)
	{
		return m.combatDistributions[static_cast<size_t>(kind)].calls.load();
	}
	static uint64_t sourceCalls(const PerformanceMetrics& m, size_t i) { return m.callbackSources[i].calls; }
	static uint64_t overflow(const PerformanceMetrics& m) { return m.callbackSources.back().calls; }
	static size_t capacity() { return PerformanceMetrics::CallbackSourceCapacity; }
	static uint64_t sourceSlow(const PerformanceMetrics& m, size_t index) { return m.callbackSources[0].slow[index]; }
	static uint64_t queueMaximum(const PerformanceMetrics& m) { return m.callbackSources[0].queueMaximum; }
	static uint64_t cryptoBytes(const PerformanceMetrics& m, PerformanceMetric metric)
	{
		return m.metrics[static_cast<size_t>(metric)].bytes.load();
	}
	static uint64_t metricCalls(const PerformanceMetrics& m, PerformanceMetric metric)
	{
		return m.metrics[static_cast<size_t>(metric)].calls.load();
	}
};

TEST_CASE(disabled_diagnostics_do_not_record_work)
{
	auto metrics = std::make_unique<PerformanceMetrics>();
	metrics->recordCombatWork(CombatWork::PayloadBytes, 12);
	metrics->recordMovementWork(MovementWork::SpectatorCandidates, 25);
	CHECK(metrics->getMovementWork(MovementWork::SpectatorCandidates) == 0);
	metrics->recordCombatDistribution(CombatDistribution::EffectCandidates, 4);
	metrics->recordReactorCallbackSource(100'000'000, "disabled", "test", 10);
	CHECK(PerformanceMetricsTestAccess::work(*metrics, CombatWork::PayloadBytes) == 0);
	CHECK(PerformanceMetricsTestAccess::distributionCalls(*metrics, CombatDistribution::EffectCandidates) == 0);
	CHECK(PerformanceMetricsTestAccess::sourceCalls(*metrics, 0) == 0);
}

TEST_CASE(movement_attribution_is_fixed_and_opt_in)
{
	auto metrics = std::make_unique<PerformanceMetrics>();
	metrics->setEnabled(true);
	metrics->recordMovementWork(MovementWork::SpectatorQueries);
	metrics->recordMovementWork(MovementWork::SpectatorCandidates, 300);
	metrics->recordMovementWork(MovementWork::WalkTileCacheHits, 12);
	CHECK(metrics->getMovementWork(MovementWork::SpectatorQueries) == 1);
	CHECK(metrics->getMovementWork(MovementWork::SpectatorCandidates) == 300);
	CHECK(metrics->getMovementWork(MovementWork::WalkTileCacheHits) == 12);
	metrics->setEnabled(false);
	metrics->recordMovementWork(MovementWork::SpectatorQueries);
	CHECK(metrics->getMovementWork(MovementWork::SpectatorQueries) == 1);
}

TEST_CASE(callback_sources_are_bounded_and_count_overflow_and_thresholds)
{
	auto metrics = std::make_unique<PerformanceMetrics>();
	metrics->setEnabled(true);
	metrics->recordReactorCallbackSource(50'000'001, "callback", "source", 1234);
	metrics->recordReactorCallbackSource(5'000'000, "callback", "source", 5678);
	CHECK(PerformanceMetricsTestAccess::sourceCalls(*metrics, 0) == 2);
	CHECK(PerformanceMetricsTestAccess::sourceSlow(*metrics, 0) == 1);
	CHECK(PerformanceMetricsTestAccess::sourceSlow(*metrics, 3) == 1);
	CHECK(PerformanceMetricsTestAccess::sourceSlow(*metrics, 4) == 0);
	CHECK(PerformanceMetricsTestAccess::queueMaximum(*metrics) == 5678);
	for (size_t i = 1; i <= PerformanceMetricsTestAccess::capacity() + 2; ++i) {
		metrics->recordReactorCallbackSource(1, "callback", std::to_string(i));
	}
	CHECK(PerformanceMetricsTestAccess::overflow(*metrics) == 3);
}

TEST_CASE(nested_combat_scopes_count_each_payload_once)
{
	g_performanceMetrics.setEnabled(true);
	const auto events = PerformanceMetricsTestAccess::work(g_performanceMetrics, CombatWork::Events);
	const auto bytes = PerformanceMetricsTestAccess::work(g_performanceMetrics, CombatWork::PayloadBytes);
	const auto distributions =
	    PerformanceMetricsTestAccess::distributionCalls(g_performanceMetrics, CombatDistribution::EventPayloadBytes);
	{
		CombatPacketScope outer;
		g_performanceMetrics.recordOutputPayload(0x83, 8);
		{
			CombatPacketScope inner;
			g_performanceMetrics.recordOutputPayload(0x8c, 6);
		}
	}
	CHECK(PerformanceMetricsTestAccess::work(g_performanceMetrics, CombatWork::Events) == events + 1);
	CHECK(PerformanceMetricsTestAccess::work(g_performanceMetrics, CombatWork::PayloadBytes) == bytes + 14);
	CHECK(PerformanceMetricsTestAccess::distributionCalls(g_performanceMetrics,
	                                                      CombatDistribution::EventPayloadBytes) == distributions + 1);
	g_performanceMetrics.setEnabled(false);
}

TEST_CASE(diagnostic_counters_accept_concurrent_network_threads)
{
	auto metrics = std::make_unique<PerformanceMetrics>();
	metrics->setEnabled(true);
	std::vector<std::thread> workers;
	for (int i = 0; i < 4; ++i) {
		workers.emplace_back([&] {
			for (int n = 0; n < 1000; ++n) {
				metrics->recordCombatWork(CombatWork::WireBytes, 8);
			}
		});
	}
	for (auto& worker : workers) {
		worker.join();
	}
	CHECK(PerformanceMetricsTestAccess::work(*metrics, CombatWork::WireBytes) == 32000);
}

TEST_CASE(xtea_diagnostics_count_calls_and_bytes_only_when_enabled)
{
	auto metrics = std::make_unique<PerformanceMetrics>();
	metrics->record(PerformanceMetric::ProtocolXteaEncrypt, 100, 64);
	CHECK(PerformanceMetricsTestAccess::cryptoBytes(*metrics, PerformanceMetric::ProtocolXteaEncrypt) == 0);
	CHECK(PerformanceMetricsTestAccess::metricCalls(*metrics, PerformanceMetric::ProtocolXteaEncrypt) == 0);
	metrics->setEnabled(true);
	std::vector<std::jthread> workers;
	for (int i = 0; i < 4; ++i) workers.emplace_back([&] {
		for (int n = 0; n < 1000; ++n) {
			metrics->record(PerformanceMetric::ProtocolXteaEncrypt, 100, 64);
			metrics->record(PerformanceMetric::ProtocolXteaDecrypt, 200, 32);
		}
	});
	workers.clear();
	CHECK(PerformanceMetricsTestAccess::cryptoBytes(*metrics, PerformanceMetric::ProtocolXteaEncrypt) == 256000);
	CHECK(PerformanceMetricsTestAccess::cryptoBytes(*metrics, PerformanceMetric::ProtocolXteaDecrypt) == 128000);
	CHECK(PerformanceMetricsTestAccess::metricCalls(*metrics, PerformanceMetric::ProtocolXteaEncrypt) == 4000);
	CHECK(PerformanceMetricsTestAccess::metricCalls(*metrics, PerformanceMetric::ProtocolXteaDecrypt) == 4000);
}

TEST_CASE(database_and_splash_metrics_are_opt_in)
{
	auto metrics = std::make_unique<PerformanceMetrics>();
	for (auto metric : {PerformanceMetric::DatabaseQueryDispatcher, PerformanceMetric::CombatSplash}) {
		metrics->record(metric, 100);
		CHECK(PerformanceMetricsTestAccess::metricCalls(*metrics, metric) == 0);
	}
	metrics->setEnabled(true);
	for (auto metric : {PerformanceMetric::DatabaseQueryDispatcher, PerformanceMetric::CombatSplash}) {
		metrics->record(metric, 100);
		CHECK(PerformanceMetricsTestAccess::metricCalls(*metrics, metric) == 1);
	}
}

TFS_TEST_MAIN()
