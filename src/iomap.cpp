// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"
#include "iomap.h"
#include "bed.h"
#include "mapcache.h"
#include "logger.h"
#include "zones.h"

/*
OTBM_ROOTV1
    |--- OTBM_MAP_DATA
    |   |--- OTBM_TILE_AREA
    |   |   |--- OTBM_TILE
    |   |   |--- OTBM_TILE_SQUARE (not implemented)
    |   |   |--- OTBM_TILE_REF (not implemented)
    |   |   |--- OTBM_HOUSETILE
    |   |
    |   |--- OTBM_SPAWNS (not implemented)
    |   |   |--- OTBM_SPAWN_AREA (not implemented)
    |   |   |--- OTBM_MONSTER (not implemented)
    |   |
    |   |--- OTBM_TOWNS
    |   |   |--- OTBM_TOWN
    |   |
    |   |--- OTBM_WAYPOINTS
    |       |--- OTBM_WAYPOINT
    |
    |--- OTBM_ITEM_DEF (not implemented)
*/

namespace {
bool mapCylinderOwnsThing(const Cylinder* cylinder, const Thing* thing)
{
	return cylinder && thing && thing->getParent() == cylinder && cylinder->getThingIndex(thing) != -1;
}

bool transferMapItem(Cylinder* cylinder, std::shared_ptr<Item>& item)
{
	if (!cylinder || !item) {
		return false;
	}

	Item* rawItem = item.get();
	cylinder->internalAddThing(rawItem);
	if (!mapCylinderOwnsThing(cylinder, rawItem)) {
		return false;
	}

	rawItem->startDecaying();
	rawItem->setLoadedFromMap(true);
	item.reset();
	return true;
}
} // namespace

std::unique_ptr<Tile> IOMap::createTile(std::shared_ptr<Item>& ground, Item* item, uint16_t x, uint16_t y, uint8_t z) {
    std::unique_ptr<Tile> tile;
    if (!ground) {
        return std::make_unique<DynamicTile>(x, y, z);
    }

    if ((item && item->isBlocking()) || ground->isBlocking()) {
        tile = std::make_unique<StaticTile>(x, y, z);
    } else {
        tile = std::make_unique<DynamicTile>(x, y, z);
    }

    transferMapItem(tile.get(), ground);
    return tile;
}

bool IOMap::loadMap(Map* map, const std::filesystem::path& fileName) {
    int64_t start = OTSYS_TIME();
    if (!std::filesystem::exists(fileName)) {
        setLastErrorString(fmt::format("Map file not found at: {}. Please check 'mapName' in config.lua and ensure the file exists in data/world/.", fileName.string()));
        return false;
    }

    try {
        OTB::Loader loader{fileName.string(), OTB::Identifier{{'O', 'T', 'B', 'M'}}};
        auto& root = loader.parseTree();
        PropStream propStream;
        if (!loader.getProps(root, propStream)) {
            setLastErrorString("Could not read root property.");
            return false;
        }

        OTBM_root_header root_header;
        if (!propStream.read(root_header)) {
            setLastErrorString("Could not read header.");
            return false;
        }

        uint32_t headerVersion = root_header.version;
        if (headerVersion == 0) {
            setLastErrorString(
                "This map need to be upgraded by using the latest map editor version to be able to load correctly.");
            return false;
        }

        if (headerVersion > 2) {
            setLastErrorString("Unknown OTBM version detected.");
            return false;
        }

        if (root_header.majorVersionItems < 3) {
            setLastErrorString(
                "This map need to be upgraded by using the latest map editor version to be able to load correctly.");
            return false;
        }

        if (root_header.majorVersionItems > Item::items.majorVersion) {
            setLastErrorString(
                "The map was saved with a different items.otb version, an upgraded items.otb is required.");
            return false;
        }

        if (root_header.minorVersionItems < CLIENT_VERSION_810) {
            setLastErrorString("This map needs to be updated.");
            return false;
        }

        if (root_header.minorVersionItems > Item::items.minorVersion) {
            g_logger().warn("This map needs an updated items.otb.");
        }

        g_logger().info(">> Map size: [\033[1;33m{}x{}\033[0m].", root_header.width, root_header.height);
        map->width = root_header.width;
        map->height = root_header.height;

        if (root.children.size() != 1 || static_cast<OTBM_NodeTypes_t>(root.children[0].type) != OTBM_NodeTypes_t::MAP_DATA) {
            setLastErrorString("Could not read data node.");
            return false;
        }

        auto& mapNode = root.children[0];
        if (!parseMapDataAttributes(loader, mapNode, *map, fileName)) {
            return false;
        }

        for (auto& mapDataNode : mapNode.children) {
            if (static_cast<OTBM_NodeTypes_t>(mapDataNode.type) == OTBM_NodeTypes_t::TILE_AREA) {
                if (!parseTileArea(loader, mapDataNode, *map)) {
                    return false;
                }
            } else if (static_cast<OTBM_NodeTypes_t>(mapDataNode.type) == OTBM_NodeTypes_t::TOWNS) {
                if (!parseTowns(loader, mapDataNode, *map)) {
                    return false;
                }
            } else if (static_cast<OTBM_NodeTypes_t>(mapDataNode.type) == OTBM_NodeTypes_t::WAYPOINTS && headerVersion > 1) {
                if (!parseWaypoints(loader, mapDataNode, *map)) {
                    return false;
                }
            } else {
                setLastErrorString("Unknown map node.");
                return false;
            }
        }
    } catch (const OTB::LoadError& err) {
        setLastErrorString(err.what());
        return false;
    } catch (const std::exception& err) {
        setLastErrorString(fmt::format("Failed to open map file [{}]: {}", fileName.string(), err.what()));
        return false;
    }

    // Flush map cache after loading (logs deduplication stats)
    MapCache::flush();
    g_logger().info(">> Map loading time: [\033[1;33m{}\033[0m] seconds.", (OTSYS_TIME() - start) / (1000.));
    return true;
}

