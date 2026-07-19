// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "performance_metrics.h"

#include "logger.h"

#include <bit>

PerformanceMetrics g_performanceMetrics;

namespace {
constexpr auto REPORT_INTERVAL = std::chrono::seconds(5);

constexpr std::array<std::string_view, static_cast<size_t>(PerformanceMetric::Count)> METRIC_NAMES = {
	"TaskReactor::runOnce", "TaskReactor::drainInbox", "TaskReactor::drainReadyTasks",
	"TaskReactor::sort", "TaskReactor::callbacks", "TaskReactor::callback",
	"TaskReactor::queueLatency", "Game::checkCreatures", "Game::checkCreatureWalk",
	"Game::updateCreatureWalk", "Creature::goToFollowCreature", "Creature::onAttacking",
	"Game::internalMoveCreature", "Map::getPathMatching", "Map::moveCreature", "Map::getSpectators",
	"Monster::onThink",
	"Monster::onWalk", "Monster::doAttacking", "CombatSpell::castSpell",
	"Combat::doCombat", "Combat::doAreaCombat",
};

size_t histogramIndex(uint64_t nanoseconds) noexcept
{
	return std::min<size_t>(std::bit_width(nanoseconds), 63);
}

uint64_t histogramUpperBound(size_t index) noexcept
{
	if (index == 0) {
		return 0;
	}
	if (index >= 63) {
		return std::numeric_limits<uint64_t>::max();
	}
	return uint64_t{1} << index;
}

uint64_t percentile(const std::array<uint64_t, 64>& histogram, uint64_t calls, uint64_t numerator) noexcept
{
	if (calls == 0) {
		return 0;
	}
	const uint64_t wanted = std::max<uint64_t>(1, (calls * numerator + 99) / 100);
	uint64_t seen = 0;
	for (size_t i = 0; i < histogram.size(); ++i) {
		seen += histogram[i];
		if (seen >= wanted) {
			return histogramUpperBound(i);
		}
	}
	return histogramUpperBound(histogram.size() - 1);
}

void updateMaximum(std::atomic<uint64_t>& maximum, uint64_t value) noexcept
{
	auto current = maximum.load(std::memory_order_relaxed);
	while (current < value && !maximum.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
	}
}
} // namespace

void PerformanceMetrics::setEnabled(bool value) noexcept
{
	enabled.store(value, std::memory_order_relaxed);
	if (value) {
		const auto next = std::chrono::steady_clock::now() + REPORT_INTERVAL;
		nextReportNanoseconds.store(
			std::chrono::duration_cast<std::chrono::nanoseconds>(next.time_since_epoch()).count(),
			std::memory_order_relaxed);
	}
}

void PerformanceMetrics::record(PerformanceMetric metric, uint64_t nanoseconds) noexcept
{
	if (!isEnabled()) {
		return;
	}
	auto& data = metrics[static_cast<size_t>(metric)];
	data.calls.fetch_add(1, std::memory_order_relaxed);
	data.totalNanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
	updateMaximum(data.maximumNanoseconds, nanoseconds);
	data.histogram[histogramIndex(nanoseconds)].fetch_add(1, std::memory_order_relaxed);
}

void PerformanceMetrics::recordQueueSize(size_t current) noexcept
{
	if (!isEnabled()) {
		return;
	}
	reactor.queueCurrent.store(current, std::memory_order_relaxed);
	updateMaximum(reactor.queueMaximum, current);
}

void PerformanceMetrics::recordTaskDeferred(uint64_t count) noexcept
{
	if (isEnabled()) reactor.deferred.fetch_add(count, std::memory_order_relaxed);
}

void PerformanceMetrics::recordTaskExpired(uint64_t count) noexcept
{
	if (isEnabled()) reactor.expired.fetch_add(count, std::memory_order_relaxed);
}

void PerformanceMetrics::recordTaskDropped(uint64_t count) noexcept
{
	if (isEnabled()) reactor.dropped.fetch_add(count, std::memory_order_relaxed);
}

