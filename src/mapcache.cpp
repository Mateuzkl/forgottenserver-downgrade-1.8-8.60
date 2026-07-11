// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"
#include "mapcache.h"
#include "item.h"
#include "container.h"
#include "definitions.h"
#include "teleport.h"
#include "depotlocker.h"
#include "tile.h"
#include "housetile.h"
#include "house.h"
#include "game.h"
#include "iomap.h"
#include "fileloader.h"
#include "logger.h"

#include <algorithm>
#include <fstream>
#include <openssl/evp.h>

// Static cache storage with weak_ptr for automatic cleanup
absl::flat_hash_map<size_t, std::weak_ptr<BasicItem>> MapCache::itemCache;
absl::flat_hash_map<size_t, const BasicTile*> MapCache::tileCache;
std::unordered_multimap<size_t, std::weak_ptr<BasicItem>> MapCache::itemCollisions;
std::unordered_multimap<size_t, const BasicTile*> MapCache::tileCollisions;
std::deque<BasicTile> MapCache::tileStore;
bool MapCache::storeHasZones = false;

// Cache statistics
size_t MapCache::itemCacheHits = 0;
size_t MapCache::itemCacheMisses = 0;
size_t MapCache::tileCacheHits = 0;
size_t MapCache::tileCacheMisses = 0;

namespace {
constexpr std::array<uint8_t, 8> MAP_CACHE_MAGIC{{'T', 'F', 'S', 'M', 'C', '0', '1', 0}};
constexpr uint32_t MAP_CACHE_VERSION = 2;
constexpr uint32_t MAX_CACHE_ENTRIES = 100'000'000;

std::mutex fingerprintMutex;
std::filesystem::path fingerprintPath;
std::optional<std::future<std::optional<MapCache::Fingerprint>>> fingerprintFuture;

class CacheWriter {
public:
    explicit CacheWriter(const std::filesystem::path& path) : stream(path, std::ios::binary | std::ios::trunc) {}

    bool good() const { return stream.good(); }
    bool close() {
        stream.flush();
        const bool ok = stream.good();
        stream.close();
        return ok;
    }

    void bytes(const void* data, size_t size) {
        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    void u8(uint8_t value) { bytes(&value, sizeof(value)); }
    void u16(uint16_t value) {
        const uint8_t data[2] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
        bytes(data, sizeof(data));
    }
    void u32(uint32_t value) {
        const uint8_t data[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                                 static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
        bytes(data, sizeof(data));
    }
    void string(std::string_view value) {
        u32(static_cast<uint32_t>(value.size()));
        bytes(value.data(), value.size());
    }

private:
    std::ofstream stream;
};

class CacheReader {
public:
    CacheReader(const std::filesystem::path& path, uint64_t limit) : stream(path, std::ios::binary), remaining(limit) {}

    bool bytes(void* data, size_t size) {
        if (size > remaining) {
            return false;
        }
        stream.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
        if (!stream) {
            return false;
        }
        remaining -= size;
        return true;
    }

    bool u8(uint8_t& value) { return bytes(&value, sizeof(value)); }
    bool u16(uint16_t& value) {
        uint8_t data[2];
        if (!bytes(data, sizeof(data))) return false;
        value = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
        return true;
    }
    bool u32(uint32_t& value) {
        uint8_t data[4];
        if (!bytes(data, sizeof(data))) return false;
        value = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
        return true;
    }
    bool string(std::string& value, uint32_t maxSize = 16 * 1024 * 1024) {
        uint32_t size;
        if (!u32(size) || size > maxSize || size > remaining) return false;
        value.resize(size);
        return bytes(value.data(), size);
    }

private:
    std::ifstream stream;
    uint64_t remaining;
};

std::optional<MapCache::Digest> digestFilePrefix(const std::filesystem::path& path, uint64_t bytesToHash) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        return std::nullopt;
    }

    // Keep the Windows loader thread well below its default stack limit.
    std::array<char, 64 * 1024> buffer;
    uint64_t remaining = bytesToHash;
    while (remaining > 0) {
        const size_t wanted = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
        stream.read(buffer.data(), static_cast<std::streamsize>(wanted));
        const auto read = static_cast<size_t>(stream.gcount());
        if (read == 0 || EVP_DigestUpdate(context.get(), buffer.data(), read) != 1) {
            return std::nullopt;
        }
        remaining -= read;
    }

    MapCache::Digest digest{};
    unsigned int digestSize = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &digestSize) != 1 || digestSize != digest.size()) {
        return std::nullopt;
    }
    return digest;
}

bool appendDigest(const std::filesystem::path& path) {
    std::error_code error;
    const uint64_t size = std::filesystem::file_size(path, error);
    if (error) return false;
    const auto digest = digestFilePrefix(path, size);
    if (!digest) return false;
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    stream.write(reinterpret_cast<const char*>(digest->data()), digest->size());
    return stream.good();
}

bool replaceMapCacheFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
    return MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}
} // namespace

void MapCache::prepare(size_t sourceBytes) {
    // Estimates are deliberately based on the source size instead of map
    // dimensions, which are mostly empty on large global maps.
    itemCache.reserve(std::max<size_t>(itemCache.size(), sourceBytes / 4096));
    tileCache.reserve(std::max<size_t>(tileCache.size(), sourceBytes / 192));
}

