// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "performance_metrics.h"

#include "logger.h"

#include <bit>

PerformanceMetrics g_performanceMetrics;

namespace {
constexpr auto REPORT_INTERVAL = std::chrono::seconds(5);
constexpr auto AREA_COMBAT_SLOW_SAMPLE_THRESHOLD = std::chrono::milliseconds(1);

constexpr std::array<std::string_view, static_cast<size_t>(PerformanceMetric::Count)> METRIC_NAMES = {
	"TaskReactor::runOnce", "TaskReactor::drainInbox", "TaskReactor::drainReadyTasks",
	"TaskReactor::sort", "TaskReactor::callbacks", "TaskReactor::callback",
	"TaskReactor::queueLatency", "Game::checkCreatures", "Game::checkCreatureWalk",
	"Game::updateCreatureWalk", "Creature::goToFollowCreature", "Creature::onAttacking",
	"Game::internalMoveCreature", "Map::getPathMatching", "Map::moveCreature", "Map::getSpectators",
	"Monster::onThink",
	"Monster::onWalk", "Monster::doAttacking", "CombatSpell::castSpell",
	"Combat::doCombat", "Combat::doAreaCombat", "Combat::area.buildTiles",
	"Combat::area.prepareDamage", "Combat::area.collectSpectators", "Combat::area.processTiles+collectTargets",
	"Combat::area.applyTargets", "Creature::executeConditions",
};

constexpr std::array<std::string_view, static_cast<size_t>(MonsterIdleMetric::Count)> MONSTER_IDLE_METRIC_NAMES = {
	"refresh_calls", "decision_true", "decision_false", "transition_to_idle", "transition_to_active",
	"same_state_calls", "prune_calls", "targets_pruned", "friends_pruned", "attacked_cleared",
	"follow_cleared", "blocked_by_target", "blocked_by_condition", "blocked_by_summon", "blocked_by_faction",
	"active_without_reason", "on_idle_status_calls", "damage_map_clears", "creature_check_adds",
	"creature_check_removes",
};

constexpr std::array<std::string_view, static_cast<size_t>(MonsterActiveReason::Count)> MONSTER_ACTIVE_REASON_NAMES = {
	"target_list", "attacked_creature", "follow_creature", "aggressive_condition", "summon",
	"faction_target", "unknown",
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

void PerformanceMetrics::recordAreaCombat(const AreaCombatMetricsSample& sample, std::string_view monsterName,
                                          std::string_view spellName) noexcept
{
	if (!isEnabled()) {
		return;
	}

	areaCombat.casts.fetch_add(1, std::memory_order_relaxed);
	areaCombat.areaRows.fetch_add(sample.areaRows, std::memory_order_relaxed);
	areaCombat.areaColumns.fetch_add(sample.areaColumns, std::memory_order_relaxed);
	areaCombat.activeCells.fetch_add(sample.activeCells, std::memory_order_relaxed);
	areaCombat.sightChecks.fetch_add(sample.sightChecks, std::memory_order_relaxed);
	areaCombat.sightRejected.fetch_add(sample.sightRejected, std::memory_order_relaxed);
	areaCombat.tilesReturned.fetch_add(sample.tilesReturned, std::memory_order_relaxed);
	areaCombat.tilesCreated.fetch_add(sample.tilesCreated, std::memory_order_relaxed);
	areaCombat.combatRejected.fetch_add(sample.combatRejected, std::memory_order_relaxed);
	areaCombat.spectators.fetch_add(sample.spectators, std::memory_order_relaxed);
	areaCombat.targets.fetch_add(sample.targets, std::memory_order_relaxed);
	areaCombat.blockedTargets.fetch_add(sample.blockedTargets, std::memory_order_relaxed);
	areaCombat.appliedTargets.fetch_add(sample.appliedTargets, std::memory_order_relaxed);
	areaCombat.conditionClones.fetch_add(sample.conditionClones, std::memory_order_relaxed);
	areaCombat.tileCallbacks.fetch_add(sample.tileCallbacks, std::memory_order_relaxed);
	areaCombat.targetCallbacks.fetch_add(sample.targetCallbacks, std::memory_order_relaxed);
	areaCombat.impactEffects.fetch_add(sample.impactEffects, std::memory_order_relaxed);
	areaCombat.fieldsCreated.fetch_add(sample.fieldsCreated, std::memory_order_relaxed);
	areaCombat.effectRecipients.fetch_add(sample.effectRecipients, std::memory_order_relaxed);

	if (sample.totalNanoseconds < static_cast<uint64_t>(
	        std::chrono::duration_cast<std::chrono::nanoseconds>(AREA_COMBAT_SLOW_SAMPLE_THRESHOLD).count()) ||
	    sample.totalNanoseconds <= slowestAreaCombat.maximumNanoseconds.load(std::memory_order_relaxed)) {
		return;
	}

	try {
		std::scoped_lock lock(slowestAreaCombat.mutex);
		if (sample.totalNanoseconds <= slowestAreaCombat.maximumNanoseconds.load(std::memory_order_relaxed)) {
			return;
		}
		slowestAreaCombat.sample = sample;
		slowestAreaCombat.monsterName.assign(monsterName);
		slowestAreaCombat.spellName.assign(spellName);
		slowestAreaCombat.maximumNanoseconds.store(sample.totalNanoseconds, std::memory_order_relaxed);
	} catch (...) {
		// Profiling must never interfere with combat execution.
	}
}

void PerformanceMetrics::recordMonsterIdle(MonsterIdleMetric metric, uint64_t count) noexcept
{
	if (isEnabled()) {
		monsterIdle[static_cast<size_t>(metric)].fetch_add(count, std::memory_order_relaxed);
	}
}

void PerformanceMetrics::recordMonsterActiveReason(MonsterActiveReason reason, uint64_t count) noexcept
{
	if (isEnabled()) {
		monsterActiveReasons[static_cast<size_t>(reason)].fetch_add(count, std::memory_order_relaxed);
	}
}

uint64_t PerformanceMetrics::getMonsterIdleMetric(MonsterIdleMetric metric) const noexcept
{
	return monsterIdle[static_cast<size_t>(metric)].load(std::memory_order_relaxed);
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
	report.reserve(8192);
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

	const uint64_t areaCombatCasts = areaCombat.casts.exchange(0, std::memory_order_relaxed);
	if (areaCombatCasts > 0) {
		report += fmt::format(
			"[Perf] area_combat casts={} rows={} cols={} active_cells={} sight_checks={} sight_rejected={} "
			"tiles={} tiles_created={} combat_rejected={} spectators={} collect_targets={} blocked={} applied={} "
			"condition_clones={} tile_callbacks={} target_callbacks={} effects={} fields={} recipients={}\n",
			areaCombatCasts,
			areaCombat.areaRows.exchange(0, std::memory_order_relaxed),
			areaCombat.areaColumns.exchange(0, std::memory_order_relaxed),
			areaCombat.activeCells.exchange(0, std::memory_order_relaxed),
			areaCombat.sightChecks.exchange(0, std::memory_order_relaxed),
			areaCombat.sightRejected.exchange(0, std::memory_order_relaxed),
			areaCombat.tilesReturned.exchange(0, std::memory_order_relaxed),
			areaCombat.tilesCreated.exchange(0, std::memory_order_relaxed),
			areaCombat.combatRejected.exchange(0, std::memory_order_relaxed),
			areaCombat.spectators.exchange(0, std::memory_order_relaxed),
			areaCombat.targets.exchange(0, std::memory_order_relaxed),
			areaCombat.blockedTargets.exchange(0, std::memory_order_relaxed),
			areaCombat.appliedTargets.exchange(0, std::memory_order_relaxed),
			areaCombat.conditionClones.exchange(0, std::memory_order_relaxed),
			areaCombat.tileCallbacks.exchange(0, std::memory_order_relaxed),
			areaCombat.targetCallbacks.exchange(0, std::memory_order_relaxed),
			areaCombat.impactEffects.exchange(0, std::memory_order_relaxed),
			areaCombat.fieldsCreated.exchange(0, std::memory_order_relaxed),
			areaCombat.effectRecipients.exchange(0, std::memory_order_relaxed));
	}

	bool hasMonsterIdleMetrics = false;
	std::array<uint64_t, static_cast<size_t>(MonsterIdleMetric::Count)> monsterIdleValues{};
	for (size_t i = 0; i < monsterIdleValues.size(); ++i) {
		monsterIdleValues[i] = monsterIdle[i].exchange(0, std::memory_order_relaxed);
		hasMonsterIdleMetrics = hasMonsterIdleMetrics || monsterIdleValues[i] != 0;
	}
	if (hasMonsterIdleMetrics) {
		report += "[Perf] monster_idle";
		for (size_t i = 0; i < monsterIdleValues.size(); ++i) {
			report += fmt::format(" {}={}", MONSTER_IDLE_METRIC_NAMES[i], monsterIdleValues[i]);
		}
		report += '\n';
	}

	bool hasMonsterActiveReasons = false;
	std::array<uint64_t, static_cast<size_t>(MonsterActiveReason::Count)> monsterActiveReasonValues{};
	for (size_t i = 0; i < monsterActiveReasonValues.size(); ++i) {
		monsterActiveReasonValues[i] = monsterActiveReasons[i].exchange(0, std::memory_order_relaxed);
		hasMonsterActiveReasons = hasMonsterActiveReasons || monsterActiveReasonValues[i] != 0;
	}
	if (hasMonsterActiveReasons) {
		report += "[Perf] monster_active_reason";
		for (size_t i = 0; i < monsterActiveReasonValues.size(); ++i) {
			report += fmt::format(" {}={}", MONSTER_ACTIVE_REASON_NAMES[i], monsterActiveReasonValues[i]);
		}
		report += '\n';
	}

	AreaCombatMetricsSample slowestAreaSample;
	std::string slowestAreaMonster;
	std::string slowestAreaSpell;
	uint64_t slowestAreaNanoseconds = 0;
	{
		std::scoped_lock lock(slowestAreaCombat.mutex);
		slowestAreaNanoseconds = slowestAreaCombat.maximumNanoseconds.exchange(0, std::memory_order_relaxed);
		if (slowestAreaNanoseconds > 0) {
			slowestAreaSample = slowestAreaCombat.sample;
			slowestAreaMonster = std::move(slowestAreaCombat.monsterName);
			slowestAreaSpell = std::move(slowestAreaCombat.spellName);
			slowestAreaCombat.sample = {};
		}
	}
	if (slowestAreaNanoseconds > 0) {
		report += fmt::format(
			"[Perf] area_combat slowest_us={:.3f} monster={} spell={} mode={} pos=({},{},{}) "
			"geometry={}x{} active={} tiles={} created={} targets={} item={} effect={} "
			"conditions={} tile_callback={} target_callback={} "
			"phases_us={{build:{:.3f},prepare:{:.3f},spectators:{:.3f},process_tiles_collect_targets:{:.3f},apply_targets:{:.3f}}}\n",
			slowestAreaNanoseconds / 1'000.0,
			slowestAreaMonster.empty() ? "none" : slowestAreaMonster,
			slowestAreaSpell.empty() ? "unknown" : slowestAreaSpell,
			slowestAreaSample.scripted ? "scripted" : "native",
			slowestAreaSample.positionX, slowestAreaSample.positionY, slowestAreaSample.positionZ,
			slowestAreaSample.areaRows, slowestAreaSample.areaColumns, slowestAreaSample.activeCells,
			slowestAreaSample.tilesReturned, slowestAreaSample.tilesCreated, slowestAreaSample.targets,
			slowestAreaSample.itemId, slowestAreaSample.impactEffect,
			slowestAreaSample.hasConditions, slowestAreaSample.hasTileCallback, slowestAreaSample.hasTargetCallback,
			slowestAreaSample.buildTilesNanoseconds / 1'000.0,
			slowestAreaSample.prepareDamageNanoseconds / 1'000.0,
			slowestAreaSample.collectSpectatorsNanoseconds / 1'000.0,
			slowestAreaSample.processTilesNanoseconds / 1'000.0,
			slowestAreaSample.applyTargetsNanoseconds / 1'000.0);
	}

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
