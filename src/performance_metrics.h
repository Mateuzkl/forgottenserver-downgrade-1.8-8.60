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
	CombatAreaBuildTiles,
	CombatAreaPrepareDamage,
	CombatAreaCollectSpectators,
	CombatAreaProcessTiles,
	CombatAreaApplyTargets,
	CreatureExecuteConditions,
	Count,
};

enum class MonsterIdleMetric : uint8_t
{
	RefreshCalls,
	DecisionTrue,
	DecisionFalse,
	TransitionToIdle,
	TransitionToActive,
	SameStateCalls,
	PruneCalls,
	TargetsPruned,
	FriendsPruned,
	AttackedCleared,
	FollowCleared,
	BlockedByTarget,
	BlockedByCondition,
	BlockedBySummon,
	BlockedByFaction,
	ActiveWithoutReason,
	OnIdleStatusCalls,
	DamageMapClears,
	CreatureCheckAdds,
	CreatureCheckRemoves,
	Count,
};

enum class MonsterActiveReason : uint8_t
{
	TargetList,
	AttackedCreature,
	FollowCreature,
	AggressiveCondition,
	Summon,
	FactionTarget,
	Unknown,
	Count,
};

struct AreaCombatMetricsSample
{
	uint64_t buildTilesNanoseconds = 0;
	uint64_t prepareDamageNanoseconds = 0;
	uint64_t collectSpectatorsNanoseconds = 0;
	uint64_t processTilesNanoseconds = 0;
	uint64_t applyTargetsNanoseconds = 0;
	uint64_t totalNanoseconds = 0;

	uint64_t areaRows = 0;
	uint64_t areaColumns = 0;
	uint64_t activeCells = 0;
	uint64_t sightChecks = 0;
	uint64_t sightRejected = 0;
	uint64_t tilesReturned = 0;
	uint64_t tilesCreated = 0;
	uint64_t combatRejected = 0;
	uint64_t spectators = 0;
	uint64_t targets = 0;
	uint64_t blockedTargets = 0;
	uint64_t appliedTargets = 0;
	uint64_t conditionClones = 0;
	uint64_t tileCallbacks = 0;
	uint64_t targetCallbacks = 0;
	uint64_t impactEffects = 0;
	uint64_t fieldsCreated = 0;
	uint64_t effectRecipients = 0;

	uint16_t positionX = 0;
	uint16_t positionY = 0;
	uint16_t itemId = 0;
	uint16_t impactEffect = 0;
	uint8_t positionZ = 0;
	bool scripted = false;
	bool hasConditions = false;
	bool hasTileCallback = false;
	bool hasTargetCallback = false;
};

class PerformanceMetrics
{
public:
	void setEnabled(bool value) noexcept;

	[[nodiscard]] bool isEnabled() const noexcept
	{
		return enabled.load(std::memory_order_relaxed);
	}

	void record(PerformanceMetric metric, uint64_t nanoseconds) noexcept;

	void recordQueueSize(size_t current) noexcept;
	void recordTaskDeferred(uint64_t count = 1) noexcept;
	void recordTaskExpired(uint64_t count = 1) noexcept;
	void recordTaskDropped(uint64_t count = 1) noexcept;

	void recordNetworkAcceptStarted() noexcept;
	void recordNetworkAccept(bool success) noexcept;
	void recordNetworkRateLimitRejection() noexcept;
	void recordNetworkIpLimitRejection() noexcept;
	void recordNetworkConnectionCount(size_t current) noexcept;

	void recordReactorCallbackSource(
	    uint64_t nanoseconds,
	    std::string_view description,
	    std::string_view origin) noexcept;

	void recordPathRequest(
	    bool success,
	    uint64_t nodesVisited,
	    uint64_t tilesRead,
	    uint64_t pathLength) noexcept;

	void recordAreaCombat(
	    const AreaCombatMetricsSample& sample,
	    std::string_view monsterName,
	    std::string_view spellName) noexcept;

	void recordMonsterIdle(
	    MonsterIdleMetric metric,
	    uint64_t count = 1) noexcept;

	void recordMonsterActiveReason(
	    MonsterActiveReason reason,
	    uint64_t count = 1) noexcept;

	[[nodiscard]] uint64_t getMonsterIdleMetric(
	    MonsterIdleMetric metric) const noexcept;

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

	struct AreaCombatData
	{
		std::atomic<uint64_t> casts{0};
		std::atomic<uint64_t> areaRows{0};
		std::atomic<uint64_t> areaColumns{0};
		std::atomic<uint64_t> activeCells{0};
		std::atomic<uint64_t> sightChecks{0};
		std::atomic<uint64_t> sightRejected{0};
		std::atomic<uint64_t> tilesReturned{0};
		std::atomic<uint64_t> tilesCreated{0};
		std::atomic<uint64_t> combatRejected{0};
		std::atomic<uint64_t> spectators{0};
		std::atomic<uint64_t> targets{0};
		std::atomic<uint64_t> blockedTargets{0};
		std::atomic<uint64_t> appliedTargets{0};
		std::atomic<uint64_t> conditionClones{0};
		std::atomic<uint64_t> tileCallbacks{0};
		std::atomic<uint64_t> targetCallbacks{0};
		std::atomic<uint64_t> impactEffects{0};
		std::atomic<uint64_t> fieldsCreated{0};
		std::atomic<uint64_t> effectRecipients{0};
	};

	struct NetworkData
	{
		std::atomic<uint64_t> acceptStarted{0};
		std::atomic<uint64_t> accepted{0};
		std::atomic<uint64_t> acceptErrors{0};
		std::atomic<uint64_t> rateLimitRejections{0};
		std::atomic<uint64_t> ipLimitRejections{0};
		std::atomic<uint64_t> connectionsCurrent{0};
		std::atomic<uint64_t> connectionsMaximum{0};
	};

	struct SlowestReactorCallback
	{
		std::mutex mutex;
		uint64_t nanoseconds = 0;
		std::string description;
		std::string origin;
	};

	struct SlowestAreaCombat
	{
		std::mutex mutex;
		std::atomic<uint64_t> maximumNanoseconds{0};
		AreaCombatMetricsSample sample;
		std::string monsterName;
		std::string spellName;
	};

	std::array<MetricData, static_cast<size_t>(PerformanceMetric::Count)> metrics;

	ReactorData reactor;
	PathData path;
	AreaCombatData areaCombat;
	NetworkData network;

	std::array<
	    std::atomic<uint64_t>,
	    static_cast<size_t>(MonsterIdleMetric::Count)>
	    monsterIdle{};

	std::array<
	    std::atomic<uint64_t>,
	    static_cast<size_t>(MonsterActiveReason::Count)>
	    monsterActiveReasons{};

	SlowestReactorCallback slowestReactorCallback;
	SlowestAreaCombat slowestAreaCombat;

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