void MapCache::resetStore() {
    itemCache.clear();
    tileCache.clear();
    itemCollisions.clear();
    tileCollisions.clear();
    tileStore.clear();
    storeHasZones = false;
    itemCacheHits = itemCacheMisses = tileCacheHits = tileCacheMisses = 0;
}

std::optional<MapCache::Digest> MapCache::digestFile(const std::filesystem::path& path) {
    std::error_code error;
    const uint64_t size = std::filesystem::file_size(path, error);
    if (error) return std::nullopt;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) return std::nullopt;
    const auto digest = digestFilePrefix(path, size);
    if (!digest) return std::nullopt;
    const uint64_t finalSize = std::filesystem::file_size(path, error);
    if (error || finalSize != size) return std::nullopt;
    const auto finalModified = std::filesystem::last_write_time(path, error);
    if (error || finalModified != modified) {
        return std::nullopt;
    }
    return digest;
}

std::optional<MapCache::Fingerprint> MapCache::fingerprint(const std::filesystem::path& mapPath) {
    std::optional<std::future<std::optional<Fingerprint>>> readyFuture;
    {
        std::scoped_lock lock(fingerprintMutex);
        if (fingerprintFuture && fingerprintPath == mapPath) {
            readyFuture.emplace(std::move(*fingerprintFuture));
            fingerprintFuture.reset();
            fingerprintPath.clear();
        }
    }
    if (readyFuture) {
        return readyFuture->get();
    }

    const auto mapDigest = digestFile(mapPath);
    const auto otbDigest = digestFile("data/items/items.otb");
    const auto xmlDigest = digestFile("data/items/items.xml");
    if (!mapDigest || !otbDigest || !xmlDigest) {
        return std::nullopt;
    }
    return Fingerprint{*mapDigest, *otbDigest, *xmlDigest};
}

void MapCache::precomputeFingerprint(const std::filesystem::path& mapPath) {
    std::scoped_lock lock(fingerprintMutex);
    fingerprintPath = mapPath;
    fingerprintFuture.emplace(std::async(std::launch::async, [mapPath]() -> std::optional<Fingerprint> {
        const auto mapDigest = digestFile(mapPath);
        const auto otbDigest = digestFile("data/items/items.otb");
        const auto xmlDigest = digestFile("data/items/items.xml");
        if (!mapDigest || !otbDigest || !xmlDigest) return std::nullopt;
        return Fingerprint{*mapDigest, *otbDigest, *xmlDigest};
    }));
}

bool MapCache::savePersistent(const Map& map, const std::filesystem::path& cachePath,
                              const Fingerprint& fingerprintValue, const Digest& houseCacheDigest) {
    std::error_code error;
    std::filesystem::create_directories(cachePath.parent_path(), error);
    if (error) {
        return false;
    }

    const auto tempPath = cachePath.string() + ".tmp";
    CacheWriter writer(tempPath);
    if (!writer.good()) {
        return false;
    }

    std::vector<std::shared_ptr<BasicItem>> items;
    std::unordered_map<const BasicItem*, uint32_t> itemIds;
    std::function<uint32_t(const std::shared_ptr<BasicItem>&)> addItem = [&](const std::shared_ptr<BasicItem>& item) {
        if (!item) return 0U;
        if (const auto it = itemIds.find(item.get()); it != itemIds.end()) return it->second;
        const uint32_t id = static_cast<uint32_t>(items.size() + 1);
        itemIds.emplace(item.get(), id);
        items.emplace_back(item);
        for (const auto& child : item->items) addItem(child);
        return id;
    };
    for (const auto& tile : tileStore) {
        addItem(tile.ground);
        for (const auto& item : tile.items) addItem(item);
    }

    uint32_t blockCount = 0;
    map.forEachBasicFloorBlock([&](uint16_t, uint16_t, uint8_t,
                                   const std::array<uint32_t, FLOOR_SIZE * FLOOR_SIZE>&) { ++blockCount; });

    writer.bytes(MAP_CACHE_MAGIC.data(), MAP_CACHE_MAGIC.size());
    writer.u32(MAP_CACHE_VERSION);
    writer.u32(CLIENT_VERSION_MIN);
    writer.u32(Item::items.majorVersion);
    writer.u32(Item::items.minorVersion);
    writer.u32(Item::items.buildNumber);
    writer.bytes(fingerprintValue.map.data(), fingerprintValue.map.size());
    writer.bytes(fingerprintValue.itemsOtb.data(), fingerprintValue.itemsOtb.size());
    writer.bytes(fingerprintValue.itemsXml.data(), fingerprintValue.itemsXml.size());
    writer.bytes(houseCacheDigest.data(), houseCacheDigest.size());
    writer.u32(map.width);
    writer.u32(map.height);
    writer.string(map.spawnfile.generic_string());
    writer.string(map.housefile.generic_string());
    writer.u32(static_cast<uint32_t>(items.size()));
    writer.u32(static_cast<uint32_t>(tileStore.size()));
    writer.u32(blockCount);

    for (const auto& item : items) {
        writer.u16(item->id);
        writer.u16(item->charges);
        writer.u16(item->actionId);
        writer.u16(item->uniqueId);
        writer.u16(item->destX);
        writer.u16(item->destY);
        writer.u16(item->doorOrDepotId);
        writer.u8(item->destZ);
        writer.string(item->text);
        writer.u32(static_cast<uint32_t>(item->items.size()));
        for (const auto& child : item->items) writer.u32(itemIds.at(child.get()));
    }

    for (const auto& tile : tileStore) {
        writer.u32(tile.ground ? itemIds.at(tile.ground.get()) : 0);
        writer.u32(static_cast<uint32_t>(tile.items.size()));
        for (const auto& item : tile.items) writer.u32(itemIds.at(item.get()));
        writer.u32(static_cast<uint32_t>(tile.zoneIds.size()));
        for (ZoneId id : tile.zoneIds) writer.u16(id);
        writer.u32(tile.flags);
        writer.u32(tile.houseId);
        writer.u8(tile.isStatic ? 1 : 0);
    }

    map.forEachBasicFloorBlock([&](uint16_t x, uint16_t y, uint8_t z,
                                   const std::array<uint32_t, FLOOR_SIZE * FLOOR_SIZE>& ids) {
        writer.u16(x);
        writer.u16(y);
        writer.u8(z);
        for (uint32_t id : ids) writer.u32(id);
    });

    if (!writer.good() || !writer.close()) {
        std::filesystem::remove(tempPath, error);
        return false;
    }
    if (!appendDigest(tempPath)) {
        std::filesystem::remove(tempPath, error);
        return false;
    }

    if (!replaceMapCacheFile(tempPath, cachePath)) {
        std::filesystem::remove(tempPath, error);
        return false;
    }

    g_logger().info(">> Wrote persistent map cache '{}' ({} items, {} tiles, {} floor blocks).",
                    cachePath.string(), items.size(), tileStore.size(), blockCount);
    return true;
}

