// FieldRegistry - registry of special field-like items (magic wall, fire/poison/energy fields, wildgrowth).
// Partitioned by floor (z) and using shared mutex for high-read concurrency.
// Stores a bitmask of FieldType per (x,y) position so multiple field types on same tile are tracked.
//
// Metrics are controlled at runtime via config.lua: fieldRegistryMetrics = true/false (default: false).
// When enabled, periodic logging prints reads/writes/adds/removes/lock contention to the console.
// When disabled, the only overhead is one relaxed atomic load per operation.
#pragma once

#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <iostream>
#include <cstdint>

#include "configmanager.h"
#include "items.h"
#include "logger.h"
#include "position.h"

#define FIELD_REGISTRY_MAX_POSITIONS 256

enum FieldType : uint8_t {
	FIELD_NONE = 0,
	FIELD_MAGICWALL = 1 << 0,
	FIELD_FIRE = 1 << 1,
	FIELD_POISON = 1 << 2,
	FIELD_ENERGY = 1 << 3,
	FIELD_WILDGROWTH = 1 << 4,
	FIELD_UNKNOWN_MAGICFIELD = 1 << 5,
};

inline FieldType operator|(FieldType a, FieldType b) { return static_cast<FieldType>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b)); }
inline FieldType operator&(FieldType a, FieldType b) { return static_cast<FieldType>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b)); }

class FieldRegistry {
public:
	static FieldRegistry& instance()
	{
		static FieldRegistry inst;
		return inst;
	}

