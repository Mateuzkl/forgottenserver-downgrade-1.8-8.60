// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"
#include "iomap.h"
#include "bed.h"
#include "mapcache.h"
#include "logger.h"
#include "zones.h"

#include <fstream>

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

void appendEscapedProperties(std::vector<uint8_t>& output, OTB::Loader& loader, const OTB::Node& node)
{
	if (node.propsBegin == node.propsEnd) {
		return;
	}

	PropStream properties;
	if (!loader.getProps(node, properties)) {
		throw OTB::InvalidOTBFormat{};
	}
	for (const unsigned char byte : properties.view()) {
		if (byte == OTB::Node::ESCAPE || byte == OTB::Node::START || byte == OTB::Node::END) {
			output.emplace_back(OTB::Node::ESCAPE);
		}
		output.emplace_back(byte);
	}
}

void appendNode(std::vector<uint8_t>& output, OTB::Loader& loader, const OTB::Node& node)
{
	output.emplace_back(OTB::Node::START);
	output.emplace_back(node.type);
	appendEscapedProperties(output, loader, node);
	for (const auto& child : node.children) {
		appendNode(output, loader, child);
	}
	output.emplace_back(OTB::Node::END);
}

bool writeHouseMapCache(OTB::Loader& loader, const OTB::Node& root, const std::filesystem::path& path)
{
	if (root.children.size() != 1 ||
	    static_cast<OTBM_NodeTypes_t>(root.children.front().type) != OTBM_NodeTypes_t::MAP_DATA) {
		return false;
	}

	const auto& mapNode = root.children.front();
	std::vector<uint8_t> output;
	output.reserve(16 * 1024 * 1024);
	output.insert(output.end(), {'O', 'T', 'B', 'M'});
	output.emplace_back(OTB::Node::START);
	output.emplace_back(root.type);
	appendEscapedProperties(output, loader, root);
	output.emplace_back(OTB::Node::START);
	output.emplace_back(mapNode.type);
	appendEscapedProperties(output, loader, mapNode);

	for (const auto& child : mapNode.children) {
		const auto type = static_cast<OTBM_NodeTypes_t>(child.type);
		if (type == OTBM_NodeTypes_t::TILE_AREA) {
			const bool hasHouseTile = std::ranges::any_of(child.children, [](const OTB::Node& tile) {
				return static_cast<OTBM_NodeTypes_t>(tile.type) == OTBM_NodeTypes_t::HOUSETILE;
			});
			if (!hasHouseTile) {
				continue;
			}

			output.emplace_back(OTB::Node::START);
			output.emplace_back(child.type);
			appendEscapedProperties(output, loader, child);
			for (const auto& tile : child.children) {
				if (static_cast<OTBM_NodeTypes_t>(tile.type) == OTBM_NodeTypes_t::HOUSETILE) {
					appendNode(output, loader, tile);
				}
			}
			output.emplace_back(OTB::Node::END);
		} else if (type == OTBM_NodeTypes_t::TOWNS || type == OTBM_NodeTypes_t::WAYPOINTS) {
			appendNode(output, loader, child);
		}
	}

	output.emplace_back(OTB::Node::END); // map data
	output.emplace_back(OTB::Node::END); // root

	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);
	if (error) return false;
	const std::filesystem::path tempPath = path.string() + ".tmp";
	std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
	stream.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size()));
	stream.close();
	if (!stream) {
		std::filesystem::remove(tempPath, error);
		return false;
	}
#ifdef _WIN32
	return MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	std::filesystem::rename(tempPath, path, error);
	return !error;
#endif
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