bool MapCache::loadPersistent(Map& map, const std::filesystem::path& cachePath,
                              const Fingerprint& expectedFingerprint, const Digest& expectedHouseDigest) {
    std::error_code error;
    const uint64_t fileSize = std::filesystem::file_size(cachePath, error);
    if (error || fileSize < MAP_CACHE_MAGIC.size() + 4 + 32) {
        return false;
    }

    Digest storedChecksum{};
    {
        std::ifstream tail(cachePath, std::ios::binary);
        tail.seekg(static_cast<std::streamoff>(fileSize - storedChecksum.size()));
        tail.read(reinterpret_cast<char*>(storedChecksum.data()), storedChecksum.size());
        if (!tail) return false;
    }
    const auto actualChecksum = digestFilePrefix(cachePath, fileSize - storedChecksum.size());
    if (!actualChecksum || *actualChecksum != storedChecksum) {
        g_logger().warn(">> Persistent map cache checksum mismatch: {}", cachePath.string());
        return false;
    }

    CacheReader reader(cachePath, fileSize - storedChecksum.size());
    std::array<uint8_t, 8> magic{};
    uint32_t version = 0;
    uint32_t clientVersion = 0, itemMajor = 0, itemMinor = 0, itemBuild = 0;
    Fingerprint storedFingerprint;
    Digest storedHouseDigest{};
    if (!reader.bytes(magic.data(), magic.size()) || magic != MAP_CACHE_MAGIC || !reader.u32(version) ||
        version != MAP_CACHE_VERSION || !reader.u32(clientVersion) || !reader.u32(itemMajor) ||
        !reader.u32(itemMinor) || !reader.u32(itemBuild) || clientVersion != CLIENT_VERSION_MIN ||
        itemMajor != Item::items.majorVersion || itemMinor != Item::items.minorVersion ||
        itemBuild != Item::items.buildNumber ||
        !reader.bytes(storedFingerprint.map.data(), storedFingerprint.map.size()) ||
        !reader.bytes(storedFingerprint.itemsOtb.data(), storedFingerprint.itemsOtb.size()) ||
        !reader.bytes(storedFingerprint.itemsXml.data(), storedFingerprint.itemsXml.size()) ||
        !reader.bytes(storedHouseDigest.data(), storedHouseDigest.size()) ||
        storedFingerprint != expectedFingerprint || storedHouseDigest != expectedHouseDigest) {
        return false;
    }

    uint32_t width = 0, height = 0, itemCount = 0, tileCount = 0, blockCount = 0;
    std::string spawnFile, houseFile;
    if (!reader.u32(width) || !reader.u32(height) || !reader.string(spawnFile, 64 * 1024) ||
        !reader.string(houseFile, 64 * 1024) || !reader.u32(itemCount) || !reader.u32(tileCount) ||
        !reader.u32(blockCount) || itemCount > MAX_CACHE_ENTRIES || tileCount > MAX_CACHE_ENTRIES ||
        blockCount > MAX_CACHE_ENTRIES) {
        return false;
    }

    struct PendingItem {
        std::shared_ptr<BasicItem> item;
        std::vector<uint32_t> children;
    };
    std::vector<PendingItem> pendingItems;
    pendingItems.reserve(itemCount);
    for (uint32_t i = 0; i < itemCount; ++i) {
        auto item = std::make_shared<BasicItem>();
        uint32_t childCount = 0;
        if (!reader.u16(item->id) || !reader.u16(item->charges) || !reader.u16(item->actionId) ||
            !reader.u16(item->uniqueId) || !reader.u16(item->destX) || !reader.u16(item->destY) ||
            !reader.u16(item->doorOrDepotId) || !reader.u8(item->destZ) || !reader.string(item->text) ||
            !reader.u32(childCount) || childCount > MAX_CACHE_ENTRIES) {
            return false;
        }
        PendingItem pending{std::move(item), std::vector<uint32_t>(childCount)};
        for (uint32_t& child : pending.children) if (!reader.u32(child)) return false;
        pendingItems.emplace_back(std::move(pending));
    }
    for (auto& pending : pendingItems) {
        pending.item->items.reserve(pending.children.size());
        for (uint32_t child : pending.children) {
            if (child == 0 || child > pendingItems.size()) return false;
            pending.item->items.emplace_back(pendingItems[child - 1].item);
        }
    }

    resetStore();
    for (uint32_t i = 0; i < tileCount; ++i) {
        BasicTile tile;
        uint32_t groundId = 0, tileItemCount = 0, zoneCount = 0;
        if (!reader.u32(groundId) || !reader.u32(tileItemCount) || tileItemCount > MAX_CACHE_ENTRIES) return false;
        if (groundId > pendingItems.size()) return false;
        if (groundId != 0) tile.ground = pendingItems[groundId - 1].item;
        tile.items.reserve(tileItemCount);
        for (uint32_t j = 0; j < tileItemCount; ++j) {
            uint32_t itemId;
            if (!reader.u32(itemId) || itemId == 0 || itemId > pendingItems.size()) return false;
            tile.items.emplace_back(pendingItems[itemId - 1].item);
        }
        if (!reader.u32(zoneCount) || zoneCount > std::numeric_limits<uint16_t>::max()) return false;
        tile.zoneIds.resize(zoneCount);
        for (ZoneId& zone : tile.zoneIds) if (!reader.u16(zone)) return false;
        uint8_t isStatic = 0;
        if (!reader.u32(tile.flags) || !reader.u32(tile.houseId) || !reader.u8(isStatic)) return false;
        tile.isStatic = isStatic != 0;
        tile.cacheId = i + 1;
        storeHasZones |= !tile.zoneIds.empty();
        tileStore.emplace_back(std::move(tile));
    }

    // The complete payload checksum has already been verified, so floor
    // blocks can be installed as a stream instead of allocating another
    // ~90 MB staging vector for the global map.
    map.width = width;
    map.height = height;
    map.spawnfile = std::filesystem::path(spawnFile);
    map.housefile = std::filesystem::path(houseFile);
    for (uint32_t i = 0; i < blockCount; ++i) {
        uint16_t x = 0, y = 0;
        uint8_t z = 0;
        std::array<uint32_t, FLOOR_SIZE * FLOOR_SIZE> ids{};
        if (!reader.u16(x) || !reader.u16(y) || !reader.u8(z)) return false;
        for (uint32_t& id : ids) {
            if (!reader.u32(id) || id > tileStore.size()) return false;
        }
        map.setBasicFloorBlock(x, y, z, ids);
    }

    g_logger().info(">> Loaded persistent map cache '{}' ({} items, {} tiles, {} floor blocks).",
                    cachePath.string(), itemCount, tileCount, blockCount);
    return true;
}

