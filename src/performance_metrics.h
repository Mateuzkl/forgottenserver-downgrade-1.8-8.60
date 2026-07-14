// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_PERFORMANCE_METRICS_H
#define FS_PERFORMANCE_METRICS_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

enum class PerformanceMetric : uint8_t
{
	ReactorCycle,
	ReactorDrainInbox,
	ReactorDrainReady,
	ReactorSort,
	ReactorCallbacks,
	ReactorCallback,
	ReactorQueueLatency,
	GameCheckCreatures,
	GameCheckCreatureWalk,
	GameUpdateCreatureWalk,
	CreatureGoToFollow,
	CreatureOnAttacking,
	GameInternalMoveCreature,
	MapGetPathMatching,
	MapMoveCreature,
	MapGetSpectators,
	MonsterOnThink,
	MonsterOnWalk,
	MonsterDoAttacking,
	CombatSpellCastSpell,
	CombatDoCombat,
	CombatDoAreaCombat,
	Count,
};

class PerformanceMetrics
{
public:
	void setEnabled(bool value) noexcept;
	[[nodiscard]] bool isEnabled() const noexcept { return enabled.load(std::memory_order_relaxed); }

	void record(PerformanceMetric metric, uint64_t nanoseconds) noexcept;
	void recordQueueSize(size_t current) noexcept;
	void recordTaskDeferred(uint64_t count = 1) noexcept;
	void recordTaskExpired(uint64_t count = 1) noexcept;
	void recordTaskDropped(uint64_t count = 1) noexcept;
	void recordReactorCallbackSource(uint64_t nanoseconds, std::string_view description,
	                                 std::string_view origin) noexcept;
	void recordPathRequest(bool success, uint64_t nodesVisited, uint64_t tilesRead, uint64_t pathLength) noexcept;
	void maybeReport();

private:
	static constexpr size_t HistogramBuckets = 64;

	struct MetricData
	{
		std::atomic<uint64_t> calls{0};
		std::atomic<uint64_t> totalNanoseconds{0};
		std::atomic<uint64_t> maximumNanoseconds{0};
		std::array<std::atomic<uint64_t>, HistogramBuckets> histogram{};
	};

	struct ReactorData
	{
		std::atomic<uint64_t> queueCurrent{0};
		std::atomic<uint64_t> queueMaximum{0};
		std::atomic<uint64_t> deferred{0};
		std::atomic<uint64_t> expired{0};
		std::atomic<uint64_t> dropped{0};
	};

	struct PathData
	{
		std::atomic<uint64_t> requests{0};
		std::atomic<uint64_t> successes{0};
		std::atomic<uint64_t> failures{0};
		std::atomic<uint64_t> nodesVisited{0};
		std::atomic<uint64_t> tilesRead{0};
		std::atomic<uint64_t> pathLength{0};
	};

	struct SlowestReactorCallback
	{
		std::mutex mutex;
		uint64_t nanoseconds = 0;
		std::string description;
		std::string origin;
	};

	std::array<MetricData, static_cast<size_t>(PerformanceMetric::Count)> metrics;
	ReactorData reactor;
	PathData path;
	SlowestReactorCallback slowestReactorCallback;
	std::atomic_bool enabled{false};
	std::atomic<int64_t> nextReportNanoseconds{0};
};

class PerformanceScope
{
public:
	explicit PerformanceScope(PerformanceMetric metric) noexcept;
	~PerformanceScope();

	PerformanceScope(const PerformanceScope&) = delete;
	PerformanceScope& operator=(const PerformanceScope&) = delete;

private:
	PerformanceMetric metric;
	std::chrono::steady_clock::time_point started;
	bool active;
};

extern PerformanceMetrics g_performanceMetrics;

#endif // FS_PERFORMANCE_METRICS_H
