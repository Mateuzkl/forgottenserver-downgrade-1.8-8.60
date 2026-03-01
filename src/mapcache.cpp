// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"
#include "mapcache.h"
#include "item.h"
#include "container.h"
#include "teleport.h"
#include "depotlocker.h"
#include "tile.h"
#include "housetile.h"
#include "house.h"
#include "game.h"
#include "iomap.h"
#include "fileloader.h"
#include <iostream>
#include "logger.h"

// Static cache storage with weak_ptr for automatic cleanup
std::unordered_map<size_t, std::weak_ptr<BasicItem>> MapCache::itemCache;
std::unordered_map<size_t, std::weak_ptr<BasicTile>> MapCache::tileCache;
std::mutex MapCache::itemCacheMutex;
std::mutex MapCache::tileCacheMutex;

// Cache statistics
std::atomic<size_t> MapCache::itemCacheHits{0};
std::atomic<size_t> MapCache::itemCacheMisses{0};
std::atomic<size_t> MapCache::tileCacheHits{0};
std::atomic<size_t> MapCache::tileCacheMisses{0};

std::shared_ptr<BasicItem> MapCache::tryGetItemFromCache(const std::shared_ptr<BasicItem>& item) {
    if (!item) {
        return nullptr;
    }
    
    size_t h = item->hash();
    
    std::lock_guard<std::mutex> lock(itemCacheMutex);
    
    auto it = itemCache.find(h);
    if (it != itemCache.end()) {
        if (auto cached = it->second.lock()) {
            // Verify hash collision
            if (*cached == *item) {
                ++itemCacheHits;
                return cached;
            } else {
                // LOG_WARN(fmt::format("[MapCache] Hash collision detected for item ID {} (hash: {})", 
                //                        item->id, h));
            }
        } else {
            // Expired weak_ptr, remove it
            itemCache.erase(it);
        }
    }
    
    ++itemCacheMisses;
    itemCache[h] = item;
    return item;
}

std::shared_ptr<BasicTile> MapCache::tryGetTileFromCache(const std::shared_ptr<BasicTile>& tile) {
    if (!tile) {
        return nullptr;
    }
    
    size_t h = tile->hash();
    
    std::lock_guard<std::mutex> lock(tileCacheMutex);
    
    auto it = tileCache.find(h);
    if (it != tileCache.end()) {
        if (auto cached = it->second.lock()) {
            // Verify hash collision
            if (*cached == *tile) {
                ++tileCacheHits;
                return cached;
            } else {
                // LOG_WARN(fmt::format("[MapCache] Hash collision detected for tile (hash: {})", h));
            }
        } else {
            // Expired weak_ptr, remove it
            tileCache.erase(it);
        }
    }
    
    ++tileCacheMisses;
    tileCache[h] = tile;
    return tile;
}

void MapCache::flush() {
    std::lock_guard<std::mutex> itemLock(itemCacheMutex);
    std::lock_guard<std::mutex> tileLock(tileCacheMutex);
    
    size_t itemCount = itemCache.size();
    size_t tileCount = tileCache.size();
    
    // Calculate hit rates
    size_t totalItemRequests = itemCacheHits + itemCacheMisses;
    size_t totalTileRequests = tileCacheHits + tileCacheMisses;
    
    double itemHitRate = totalItemRequests > 0 ? 
        (static_cast<double>(itemCacheHits) / totalItemRequests * 100.0) : 0.0;
    double tileHitRate = totalTileRequests > 0 ? 
        (static_cast<double>(tileCacheHits) / totalTileRequests * 100.0) : 0.0;
    
    itemCache.clear();
    tileCache.clear();
    
    LOG_INFO(fmt::format(">> Map cache flushed: {} unique items, {} unique tiles deduplicated", 
                         itemCount, tileCount));
    LOG_INFO(fmt::format(">> Cache hit rates: Items {:.2f}% ({}/{}), Tiles {:.2f}% ({}/{})",
                         itemHitRate, itemCacheHits.load(), totalItemRequests,
                         tileHitRate, tileCacheHits.load(), totalTileRequests));
    
    // Reset counters
    itemCacheHits = 0;
    itemCacheMisses = 0;
    tileCacheHits = 0;
    tileCacheMisses = 0;
}

