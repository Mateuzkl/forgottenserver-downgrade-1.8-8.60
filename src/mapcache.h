// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_MAPCACHE_H
#define FS_MAPCACHE_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Item;
class Tile;
class House;
struct Position;

// Hash combine utility (similar to boost::hash_combine)
inline void hash_combine(size_t& seed, size_t value) { seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2); }

/**
 * BasicItem - Lightweight item structure for map loading cache
 * Uses minimal memory compared to full Item objects
 */
struct BasicItem
{
	std::string text;

	uint16_t id{0};
	uint16_t charges{0}; // Runecharges and Count
	uint16_t actionId{0};
	uint16_t uniqueId{0};
	uint16_t destX{0}, destY{0};
	uint16_t doorOrDepotId{0};

	uint8_t destZ{0};

	std::vector<std::shared_ptr<BasicItem>> items; // For containers

	/**
	 * Calculate hash for deduplication
	 */
	size_t hash() const
	{
		size_t h = 0;

		if (id > 0) hash_combine(h, id);
		if (charges > 0) hash_combine(h, charges);
		if (actionId > 0) hash_combine(h, actionId);
		if (uniqueId > 0) hash_combine(h, uniqueId);
		if (destX > 0) hash_combine(h, destX);
		if (destY > 0) hash_combine(h, destY);
		if (destZ > 0) hash_combine(h, destZ);
		if (doorOrDepotId > 0) hash_combine(h, doorOrDepotId);

		if (!text.empty()) {
			hash_combine(h, std::hash<std::string>{}(text));
		}

		if (!items.empty()) {
			hash_combine(h, items.size());
			for (const auto& item : items) {
				if (item) { // Null-safety
					hash_combine(h, item->hash());
				}
			}
		}

		return h;
	}

	/**
	 * Equality operator for hash collision detection
	 */
	bool operator==(const BasicItem& other) const
	{
		if (id != other.id || charges != other.charges || actionId != other.actionId || uniqueId != other.uniqueId ||
		    doorOrDepotId != other.doorOrDepotId || text != other.text || destX != other.destX ||
		    destY != other.destY || destZ != other.destZ) {
			return false;
		}

		if (items.size() != other.items.size()) {
			return false;
		}

		for (size_t i = 0; i < items.size(); ++i) {
			if (!items[i] || !other.items[i]) {
				if (items[i] != other.items[i]) {
					return false;
				}
			} else if (*items[i] != *other.items[i]) {
				return false;
			}
		}

		return true;
	}

	bool operator!=(const BasicItem& other) const { return !(*this == other); }
};

/**
 * BasicTile - Lightweight tile structure for map loading cache
 */
struct BasicTile
{
	std::shared_ptr<BasicItem> ground{nullptr};
	std::vector<std::shared_ptr<BasicItem>> items;

	uint32_t flags{0};
	uint32_t houseId{0};

	bool isStatic{false};

	bool isEmpty(bool ignoreFlag = false) const
	{
		return (ignoreFlag || flags == 0) && ground == nullptr && items.empty();
	}

	bool isHouse() const { return houseId != 0; }

	/**
	 * Calculate hash for deduplication
	 */
	size_t hash() const
	{
		size_t h = 0;

		if (flags > 0) hash_combine(h, flags);
		if (houseId > 0) hash_combine(h, houseId);
		if (isStatic) hash_combine(h, 1);

		if (ground != nullptr) {
			hash_combine(h, ground->hash());
		}

		if (!items.empty()) {
			hash_combine(h, items.size());
			for (const auto& item : items) {
				if (item) { // Null-safety
					hash_combine(h, item->hash());
				}
			}
		}

		return h;
	}

	/**
	 * Equality operator for hash collision detection
	 */
	bool operator==(const BasicTile& other) const
	{
		if (flags != other.flags || houseId != other.houseId || isStatic != other.isStatic) {
			return false;
		}

		// Compare ground
		if ((ground == nullptr) != (other.ground == nullptr)) {
			return false;
		}
		if (ground && other.ground && *ground != *other.ground) {
			return false;
		}

		// Compare items
		if (items.size() != other.items.size()) {
			return false;
		}

		for (size_t i = 0; i < items.size(); ++i) {
			if (!items[i] || !other.items[i]) {
				if (items[i] != other.items[i]) {
					return false;
				}
			} else if (*items[i] != *other.items[i]) {
				return false;
			}
		}

		return true;
	}

	bool operator!=(const BasicTile& other) const { return !(*this == other); }
};

/**
 * MapCache - Global cache for item and tile deduplication during map loading
 *
 * This class provides hash-based deduplication of items and tiles,
 * significantly reducing memory usage when loading large maps.
 */
class MapCache
{
public:
	/**
	 * Try to get cached item or store new one
	 * @param item The item to cache or find
	 * @return Shared pointer to the cached item (may be the same or existing one)
	 */
	static std::shared_ptr<BasicItem> tryGetItemFromCache(const std::shared_ptr<BasicItem>& item);

	/**
	 * Try to get cached tile or store new one
	 * @param tile The tile to cache or find
	 * @return Shared pointer to the cached tile (may be the same or existing one)
	 */
	static std::shared_ptr<BasicTile> tryGetTileFromCache(const std::shared_ptr<BasicTile>& tile);

	/**
	 * Clear all caches after map loading is complete
	 */
	static void flush();

	/**
	 * Cleanup expired entries from the cache
	 */
	static void cleanupExpiredEntries();

	/**
	 * Map Parsing Helpers (used by IOMap)
	 */
	static std::shared_ptr<BasicItem> parseBasicItem(void* loaderptr, const void* nodeptr, BasicItem* parent = nullptr);
	static std::shared_ptr<BasicTile> parseBasicTile(void* loaderptr, const void* nodeptr, uint8_t& xOffset,
	                                                 uint8_t& yOffset);

	/**
	 * Get cache statistics for debugging
	 */
	static size_t getItemCacheSize();
	static size_t getTileCacheSize();

private:
	static std::unordered_map<size_t, std::weak_ptr<BasicItem>> itemCache;
	static std::unordered_map<size_t, std::weak_ptr<BasicTile>> tileCache;
	static std::mutex itemCacheMutex;
	static std::mutex tileCacheMutex;

	// Performance metrics
	static std::atomic<size_t> itemCacheHits;
	static std::atomic<size_t> itemCacheMisses;
	static std::atomic<size_t> tileCacheHits;
	static std::atomic<size_t> tileCacheMisses;
};

class Houses;

/**
 * MapCacheUtils - Helper functions for converting BasicItem/BasicTile to real objects
 */
namespace MapCacheUtils {
/**
 * Create a real Item from a BasicItem
 */
Item* createItemFromBasic(const std::shared_ptr<BasicItem>& basicItem, const Position& pos);

/**
 * Create a real Tile from a BasicTile
 */
std::unique_ptr<Tile> createTileFromBasic(const std::shared_ptr<BasicTile>& basicTile, uint16_t x, uint16_t y,
                                          uint8_t z, Houses& houses);
} // namespace MapCacheUtils

#endif // FS_MAPCACHE_H