	void addPosition(const Position& pos, FieldType type)
	{
		if (type == FIELD_NONE) return;

		bool enabled = metricsEnabled_.load(std::memory_order_relaxed);
		const auto t0 = enabled ? now() : 0;

		{
			std::unique_lock lock(mutex_);
			if (enabled) {
				lockWaitUniqueNs_.fetch_add(durationSince(t0), std::memory_order_relaxed);
			}
			auto& bucket = map_[pos.z];
			uint64_t key = hashXY(pos.x, pos.y);
			auto it = bucket.find(key);
			if (it == bucket.end()) {
				bucket.emplace(key, static_cast<uint8_t>(type));
				if (enabled) {
					adds_.fetch_add(1, std::memory_order_relaxed);
				}
			} else {
				uint8_t newMask = it->second | static_cast<uint8_t>(type);
				it->second = newMask;
			}
		}

		if (enabled) {
			writes_.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void removePosition(const Position& pos, FieldType type)
	{
		if (type == FIELD_NONE) return;

		bool enabled = metricsEnabled_.load(std::memory_order_relaxed);
		const auto t0 = enabled ? now() : 0;

		{
			std::unique_lock lock(mutex_);
			if (enabled) {
				lockWaitUniqueNs_.fetch_add(durationSince(t0), std::memory_order_relaxed);
			}
			auto itz = map_.find(pos.z);
			if (itz == map_.end()) return;
			uint64_t key = hashXY(pos.x, pos.y);
			auto it = itz->second.find(key);
			if (it == itz->second.end()) return;
			uint8_t newMask = it->second & (~static_cast<uint8_t>(type));
			if (newMask == 0) {
				itz->second.erase(it);
				if (itz->second.empty()) map_.erase(itz);
				if (enabled) {
					removes_.fetch_add(1, std::memory_order_relaxed);
				}
			} else {
				it->second = newMask;
			}
		}

		if (enabled) {
			writes_.fetch_add(1, std::memory_order_relaxed);
		}
	}

	std::vector<Position> getPositionsInRange(const Position& minPos, const Position& maxPos, int16_t z, FieldType typeMask = FIELD_NONE)
	{
		bool enabled = metricsEnabled_.load(std::memory_order_relaxed);
		const auto t0 = enabled ? now() : 0;

		std::vector<Position> out;
		std::shared_lock lock(mutex_);

		if (enabled) {
			lockWaitSharedNs_.fetch_add(durationSince(t0), std::memory_order_relaxed);
		}

		auto itz = map_.find(static_cast<uint8_t>(z));
		if (itz == map_.end()) return out;

		out.reserve(32);
		for (uint32_t x = minPos.x; x <= maxPos.x; ++x) {
			for (uint32_t y = minPos.y; y <= maxPos.y; ++y) {
				if (out.size() >= FIELD_REGISTRY_MAX_POSITIONS) {
					break;
				}
				uint64_t key = hashXY(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
				auto it = itz->second.find(key);
				if (it != itz->second.end()) {
					if (typeMask != FIELD_NONE) {
						if ((it->second & static_cast<uint8_t>(typeMask)) == 0) {
							continue;
						}
					}
					out.emplace_back(static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint8_t>(z));
				}
			}
		}

		if (enabled) {
			totalPositionsReturned_.fetch_add(out.size(), std::memory_order_relaxed);
			reads_.fetch_add(1, std::memory_order_relaxed);
		}
		return out;
	}

	// --- Metrics (always compiled; controlled by config.lua at runtime) ---

	struct Metrics {
		uint64_t reads = 0;
		uint64_t writes = 0;
		uint64_t adds = 0;
		uint64_t removes = 0;
		uint64_t totalPositionsReturned = 0;
		uint64_t lockWaitSharedNs = 0;
		uint64_t lockWaitUniqueNs = 0;
	};

	Metrics snapshotMetrics() const
	{
		Metrics m;
		m.reads = reads_.load(std::memory_order_relaxed);
		m.writes = writes_.load(std::memory_order_relaxed);
		m.adds = adds_.load(std::memory_order_relaxed);
		m.removes = removes_.load(std::memory_order_relaxed);
		m.totalPositionsReturned = totalPositionsReturned_.load(std::memory_order_relaxed);
		m.lockWaitSharedNs = lockWaitSharedNs_.load(std::memory_order_relaxed);
		m.lockWaitUniqueNs = lockWaitUniqueNs_.load(std::memory_order_relaxed);
		return m;
	}

	void dumpMetrics()
	{
		Metrics m = snapshotMetrics();
		std::cout << "[FieldRegistry Metrics] reads=" << m.reads << " writes=" << m.writes
		          << " adds=" << m.adds << " removes=" << m.removes
		          << " positionsReturned=" << m.totalPositionsReturned
		          << " lockSharedMs=" << (m.lockWaitSharedNs / 1000000.0)
		          << " lockUniqueMs=" << (m.lockWaitUniqueNs / 1000000.0) << std::endl;
	}

	void resetMetrics()
	{
		reads_.store(0);
		writes_.store(0);
		adds_.store(0);
		removes_.store(0);
		totalPositionsReturned_.store(0);
		lockWaitSharedNs_.store(0);
		lockWaitUniqueNs_.store(0);
	}

	void startMetricsLogger(int intervalSeconds = 30)
	{
		if (!ConfigManager::getBoolean(ConfigManager::FIELD_REGISTRY_METRICS)) {
			return;
		}

		metricsEnabled_.store(true, std::memory_order_relaxed);

		std::lock_guard<std::mutex> lk(metricsMutex_);
		if (metricsRunning_) return;
		metricsRunning_ = true;
		metricsThread_ = std::thread([this, intervalSeconds]() {
			std::unique_lock<std::mutex> lk(metricsMutex_);
			while (metricsRunning_) {
				if (metricsCv_.wait_for(lk, std::chrono::seconds(intervalSeconds), [this] { return !metricsRunning_; })) {
					break;
				}
				this->dumpMetrics();
				this->resetMetrics();
			}
		});
	}

	void stopMetricsLogger()
	{
		{
			std::lock_guard<std::mutex> lk(metricsMutex_);
			if (!metricsRunning_) return;
			metricsRunning_ = false;
			metricsCv_.notify_all();
		}
		if (metricsThread_.joinable()) metricsThread_.join();
		metricsEnabled_.store(false, std::memory_order_relaxed);
	}

private:
	FieldRegistry() = default;
	~FieldRegistry() { stopMetricsLogger(); }
	FieldRegistry(const FieldRegistry&) = delete;
	FieldRegistry& operator=(const FieldRegistry&) = delete;

	static uint64_t hashXY(uint16_t x, uint16_t y) noexcept
	{
		return (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(y);
	}

	static std::pair<uint32_t, uint32_t> unhashXY(uint64_t v) noexcept
	{
		uint32_t x = static_cast<uint32_t>(v >> 32);
		uint32_t y = static_cast<uint32_t>(v & 0xFFFFFFFF);
		return {x, y};
	}

	static inline uint64_t now() noexcept
	{
		return static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
	}

	static inline uint64_t durationSince(uint64_t t0) noexcept
	{
		uint64_t t1 = now();
		return (t1 > t0) ? (t1 - t0) : 0;
	}

	std::unordered_map<uint8_t, std::unordered_map<uint64_t, uint8_t>> map_;
	mutable std::shared_mutex mutex_;

	std::atomic<bool> metricsEnabled_{false};

	std::atomic<uint64_t> reads_{0};
	std::atomic<uint64_t> writes_{0};
	std::atomic<uint64_t> adds_{0};
	std::atomic<uint64_t> removes_{0};
	std::atomic<uint64_t> totalPositionsReturned_{0};
	std::atomic<uint64_t> lockWaitSharedNs_{0};
	std::atomic<uint64_t> lockWaitUniqueNs_{0};

	std::thread metricsThread_;
	std::mutex metricsMutex_;
	std::condition_variable metricsCv_;
	bool metricsRunning_ = false;
};

inline FieldType getFieldTypeFromItemId(uint16_t itemId)
{
	switch (itemId) {
		case ITEM_MAGICWALL:
		case ITEM_MAGICWALL_SAFE:
		case ITEM_MAGICWALL_PERSISTENT:
		case ITEM_MAGICWALL_NOPVP:
			return FIELD_MAGICWALL;

		case ITEM_FIREFIELD_PVP_FULL:
		case ITEM_FIREFIELD_PVP_MEDIUM:
		case ITEM_FIREFIELD_PVP_SMALL:
		case ITEM_FIREFIELD_PERSISTENT_FULL:
		case ITEM_FIREFIELD_PERSISTENT_MEDIUM:
		case ITEM_FIREFIELD_PERSISTENT_SMALL:
		case ITEM_FIREFIELD_NOPVP:
			return FIELD_FIRE;

		case ITEM_POISONFIELD_PVP:
		case ITEM_POISONFIELD_PERSISTENT:
		case ITEM_POISONFIELD_NOPVP:
			return FIELD_POISON;

		case ITEM_ENERGYFIELD_PVP:
		case ITEM_ENERGYFIELD_PERSISTENT:
		case ITEM_ENERGYFIELD_NOPVP:
			return FIELD_ENERGY;

		case ITEM_WILDGROWTH:
		case ITEM_WILDGROWTH_PERSISTENT:
		case ITEM_WILDGROWTH_SAFE:
		case ITEM_WILDGROWTH_NOPVP:
			return FIELD_WILDGROWTH;

		default:
			break;
	}

	if (itemId < Item::items.size()) {
		const ItemType& it = Item::items[itemId];
		if (it.isMagicField()) {
			static std::unordered_set<uint16_t> warnedIds;
			static std::mutex warnMutex;
			{
				std::lock_guard<std::mutex> lk(warnMutex);
				if (warnedIds.insert(itemId).second) {
					LOG_WARN("[FieldRegistry] Unknown magic field item ID {}. Add it to getFieldTypeFromItemId() in field_registry.h.", itemId);
				}
			}
			return FIELD_UNKNOWN_MAGICFIELD;
		}
	}

	return FIELD_NONE;
}