bool IOMap::parseMapDataAttributes(OTB::Loader& loader, const OTB::Node& mapNode, Map& map,
                                    const std::filesystem::path& fileName) {
    PropStream propStream;
    if (!loader.getProps(mapNode, propStream)) {
        setLastErrorString("Could not read map data attributes.");
        return false;
    }

    uint8_t attribute;
    while (propStream.read(attribute)) {
        switch (static_cast<OTBM_AttrTypes_t>(attribute)) {
            case OTBM_AttrTypes_t::DESCRIPTION: {
                auto [mapDescription, ok] = propStream.readString();
                if (!ok) {
                    setLastErrorString("Invalid description tag.");
                    return false;
                }
                break;
            }

            case OTBM_AttrTypes_t::EXT_SPAWN_FILE: {
                auto [spawnFile, ok] = propStream.readString();
                if (!ok) {
                    setLastErrorString("Invalid spawn tag.");
                    return false;
                }
                map.spawnfile = fileName.parent_path() / spawnFile;
                break;
            }

            case OTBM_AttrTypes_t::EXT_HOUSE_FILE: {
                auto [houseFile, ok] = propStream.readString();
                if (!ok) {
                    setLastErrorString("Invalid house tag.");
                    return false;
                }
                map.housefile = fileName.parent_path() / houseFile;
                break;
            }

            default:
                setLastErrorString("Unknown header node.");
                return false;
        }
    }
    return true;
}