void MapCache::cleanupExpiredEntries() {
    {
        std::lock_guard<std::mutex> lock(itemCacheMutex);
        for (auto it = itemCache.begin(); it != itemCache.end();) {
            if (it->second.expired()) {
                it = itemCache.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(tileCacheMutex);
        for (auto it = tileCache.begin(); it != tileCache.end();) {
            if (it->second.expired()) {
                it = tileCache.erase(it);
            } else {
                ++it;
            }
        }
    }
}

size_t MapCache::getItemCacheSize() {
    std::lock_guard<std::mutex> lock(itemCacheMutex);
    return itemCache.size();
}

size_t MapCache::getTileCacheSize() {
    std::lock_guard<std::mutex> lock(tileCacheMutex);
    return tileCache.size();
}

// Helper functions for parsing
namespace {
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
std::shared_ptr<BasicItem> parseBasicItemFromStream(PropStream& propStream) {
    uint16_t id;
    if (!propStream.read<uint16_t>(id)) {
        LOG_ERROR("[MapCache] Failed to read item ID from stream");
        return nullptr;
    }
    
    // ID substitutions
    applyItemIdSubstitutions(id);

    auto item = std::make_shared<BasicItem>();
    item->id = id;
    
    // Default values based on ItemType
    const ItemType& it = Item::items[id];
    if (it.stackable && it.charges == 0) {
        item->charges = 1;
    } else if (it.charges != 0) {
        item->charges = it.charges;
    }
    
    // Read attributes
    uint8_t attr_type;
    while (propStream.read<uint8_t>(attr_type)) {
        switch (attr_type) {
            case ATTR_COUNT:
            case ATTR_RUNE_CHARGES: {
                uint8_t count;
                if (propStream.read<uint8_t>(count)) {
                    item->charges = count;
                }
                break;
            }
            
            case ATTR_CHARGES: {
                uint16_t charges;
                if (propStream.read<uint16_t>(charges)) {
                    item->charges = charges;
                }
                break;
            }
            
            case ATTR_ACTION_ID: {
                uint16_t actionId;
                if (propStream.read<uint16_t>(actionId)) {
                    item->actionId = actionId;
                }
                break;
            }
            
            case ATTR_UNIQUE_ID: {
                uint16_t uniqueId;
                if (propStream.read<uint16_t>(uniqueId)) {
                    item->uniqueId = uniqueId;
                }
                break;
            }
            
            case ATTR_TEXT: {
                auto [text, ok] = propStream.readString();
                if (ok) {
                    item->text = std::string(text);
                }
                break;
            }
            
            case ATTR_TELE_DEST: {
                OTBM_Destination_coords dest;
                if (propStream.read(dest)) {
                    item->destX = dest.x;
                    item->destY = dest.y;
                    item->destZ = dest.z;
                }
                break;
            }
            
            case ATTR_HOUSEDOORID: {
                uint8_t doorId;
                if (propStream.read<uint8_t>(doorId)) {
                    item->doorOrDepotId = doorId;
                }
                break;
            }
            
            case ATTR_DEPOT_ID: {
                uint16_t depotId;
                if (propStream.read<uint16_t>(depotId)) {
                    item->doorOrDepotId = depotId;
                }
                break;
            }
            
            // Ignored string attributes
            case OTBM_ATTR_DESCRIPTION:
            case OTBM_ATTR_DESC:
            case OTBM_ATTR_EXT_FILE:
            case OTBM_ATTR_EXT_SPAWN_FILE:
            case OTBM_ATTR_EXT_HOUSE_FILE:
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
    return item;
}

std::shared_ptr<BasicItem> MapCache::parseBasicItem(void* loaderptr, const void* nodeptr, BasicItem* parent) {
    (void)parent;
    OTB::Loader& loader = *static_cast<OTB::Loader*>(loaderptr);
    const OTB::Node& node = *static_cast<const OTB::Node*>(nodeptr);
    PropStream propStream;

    if (!loader.getProps(node, propStream)) {
        LOG_ERROR("[MapCache] Failed to get props from item node");
        return nullptr;
    }

    auto item = parseBasicItemFromStream(propStream);
    if (!item) {
        return nullptr;
    }
    
    // Parse children (containers)
    for (auto& childNode : node.children) {
        if (childNode.type == OTBM_ITEM) {
            auto child = parseBasicItem(loaderptr, &childNode, item.get());
            if (child) {
                item->items.push_back(child);
            }
        }
    }

    return tryGetItemFromCache(item);
}

std::shared_ptr<BasicTile> MapCache::parseBasicTile(void* loaderptr, const void* nodeptr, uint8_t& xOffset, uint8_t& yOffset) {
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
    
    auto tile = std::make_shared<BasicTile>();
    
    // Check for HouseTile
    if (tileNode.type == OTBM_HOUSETILE) {
        uint32_t houseId;
        if (propStream.read<uint32_t>(houseId)) {
            tile->houseId = houseId;
        } else {
            LOG_ERROR("[MapCache] Failed to read house ID from house tile");
        }
    }
    
    // Read tile attributes
    uint8_t attribute;
    while (propStream.read<uint8_t>(attribute)) {
        switch (attribute) {
            case OTBM_ATTR_TILE_FLAGS: {
                uint32_t flags;
                if (propStream.read<uint32_t>(flags)) {
                    if ((flags & OTBM_TILEFLAG_PROTECTIONZONE) != 0) tile->flags |= TILESTATE_PROTECTIONZONE;
                    if ((flags & OTBM_TILEFLAG_NOPVPZONE) != 0) tile->flags |= TILESTATE_NOPVPZONE;
                    if ((flags & OTBM_TILEFLAG_PVPZONE) != 0) tile->flags |= TILESTATE_PVPZONE;
                    if ((flags & OTBM_TILEFLAG_NOLOGOUT) != 0) tile->flags |= TILESTATE_NOLOGOUT;
                } else {
                    LOG_ERROR("[MapCache] Failed to read tile flags");
                }
                break;
            }
            case OTBM_ATTR_ITEM: {
                // Inline item
                auto item = parseBasicItemFromStream(propStream);
                if (item) {
                     const ItemType& it = Item::items[item->id];
                     if (it.isGroundTile()) {
                         tile->ground = MapCache::tryGetItemFromCache(item);
                     } else {
                         tile->items.push_back(MapCache::tryGetItemFromCache(item));
                     }
                }
                break; 
            }
            default:
                LOG_WARN(fmt::format("[MapCache] Unknown tile attribute: {}", attribute));
                break;
        }
    }
    
    // Parse nested items
    for (auto& itemNode : tileNode.children) {
        if (itemNode.type == OTBM_ITEM) {
            auto item = parseBasicItem(loaderptr, &itemNode, nullptr);
            if (item) {
                const ItemType& it = Item::items[item->id];
                if (it.isGroundTile() && tile->ground == nullptr) {
                    tile->ground = item;
                } else {
                    tile->items.push_back(item);
                }
            }
        }
    }
    
    return tryGetTileFromCache(tile);
}

/**
 * Helper functions for converting BasicItem/BasicTile to real Item/Tile objects
 * These are used during lazy loading when a tile is first accessed
 */
namespace MapCacheUtils {

Item* createItemFromBasic(const std::shared_ptr<BasicItem>& basicItem, const Position& pos) {
    if (!basicItem) {
        return nullptr;
    }
    
    Item* item = Item::CreateItem(basicItem->id, basicItem->charges);
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
            if (Item* childItem = createItemFromBasic(childBasicItem, pos)) {
                container->internalAddThing(childItem);
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

std::unique_ptr<Tile> createTileFromBasic(const std::shared_ptr<BasicTile>& basicTile, 
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
        if (Item* groundItem = createItemFromBasic(basicTile->ground, pos)) {
            tile->internalAddThing(groundItem);
        }
    }
    
    // Add items
    for (const auto& basicItem : basicTile->items) {
        if (Item* item = createItemFromBasic(basicItem, pos)) {
            tile->internalAddThing(item);
        }
    }
    
    // Set flags
    tile->setFlag(static_cast<tileflags_t>(basicTile->flags));
    
    return tile;
}

} // namespace MapCacheUtils
