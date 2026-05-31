// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_IOMAP_H
#define FS_IOMAP_H

#include "configmanager.h"
#include "house.h"
#include "item.h"
#include "map.h"
#include "spawn.h"

enum class OTBM_AttrTypes_t : uint8_t {
    DESCRIPTION = 1,
    EXT_FILE = 2,
    TILE_FLAGS = 3,
    ACTION_ID = 4,
    UNIQUE_ID = 5,
    TEXT = 6,
    DESC = 7,
    TELE_DEST = 8,
    ITEM = 9,
    DEPOT_ID = 10,
    EXT_SPAWN_FILE = 11,
    RUNE_CHARGES = 12,
    EXT_HOUSE_FILE = 13,
    HOUSEDOORID = 14,
    COUNT = 15,
    DURATION = 16,
    DECAYING_STATE = 17,
    WRITTENDATE = 18,
    WRITTENBY = 19,
    SLEEPERGUID = 20,
    SLEEPSTART = 21,
    CHARGES = 22,
};

enum class OTBM_NodeTypes_t : uint8_t {
    ROOTV1 = 1,
    MAP_DATA = 2,
    ITEM_DEF = 3,
    TILE_AREA = 4,
    TILE = 5,
    ITEM = 6,
    TILE_SQUARE = 7,
    TILE_REF = 8,
    SPAWNS = 9,
    SPAWN_AREA = 10,
    MONSTER = 11,
    TOWNS = 12,
    TOWN = 13,
    HOUSETILE = 14,
    WAYPOINTS = 15,
    WAYPOINT = 16,
    TILE_ZONE = 19,
};

enum class OTBM_TileFlag_t : uint32_t {
    PROTECTIONZONE = 1 << 0,
    NOPVPZONE = 1 << 2,
    NOLOGOUT = 1 << 3,
    PVPZONE = 1 << 4,
    ZONE = 1 << 6
};

#pragma pack(1)
struct OTBM_root_header {
    uint32_t version;
    uint16_t width;
    uint16_t height;
    uint32_t majorVersionItems;
    uint32_t minorVersionItems;
};

struct OTBM_Destination_coords {
    uint16_t x;
    uint16_t y;
    uint8_t z;
};

struct OTBM_Tile_coords {
    uint8_t x;
    uint8_t y;
};
#pragma pack()

class IOMap {
    static std::unique_ptr<Tile> createTile(std::shared_ptr<Item>& ground, Item* item, uint16_t x, uint16_t y, uint8_t z);

public:
    bool loadMap(Map* map, const std::filesystem::path& fileName);

    /* Load the spawns
     * \param map pointer to the Map class
     * \returns Returns true if the spawns were loaded successfully
     */
    static bool loadSpawns(Map* map) {
        if (map->spawnfile.empty()) {
            // OTBM file doesn't tell us about the spawnfile,
            // lets guess it is mapname-spawn.xml.
            map->spawnfile = getString(ConfigManager::MAP_NAME);
            map->spawnfile += "-spawn.xml";
        }

        // Load traditional spawn file
        bool spawnsLoaded = map->spawns.loadFromXml(map->spawnfile.string());

        // Also try Canary/Crystal-style monster+npc files
        std::string mapName(getString(ConfigManager::MAP_NAME));
        auto worldPath = map->spawnfile.parent_path();
        if (worldPath.empty()) {
            worldPath = "data/world";
        }

        auto monsterPath = worldPath / (mapName + "-monster.xml");
        if (std::filesystem::exists(monsterPath)) {
            spawnsLoaded |= map->spawns.loadFromMonsterNpcXml(monsterPath.string());
        }

        auto npcPath = worldPath / (mapName + "-npc.xml");
        if (std::filesystem::exists(npcPath)) {
            spawnsLoaded |= map->spawns.loadFromMonsterNpcXml(npcPath.string());
        }

        return spawnsLoaded;
    }

    /* Load the houses (not house tile-data)
     * \param map pointer to the Map class
     * \returns Returns true if the houses were loaded successfully
     */
    static bool loadHouses(Map* map) {
        if (map->housefile.empty()) {
            // OTBM file doesn't tell us about the housefile,
            // lets guess it is mapname-house.xml.
            map->housefile = getString(ConfigManager::MAP_NAME);
            map->housefile += "-house.xml";
        }
        return map->houses.loadHousesXML(map->housefile.string());
    }

    std::string_view getLastErrorString() const { return errorString; }
    void setLastErrorString(std::string_view error) { errorString = error; }

    static bool parseTileZoneNode(OTB::Loader& loader, const OTB::Node& zoneNode, std::vector<ZoneId>& zoneIds, std::string& errorType);

private:
    bool parseMapDataAttributes(OTB::Loader& loader, const OTB::Node& mapNode, Map& map,
                                const std::filesystem::path& fileName);
    bool parseWaypoints(OTB::Loader& loader, const OTB::Node& waypointsNode, Map& map);
    bool parseTowns(OTB::Loader& loader, const OTB::Node& townsNode, Map& map);
    bool parseTileArea(OTB::Loader& loader, const OTB::Node& tileAreaNode, Map& map);

    std::string errorString;
};

#endif // FS_IOMAP_H