bool IOMap::parseTileArea(OTB::Loader& loader, const OTB::Node& tileAreaNode, Map& map) {
    PropStream propStream;
    if (!loader.getProps(tileAreaNode, propStream)) {
        setLastErrorString("Invalid map node.");
        return false;
    }

    OTBM_Destination_coords area_coord;
    if (!propStream.read(area_coord)) {
        setLastErrorString("Invalid map node.");
        return false;
    }

    uint16_t base_x = area_coord.x;
    uint16_t base_y = area_coord.y;
    uint16_t z = area_coord.z;

    for (auto& tileNode : tileAreaNode.children) {
        if (static_cast<OTBM_NodeTypes_t>(tileNode.type) == OTBM_NodeTypes_t::HOUSETILE) {
            // Legacy parsing for House Tiles to ensure full compatibility
            PropStream tilePropStream;
            if (!loader.getProps(tileNode, tilePropStream)) {
                setLastErrorString("Invalid map node.");
                return false;
            }

            OTBM_Tile_coords tile_coord;
            if (!tilePropStream.read(tile_coord)) {
                setLastErrorString("Invalid map node.");
                return false;
            }

            uint16_t x = base_x + tile_coord.x;
            uint16_t y = base_y + tile_coord.y;

            House* house = nullptr;
            std::unique_ptr<Tile> tilePtr;
            Tile* tile = nullptr;
            std::shared_ptr<Item> ground_item;
            std::vector<ZoneId> zoneIds;
            uint32_t tileflags = TILESTATE_NONE;

            uint32_t houseId;
            if (!tilePropStream.read(houseId)) {
                setLastErrorString(fmt::format("[x:{:d}, y:{:d}, z:{:d}] Could not read house id.", x, y, z));
                return false;
            }

            house = map.houses.addHouse(houseId);
            if (!house) {
                setLastErrorString(fmt::format("[x:{:d}, y:{:d}, z:{:d}] Could not create house id: {:d}", x, y, z, houseId));
                return false;
            }

            tilePtr = std::make_unique<HouseTile>(x, y, z, house);
            tile = tilePtr.get();
            house->addTile(static_cast<HouseTile*>(tile));

            uint8_t attribute;
            while (tilePropStream.read(attribute)) {
                switch (static_cast<OTBM_AttrTypes_t>(attribute)) {
                    case OTBM_AttrTypes_t::TILE_FLAGS: {
                        uint32_t flags;
                        if (!tilePropStream.read(flags)) {
                            setLastErrorString(fmt::format("[x:{:d}, y:{:d}, z:{:d}] Failed to read tile flags.", x, y, z));
                            return false;
                        }

                        if ((flags & tfs::to_underlying(OTBM_TileFlag_t::PROTECTIONZONE)) != 0) {
                            tileflags |= TILESTATE_PROTECTIONZONE;
                        } else if ((flags & tfs::to_underlying(OTBM_TileFlag_t::NOPVPZONE)) != 0) {
                            tileflags |= TILESTATE_NOPVPZONE;
                        } else if ((flags & tfs::to_underlying(OTBM_TileFlag_t::PVPZONE)) != 0) {
                            tileflags |= TILESTATE_PVPZONE;
                        }

                        if ((flags & tfs::to_underlying(OTBM_TileFlag_t::NOLOGOUT)) != 0) {
                            tileflags |= TILESTATE_NOLOGOUT;
                        }

                        if ((flags & tfs::to_underlying(OTBM_TileFlag_t::ZONE)) != 0) {
                            ZoneId zoneId = 0;
                            do {
                                if (!tilePropStream.read<ZoneId>(zoneId)) {
                                    setLastErrorString(fmt::format("[x:{:d}, y:{:d}, z:{:d}] Failed to read tile zone id.", x, y, z));
                                    return false;
                                }

                                if (zoneId != 0) {
                                    zoneIds.emplace_back(zoneId);
                                }
                            } while (zoneId != 0);
                        }
                        break;
                    }

                    case OTBM_AttrTypes_t::ITEM: {
                        auto item = Item::CreateItem(tilePropStream);
                        if (!item) {
                            setLastErrorString(fmt::format("[x:{:d}, y:{:d}, z:{:d}] Failed to create item.", x, y, z));
                            return false;
                        }

                        if (item->isMoveable()) {
                            g_logger().warn("Moveable item with ID: {} in house: {} at position [x: {}, y: {}, z: {}].",
                                          item->getID(), house->getId(), x, y, z);
                        } else {
                            if (item->getItemCount() == 0) {
                                item->setItemCount(1);
                            }

                            if (tile) {
                                transferMapItem(tile, item);
                            } else if (item->isGroundTile()) {
                                ground_item = std::move(item);
                            } else {
                                tilePtr = createTile(ground_item, item.get(), x, y, z);
                                tile = tilePtr.get();
                                transferMapItem(tile, item);
                            }
                        }
                        break;
                    }

                    default:
                        setLastErrorString(fmt::format("[x:{:d}, y:{:d}, z:{:d}] Unknown tile attribute.", x, y, z));
                        return false;
                }
            }

            // Parse children items and Canary-style tile zone nodes.
            for (auto& itemNode : tileNode.children) {
                const auto childNodeType = static_cast<OTBM_NodeTypes_t>(itemNode.type);
                if (childNodeType == OTBM_NodeTypes_t::TILE_ZONE) {
                    std::string errorType;
                    if (!parseTileZoneNode(loader, itemNode, zoneIds, errorType)) {
                        setLastErrorString(fmt::format("[x:{:d}, y:{:d}, z:{:d}] {}", x, y, z, errorType));
                        return false;
                    }
                    continue;
                }

                if (childNodeType != OTBM_NodeTypes_t::ITEM) {
                    setLastErrorString(fmt::format("[x:{:d}, y:{:d}, z:{:d}] Unknown node type.", x, y, z));
                    return false;
                }

                PropStream stream;
                if (!loader.getProps(itemNode, stream)) {
                    setLastErrorString("Invalid item node.");
                    return false;
                }

                auto item = Item::CreateItem(stream);
                if (!item) {
                    setLastErrorString(fmt::format("[x:{:d}, y:{:d}, z:{:d}] Failed to create item.", x, y, z));
                    return false;
                }

                if (!item->unserializeItemNode(loader, itemNode, stream)) {
                    setLastErrorString(fmt::format("[x:{:d}, y:{:d}, z:{:d}] Failed to load item {:d}.", x, y, z, item->getID()));
                    return false;
                }

                if (item->isMoveable()) {
                    g_logger().warn("Moveable item with ID: {} in house: {} at position [x: {}, y: {}, z: {}].",
                                  item->getID(), house->getId(), x, y, z);
                } else {
                    if (item->getItemCount() == 0) {
                        item->setItemCount(1);
                    }

                    if (tile) {
                        transferMapItem(tile, item);
                    } else if (item->isGroundTile()) {
                        ground_item = std::move(item);
                    } else {
                        tilePtr = createTile(ground_item, item.get(), x, y, z);
                        tile = tilePtr.get();
                        transferMapItem(tile, item);
                    }
                }
            }

            if (!tilePtr) {
                tilePtr = createTile(ground_item, nullptr, x, y, z);
                tile = tilePtr.get();
            }

            if (tile) {
                tile->setFlag(static_cast<tileflags_t>(tileflags));

                std::sort(zoneIds.begin(), zoneIds.end());
                zoneIds.erase(std::unique(zoneIds.begin(), zoneIds.end()), zoneIds.end());
                
                tile->setZoneIds(std::move(zoneIds));
            }

            if (tilePtr) {
                map.setTile(x, y, z, std::move(tilePtr));
            }

        } else {
            // Optimized MapCache parsing for standard tiles
            if (static_cast<OTBM_NodeTypes_t>(tileNode.type) != OTBM_NodeTypes_t::TILE) {
                setLastErrorString("Unknown tile node.");
                return false;
            }

            uint8_t xOffset, yOffset;
            auto basicTile = MapCache::parseBasicTile(&loader, &tileNode, xOffset, yOffset);
            if (!basicTile) {
                setLastErrorString("Failed to parse basic tile.");
                return false;
            }

            uint16_t x = base_x + xOffset;
            uint16_t y = base_y + yOffset;
            map.setBasicTile(x, y, z, basicTile);
        }
    }
    return true;
}