bool IOMap::loadMap(Map* map, const std::filesystem::path& fileName, bool usePersistentCache) {
    const auto start = std::chrono::steady_clock::now();
    if (!std::filesystem::exists(fileName)) {
        setLastErrorString(fmt::format("Map file not found at: {}. Please check 'mapName' in config.lua and ensure the file exists in data/world/.", fileName.string()));
        return false;
    }

    const std::string cacheMode = asLowerCaseString(std::string{getString(ConfigManager::MAP_CACHE_MODE)});
    const bool cacheEnabled = usePersistentCache && cacheMode == "auto";
    if (usePersistentCache && cacheMode != "auto" && cacheMode != "off") {
        g_logger().warn(">> Unknown mapCacheMode '{}'; persistent map cache disabled.", cacheMode);
    }

    std::filesystem::path cachePath;
    std::filesystem::path houseCachePath;
    std::optional<MapCache::Fingerprint> initialFingerprint;
    if (cacheEnabled) {
        const std::filesystem::path cacheDirectory{getString(ConfigManager::MAP_CACHE_DIRECTORY)};
        const std::string cacheName = fileName.stem().string();
        cachePath = cacheDirectory / (cacheName + ".tfsmc");
        houseCachePath = cacheDirectory / (cacheName + ".houses.otbm");
        initialFingerprint = MapCache::fingerprint(fileName);

        const bool forceRebuild = ConfigManager::getBoolean(ConfigManager::REBUILD_MAP_CACHE);
        const auto houseDigest = MapCache::digestFile(houseCachePath);
        if (!forceRebuild && initialFingerprint && houseDigest && std::filesystem::exists(cachePath) &&
            MapCache::loadPersistent(*map, cachePath, *initialFingerprint, *houseDigest)) {
            const auto spawnFile = map->spawnfile;
            const auto houseFile = map->housefile;
            IOMap houseLoader;
            if (!houseLoader.loadMap(map, houseCachePath, false)) {
                setLastErrorString(fmt::format("Persistent house cache failed: {}", houseLoader.getLastErrorString()));
                return false;
            }
            // External files remain relative to the original OTBM, not to the
            // generated house-only cache.
            map->spawnfile = spawnFile;
            map->housefile = houseFile;
            g_logger().info(">> Persistent map cache hit completed in [\033[1;33m{:.3f}\033[0m] seconds.",
                            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
            return true;
        }
        g_logger().info(">> Persistent map cache miss; rebuilding from OTBM.");
        MapCache::resetStore();
    }

    std::error_code fileSizeError;
    const auto sourceBytes = std::filesystem::file_size(fileName, fileSizeError);
    if (!fileSizeError) {
        MapCache::prepare(static_cast<size_t>(sourceBytes));
    }

    bool wroteHouseCache = false;
    try {
        OTB::Loader loader{fileName.string(), OTB::Identifier{{'O', 'T', 'B', 'M'}}};
        const auto treeStart = std::chrono::steady_clock::now();
        auto& root = loader.parseTree();
        const auto treeEnd = std::chrono::steady_clock::now();
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

        const auto dataStart = std::chrono::steady_clock::now();
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

        const auto dataEnd = std::chrono::steady_clock::now();
        g_logger().info(">> Map phases: tree [\033[1;33m{:.3f}\033[0m] s, data [\033[1;33m{:.3f}\033[0m] s.",
                        std::chrono::duration<double>(treeEnd - treeStart).count(),
                        std::chrono::duration<double>(dataEnd - dataStart).count());

        if (cacheEnabled && initialFingerprint) {
            wroteHouseCache = writeHouseMapCache(loader, root, houseCachePath);
            if (!wroteHouseCache) {
                g_logger().warn(">> Failed to write house-only map cache '{}'.", houseCachePath.string());
            }
        }
    } catch (const OTB::LoadError& err) {
        setLastErrorString(err.what());
        return false;
    } catch (const std::exception& err) {
        setLastErrorString(fmt::format("Failed to open map file [{}]: {}", fileName.string(), err.what()));
        return false;
    }

    if (cacheEnabled && initialFingerprint && wroteHouseCache) {
        const auto finalFingerprint = MapCache::fingerprint(fileName);
        const auto houseDigest = MapCache::digestFile(houseCachePath);
        if (finalFingerprint && *finalFingerprint == *initialFingerprint && houseDigest) {
            if (!MapCache::savePersistent(*map, cachePath, *finalFingerprint, *houseDigest)) {
                g_logger().warn(">> Failed to write persistent map cache '{}'.", cachePath.string());
            }
        } else {
            g_logger().warn(">> Map inputs changed while building the cache; generated cache was discarded.");
        }
    }

    // Flush only the lookup tables; compact tileStore ids remain valid for
    // lazy materialization throughout the Map lifetime.
    MapCache::flush();
    g_logger().info(">> Map loading time: [\033[1;33m{:.3f}\033[0m] seconds.",
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
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
