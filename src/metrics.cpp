// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "metrics.h"

#include "logger.h"

Metrics g_metrics;

#ifndef METRICS_ENABLED

struct Metrics::Impl
{
};

Metrics::Metrics() = default;
Metrics::~Metrics() = default;

void Metrics::init(const MetricsOptions& options)
{
	if (options.enablePrometheus || options.enableOstream) {
		LOG_WARN(
		    "[Metrics] metricsEnablePrometheus/metricsEnableOstream set, but this binary was built without "
		    "-DENABLE_METRICS=ON; no metrics will be exported");
	}
}

void Metrics::shutdown() {}

bool Metrics::isEnabled() const noexcept { return false; }

#else // METRICS_ENABLED

#include "creature_scheduler_metrics.h"
#include "game.h"
#include "performance_metrics.h"
#include "tasks.h"

#ifdef METRICS_OSTREAM_ENABLED
#include <opentelemetry/exporters/ostream/metric_exporter_factory.h>
#endif
#include <opentelemetry/exporters/prometheus/exporter_factory.h>
#include <opentelemetry/exporters/prometheus/exporter_options.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>

extern Game g_game;

namespace {

namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace metrics_exporter = opentelemetry::exporter::metrics;

using AttributeMap = std::map<std::string, std::string>;

using Observer = opentelemetry::nostd::shared_ptr<metrics_api::ObserverResultT<int64_t>>;

Observer observerOf(metrics_api::ObserverResult result)
{
	return opentelemetry::nostd::get<Observer>(result);
}

int64_t toInt64(uint64_t value) noexcept
{
	return static_cast<int64_t>(std::min<uint64_t>(value, std::numeric_limits<int64_t>::max()));
}

void observeWalkEvents(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	auto& m = g_creatureSchedulerMetrics;
	observer->Observe(toInt64(m.walkScheduled.load(std::memory_order_relaxed)), AttributeMap{{"state", "scheduled"}});
	observer->Observe(toInt64(m.walkExecuted.load(std::memory_order_relaxed)), AttributeMap{{"state", "executed"}});
	observer->Observe(toInt64(m.walkCancelled.load(std::memory_order_relaxed)), AttributeMap{{"state", "cancelled"}});
	observer->Observe(toInt64(m.walkOrphaned.load(std::memory_order_relaxed)), AttributeMap{{"state", "orphaned"}});
	observer->Observe(toInt64(m.walkStale.load(std::memory_order_relaxed)), AttributeMap{{"state", "stale"}});
	observer->Observe(toInt64(m.walkRejectedGeneration.load(std::memory_order_relaxed)),
	                  AttributeMap{{"state", "rejected_generation"}});
	observer->Observe(toInt64(m.walkRejectedEventId.load(std::memory_order_relaxed)),
	                  AttributeMap{{"state", "rejected_event_id"}});
}

void observeWalkPending(metrics_api::ObserverResult result, void*)
{
	observerOf(result)->Observe(g_creatureSchedulerMetrics.walkPending());
}

void observeWalkDelay(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	const auto& delay = g_creatureSchedulerMetrics.walkExecutionDelay;
	observer->Observe(toInt64(delay.percentileMicroseconds(0.50)), AttributeMap{{"quantile", "0.50"}});
	observer->Observe(toInt64(delay.percentileMicroseconds(0.95)), AttributeMap{{"quantile", "0.95"}});
	observer->Observe(toInt64(delay.percentileMicroseconds(0.99)), AttributeMap{{"quantile", "0.99"}});
	observer->Observe(toInt64(delay.maximumMicroseconds.load(std::memory_order_relaxed)),
	                  AttributeMap{{"quantile", "max"}});
}

void observeWalkDelaySum(metrics_api::ObserverResult result, void*)
{
	observerOf(result)->Observe(
	    toInt64(g_creatureSchedulerMetrics.walkExecutionDelay.totalMicroseconds.load(std::memory_order_relaxed)));
}

void observeWalkDelayCount(metrics_api::ObserverResult result, void*)
{
	observerOf(result)->Observe(
	    toInt64(g_creatureSchedulerMetrics.walkExecutionDelay.calls.load(std::memory_order_relaxed)));
}

void observeFollowUpdates(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	auto& m = g_creatureSchedulerMetrics;
	observer->Observe(toInt64(m.followRequested.load(std::memory_order_relaxed)), AttributeMap{{"state", "requested"}});
	observer->Observe(toInt64(m.followCoalescedReuse.load(std::memory_order_relaxed)),
	                  AttributeMap{{"state", "coalesced_reuse"}});
	observer->Observe(toInt64(m.followCoalescedBackoff.load(std::memory_order_relaxed)),
	                  AttributeMap{{"state", "coalesced_backoff"}});
	observer->Observe(toInt64(m.followCoalescedPending.load(std::memory_order_relaxed)),
	                  AttributeMap{{"state", "coalesced_pending"}});
	observer->Observe(toInt64(m.followExecuted.load(std::memory_order_relaxed)), AttributeMap{{"state", "executed"}});
	observer->Observe(toInt64(m.followStale.load(std::memory_order_relaxed)), AttributeMap{{"state", "stale"}});
}

void observeMoveSpectators(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	auto& m = g_creatureSchedulerMetrics;
	observer->Observe(toInt64(m.moveCalls.load(std::memory_order_relaxed)), AttributeMap{{"stat", "moves"}});
	observer->Observe(toInt64(m.moveSpectatorsTotal.load(std::memory_order_relaxed)),
	                  AttributeMap{{"stat", "spectators_sum"}});
	observer->Observe(toInt64(m.movePlayerSpectatorsTotal.load(std::memory_order_relaxed)),
	                  AttributeMap{{"stat", "player_spectators_sum"}});
}

void observeMoveSpectatorsMaximum(metrics_api::ObserverResult result, void*)
{
	observerOf(result)->Observe(
	    toInt64(g_creatureSchedulerMetrics.moveSpectatorsMaximum.load(std::memory_order_relaxed)));
}

void observeMovementPackets(metrics_api::ObserverResult result, void*)
{
	observerOf(result)->Observe(
	    toInt64(g_creatureSchedulerMetrics.networkMovePackets.load(std::memory_order_relaxed)));
}

void observeMovementBytes(metrics_api::ObserverResult result, void*)
{
	observerOf(result)->Observe(toInt64(g_creatureSchedulerMetrics.networkMoveBytes.load(std::memory_order_relaxed)));
}

void observeReactorQueue(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	const auto reactor = g_performanceMetrics.reactorSnapshot();
	observer->Observe(toInt64(reactor.queueCurrent), AttributeMap{{"stat", "queue_current"}});
	observer->Observe(toInt64(reactor.queueMaximum), AttributeMap{{"stat", "queue_max"}});
}

void observeReactorEvents(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	const auto reactor = g_performanceMetrics.reactorSnapshot();
	observer->Observe(toInt64(reactor.deferred), AttributeMap{{"stat", "deferred"}});
	observer->Observe(toInt64(reactor.expired), AttributeMap{{"stat", "expired"}});
	observer->Observe(toInt64(reactor.dropped), AttributeMap{{"stat", "dropped"}});
}

void observeDispatcher(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	observer->Observe(toInt64(g_dispatcher.getTotalTasksProcessed()), AttributeMap{{"stat", "tasks_processed"}});
	observer->Observe(toInt64(g_dispatcher.getSlowTaskCount()), AttributeMap{{"stat", "slow_tasks"}});
}

void observePath(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	const auto path = g_performanceMetrics.pathSnapshot();
	observer->Observe(toInt64(path.requests), AttributeMap{{"stat", "requests"}});
	observer->Observe(toInt64(path.successes), AttributeMap{{"stat", "successes"}});
	observer->Observe(toInt64(path.failures), AttributeMap{{"stat", "failures"}});
	observer->Observe(toInt64(path.nodesVisited), AttributeMap{{"stat", "nodes_visited"}});
	observer->Observe(toInt64(path.tilesRead), AttributeMap{{"stat", "tiles_read"}});
	observer->Observe(toInt64(path.pathLength), AttributeMap{{"stat", "path_length_sum"}});
	observer->Observe(toInt64(path.reused), AttributeMap{{"stat", "reused"}});
	observer->Observe(toInt64(path.invalidated), AttributeMap{{"stat", "invalidated"}});
}

void observeMethodCalls(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	for (size_t i = 0; i < static_cast<size_t>(PerformanceMetric::Count); ++i) {
		const auto metric = static_cast<PerformanceMetric>(i);
		const auto data = g_performanceMetrics.snapshot(metric);
		observer->Observe(toInt64(data.calls),
		                  AttributeMap{{"method", std::string(PerformanceMetrics::metricName(metric))}});
	}
}

void observeMethodDuration(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	for (size_t i = 0; i < static_cast<size_t>(PerformanceMetric::Count); ++i) {
		const auto metric = static_cast<PerformanceMetric>(i);
		const auto data = g_performanceMetrics.snapshot(metric);
		observer->Observe(toInt64(data.totalNanoseconds),
		                  AttributeMap{{"method", std::string(PerformanceMetrics::metricName(metric))}});
	}
}

void observeMethodMaximum(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	for (size_t i = 0; i < static_cast<size_t>(PerformanceMetric::Count); ++i) {
		const auto metric = static_cast<PerformanceMetric>(i);
		const auto data = g_performanceMetrics.snapshot(metric);
		observer->Observe(toInt64(data.maximumNanoseconds),
		                  AttributeMap{{"method", std::string(PerformanceMetrics::metricName(metric))}});
	}
}

void observeOnlineCounts(metrics_api::ObserverResult result, void*)
{
	auto observer = observerOf(result);
	observer->Observe(static_cast<int64_t>(g_game.getPlayersOnline()), AttributeMap{{"type", "players"}});
	observer->Observe(static_cast<int64_t>(g_game.getMonstersOnline()), AttributeMap{{"type", "monsters"}});
	observer->Observe(static_cast<int64_t>(g_game.getNpcsOnline()), AttributeMap{{"type", "npcs"}});
}

} // namespace