std::shared_ptr<BasicItem> MapCache::tryGetItemFromCache(BasicItem&& item) {
    const size_t h = item.hash();
    bool hasPrimary = false;
    if (auto it = itemCache.find(h); it != itemCache.end()) {
        if (auto cached = it->second.lock()) {
            hasPrimary = true;
            if (*cached == item) {
                ++itemCacheHits;
                return cached;
            }
        } else {
            itemCache.erase(it);
        }
    }
    const auto [begin, end] = itemCollisions.equal_range(h);
    for (auto it = begin; it != end; ++it) {
        if (auto cached = it->second.lock(); cached && *cached == item) {
            ++itemCacheHits;
            return cached;
        }
    }

    ++itemCacheMisses;
    auto stored = std::make_shared<BasicItem>(std::move(item));
    if (hasPrimary) itemCollisions.emplace(h, stored);
    else itemCache.emplace(h, stored);
    return stored;
}

const BasicTile* MapCache::tryGetTileFromCache(BasicTile&& tile) {
    const size_t h = tile.hash();
    bool hasPrimary = false;
    if (auto it = tileCache.find(h); it != tileCache.end()) {
        const BasicTile* cached = it->second;
        hasPrimary = true;
        if (*cached == tile) {
            ++tileCacheHits;
            return cached;
        }
    }
    const auto [begin, end] = tileCollisions.equal_range(h);
    for (auto it = begin; it != end; ++it) {
        const BasicTile* cached = it->second;
        if (*cached == tile) {
            ++tileCacheHits;
            return cached;
        }
    }

    ++tileCacheMisses;
    tile.cacheId = static_cast<uint32_t>(tileStore.size() + 1);
    storeHasZones |= !tile.zoneIds.empty();
    tileStore.emplace_back(std::move(tile));
    const BasicTile* stored = &tileStore.back();
    if (hasPrimary) tileCollisions.emplace(h, stored);
    else tileCache.emplace(h, stored);
    return stored;
}

