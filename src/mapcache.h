// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_MAPCACHE_H
#define FS_MAPCACHE_H

#include "zones.h"

class Item;
class Tile;
class House;
class Map;
struct Position;

// Hash combine utility for composite keys.
inline void hash_combine(size_t& seed, size_t value) {
    seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

/**
 * BasicItem - Lightweight item structure for map loading cache
 * Uses minimal memory compared to full Item objects
 */
struct BasicItem {
    std::string text;
    
    uint16_t id{0};
    uint16_t charges{0};     // Runecharges and Count
    uint16_t actionId{0};
    uint16_t uniqueId{0};
    uint16_t destX{0}, destY{0};
    uint16_t doorOrDepotId{0};
    
    uint8_t destZ{0};
    
    std::vector<std::shared_ptr<BasicItem>> items;  // For containers

    mutable size_t cachedHash{0};
    mutable bool hashReady{false};
    
    /**
     * Calculate hash for deduplication
     */
    size_t hash() const {
        if (hashReady) {
            return cachedHash;
        }

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
                if (item) {  // Null-safety
                    hash_combine(h, item->hash());
                }
            }
        }
        
        cachedHash = h;
        hashReady = true;
        return cachedHash;
    }
    
    /**
     * Equality operator for hash collision detection
     */
    bool operator==(const BasicItem& other) const {
        if (id != other.id || charges != other.charges || 
            actionId != other.actionId || uniqueId != other.uniqueId ||
            doorOrDepotId != other.doorOrDepotId || text != other.text ||
            destX != other.destX || destY != other.destY || destZ != other.destZ) {
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
    
    bool operator!=(const BasicItem& other) const {
        return !(*this == other);
    }
};

/**
 * BasicTile - Lightweight tile structure for map loading cache
 */
struct BasicTile {
    std::shared_ptr<BasicItem> ground{nullptr};
    std::vector<std::shared_ptr<BasicItem>> items;
    std::vector<ZoneId> zoneIds;
    
    uint32_t flags{0};
    uint32_t houseId{0};
    
    bool isStatic{false};

    // Stable, one-based identifier assigned by MapCache. Floor slots retain
    // this compact handle instead of millions of shared_ptr copies.
    uint32_t cacheId{0};

    mutable size_t cachedHash{0};
    mutable bool hashReady{false};
    
    bool isEmpty(bool ignoreFlag = false) const {
        return (ignoreFlag || flags == 0) && ground == nullptr && items.empty() && zoneIds.empty();
    }
    
    bool isHouse() const {
        return houseId != 0;
    }
    
    /**
     * Calculate hash for deduplication
     */
    size_t hash() const {
        if (hashReady) {
            return cachedHash;
        }

        size_t h = 0;
        
        if (flags > 0) hash_combine(h, flags);
        if (houseId > 0) hash_combine(h, houseId);
        if (isStatic) hash_combine(h, 1);
        if (!zoneIds.empty()) {
            hash_combine(h, zoneIds.size());
            for (ZoneId zoneId : zoneIds) {
                hash_combine(h, zoneId);
            }
        }
        
        if (ground != nullptr) {
            hash_combine(h, ground->hash());
        }
        
        if (!items.empty()) {
            hash_combine(h, items.size());
            for (const auto& item : items) {
                if (item) {  // Null-safety
                    hash_combine(h, item->hash());
                }
            }
        }
        
        cachedHash = h;
        hashReady = true;
        return cachedHash;
    }
    
    /**
     * Equality operator for hash collision detection
     */
    bool operator==(const BasicTile& other) const {
        if (flags != other.flags || houseId != other.houseId || isStatic != other.isStatic ||
            zoneIds != other.zoneIds) {
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
    
    bool operator!=(const BasicTile& other) const {
        return !(*this == other);
    }
};

/**
 * MapCache - Global cache for item and tile deduplication during map loading
 * 
 * This class provides hash-based deduplication of items and tiles,
 * significantly reducing memory usage when loading large maps.
 */
class MapCache {
public:
    using Digest = std::array<uint8_t, 32>;
    struct Fingerprint {
        Digest map{};
        Digest itemsOtb{};
        Digest itemsXml{};

        bool operator==(const Fingerprint&) const = default;
    };

    static void prepare(size_t sourceBytes);
    static void resetStore();
    static void precomputeFingerprint(const std::filesystem::path& mapPath);
    static std::optional<Fingerprint> fingerprint(const std::filesystem::path& mapPath);
    static std::optional<Digest> digestFile(const std::filesystem::path& path);
    static bool loadPersistent(Map& map, const std::filesystem::path& cachePath,
                               const Fingerprint& fingerprint, const Digest& houseCacheDigest);
    static bool savePersistent(const Map& map, const std::filesystem::path& cachePath,
                               const Fingerprint& fingerprint, const Digest& houseCacheDigest);

    /**
     * Try to get cached item or store new one
     * @param item The item to cache or find
     * @return Shared pointer to the cached item (may be the same or existing one)
     */
    static std::shared_ptr<BasicItem> tryGetItemFromCache(BasicItem&& item);
    
    /**
     * Try to get cached tile or store new one
     * @param tile The tile to cache or find
     * @return Shared pointer to the cached tile (may be the same or existing one)
     */
    static const BasicTile* tryGetTileFromCache(BasicTile&& tile);
    static const BasicTile* getTileById(uint32_t id);
    static bool hasZoneTemplates() { return storeHasZones; }
    static const std::deque<BasicTile>& getTileStore() { return tileStore; }
    
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
    static std::shared_ptr<BasicItem> parseBasicItem(void* loaderptr, const void* nodeptr);
    static const BasicTile* parseBasicTile(void* loaderptr, const void* nodeptr, uint8_t& xOffset, uint8_t& yOffset);
    /**
     * Get cache statistics for debugging
     */
    static size_t getItemCacheSize();
    static size_t getTileCacheSize();
    static size_t getItemCacheHits();
    static size_t getItemCacheMisses();
    static size_t getTileCacheHits();
    static size_t getTileCacheMisses();

private:
    // Map loading is serialized by the game thread. Using multimaps preserves
    // all non-equal values that happen to share a hash.
    static absl::flat_hash_map<size_t, std::weak_ptr<BasicItem>> itemCache;
    static absl::flat_hash_map<size_t, const BasicTile*> tileCache;
    static std::unordered_multimap<size_t, std::weak_ptr<BasicItem>> itemCollisions;
    static std::unordered_multimap<size_t, const BasicTile*> tileCollisions;
    static std::deque<BasicTile> tileStore;
    static bool storeHasZones;
    
    // Performance metrics
    static size_t itemCacheHits;
    static size_t itemCacheMisses;
    static size_t tileCacheHits;
    static size_t tileCacheMisses;
};

class Houses;

/**
 * MapCacheUtils - Helper functions for converting BasicItem/BasicTile to real objects
 */
namespace MapCacheUtils {
    /**
     * Create a real Item from a BasicItem
     */
    std::shared_ptr<Item> createItemFromBasic(const std::shared_ptr<BasicItem>& basicItem, const Position& pos);
    
    /**
     * Create a real Tile from a BasicTile
     */
    std::unique_ptr<Tile> createTileFromBasic(const BasicTile* basicTile,
                                               uint16_t x, uint16_t y, uint8_t z,
                                               Houses& houses);
}

#endif // FS_MAPCACHE_H