bool IOMap::parseTowns(OTB::Loader& loader, const OTB::Node& townsNode, Map& map) {
    for (auto& townNode : townsNode.children) {
        PropStream propStream;
        if (static_cast<OTBM_NodeTypes_t>(townNode.type) != OTBM_NodeTypes_t::TOWN) {
            setLastErrorString("Unknown town node.");
            return false;
        }

        if (!loader.getProps(townNode, propStream)) {
            setLastErrorString("Could not read town data.");
            return false;
        }

        uint32_t townId;
        if (!propStream.read(townId)) {
            setLastErrorString("Could not read town id.");
            return false;
        }

        Town* town = map.towns.getTown(townId);
        if (!town) {
            auto newTown = std::make_shared<Town>(townId);
            town = newTown.get();
            map.towns.addTown(townId, std::move(newTown));
        }

        auto [townName, ok] = propStream.readString();
        if (!ok) {
            setLastErrorString("Could not read town name.");
            return false;
        }

        town->setName(townName);

        OTBM_Destination_coords town_coords;
        if (!propStream.read(town_coords)) {
            setLastErrorString("Could not read town coordinates.");
            return false;
        }

        town->setTemplePos(Position(town_coords.x, town_coords.y, town_coords.z));
    }
    return true;
}

bool IOMap::parseWaypoints(OTB::Loader& loader, const OTB::Node& waypointsNode, Map& map) {
    PropStream propStream;
    for (auto& node : waypointsNode.children) {
        if (static_cast<OTBM_NodeTypes_t>(node.type) != OTBM_NodeTypes_t::WAYPOINT) {
            setLastErrorString("Unknown waypoint node.");
            return false;
        }

        if (!loader.getProps(node, propStream)) {
            setLastErrorString("Could not read waypoint data.");
            return false;
        }

        auto [name, ok] = propStream.readString();
        if (!ok) {
            setLastErrorString("Could not read waypoint name.");
            return false;
        }

        OTBM_Destination_coords waypoint_coords;
        if (!propStream.read(waypoint_coords)) {
            setLastErrorString("Could not read waypoint coordinates.");
            return false;
        }

        map.waypoints[std::string{name}] = Position(waypoint_coords.x, waypoint_coords.y, waypoint_coords.z);
    }
    return true;
}

bool IOMap::parseTileZoneNode(OTB::Loader& loader, const OTB::Node& zoneNode, std::vector<ZoneId>& zoneIds, std::string& errorType) {
    PropStream stream;
    if (!loader.getProps(zoneNode, stream)) {
        errorType = "Invalid tile zone node.";
        return false;
    }

    uint16_t zoneCount = 0;
    if (!stream.read<uint16_t>(zoneCount)) {
        errorType = "Failed to read tile zone count.";
        return false;
    }

    for (uint16_t i = 0; i < zoneCount; ++i) {
        ZoneId zoneId = 0;
        if (!stream.read<ZoneId>(zoneId)) {
            errorType = "Failed to read tile zone id.";
            return false;
        }
        if (zoneId != 0) {
            zoneIds.emplace_back(zoneId);
        }
    }
    return true;
}