const BasicTile* MapCache::getTileById(uint32_t id) {
    if (id == 0 || id > tileStore.size()) {
        return nullptr;
    }
    return &tileStore[id - 1];
}

void MapCache::flush() {
    size_t itemCount = itemCache.size() + itemCollisions.size();
    size_t tileCount = tileCache.size() + tileCollisions.size();
    
    // Calculate hit rates
    size_t totalItemRequests = itemCacheHits + itemCacheMisses;
    size_t totalTileRequests = tileCacheHits + tileCacheMisses;
    
    double itemHitRate = totalItemRequests > 0 ? 
        (static_cast<double>(itemCacheHits) / totalItemRequests * 100.0) : 0.0;
    double tileHitRate = totalTileRequests > 0 ? 
        (static_cast<double>(tileCacheHits) / totalTileRequests * 100.0) : 0.0;
    
    itemCache.clear();
    tileCache.clear();
    itemCollisions.clear();
    tileCollisions.clear();
    
    LOG_INFO(fmt::format(">> Map cache flushed: [\033[1;36m{}\033[0m] unique items, [\033[1;36m{}\033[0m] unique tiles deduplicated", 
                         itemCount, tileCount));
    LOG_INFO(fmt::format(">> Cache hit rates: Items [\033[1;33m{:.2f}%\033[0m] (\033[1;36m{}\033[0m/\033[1;36m{}\033[0m), Tiles [\033[1;33m{:.2f}%\033[0m] (\033[1;36m{}\033[0m/\033[1;36m{}\033[0m)",
                         itemHitRate, itemCacheHits, totalItemRequests,
                         tileHitRate, tileCacheHits, totalTileRequests));
    
    // Reset counters
    itemCacheHits = 0;
    itemCacheMisses = 0;
    tileCacheHits = 0;
    tileCacheMisses = 0;
}

void MapCache::cleanupExpiredEntries() {
    for (auto it = itemCache.begin(); it != itemCache.end();) {
        if (it->second.expired()) itemCache.erase(it++);
        else ++it;
    }
    std::erase_if(itemCollisions, [](const auto& entry) { return entry.second.expired(); });
}

size_t MapCache::getItemCacheSize() {
    return itemCache.size() + itemCollisions.size();
}

size_t MapCache::getTileCacheSize() {
    return tileCache.size() + tileCollisions.size();
}

size_t MapCache::getItemCacheHits() { return itemCacheHits; }
size_t MapCache::getItemCacheMisses() { return itemCacheMisses; }
size_t MapCache::getTileCacheHits() { return tileCacheHits; }
size_t MapCache::getTileCacheMisses() { return tileCacheMisses; }

// Helper functions for parsing
namespace {
    bool cacheCylinderOwnsThing(const Cylinder* cylinder, const Thing* thing) {
        return cylinder && thing && thing->getParent() == cylinder && cylinder->getThingIndex(thing) != -1;
    }

    bool placeCachedMapItem(Cylinder* cylinder, Item* item) {
        if (!cylinder || !item) {
            return false;
        }

        cylinder->internalAddThing(item);
        if (!cacheCylinderOwnsThing(cylinder, item)) {
            return false;
        }

        return true;
    }

    void applyItemIdSubstitutions(uint16_t& id) {
        switch (id) {
            case ITEM_FIREFIELD_PVP_FULL: id = ITEM_FIREFIELD_PERSISTENT_FULL; break;
            case ITEM_FIREFIELD_PVP_MEDIUM: id = ITEM_FIREFIELD_PERSISTENT_MEDIUM; break;
            case ITEM_FIREFIELD_PVP_SMALL: id = ITEM_FIREFIELD_PERSISTENT_SMALL; break;
            case ITEM_ENERGYFIELD_PVP: id = ITEM_ENERGYFIELD_PERSISTENT; break;
            case ITEM_POISONFIELD_PVP: id = ITEM_POISONFIELD_PERSISTENT; break;
            case ITEM_MAGICWALL: id = ITEM_MAGICWALL_PERSISTENT; break;
            case ITEM_WILDGROWTH: id = ITEM_WILDGROWTH_PERSISTENT; break;
            default: break;
        }
    }
    
    void parseCustomAttributes(PropStream& propStream) {
        uint64_t size;
        if (!propStream.read<uint64_t>(size)) {
            return;
        }
        
        for (uint64_t i = 0; i < size; ++i) {
            propStream.readString(); // key
            
            uint8_t type;
            if (propStream.read<uint8_t>(type)) {
                switch (type) {
                    case 0: break; // blank
                    case 1: propStream.readString(); break; // string
                    case 2: propStream.skip(8); break; // int64
                    case 3: propStream.skip(8); break; // double
                    case 4: propStream.skip(1); break; // bool
                    default:
                        LOG_WARN(fmt::format("[MapCache] Unknown custom attribute type: {}", type));
                        break;
                }
            }
        }
    }
    