struct Metrics::Impl
{
	std::shared_ptr<metrics_sdk::MeterProvider> provider;
	std::vector<opentelemetry::nostd::shared_ptr<metrics_api::ObservableInstrument>> instruments;
	bool enabled = false;

	void addCounter(const opentelemetry::nostd::shared_ptr<metrics_api::Meter>& meter, const char* name,
	                const char* description, const char* unit, void (*callback)(metrics_api::ObserverResult, void*))
	{
		auto instrument = meter->CreateInt64ObservableCounter(name, description, unit);
		instrument->AddCallback(callback, nullptr);
		instruments.push_back(std::move(instrument));
	}

	void addGauge(const opentelemetry::nostd::shared_ptr<metrics_api::Meter>& meter, const char* name,
	              const char* description, const char* unit, void (*callback)(metrics_api::ObserverResult, void*))
	{
		auto instrument = meter->CreateInt64ObservableGauge(name, description, unit);
		instrument->AddCallback(callback, nullptr);
		instruments.push_back(std::move(instrument));
	}
};

Metrics::Metrics() : impl(std::make_unique<Impl>()) {}

Metrics::~Metrics() = default;

void Metrics::init(const MetricsOptions& options)
{
	if (!options.enablePrometheus && !options.enableOstream) {
		return;
	}

	auto provider = std::make_shared<metrics_sdk::MeterProvider>();

	if (options.enablePrometheus) {
		metrics_exporter::PrometheusExporterOptions prometheusOptions;
		prometheusOptions.url = options.prometheusAddress;
		provider->AddMetricReader(metrics_exporter::PrometheusExporterFactory::Create(prometheusOptions));
		LOG_INFO(">> Metrics: Prometheus endpoint at http://{}/metrics", options.prometheusAddress);
	}

	if (options.enableOstream) {
#ifdef METRICS_OSTREAM_ENABLED
		metrics_sdk::PeriodicExportingMetricReaderOptions readerOptions;
		readerOptions.export_interval_millis =
		    std::chrono::milliseconds(std::max<int64_t>(100, options.ostreamIntervalMilliseconds));
		readerOptions.export_timeout_millis =
		    std::chrono::milliseconds(std::max<int64_t>(50, options.ostreamIntervalMilliseconds / 2));
		provider->AddMetricReader(metrics_sdk::PeriodicExportingMetricReaderFactory::Create(
		    metrics_exporter::OStreamMetricExporterFactory::Create(), readerOptions));
		LOG_INFO(">> Metrics: OStream exporter every {} ms", options.ostreamIntervalMilliseconds);
#else
		LOG_WARN(
		    "[Metrics] metricsEnableOstream set, but this opentelemetry-cpp build lacks the ostream exporter; "
		    "ignoring");
#endif
	}

	auto meter = provider->GetMeter("tfs", "1.0.0");

	impl->addCounter(meter, "tfs_creature_walk_events", "Creature walk event lifecycle transitions", "{event}",
	                 observeWalkEvents);
	impl->addGauge(meter, "tfs_creature_walk_pending", "Walk events scheduled but not yet executed or cancelled",
	               "{event}", observeWalkPending);
	impl->addGauge(meter, "tfs_creature_walk_delay_microseconds",
	               "Walk callback delay percentiles (since startup)", "us", observeWalkDelay);
	impl->addCounter(meter, "tfs_creature_walk_delay_sum_microseconds", "Total walk callback delay", "us",
	                 observeWalkDelaySum);
	impl->addCounter(meter, "tfs_creature_walk_delay_count", "Walk callback delay samples", "{sample}",
	                 observeWalkDelayCount);
	impl->addCounter(meter, "tfs_follow_updates", "Follow path update lifecycle transitions", "{update}",
	                 observeFollowUpdates);
	impl->addCounter(meter, "tfs_move_spectators", "Spectator fan-out per Map::moveCreature", "{spectator}",
	                 observeMoveSpectators);
	impl->addGauge(meter, "tfs_move_spectators_max", "Largest spectator fan-out seen for a single move",
	               "{spectator}", observeMoveSpectatorsMaximum);
	impl->addCounter(meter, "tfs_network_movement_packets", "Movement packets written to clients", "{packet}",
	                 observeMovementPackets);
	impl->addCounter(meter, "tfs_network_movement_bytes", "Movement packet payload bytes (lower bound)", "By",
	                 observeMovementBytes);
	impl->addGauge(meter, "tfs_reactor", "TaskReactor queue depth (current and interval max)", "{task}",
	               observeReactorQueue);
	impl->addCounter(meter, "tfs_reactor_events", "TaskReactor deferred/expired/dropped task totals", "{task}",
	                 observeReactorEvents);
	impl->addCounter(meter, "tfs_dispatcher", "Dispatcher task statistics", "{task}", observeDispatcher);
	impl->addCounter(meter, "tfs_path", "Pathfinding request statistics", "{request}", observePath);
	impl->addCounter(meter, "tfs_method_calls", "Instrumented method call counts (performanceMetricsEnabled)",
	                 "{call}", observeMethodCalls);
	impl->addCounter(meter, "tfs_method_duration_nanoseconds",
	                 "Instrumented method cumulative duration (performanceMetricsEnabled)", "ns",
	                 observeMethodDuration);
	impl->addGauge(meter, "tfs_method_max_nanoseconds",
	               "Instrumented method maximum duration (performanceMetricsEnabled)", "ns", observeMethodMaximum);
	impl->addGauge(meter, "tfs_online", "Online creature counts by type", "{creature}", observeOnlineCounts);

	impl->provider = std::move(provider);
	impl->enabled = true;

	std::shared_ptr<metrics_api::MeterProvider> apiProvider = impl->provider;
	metrics_api::Provider::SetMeterProvider(apiProvider);
}

void Metrics::shutdown()
{
	if (!impl->enabled) {
		return;
	}

	impl->enabled = false;
	if (impl->provider) {
		impl->provider->Shutdown();
	}
	impl->instruments.clear();
	impl->provider.reset();
}

bool Metrics::isEnabled() const noexcept { return impl->enabled; }

#endif // METRICS_ENABLED
