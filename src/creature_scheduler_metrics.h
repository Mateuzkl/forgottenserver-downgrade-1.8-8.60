// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CREATURE_SCHEDULER_METRICS_H
#define FS_CREATURE_SCHEDULER_METRICS_H

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>

// Aggregated lifecycle counters for creature walk/follow events.
// Lifecycle counters (scheduled/executed/cancelled/stale, follow states) are
// always on — a single relaxed fetch_add per event — so their balance holds
// from process start (walkPending derives from it). creatureSchedulerMetrics
// in config.lua gates the costlier sampling: the delay histogram, spectator
// fan-out and movement packet accounting. Counters are cumulative since
// startup: consumers (Prometheus, /creaturestats) compute rates and deltas.
class CreatureSchedulerMetrics
{
public:
	// Power-of-two microsecond histogram: bucket N counts samples with
	// value < 2^N µs. 40 buckets cover ~12.7 days, far beyond any delay.
	static constexpr size_t HistogramBuckets = 40;

	struct Histogram
	{
		std::atomic<uint64_t> calls{0};
		std::atomic<uint64_t> totalMicroseconds{0};
		std::atomic<uint64_t> maximumMicroseconds{0};
		std::array<std::atomic<uint64_t>, HistogramBuckets> buckets{};

		void record(uint64_t microseconds) noexcept
		{
			calls.fetch_add(1, std::memory_order_relaxed);
			totalMicroseconds.fetch_add(microseconds, std::memory_order_relaxed);

			uint64_t previousMaximum = maximumMicroseconds.load(std::memory_order_relaxed);
			while (microseconds > previousMaximum &&
			       !maximumMicroseconds.compare_exchange_weak(previousMaximum, microseconds,
			                                                  std::memory_order_relaxed)) {
			}

			const size_t index =
			    std::min<size_t>(std::bit_width(microseconds), HistogramBuckets - 1);
			buckets[index].fetch_add(1, std::memory_order_relaxed);
		}

		// Approximate percentile: the upper bound (2^N µs) of the bucket
		// containing the requested quantile, clamped to the observed maximum.
		[[nodiscard]] uint64_t percentileMicroseconds(double quantile) const noexcept
		{
			const uint64_t totalCalls = calls.load(std::memory_order_relaxed);
			if (totalCalls == 0) {
				return 0;
			}

			const uint64_t observedMaximum = maximumMicroseconds.load(std::memory_order_relaxed);
			const auto threshold = static_cast<uint64_t>(static_cast<double>(totalCalls) * quantile);
			uint64_t cumulative = 0;
			for (size_t index = 0; index < HistogramBuckets; ++index) {
				cumulative += buckets[index].load(std::memory_order_relaxed);
				if (cumulative >= threshold && cumulative > 0) {
					const uint64_t upperBound = index == 0 ? 1 : (uint64_t{1} << index);
					return std::min(upperBound, observedMaximum);
				}
			}
			return observedMaximum;
		}
	};

	void setEnabled(bool value) noexcept { enabled.store(value, std::memory_order_relaxed); }
	[[nodiscard]] bool isEnabled() const noexcept { return enabled.load(std::memory_order_relaxed); }

	void setDebug(bool value) noexcept { debug.store(value, std::memory_order_relaxed); }
	[[nodiscard]] bool isDebug() const noexcept { return debug.load(std::memory_order_relaxed); }

	// Walk event lifecycle. Every scheduled event terminates exactly once:
	// executed as the tracked event, or cancelled by stopEventWalk (a
	// cancelled event that still runs is additionally counted stale).
	// Pending events are therefore derived: scheduled - executed - cancelled.
	std::atomic<uint64_t> walkScheduled{0};
	std::atomic<uint64_t> walkExecuted{0};
	std::atomic<uint64_t> walkCancelled{0};
	std::atomic<uint64_t> walkStale{0};
	std::atomic<uint64_t> walkRejectedGeneration{0}; // incremented once stale rejection ships
	std::atomic<uint64_t> walkRejectedEventId{0};    // incremented once stale rejection ships
	Histogram walkExecutionDelay;                    // scheduled fire time -> callback entry

	[[nodiscard]] int64_t walkPending() const noexcept
	{
		const auto scheduled = static_cast<int64_t>(walkScheduled.load(std::memory_order_relaxed));
		const auto executed = static_cast<int64_t>(walkExecuted.load(std::memory_order_relaxed));
		const auto cancelled = static_cast<int64_t>(walkCancelled.load(std::memory_order_relaxed));
		return scheduled - executed - cancelled;
	}

	// Follow path update lifecycle (requestFollowPathUpdate / updateCreatureWalk).
	std::atomic<uint64_t> followRequested{0};
	std::atomic<uint64_t> followCoalescedReuse{0};   // served from FollowPathState snapshot
	std::atomic<uint64_t> followCoalescedBackoff{0}; // suppressed by failure backoff
	std::atomic<uint64_t> followCoalescedPending{0}; // request already in flight
	std::atomic<uint64_t> followExecuted{0};
	std::atomic<uint64_t> followStale{0}; // generation/lifetime mismatch on delivery

	// Spectator fan-out per Map::moveCreature.
	std::atomic<uint64_t> moveCalls{0};
	std::atomic<uint64_t> moveSpectatorsTotal{0};
	std::atomic<uint64_t> moveSpectatorsMaximum{0};
	std::atomic<uint64_t> movePlayerSpectatorsTotal{0};

	void recordMoveSpectators(uint64_t spectatorCount, uint64_t playerCount) noexcept
	{
		moveCalls.fetch_add(1, std::memory_order_relaxed);
		moveSpectatorsTotal.fetch_add(spectatorCount, std::memory_order_relaxed);
		movePlayerSpectatorsTotal.fetch_add(playerCount, std::memory_order_relaxed);

		uint64_t previousMaximum = moveSpectatorsMaximum.load(std::memory_order_relaxed);
		while (spectatorCount > previousMaximum &&
		       !moveSpectatorsMaximum.compare_exchange_weak(previousMaximum, spectatorCount,
		                                                    std::memory_order_relaxed)) {
		}
	}

	// Movement packets written by ProtocolGame::sendMoveCreature. Bytes cover
	// the locally-built messages only (delegated map refreshes are counted as
	// packets, not bytes), so treat bytes as a lower bound.
	std::atomic<uint64_t> networkMovePackets{0};
	std::atomic<uint64_t> networkMoveBytes{0};

private:
	std::atomic<bool> enabled{false};
	std::atomic<bool> debug{false};
};

inline CreatureSchedulerMetrics g_creatureSchedulerMetrics;

#endif // FS_CREATURE_SCHEDULER_METRICS_H