    void parseReflectAttribute(PropStream& propStream) {
        uint16_t size;
        if (!propStream.read<uint16_t>(size)) {
            LOG_ERROR("[MapCache] Failed to read ATTR_REFLECT size");
            return;
        }
        
        for (uint16_t i = 0; i < size; ++i) {
            CombatType_t combatType;
            uint16_t percent, chance;
            if (propStream.read<CombatType_t>(combatType) &&
                propStream.read<uint16_t>(percent) &&
                propStream.read<uint16_t>(chance)) {
                // Successfully read reflect data
            } else {
                LOG_ERROR("[MapCache] Failed to parse ATTR_REFLECT entry");
                break;
            }
        }
    }
    
    void parseBoostAttribute(PropStream& propStream) {
        uint16_t size;
        if (!propStream.read<uint16_t>(size)) {
            LOG_ERROR("[MapCache] Failed to read ATTR_BOOST size");
            return;
        }
        
        for (uint16_t i = 0; i < size; ++i) {
            CombatType_t combatType;
            uint16_t percent;
            if (propStream.read<CombatType_t>(combatType) &&
                propStream.read<uint16_t>(percent)) {
                // Successfully read boost data
            } else {
                LOG_ERROR("[MapCache] Failed to parse ATTR_BOOST entry");
                break;
            }
        }
    }
}

// Helper to parse item from stream (used by both node-based and inline items)
bool parseBasicItemFromStream(PropStream& propStream, BasicItem& item) {
    uint16_t id;
    if (!propStream.read<uint16_t>(id)) {
        LOG_ERROR("[MapCache] Failed to read item ID from stream");
        return false;
    }
    
    // ID substitutions
    applyItemIdSubstitutions(id);

    item.id = id;
    
    // Default values based on ItemType
    const ItemType& it = Item::items[id];
    if (it.stackable && it.charges == 0) {
        item.charges = 1;
    } else if (it.charges != 0) {
        item.charges = it.charges;
    }
    
    // Read attributes
    uint8_t attr_type;
    while (propStream.read<uint8_t>(attr_type)) {
        switch (attr_type) {
            case ATTR_COUNT:
            case ATTR_RUNE_CHARGES: {
                uint8_t count;
                if (propStream.read<uint8_t>(count)) {
                    item.charges = count;
                }
                break;
            }
            
            case ATTR_CHARGES: {
                uint16_t charges;
                if (propStream.read<uint16_t>(charges)) {
                    item.charges = charges;
                }
                break;
            }
            
            case ATTR_ACTION_ID: {
                uint16_t actionId;
                if (propStream.read<uint16_t>(actionId)) {
                    item.actionId = actionId;
                }
                break;
            }
            
            case ATTR_UNIQUE_ID: {
                uint16_t uniqueId;
                if (propStream.read<uint16_t>(uniqueId)) {
                    item.uniqueId = uniqueId;
                }
                break;
            }
            
            case ATTR_TEXT: {
                auto [text, ok] = propStream.readString();
                if (ok) {
                    item.text = std::string(text);
                }
                break;
            }
            
            case ATTR_TELE_DEST: {
                OTBM_Destination_coords dest;
                if (propStream.read(dest)) {
                    item.destX = dest.x;
                    item.destY = dest.y;
                    item.destZ = dest.z;
                }
                break;
            }
            
            case ATTR_HOUSEDOORID: {
                uint8_t doorId;
                if (propStream.read<uint8_t>(doorId)) {
                    item.doorOrDepotId = doorId;
                }
                break;
            }
            
            case ATTR_DEPOT_ID: {
                uint16_t depotId;
                if (propStream.read<uint16_t>(depotId)) {
                    item.doorOrDepotId = depotId;
                }
                break;
            }
            
            // Ignored string attributes
            case tfs::to_underlying(OTBM_AttrTypes_t::DESCRIPTION):
            case tfs::to_underlying(OTBM_AttrTypes_t::DESC):
            case tfs::to_underlying(OTBM_AttrTypes_t::EXT_FILE):
            case tfs::to_underlying(OTBM_AttrTypes_t::EXT_SPAWN_FILE):
            case tfs::to_underlying(OTBM_AttrTypes_t::EXT_HOUSE_FILE):
            case ATTR_NAME:
            case ATTR_ARTICLE:
            case ATTR_PLURALNAME:
            case ATTR_WRITTENBY:
                propStream.readString();
                break;
                
            // Ignored 4-byte attributes
            case ATTR_TILE_FLAGS:
            case ATTR_WEIGHT:
            case ATTR_ATTACK:
            case ATTR_DEFENSE:
            case ATTR_EXTRADEFENSE:
            case ATTR_ARMOR:
            case ATTR_ATTACK_SPEED:
            case ATTR_REWARDID:
            case ATTR_CLASSIFICATION:
            case ATTR_TIER:
            case ATTR_WRITTENDATE:
            case ATTR_DURATION:
                propStream.skip(4);
                break;
                
            // Ignored 1-byte attributes
            case ATTR_DECAYING_STATE:
            case ATTR_HITCHANCE:
            case ATTR_SHOOTRANGE:
            case ATTR_STOREITEM:
                propStream.skip(1);
                break;
                
            // Ignored 2-byte attributes
            case ATTR_WRAPID:
                propStream.skip(2);
                break;
                
            // Ignored 4-byte attributes (sleep)
            case ATTR_SLEEPERGUID:
            case ATTR_SLEEPSTART:
                propStream.skip(4);
                break;
                
            case ATTR_CUSTOM_ATTRIBUTES:
                parseCustomAttributes(propStream);
                break;
                
            case ATTR_REFLECT:
                parseReflectAttribute(propStream);
                break;
                
            case ATTR_BOOST:
                parseBoostAttribute(propStream);
                break;
            
            default:
                // Unknown attribute, log warning
                LOG_WARN(fmt::format("[MapCache] Unknown item attribute: {}", attr_type));
                break;
        }
    }
    return true;
}