void PerformanceMetrics::recordNetworkAcceptStarted() noexcept
{
	if (isEnabled()) network.acceptStarted.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceMetrics::recordNetworkAccept(bool success) noexcept
{
	if (!isEnabled()) {
		return;
	}
	(success ? network.accepted : network.acceptErrors).fetch_add(1, std::memory_order_relaxed);
}

void PerformanceMetrics::recordNetworkRateLimitRejection() noexcept
{
	if (isEnabled()) network.rateLimitRejections.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceMetrics::recordNetworkIpLimitRejection() noexcept
{
	if (isEnabled()) network.ipLimitRejections.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceMetrics::recordNetworkConnectionCount(size_t current) noexcept
{
	if (!isEnabled()) {
		return;
	}
	network.connectionsCurrent.store(current, std::memory_order_relaxed);
	updateMaximum(network.connectionsMaximum, current);
}

void PerformanceMetrics::recordReactorCallbackSource(uint64_t nanoseconds, std::string_view description,
                                                      std::string_view origin) noexcept
{
	if (!isEnabled()) {
		return;
	}

	try {
		std::scoped_lock lock(slowestReactorCallback.mutex);
		if (nanoseconds <= slowestReactorCallback.nanoseconds) {
			return;
		}
		std::string newDescription(description);
		std::string newOrigin(origin);
		slowestReactorCallback.description = std::move(newDescription);
		slowestReactorCallback.origin = std::move(newOrigin);
		slowestReactorCallback.nanoseconds = nanoseconds;
	} catch (...) {
		// Profiling must never interfere with task execution.
	}
}

void PerformanceMetrics::recordPathRequest(bool success, uint64_t nodesVisited, uint64_t tilesRead,
											 uint64_t pathLength) noexcept
{
	if (!isEnabled()) {
		return;
	}
	path.requests.fetch_add(1, std::memory_order_relaxed);
	(success ? path.successes : path.failures).fetch_add(1, std::memory_order_relaxed);
	path.nodesVisited.fetch_add(nodesVisited, std::memory_order_relaxed);
	path.tilesRead.fetch_add(tilesRead, std::memory_order_relaxed);
	path.pathLength.fetch_add(pathLength, std::memory_order_relaxed);
}

void PerformanceMetrics::maybeReport()
{
	if (!isEnabled()) {
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
	auto expected = nextReportNanoseconds.load(std::memory_order_relaxed);
	if (nowNs < expected || !nextReportNanoseconds.compare_exchange_strong(
			expected, nowNs + std::chrono::duration_cast<std::chrono::nanoseconds>(REPORT_INTERVAL).count(),
			std::memory_order_relaxed)) {
		return;
	}

	std::string report;
	report.reserve(4096);
	for (size_t metricIndex = 0; metricIndex < metrics.size(); ++metricIndex) {
		auto& data = metrics[metricIndex];
		const uint64_t calls = data.calls.exchange(0, std::memory_order_relaxed);
		if (calls == 0) {
			continue;
		}
		const uint64_t total = data.totalNanoseconds.exchange(0, std::memory_order_relaxed);
		const uint64_t maximum = data.maximumNanoseconds.exchange(0, std::memory_order_relaxed);
		std::array<uint64_t, HistogramBuckets> histogram{};
		for (size_t i = 0; i < histogram.size(); ++i) {
			histogram[i] = data.histogram[i].exchange(0, std::memory_order_relaxed);
		}

		report += fmt::format(
			"[Perf] {} calls={} total_ms={:.3f} avg_us={:.3f} max_us={:.3f} "
			"p50_us={:.3f} p95_us={:.3f} p99_us={:.3f}\n",
			METRIC_NAMES[metricIndex], calls, total / 1'000'000.0,
			total / static_cast<double>(calls) / 1'000.0, maximum / 1'000.0,
			percentile(histogram, calls, 50) / 1'000.0, percentile(histogram, calls, 95) / 1'000.0,
			percentile(histogram, calls, 99) / 1'000.0);
	}

	report += fmt::format(
		"[Perf] reactor queue={} backlog_max={} deferred={} expired={} dropped={}\n",
		reactor.queueCurrent.load(std::memory_order_relaxed),
		reactor.queueMaximum.exchange(0, std::memory_order_relaxed),
		reactor.deferred.exchange(0, std::memory_order_relaxed),
		reactor.expired.exchange(0, std::memory_order_relaxed),
		reactor.dropped.exchange(0, std::memory_order_relaxed));

	uint64_t slowestNanoseconds = 0;
	std::string slowestDescription;
	std::string slowestOrigin;
	{
		std::scoped_lock lock(slowestReactorCallback.mutex);
		slowestNanoseconds = slowestReactorCallback.nanoseconds;
		slowestDescription = std::move(slowestReactorCallback.description);
		slowestOrigin = std::move(slowestReactorCallback.origin);
		slowestReactorCallback.nanoseconds = 0;
	}
	if (slowestNanoseconds > 0) {
		std::string label;
		if (slowestDescription.empty()) {
			label = slowestOrigin.empty() ? "unknown" : std::move(slowestOrigin);
		} else if (slowestOrigin.empty()) {
			label = std::move(slowestDescription);
		} else {
			label = fmt::format("{} @ {}", slowestDescription, slowestOrigin);
		}
		report += fmt::format("[Perf] reactor slowest_callback={} max_us={:.3f}\n", label,
		                      slowestNanoseconds / 1'000.0);
	}
	report += fmt::format(
		"[Perf] path requests={} success={} failure={} nodes={} tiles={} path_steps={}",
		path.requests.exchange(0, std::memory_order_relaxed),
		path.successes.exchange(0, std::memory_order_relaxed),
		path.failures.exchange(0, std::memory_order_relaxed),
		path.nodesVisited.exchange(0, std::memory_order_relaxed),
		path.tilesRead.exchange(0, std::memory_order_relaxed),
		path.pathLength.exchange(0, std::memory_order_relaxed));
	const uint64_t currentConnections = network.connectionsCurrent.load(std::memory_order_relaxed);
	const uint64_t maximumConnections =
		std::max(currentConnections,
		         network.connectionsMaximum.exchange(currentConnections, std::memory_order_relaxed));
	report += fmt::format(
		"\n[Perf] network accept_started={} accept_ok={} accept_error={} rate_limited={} ip_limited={} "
		"active={} active_max={}",
		network.acceptStarted.exchange(0, std::memory_order_relaxed),
		network.accepted.exchange(0, std::memory_order_relaxed),
		network.acceptErrors.exchange(0, std::memory_order_relaxed),
		network.rateLimitRejections.exchange(0, std::memory_order_relaxed),
		network.ipLimitRejections.exchange(0, std::memory_order_relaxed), currentConnections, maximumConnections);
	LOG_INFO("{}", report);
}

PerformanceScope::PerformanceScope(PerformanceMetric metric) noexcept :
	metric(metric), active(g_performanceMetrics.isEnabled())
{
	if (active) {
		started = std::chrono::steady_clock::now();
	}
}

PerformanceScope::~PerformanceScope()
{
	if (active) {
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - started).count();
		g_performanceMetrics.record(metric, elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0);
	}
}
