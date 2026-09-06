// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_MOVEMENT_DIAGNOSTICS_H
#define FS_MOVEMENT_DIAGNOSTICS_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct WalkTimingContext
{
	std::chrono::steady_clock::time_point requestTime{};
	std::chrono::steady_clock::time_point scheduleTime{};
	std::chrono::steady_clock::time_point fireAt{};
	std::chrono::steady_clock::time_point reactorStartTime{};
	std::chrono::steady_clock::time_point dispatcherStartTime{};
	std::chrono::steady_clock::time_point checkWalkStartTime{};
	std::chrono::steady_clock::time_point physicalMoveTime{};
	uint32_t playerId = 0;
	uint32_t requestedDelayMs = 0;
	uint32_t generation = 0;
};

struct MovementLatencySample
{
	double schedulerOvershootMs = 0.0;
	double reactorQueueMs = 0.0;
	double totalLatencyMs = 0.0;
	double requestToScheduleMs = 0.0;
	double dispatcherDelayMs = 0.0;
};

struct MetricPercentiles
{
	size_t count = 0;
	double min = 0.0;
	double avg = 0.0;
	double p50 = 0.0;
	double p95 = 0.0;
	double p99 = 0.0;
	double max = 0.0;
};

class MovementDiagnostics
{
public:
	static MovementDiagnostics& getInstance() noexcept
	{
		static MovementDiagnostics instance;
		return instance;
	}

	void setEnabled(bool value) noexcept;
	[[nodiscard]] bool isEnabled() const noexcept { return enabled.load(std::memory_order_relaxed); }

	void setReportIntervalSeconds(uint32_t seconds) noexcept
	{
		reportIntervalSeconds.store(seconds > 0 ? seconds : 10, std::memory_order_relaxed);
	}
	[[nodiscard]] uint32_t getReportIntervalSeconds() const noexcept
	{
		return reportIntervalSeconds.load(std::memory_order_relaxed);
	}

	void recordSample(const WalkTimingContext& context) noexcept;
	void recordWalkCallbackExecuted() noexcept { walkCallbacksExecuted.fetch_add(1, std::memory_order_relaxed); }
	void recordWalkStale() noexcept { walkCallbacksStale.fetch_add(1, std::memory_order_relaxed); }
	void recordWalkCancelled() noexcept { walkCallbacksCancelled.fetch_add(1, std::memory_order_relaxed); }

	void maybeReport();
	[[nodiscard]] std::string formatReportSnapshot() const;
	void reset();

	// Synthetic load test
	bool startStressTest(uint32_t level);
	[[nodiscard]] bool isStressTestRunning() const noexcept { return stressRunning.load(std::memory_order_relaxed); }
	void stopStressTest();

	// Accessors for metrics/testing
	[[nodiscard]] uint64_t getWalkCallbacksExecuted() const noexcept { return walkCallbacksExecuted.load(std::memory_order_relaxed); }
	[[nodiscard]] uint64_t getWalkCallbacksStale() const noexcept { return walkCallbacksStale.load(std::memory_order_relaxed); }
	[[nodiscard]] uint64_t getWalkCallbacksCancelled() const noexcept { return walkCallbacksCancelled.load(std::memory_order_relaxed); }
	[[nodiscard]] size_t getSampleCount() const noexcept;
	[[nodiscard]] MetricPercentiles getTotalLatencyPercentiles() const;
	[[nodiscard]] MetricPercentiles getSchedulerOvershootPercentiles() const;
	[[nodiscard]] MetricPercentiles getReactorQueuePercentiles() const;

private:
	MovementDiagnostics() = default;

	static constexpr size_t BUFFER_SIZE = 4096;

	std::atomic<bool> enabled{false};
	std::atomic<uint32_t> reportIntervalSeconds{10};
	std::atomic<int64_t> nextReportNanoseconds{0};

	mutable std::mutex sampleMutex;
	std::array<MovementLatencySample, BUFFER_SIZE> samples{};
	size_t sampleHead = 0;
	size_t sampleCount = 0;
	uint64_t totalSamplesRecorded = 0;

	std::atomic<uint64_t> walkCallbacksExecuted{0};
	std::atomic<uint64_t> walkCallbacksStale{0};
	std::atomic<uint64_t> walkCallbacksCancelled{0};
	mutable std::atomic<size_t> maxQueueObserved{0};

	// Stress test state
	void stepStressTest();
	std::atomic<bool> stressRunning{false};
	uint32_t stressTicksRemaining = 0;
	uint32_t stressTasksPerTick = 0;
	uint32_t stressActiveLevel = 0;
};

inline MovementDiagnostics& g_movementDiagnostics = MovementDiagnostics::getInstance();

#endif // FS_MOVEMENT_DIAGNOSTICS_H