std::shared_ptr<BasicItem> MapCache::parseBasicItem(void* loaderptr, const void* nodeptr) {
    OTB::Loader& loader = *static_cast<OTB::Loader*>(loaderptr);
    const OTB::Node& node = *static_cast<const OTB::Node*>(nodeptr);
    PropStream propStream;

    if (!loader.getProps(node, propStream)) {
        LOG_ERROR("[MapCache] Failed to get props from item node");
        return nullptr;
    }

    BasicItem item;
    if (!parseBasicItemFromStream(propStream, item)) {
        return nullptr;
    }
    
    // Parse children (containers)
    for (auto& childNode : node.children) {
        if (static_cast<OTBM_NodeTypes_t>(childNode.type) == OTBM_NodeTypes_t::ITEM) {
            auto child = parseBasicItem(loaderptr, &childNode);
            if (child) {
                item.items.push_back(child);
            }
        }
    }

    return tryGetItemFromCache(std::move(item));
}

const BasicTile* MapCache::parseBasicTile(void* loaderptr, const void* nodeptr, uint8_t& xOffset, uint8_t& yOffset) {
    OTB::Loader& loader = *static_cast<OTB::Loader*>(loaderptr);
    const OTB::Node& tileNode = *static_cast<const OTB::Node*>(nodeptr);
    PropStream propStream;

    if (!loader.getProps(tileNode, propStream)) {
        LOG_ERROR("[MapCache] Failed to get props from tile node");
        return nullptr;
    }

    OTBM_Tile_coords tile_coord;
    if (!propStream.read(tile_coord)) {
        LOG_ERROR("[MapCache] Failed to read tile coordinates");
        return nullptr;
    }
    
    xOffset = tile_coord.x;
    yOffset = tile_coord.y;
    
    BasicTile tile;
    
    // Check for HouseTile
    if (static_cast<OTBM_NodeTypes_t>(tileNode.type) == OTBM_NodeTypes_t::HOUSETILE) {
        uint32_t houseId;
        if (propStream.read<uint32_t>(houseId)) {
            tile.houseId = houseId;
        } else {
            LOG_ERROR("[MapCache] Failed to read house ID from house tile");
        }
    }
    
    // Read tile attributes
    uint8_t attribute;
    while (propStream.read<uint8_t>(attribute)) {
        switch (static_cast<OTBM_AttrTypes_t>(attribute)) {
            case OTBM_AttrTypes_t::TILE_FLAGS: {
                uint32_t flags;
                if (propStream.read<uint32_t>(flags)) {
                    if ((flags & tfs::to_underlying(OTBM_TileFlag_t::PROTECTIONZONE)) != 0) tile.flags |= TILESTATE_PROTECTIONZONE;
                    if ((flags & tfs::to_underlying(OTBM_TileFlag_t::NOPVPZONE)) != 0) tile.flags |= TILESTATE_NOPVPZONE;
                    if ((flags & tfs::to_underlying(OTBM_TileFlag_t::PVPZONE)) != 0) tile.flags |= TILESTATE_PVPZONE;
                    if ((flags & tfs::to_underlying(OTBM_TileFlag_t::NOLOGOUT)) != 0) tile.flags |= TILESTATE_NOLOGOUT;
                    if ((flags & tfs::to_underlying(OTBM_TileFlag_t::ZONE)) != 0) {
                        ZoneId zoneId = 0;
                        do {
                            if (!propStream.read<ZoneId>(zoneId)) {
                                LOG_ERROR("[MapCache] Failed to read tile zone id");
                                return nullptr;
                            }

                            if (zoneId != 0) {
                                tile.zoneIds.emplace_back(zoneId);
                            }
                        } while (zoneId != 0);
                    }
                } else {
                    LOG_ERROR("[MapCache] Failed to read tile flags");
                    return nullptr;
                }
                break;
            }
            case OTBM_AttrTypes_t::ITEM: {
                // Inline item
                BasicItem item;
                if (parseBasicItemFromStream(propStream, item)) {
                     const ItemType& it = Item::items[item.id];
                     if (it.isGroundTile()) {
                         tile.ground = MapCache::tryGetItemFromCache(std::move(item));
                     } else {
                         tile.items.push_back(MapCache::tryGetItemFromCache(std::move(item)));
                     }
                }
                break; 
            }
            default:
                LOG_WARN(fmt::format("[MapCache] Unknown tile attribute: {}", attribute));
                break;
        }
    }
    
    // Parse nested items and Canary-style tile zone nodes
    for (auto& itemNode : tileNode.children) {
        const auto childNodeType = static_cast<OTBM_NodeTypes_t>(itemNode.type);
        if (childNodeType == OTBM_NodeTypes_t::ITEM) {
            auto item = parseBasicItem(loaderptr, &itemNode);
            if (item) {
                const ItemType& it = Item::items[item->id];
                if (it.isGroundTile() && tile.ground == nullptr) {
                    tile.ground = item;
                } else {
                    tile.items.push_back(item);
                }
            }
        } else if (childNodeType == OTBM_NodeTypes_t::TILE_ZONE) {
            std::string errorType;
            if (!IOMap::parseTileZoneNode(loader, itemNode, tile.zoneIds, errorType)) {
                LOG_ERROR(fmt::format("[MapCache] {}", errorType));
                return nullptr;
            }
        }
    }

    std::sort(tile.zoneIds.begin(), tile.zoneIds.end());
    tile.zoneIds.erase(std::unique(tile.zoneIds.begin(), tile.zoneIds.end()), tile.zoneIds.end());
    
    return tryGetTileFromCache(std::move(tile));
}

