#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <cstdint>

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

		std::unique_lock lock(mutex_);
		auto& bucket = map_[pos.z];
		uint64_t key = hashXY(pos.x, pos.y);
		auto it = bucket.find(key);
		if (it == bucket.end()) {
			bucket.emplace(key, static_cast<uint8_t>(type));
		} else {
			it->second |= static_cast<uint8_t>(type);
		}
	}

	void removePosition(const Position& pos, FieldType type)
	{
		if (type == FIELD_NONE) return;

		std::unique_lock lock(mutex_);
		auto itz = map_.find(pos.z);
		if (itz == map_.end()) return;
		uint64_t key = hashXY(pos.x, pos.y);
		auto it = itz->second.find(key);
		if (it == itz->second.end()) return;
		uint8_t newMask = it->second & (~static_cast<uint8_t>(type));
		if (newMask == 0) {
			itz->second.erase(it);
			if (itz->second.empty()) map_.erase(itz);
		} else {
			it->second = newMask;
		}
	}

	std::vector<Position> getPositionsInRange(const Position& minPos, const Position& maxPos, int16_t z, FieldType typeMask = FIELD_NONE)
	{
		std::vector<Position> out;
		std::shared_lock lock(mutex_);

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

		return out;
	}

private:
	FieldRegistry() = default;
	~FieldRegistry() = default;
	FieldRegistry(const FieldRegistry&) = delete;
	FieldRegistry& operator=(const FieldRegistry&) = delete;

	static uint64_t hashXY(uint16_t x, uint16_t y) noexcept
	{
		return (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(y);
	}

	std::unordered_map<uint8_t, std::unordered_map<uint64_t, uint8_t>> map_;
	mutable std::shared_mutex mutex_;
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