/**
 * Helper functions for converting BasicItem/BasicTile to real Item/Tile objects
 * These are used during lazy loading when a tile is first accessed
 */
namespace MapCacheUtils {

std::shared_ptr<Item> createItemFromBasic(const std::shared_ptr<BasicItem>& basicItem, const Position& pos) {
    if (!basicItem) {
        return nullptr;
    }
    
    auto item = Item::CreateItem(basicItem->id, basicItem->charges);
    if (!item) {
        LOG_ERROR(fmt::format("[MapCache] Failed to create item with ID {}", basicItem->id));
        return nullptr;
    }
    
    // Set attributes
    if (basicItem->actionId > 0) {
        item->setActionId(basicItem->actionId);
    }
    
    if (basicItem->uniqueId > 0) {
        item->setIntAttr(ITEM_ATTRIBUTE_UNIQUEID, basicItem->uniqueId);
    }
    
    // Handle teleport destination
    if (Teleport* teleport = item->getTeleport()) {
        if (basicItem->destX != 0 || basicItem->destY != 0 || basicItem->destZ != 0) {
            teleport->setDestPos(Position(basicItem->destX, basicItem->destY, basicItem->destZ));
        }
    }
    
    // Handle door/depot ID
    if (basicItem->doorOrDepotId != 0) {
        if (Door* door = item->getDoor()) {
            door->setDoorId(basicItem->doorOrDepotId);
        } else if (Container* container = item->getContainer()) {
            if (DepotLocker* depot = container->getDepotLocker()) {
                depot->setDepotId(basicItem->doorOrDepotId);
            }
        }
    }
    
    // Handle text
    if (!basicItem->text.empty()) {
        item->setText(basicItem->text);
    }
    
    // Handle container items recursively
    if (Container* container = item->getContainer()) {
        for (const auto& childBasicItem : basicItem->items) {
            if (auto childItem = createItemFromBasic(childBasicItem, pos)) {
                if (!placeCachedMapItem(container, childItem.get())) {
                    LOG_WARN(fmt::format("[MapCache] Failed to attach child item {} at {}", childItem->getID(), pos));
                }
            }
        }
    }
    
    if (item->getItemCount() == 0) {
        item->setItemCount(1);
    }
    
    item->startDecaying();
    item->setLoadedFromMap(true);
    
    return item;
}

std::unique_ptr<Tile> createTileFromBasic(const BasicTile* basicTile,
                                           uint16_t x, uint16_t y, uint8_t z,
                                           Houses& houses) {
    if (!basicTile) {
        return nullptr;
    }
    
    Position pos(x, y, z);
    std::unique_ptr<Tile> tile;
    
    // Create appropriate tile type
    if (basicTile->isHouse()) {
        House* house = houses.getHouse(basicTile->houseId);
        if (house) {
            auto houseTile = std::make_unique<HouseTile>(x, y, z, house);
            house->addTile(houseTile.get());
            tile = std::move(houseTile);
        } else {
            LOG_ERROR(fmt::format("[MapCache] House not found for houseId {}", basicTile->houseId));
            tile = std::make_unique<StaticTile>(x, y, z);
        }
    } else if (basicTile->isStatic) {
        tile = std::make_unique<StaticTile>(x, y, z);
    } else {
        tile = std::make_unique<DynamicTile>(x, y, z);
    }
    
    // Add ground
    if (basicTile->ground) {
        if (auto groundItem = createItemFromBasic(basicTile->ground, pos)) {
            if (!placeCachedMapItem(tile.get(), groundItem.get())) {
                LOG_WARN(fmt::format("[MapCache] Failed to attach ground item {} at {}", groundItem->getID(), pos));
            }
        }
    }
    
    // Add items
    for (const auto& basicItem : basicTile->items) {
        if (auto item = createItemFromBasic(basicItem, pos)) {
            if (!placeCachedMapItem(tile.get(), item.get())) {
                LOG_WARN(fmt::format("[MapCache] Failed to attach item {} at {}", item->getID(), pos));
            }
        }
    }
    
    // Set flags
    tile->setFlag(static_cast<tileflags_t>(basicTile->flags));
    tile->setZoneIds(basicTile->zoneIds);
    
    return tile;
}

} // namespace MapCacheUtils
