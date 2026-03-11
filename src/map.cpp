// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "map.h"

#include "combat.h"
#include "creature.h"
#include "game.h"
#include "iomap.h"
#include "iomapserialize.h"
#include "mapcache.h"
#include "monster.h"
#include "spectators.h"
#include "logger.h"
#include <fmt/format.h>

extern Game g_game;

bool Map::loadMap(const std::string& identifier, bool loadHouses)
{
	IOMap loader;
	if (!loader.loadMap(this, identifier)) {
		LOG_ERROR(fmt::format("[Fatal - Map::loadMap] {}", loader.getLastErrorString()));
		return false;
	}

	if (loadHouses) {
		if (!IOMap::loadHouses(this)) {
			LOG_WARN("[Warning - Map::loadMap] Failed to load house data.");
		}

		IOMapSerialize::loadHouseInfo();
		IOMapSerialize::loadHouseItems(this);
		
		LOG_INFO(fmt::format(">> Loaded \033[1;33m{}\033[0m towns with \033[1;33m{}\033[0m houses in total", towns.getTowns().size(), houses.getHouses().size()));
	}

	if (!IOMap::loadSpawns(this)) {
		LOG_WARN("[Warning - Map::loadMap] Failed to load spawn data.");
	} else {
		LOG_INFO(fmt::format(">> Loaded \033[1;33m{}\033[0m npcs and spawned \033[1;33m{}\033[0m monsters", spawns.getNpcCount(), spawns.getMonsterCount()));
	}
	return true;
}

bool Map::save()
{
	bool saved = false;
	for (uint32_t tries = 0; tries < 3; tries++) {
		if (IOMapSerialize::saveHouseInfo()) {
			saved = true;
			break;
		}
	}

	if (!saved) {
		return false;
	}

	saved = false;
	for (uint32_t tries = 0; tries < 3; tries++) {
		if (IOMapSerialize::saveHouseItems()) {
			saved = true;
			break;
		}
	}
	return saved;
}

Tile* Map::getTile(uint16_t x, uint16_t y, uint8_t z) const
{
	if (z >= MAP_MAX_LAYERS) {
		return nullptr;
	}

	const QTreeLeafNode* leaf = QTreeNode::getLeafStatic<const QTreeLeafNode*, const QTreeNode*>(&root, x, y);
	if (!leaf) {
		return nullptr;
	}

	Floor* floor = const_cast<Floor*>(leaf->getFloor(z));
	if (!floor) {
		return nullptr;
	}
	
	// Use lazy loading: get tile from cache if real tile doesn't exist
	return floor->getTile(x, y, z);
}

void Map::setTile(uint16_t x, uint16_t y, uint8_t z, std::unique_ptr<Tile> newTile)
{
	if (z >= MAP_MAX_LAYERS) {
		LOG_ERROR(fmt::format("ERROR: Attempt to set tile on invalid coordinate {}!", Position(x, y, z)));
		return;
	}

	QTreeLeafNode::newLeaf = false;
	QTreeLeafNode* leaf = root.createLeaf(x, y, 15);

	if (QTreeLeafNode::newLeaf) {
		// update north
		QTreeLeafNode* northLeaf = root.getLeaf(x, y - FLOOR_SIZE);
		if (northLeaf) {
			northLeaf->leafS = leaf;
		}

		// update west leaf
		QTreeLeafNode* westLeaf = root.getLeaf(x - FLOOR_SIZE, y);
		if (westLeaf) {
			westLeaf->leafE = leaf;
		}

		// update south
		QTreeLeafNode* southLeaf = root.getLeaf(x, y + FLOOR_SIZE);
		if (southLeaf) {
			leaf->leafS = southLeaf;
		}

		// update east
		QTreeLeafNode* eastLeaf = root.getLeaf(x + FLOOR_SIZE, y);
		if (eastLeaf) {
			leaf->leafE = eastLeaf;
		}
	}

	Floor* floor = leaf->createFloor(z);
	uint32_t offsetX = x & FLOOR_MASK;
	uint32_t offsetY = y & FLOOR_MASK;

	auto& tilePair = floor->tiles[offsetX][offsetY];
	auto& tile = tilePair.first;
	if (tile) {
		TileItemVector* items = newTile->getItemList();
		if (items) {
			for (auto it = items->rbegin(), end = items->rend(); it != end; ++it) {
				tile->addThing(*it);
			}
			items->clear();
		}

		Item* ground = newTile->getGround();
		if (ground) {
			tile->addThing(ground);
			newTile->setGround(nullptr);
		}
		// newTile is automatically deleted when unique_ptr goes out of scope
	} else {
		tile = std::move(newTile);
		// Clear cache since we now have real tile
		tilePair.second = nullptr;
	}
}

void Map::removeTile(uint16_t x, uint16_t y, uint8_t z)
{
	if (z >= MAP_MAX_LAYERS) {
		return;
	}

	const QTreeLeafNode* leaf = QTreeNode::getLeafStatic<const QTreeLeafNode*, const QTreeNode*>(&root, x, y);
	if (!leaf) {
		return;
	}

	Floor* floor = const_cast<Floor*>(leaf->getFloor(z));
	if (!floor) {
		return;
	}

	auto& tilePair = floor->tiles[x & FLOOR_MASK][y & FLOOR_MASK];
	auto& tile = tilePair.first;
	if (tile) {
		if (const CreatureVector* creatures = tile->getCreatures()) {
			for (int32_t i = creatures->size(); --i >= 0;) {
				if (Player* player = (*creatures)[i]->getPlayer()) {
					g_game.internalTeleport(player, player->getTown()->getTemplePosition(), false, FLAG_NOLIMIT);
				} else {
					g_game.removeCreature((*creatures)[i]);
				}
			}
		}

		if (TileItemVector* items = tile->getItemList()) {
			for (auto it = items->begin(), end = items->end(); it != end; ++it) {
				g_game.internalRemoveItem(*it);
			}
		}

		Item* ground = tile->getGround();
		if (ground) {
			g_game.internalRemoveItem(ground);
			tile->setGround(nullptr);
		}
		
		// Reset unique_ptr to delete the tile
		tile.reset();
	}
	
	// Also clear cache
	tilePair.second = nullptr;
}

bool Map::placeCreature(const Position& centerPos, Creature* creature, bool extendedPos /* = false*/,
                        bool forceLogin /* = false*/)
{
	bool foundTile;
	bool placeInPZ;

	Tile* tile = getTile(centerPos.x, centerPos.y, centerPos.z);
	if (tile) {
		placeInPZ = tile->hasFlag(TILESTATE_PROTECTIONZONE);
        uint32_t flags = FLAG_IGNOREBLOCKITEM;
    ReturnValue ret = tile->queryAdd(0, *creature, 1, flags);
    foundTile = forceLogin || ret == RETURNVALUE_NOERROR || ret == RETURNVALUE_PLAYERISNOTINVITED || ret == RETURNVALUE_ONLYGUILDMEMBERSMAYENTER;
	} else {
		placeInPZ = false;
		foundTile = false;
	}

	if (!foundTile) {
		static std::vector<std::pair<int32_t, int32_t>> extendedRelList{
		    {0, -2}, {2, 0}, {0, 2}, {-2, 0}, {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};

		static std::vector<std::pair<int32_t, int32_t>> normalRelList{{-1, -1}, {0, -1}, {1, -1}, {-1, 0},
		                                                              {1, 0},   {-1, 1}, {0, 1},  {1, 1}};

		std::vector<std::pair<int32_t, int32_t>>& relList = (extendedPos ? extendedRelList : normalRelList);

		if (extendedPos) {
			std::shuffle(relList.begin(), relList.begin() + 4, getRandomGenerator());
			std::shuffle(relList.begin() + 4, relList.end(), getRandomGenerator());
		} else {
			std::shuffle(relList.begin(), relList.end(), getRandomGenerator());
		}

		for (const auto& it : relList) {
			Position tryPos(centerPos.x + it.first, centerPos.y + it.second, centerPos.z);

			tile = getTile(tryPos.x, tryPos.y, tryPos.z);
			if (!tile || (placeInPZ && !tile->hasFlag(TILESTATE_PROTECTIONZONE))) {
				continue;
			}

			if (tile->queryAdd(0, *creature, 1, 0) == RETURNVALUE_NOERROR) {
				if (!extendedPos || isSightClear(centerPos, tryPos, false)) {
					foundTile = true;
					break;
				}
			}
		}

		if (!foundTile) {
			return false;
		}
	}

	int32_t index = 0;
	uint32_t flags = 0;
	Item* toItem = nullptr;

	Cylinder* toCylinder = tile->queryDestination(index, *creature, &toItem, flags);
	toCylinder->internalAddThing(creature);

	const Position& dest = toCylinder->getPosition();
	getQTNode(dest.x, dest.y)->addCreature(creature);
	return true;
}

void Map::moveCreature(Creature& creature, Tile& newTile, bool forceTeleport /* = false*/)
{
	Tile& oldTile = *creature.getTile();

	// If the tile does not have the creature it means that the creature is ready for elimination, we skip the move.
	if (!oldTile.hasCreature(&creature)) {
		return;
	}

	Position oldPos = oldTile.getPosition();
	Position newPos = newTile.getPosition();

	bool teleport = forceTeleport || !newTile.getGround() || !oldPos.isInRange(newPos, 1, 1, 0);

	SpectatorVec spectators, newPosSpectators;
	getSpectators(spectators, oldPos, true);
	getSpectators(newPosSpectators, newPos, true);
	spectators.addSpectators(newPosSpectators);

	std::vector<int32_t> oldStackPosVector;
	for (Creature* spectator : spectators) {
		if (Player* tmpPlayer = spectator->getPlayer()) {
			if (tmpPlayer->canSeeCreature(&creature)) {
				oldStackPosVector.push_back(oldTile.getClientIndexOfCreature(tmpPlayer, &creature));
			} else {
				oldStackPosVector.push_back(-1);
			}
		}
	}

	// remove the creature
	oldTile.removeThing(&creature, 0);

	QTreeLeafNode* leaf = getQTNode(oldPos.x, oldPos.y);
	QTreeLeafNode* new_leaf = getQTNode(newPos.x, newPos.y);

	// Switch the node ownership
	if (leaf != new_leaf) {
		leaf->removeCreature(&creature);
		new_leaf->addCreature(&creature);
	}

	// add the creature
	newTile.addThing(&creature);

	if (!teleport) {
		if (oldPos.y > newPos.y) {
			creature.setDirection(DIRECTION_NORTH);
		} else if (oldPos.y < newPos.y) {
			creature.setDirection(DIRECTION_SOUTH);
		}

		if (oldPos.x < newPos.x) {
			creature.setDirection(DIRECTION_EAST);
		} else if (oldPos.x > newPos.x) {
			creature.setDirection(DIRECTION_WEST);
		}
	}

	// send to client
	size_t i = 0;
	for (Creature* spectator : spectators) {
		if (Player* tmpPlayer = spectator->getPlayer()) {
			// Use the correct stackpos
			int32_t oldStackPos = oldStackPosVector[i++];
			if (oldStackPos != -1) {
				tmpPlayer->sendCreatureMove(&creature, newPos, newTile.getClientIndexOfCreature(tmpPlayer, &creature),
				                            oldPos, oldStackPos, teleport);
			}
		}
	}

	// event method
	for (Creature* spectator : spectators) {
		spectator->onCreatureMove(&creature, &newTile, newPos, &oldTile, oldPos, teleport);
	}

	oldTile.postRemoveNotification(&creature, &newTile, 0);
	newTile.postAddNotification(&creature, &oldTile, 0);
}

void Map::getSpectatorsInternal(SpectatorVec& spectators, const Position& centerPos, int32_t minRangeX,
                                int32_t maxRangeX, int32_t minRangeY, int32_t maxRangeY, int32_t minRangeZ,
                                int32_t maxRangeZ, bool onlyPlayers, bool onlyMonsters, bool onlyNpcs) const
{
	auto min_y = centerPos.y + minRangeY;
	auto min_x = centerPos.x + minRangeX;
	auto max_y = centerPos.y + maxRangeY;
	auto max_x = centerPos.x + maxRangeX;

	int32_t minoffset = centerPos.getZ() - maxRangeZ;
	uint16_t x1 = static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, std::max<int32_t>(0, (min_x + minoffset))));
	uint16_t y1 = static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, std::max<int32_t>(0, (min_y + minoffset))));

	int32_t maxoffset = centerPos.getZ() - minRangeZ;
	uint16_t x2 = static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, std::max<int32_t>(0, (max_x + maxoffset))));
	uint16_t y2 = static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, std::max<int32_t>(0, (max_y + maxoffset))));

	int32_t startx1 = x1 - (x1 % FLOOR_SIZE);
	int32_t starty1 = y1 - (y1 % FLOOR_SIZE);
	int32_t endx2 = x2 - (x2 % FLOOR_SIZE);
	int32_t endy2 = y2 - (y2 % FLOOR_SIZE);

	const QTreeLeafNode* startLeaf =
	    QTreeNode::getLeafStatic<const QTreeLeafNode*, const QTreeNode*>(&root, startx1, starty1);
	const QTreeLeafNode* leafS = startLeaf;
	const QTreeLeafNode* leafE;

	for (int_fast32_t ny = starty1; ny <= endy2; ny += FLOOR_SIZE) {
		leafE = leafS;
		for (int_fast32_t nx = startx1; nx <= endx2; nx += FLOOR_SIZE) {
			if (leafE) {
				const CreatureVector& node_list = (onlyPlayers ? leafE->player_list : leafE->creature_list);
				for (Creature* creature : node_list) {
					if (!creature || creature->isRemoved()) {
						continue;
					}

					if (onlyMonsters && !creature->getMonster()) {
						continue;
					}

					if (onlyNpcs && !creature->getNpc()) {
						continue;
					}

					const Position& cpos = creature->getPosition();
					if (minRangeZ > cpos.z || maxRangeZ < cpos.z) {
						continue;
					}

					int16_t offsetZ = centerPos.getOffsetZ(cpos);
					if ((min_y + offsetZ) > cpos.y || (max_y + offsetZ) < cpos.y || (min_x + offsetZ) > cpos.x ||
					    (max_x + offsetZ) < cpos.x) {
						continue;
					}

					spectators.emplace_back(creature);
				}
				leafE = leafE->leafE;
			} else {
				leafE = QTreeNode::getLeafStatic<const QTreeLeafNode*, const QTreeNode*>(&root, nx + FLOOR_SIZE, ny);
			}
		}

		if (leafS) {
			leafS = leafS->leafS;
		} else {
			leafS = QTreeNode::getLeafStatic<const QTreeLeafNode*, const QTreeNode*>(&root, startx1, ny + FLOOR_SIZE);
		}
	}
}

void Map::getSpectators(SpectatorVec& spectators, const Position& centerPos, bool multifloor /*= false*/,
                        bool onlyPlayers /*= false*/, int32_t minRangeX /*= 0*/, int32_t maxRangeX /*= 0*/,
                        int32_t minRangeY /*= 0*/, int32_t maxRangeY /*= 0*/, bool onlyMonsters /*= false*/, bool onlyNpcs /*= false*/)
{
	if (centerPos.z >= MAP_MAX_LAYERS) {
		return;
	}

	minRangeX = (minRangeX == 0 ? -maxViewportX : -minRangeX);
	maxRangeX = (maxRangeX == 0 ? maxViewportX : maxRangeX);
	minRangeY = (minRangeY == 0 ? -maxViewportY : -minRangeY);
	maxRangeY = (maxRangeY == 0 ? maxViewportY : maxRangeY);

	bool cacheResult = (minRangeX == -maxViewportX && maxRangeX == maxViewportX &&
	                    minRangeY == -maxViewportY && maxRangeY == maxViewportY);

	SpectatorVec* cacheOpt = nullptr;
	bool* hasCacheOpt = nullptr;

	if (cacheResult) {
		auto iter = spectatorsCache.find(centerPos);
		if (iter != spectatorsCache.end()) {
			auto& entry = iter->second;
			if (entry.minRangeX == minRangeX && entry.maxRangeX == maxRangeX && entry.minRangeY == minRangeY &&
			    entry.maxRangeY == maxRangeY) {
				SpectatorsCache::FloorData* cacheFloorData;
				if (onlyPlayers) {
					cacheFloorData = &entry.players;
				} else if (onlyMonsters) {
					cacheFloorData = &entry.monsters;
				} else if (onlyNpcs) {
					cacheFloorData = &entry.npcs;
				} else {
					cacheFloorData = &entry.creatures;
				}

				if (multifloor) {
					cacheOpt = &cacheFloorData->multiFloor;
					hasCacheOpt = &cacheFloorData->hasMultiFloor;
				} else {
					cacheOpt = &cacheFloorData->floor;
					hasCacheOpt = &cacheFloorData->hasFloor;
				}

				if (*hasCacheOpt) {
					if (!spectators.empty()) {
						spectators.addSpectators(*cacheOpt);
					} else {
						spectators = *cacheOpt;
					}
					return;
				}
			} else {
				spectatorsCache.erase(iter);
			}
		}
	}

	int32_t minRangeZ;
	int32_t maxRangeZ;

	if (multifloor) {
		if (centerPos.z > 7) {
			// underground (8->15) — ±2 floors
			minRangeZ = std::max(centerPos.getZ() - 2, 0);
			maxRangeZ = std::min(centerPos.getZ() + 2, MAP_MAX_LAYERS - 1);
		} else {
			// above ground — limit to ±2 floors
			minRangeZ = std::max(centerPos.getZ() - 2, 0);
			maxRangeZ = std::min(centerPos.getZ() + 2, 7);
		}
	} else {
		minRangeZ = centerPos.z;
		maxRangeZ = centerPos.z;
	}

	getSpectatorsInternal(spectators, centerPos, minRangeX, maxRangeX, minRangeY, maxRangeY, minRangeZ, maxRangeZ,
	                      onlyPlayers, onlyMonsters, onlyNpcs);

	if (cacheResult) {
		auto& entry = spectatorsCache[centerPos];
		entry.minRangeX = minRangeX;
		entry.maxRangeX = maxRangeX;
		entry.minRangeY = minRangeY;
		entry.maxRangeY = maxRangeY;

		SpectatorsCache::FloorData* cacheFloorData;
		if (onlyPlayers) {
			cacheFloorData = &entry.players;
		} else if (onlyMonsters) {
			cacheFloorData = &entry.monsters;
		} else if (onlyNpcs) {
			cacheFloorData = &entry.npcs;
		} else {
			cacheFloorData = &entry.creatures;
		}

		if (multifloor) {
			cacheFloorData->hasMultiFloor = true;
			cacheFloorData->multiFloor = spectators;
		} else {
			cacheFloorData->hasFloor = true;
			cacheFloorData->floor = spectators;
		}
	}
}

void Map::clearSpectatorCache() { spectatorsCache.clear(); }

bool Map::canThrowObjectTo(const Position& fromPos, const Position& toPos, bool checkLineOfSight /*= true*/,
                           bool sameFloor /*= false*/, int32_t rangex /*= Map::maxClientViewportX*/,
                           int32_t rangey /*= Map::maxClientViewportY*/) const
{
	if (fromPos.getDistanceX(toPos) > rangex || fromPos.getDistanceY(toPos) > rangey) {
		return false;
	}

	return !checkLineOfSight || isSightClear(fromPos, toPos, sameFloor);
}

bool Map::isTileClear(uint16_t x, uint16_t y, uint8_t z, bool blockFloor /*= false*/) const
{
	const Tile* tile = getTile(x, y, z);
	if (!tile) {
		return true;
	}

	if (blockFloor && tile->getGround()) {
		return false;
	}

	return !tile->hasProperty(CONST_PROP_BLOCKPROJECTILE);
}

namespace {

[[maybe_unused]] bool checkSteepLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t z)
{
	float dx = x1 - x0;
	float slope = (dx == 0) ? 1 : (y1 - y0) / dx;
	float yi = y0 + slope;

	for (uint16_t x = x0 + 1; x < x1; ++x) {
		// 0.1 is necessary to avoid loss of precision during calculation
		if (!g_game.map.isTileClear(std::floor(yi + 0.1), x, z)) {
			return false;
		}
		yi += slope;
	}

	return true;
}

[[maybe_unused]] bool checkSlightLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t z)
{
	float dx = x1 - x0;
	float slope = (dx == 0) ? 1 : (y1 - y0) / dx;
	float yi = y0 + slope;

	for (uint16_t x = x0 + 1; x < x1; ++x) {
		// 0.1 is necessary to avoid loss of precision during calculation
		if (!g_game.map.isTileClear(x, std::floor(yi + 0.1), z)) {
			return false;
		}
		yi += slope;
	}

	return true;
}

} // namespace

bool Map::checkSightLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t z) const
{
    if (x0 == x1 && y0==y1) {
        return true;
    }

    Position start(x0,y0,z);
    Position destination(x1,y1,z);

    const int8_t mx = start.x < destination.x ? 1 : start.x == destination.x ? 0
        : -1;
    const int8_t my = start.y < destination.y ? 1 : start.y == destination.y ? 0
        : -1;

    int32_t A = destination.getOffsetY(start);
    int32_t B = start.getOffsetX(destination);
    int32_t C = -(A * destination.x + B * destination.y);

    while (start.x != destination.x || start.y != destination.y) {
        int32_t move_hor = std::abs(A * (start.x + mx) + B * (start.y) + C);
        int32_t move_ver = std::abs(A * (start.x) + B * (start.y + my) + C);
        int32_t move_cross = std::abs(A * (start.x + mx) + B * (start.y + my) + C);

        if (start.y != destination.y && (start.x == destination.x || move_hor > move_ver || move_hor > move_cross)) {
            start.y += my;
        }

        if (start.x != destination.x && (start.y == destination.y || move_ver > move_hor || move_ver > move_cross)) {
            start.x += mx;
        }

        if (!g_game.map.isTileClear(start.x, start.y, start.z)) {
            return false;
        }
    }

    return true;
}

bool Map::isSightClear(const Position& fromPos, const Position& toPos, bool sameFloor /*= false*/) const
{
	// target is on the same floor
	if (fromPos.z == toPos.z) {
		// skip checks if toPos is next to us
		if (fromPos.getDistanceX(toPos) < 2 && fromPos.getDistanceY(toPos) < 2) {
			return true;
		}

		// sight is clear or sameFloor is enabled
		bool sightClear = checkSightLine(fromPos.x, fromPos.y, toPos.x, toPos.y, fromPos.z) || checkSightLine(toPos.x, toPos.y, fromPos.x, fromPos.y, fromPos.z);
		if (sightClear || sameFloor) {
			return sightClear;
		}

		// no obstacles above floor 0 so we can throw above the obstacle
		if (fromPos.z == 0) {
			return true;
		}

		// check if tiles above us and the target are clear and check for a clear sight between them
		uint8_t newZ = fromPos.z - 1;
		return isTileClear(fromPos.x, fromPos.y, newZ, true) && isTileClear(toPos.x, toPos.y, newZ, true) &&
		       checkSightLine(fromPos.x, fromPos.y, toPos.x, toPos.y, newZ);
	}

	// target is on a different floor
	if (sameFloor) {
		return false;
	}

	// skip checks for sight line in case fromPos and toPos cross the ground floor
	if ((fromPos.z < 8 && toPos.z > 7) || (fromPos.z > 7 && toPos.z < 8)) {
		return false;
	}

	// target is above us
	if (fromPos.z > toPos.z) {
		if (fromPos.getDistanceZ(toPos) > 1) {
			return false;
		}

		// check a tile above us and the path to the target
		uint8_t newZ = fromPos.z - 1;
		return isTileClear(fromPos.x, fromPos.y, newZ, true) &&
		       checkSightLine(fromPos.x, fromPos.y, toPos.x, toPos.y, newZ);
	}

	// target is below us check if tiles above the target are clear
	for (uint8_t z = fromPos.z; z < toPos.z; ++z) {
		if (!isTileClear(toPos.x, toPos.y, z, true)) {
			return false;
		}
	}

	// check if we can throw to the tile above the target
	return checkSightLine(fromPos.x, fromPos.y, toPos.x, toPos.y, fromPos.z);
}

const Tile* Map::canWalkTo(const Creature& creature, const Position& pos) const
{
	Tile* tile = getTile(pos.x, pos.y, pos.z);
	if (creature.getTile() != tile) {
		if (!tile) {
			return nullptr;
		}

		uint32_t flags = FLAG_PATHFINDING;
		if (!creature.getPlayer()) {
			flags |= FLAG_IGNOREFIELDDAMAGE;
		}

		if (tile->queryAdd(0, creature, 1, flags) != RETURNVALUE_NOERROR) {
			return nullptr;
		}
	}
	return tile;
}

bool Map::getPathMatching(const Creature& creature, std::vector<Direction>& dirList,
                          const FrozenPathingConditionCall& pathCondition, const FindPathParams& fpp) const
{
	Position pos = creature.getPosition();
	Position endPos;

	AStarNodes nodes(pos.x, pos.y);

	int32_t bestMatch = 0;

	static int_fast32_t dirNeighbors[8][5][2] = {
	    {{-1, 0}, {0, 1}, {1, 0}, {1, 1}, {-1, 1}},    {{-1, 0}, {0, 1}, {0, -1}, {-1, -1}, {-1, 1}},
	    {{-1, 0}, {1, 0}, {0, -1}, {-1, -1}, {1, -1}}, {{0, 1}, {1, 0}, {0, -1}, {1, -1}, {1, 1}},
	    {{1, 0}, {0, -1}, {-1, -1}, {1, -1}, {1, 1}},  {{-1, 0}, {0, -1}, {-1, -1}, {1, -1}, {-1, 1}},
	    {{0, 1}, {1, 0}, {1, -1}, {1, 1}, {-1, 1}},    {{-1, 0}, {0, 1}, {-1, -1}, {1, 1}, {-1, 1}}};
	static int_fast32_t allNeighbors[8][2] = {{-1, 0}, {0, 1}, {1, 0}, {0, -1}, {-1, -1}, {1, -1}, {1, 1}, {-1, 1}};

	const Position startPos = pos;

	AStarNode* found = nullptr;
	while (fpp.maxSearchDist != 0 || nodes.getClosedNodes() < 100) {
		AStarNode* n = nodes.getBestNode();
		if (!n) {
			if (found) {
				break;
			}
			return false;
		}

		const int_fast32_t x = n->x;
		const int_fast32_t y = n->y;
		pos.x = static_cast<uint16_t>(x);
		pos.y = static_cast<uint16_t>(y);
		if (pathCondition(startPos, pos, fpp, bestMatch)) {
			found = n;
			endPos = pos;
			if (bestMatch == 0) {
				break;
			}
		}

		uint_fast32_t dirCount;
		int_fast32_t* neighbors;
		if (n->parent) {
			const int_fast32_t offset_x = n->parent->x - x;
			const int_fast32_t offset_y = n->parent->y - y;
			if (offset_y == 0) {
				if (offset_x == -1) {
					neighbors = *dirNeighbors[DIRECTION_WEST];
				} else {
					neighbors = *dirNeighbors[DIRECTION_EAST];
				}
			} else if (!fpp.allowDiagonal || offset_x == 0) {
				if (offset_y == -1) {
					neighbors = *dirNeighbors[DIRECTION_NORTH];
				} else {
					neighbors = *dirNeighbors[DIRECTION_SOUTH];
				}
			} else if (offset_y == -1) {
				if (offset_x == -1) {
					neighbors = *dirNeighbors[DIRECTION_NORTHWEST];
				} else {
					neighbors = *dirNeighbors[DIRECTION_NORTHEAST];
				}
			} else if (offset_x == -1) {
				neighbors = *dirNeighbors[DIRECTION_SOUTHWEST];
			} else {
				neighbors = *dirNeighbors[DIRECTION_SOUTHEAST];
			}
			dirCount = fpp.allowDiagonal ? 5 : 3;
		} else {
			dirCount = 8;
			neighbors = *allNeighbors;
		}

		const int_fast32_t f = n->f;
		for (uint_fast32_t i = 0; i < dirCount; ++i) {
			pos.x = x + *neighbors++;
			pos.y = y + *neighbors++;

			if (fpp.maxSearchDist != 0 &&
			    (startPos.getDistanceX(pos) > fpp.maxSearchDist || startPos.getDistanceY(pos) > fpp.maxSearchDist)) {
				continue;
			}

			if (fpp.keepDistance && !pathCondition.isInRange(startPos, pos, fpp)) {
				continue;
			}

			const Tile* tile;
			AStarNode* neighborNode = nodes.getNodeByPosition(pos.x, pos.y);
			if (neighborNode) {
				tile = getTile(pos.x, pos.y, pos.z);
			} else {
				tile = canWalkTo(creature, pos);
				if (!tile) {
					continue;
				}
			}

			// The cost (g) for this neighbor
			const int_fast32_t cost = AStarNodes::getMapWalkCost(n, pos);
			const int_fast32_t extraCost = AStarNodes::getTileWalkCost(creature, tile);
			const int_fast32_t newf = f + cost + extraCost;

			if (neighborNode) {
				if (neighborNode->f <= newf) {
					// The node on the closed/open list is cheaper than this one
					continue;
				}

				neighborNode->f = newf;
				neighborNode->parent = n;
				nodes.openNode(neighborNode);
			} else {
				// Does not exist in the open/closed list, create a new node
				neighborNode = nodes.createOpenNode(n, pos.x, pos.y, newf);
				if (!neighborNode) {
					if (found) {
						break;
					}
					return false;
				}
			}
		}

		nodes.closeNode(n);
	}

	if (!found) {
		return false;
	}

	int32_t prevx = endPos.getX();
	int32_t prevy = endPos.getY();

	found = found->parent;
	while (found) {
		pos.x = found->x;
		pos.y = found->y;

		int32_t dx = pos.getX() - prevx;
		int32_t dy = pos.getY() - prevy;

		prevx = pos.x;
		prevy = pos.y;

		if (dx == 1 && dy == 1) {
			dirList.push_back(DIRECTION_NORTHWEST);
		} else if (dx == -1 && dy == 1) {
			dirList.push_back(DIRECTION_NORTHEAST);
		} else if (dx == 1 && dy == -1) {
			dirList.push_back(DIRECTION_SOUTHWEST);
		} else if (dx == -1 && dy == -1) {
			dirList.push_back(DIRECTION_SOUTHEAST);
		} else if (dx == 1) {
			dirList.push_back(DIRECTION_WEST);
		} else if (dx == -1) {
			dirList.push_back(DIRECTION_EAST);
		} else if (dy == 1) {
			dirList.push_back(DIRECTION_NORTH);
		} else if (dy == -1) {
			dirList.push_back(DIRECTION_SOUTH);
		}

		found = found->parent;
	}
	return true;
}

// AStarNodes

AStarNodes::AStarNodes(uint32_t x, uint32_t y) : nodes(), openNodes()
{
	curNode = 1;
	closedNodes = 0;
	openNodes[0] = true;

	AStarNode& startNode = nodes[0];
	startNode.parent = nullptr;
	startNode.x = static_cast<uint16_t>(x);
	startNode.y = static_cast<uint16_t>(y);
	startNode.f = 0;
	nodeTable[(x << 16) | y] = nodes;
}

AStarNode* AStarNodes::createOpenNode(AStarNode* parent, uint32_t x, uint32_t y, int_fast32_t f)
{
	if (curNode >= MAX_NODES) {
		return nullptr;
	}

	size_t retNode = curNode++;
	openNodes[retNode] = true;

	AStarNode* node = nodes + retNode;
	nodeTable[(x << 16) | y] = node;
	node->parent = parent;
	node->x = static_cast<uint16_t>(x);
	node->y = static_cast<uint16_t>(y);
	node->f = f;
	return node;
}

AStarNode* AStarNodes::getBestNode()
{
	if (curNode == 0) {
		return nullptr;
	}

	int32_t best_node_f = std::numeric_limits<int32_t>::max();
	int32_t best_node = -1;
	for (size_t i = 0; i < curNode; i++) {
		if (openNodes[i] && nodes[i].f < best_node_f) {
			best_node_f = nodes[i].f;
			best_node = i;
		}
	}

	if (best_node >= 0) {
		return nodes + best_node;
	}
	return nullptr;
}

void AStarNodes::closeNode(AStarNode* node)
{
	size_t index = node - nodes;
	assert(index < MAX_NODES);
	openNodes[index] = false;
	++closedNodes;
}

void AStarNodes::openNode(AStarNode* node)
{
	size_t index = node - nodes;
	assert(index < MAX_NODES);
	if (!openNodes[index]) {
		openNodes[index] = true;
		--closedNodes;
	}
}

int_fast32_t AStarNodes::getClosedNodes() const { return closedNodes; }

AStarNode* AStarNodes::getNodeByPosition(uint32_t x, uint32_t y)
{
	auto it = nodeTable.find((x << 16) | y);
	if (it == nodeTable.end()) {
		return nullptr;
	}
	return it->second;
}

int_fast32_t AStarNodes::getMapWalkCost(AStarNode* node, const Position& neighborPos)
{
	if (std::abs(node->x - neighborPos.x) == std::abs(node->y - neighborPos.y)) {
		// diagonal movement extra cost
		return MAP_DIAGONALWALKCOST;
	}
	return MAP_NORMALWALKCOST;
}

int_fast32_t AStarNodes::getTileWalkCost(const Creature& creature, const Tile* tile)
{
	int_fast32_t cost = 0;
	if (tile->getTopVisibleCreature(&creature)) {
		// destroy creature cost
		cost += MAP_NORMALWALKCOST * 3;
	}

	if (const MagicField* field = tile->getFieldItem()) {
		CombatType_t combatType = field->getCombatType();
		const Monster* monster = creature.getMonster();
		if (!creature.isImmune(combatType) && !creature.hasCondition(Combat::DamageToConditionType(combatType)) &&
		    (monster && !monster->canWalkOnFieldType(combatType))) {
			cost += MAP_NORMALWALKCOST * 18;
		}
	}
	return cost;
}

// Floor destructor removed - unique_ptr handles cleanup automatically

// QTreeNode destructor removed - unique_ptr handles cleanup automatically

QTreeLeafNode* QTreeNode::getLeaf(uint32_t x, uint32_t y)
{
	if (leaf) {
		return static_cast<QTreeLeafNode*>(this);
	}

	QTreeNode* node = child[((x & 0x8000) >> 15) | ((y & 0x8000) >> 14)].get();
	if (!node) {
		return nullptr;
	}
	return node->getLeaf(x << 1, y << 1);
}

QTreeLeafNode* QTreeNode::createLeaf(uint32_t x, uint32_t y, uint32_t level)
{
	if (!isLeaf()) {
		uint32_t index = ((x & 0x8000) >> 15) | ((y & 0x8000) >> 14);
		if (!child[index]) {
			if (level != FLOOR_BITS) {
				child[index] = std::make_unique<QTreeNode>();
			} else {
				child[index] = std::make_unique<QTreeLeafNode>();
				QTreeLeafNode::newLeaf = true;
			}
		}
		return child[index]->createLeaf(x * 2, y * 2, level - 1);
	}
	return static_cast<QTreeLeafNode*>(this);
}

// QTreeLeafNode
bool QTreeLeafNode::newLeaf = false;

// QTreeLeafNode destructor removed - unique_ptr handles cleanup automatically

Floor* QTreeLeafNode::createFloor(uint32_t z)
{
	if (!array[z]) {
		array[z] = std::make_unique<Floor>();
	}
	return array[z].get();
}

void QTreeLeafNode::addCreature(Creature* c)
{
	creature_list.push_back(c);

	if (c->getPlayer()) {
		player_list.push_back(c);
	}
}

void QTreeLeafNode::removeCreature(Creature* c)
{
	auto iter = std::find(creature_list.begin(), creature_list.end(), c);
	assert(iter != creature_list.end());
	*iter = creature_list.back();
	creature_list.pop_back();

	if (c->getPlayer()) {
		iter = std::find(player_list.begin(), player_list.end(), c);
		assert(iter != player_list.end());
		*iter = player_list.back();
		player_list.pop_back();
	}
}

uint32_t Map::clean() const
{
	uint64_t start = OTSYS_TIME();
	size_t tiles = 0;

	if (g_game.getGameState() == GAME_STATE_NORMAL) {
		g_game.setGameState(GAME_STATE_MAINTAIN);
	}

	std::vector<Item*> toRemove;

	for (auto tile : g_game.getTilesToClean()) {
		if (!tile) {
			continue;
		}

		if (auto items = tile->getItemList()) {
			++tiles;
			for (auto item : *items) {
				if (item->isCleanable()) {
					toRemove.emplace_back(item);
				}
			}
		}
	}

	for (auto item : toRemove) {
		g_game.internalRemoveItem(item, -1);
	}

	size_t count = toRemove.size();
	g_game.clearTilesToClean();

	if (g_game.getGameState() == GAME_STATE_MAINTAIN) {
		g_game.setGameState(GAME_STATE_NORMAL);
	}

	LOG_INFO(fmt::format("> CLEAN: Removed {} item{} from {} tile{} in {} seconds.", count, count != 1 ? "s" : "", tiles, tiles != 1 ? "s" : "", (OTSYS_TIME() - start) / (1000.)));
	return count;
}

// ============================================================================
// Floor methods for lazy loading
// ============================================================================

Tile* Floor::getTile(uint16_t x, uint16_t y, uint8_t z) {
	uint32_t offsetX = x & FLOOR_MASK;
	uint32_t offsetY = y & FLOOR_MASK;
	
	auto& tilePair = tiles[offsetX][offsetY];
	
	// If real tile exists, return it
	if (tilePair.first) {
		return tilePair.first.get();
	}
	
	// If no cache, return nullptr
	if (!tilePair.second) {
		return nullptr;
	}
	
	// Lazy loading: create real tile from cache
	const auto& basicTile = tilePair.second;
	
	tilePair.first = MapCacheUtils::createTileFromBasic(basicTile, x, y, z, g_game.map.houses);
	tilePair.second = nullptr; // Clear cache
	
	return tilePair.first.get();
}

void Floor::setTileCache(uint16_t x, uint16_t y, const std::shared_ptr<BasicTile>& basicTile) {
	uint32_t offsetX = x & FLOOR_MASK;
	uint32_t offsetY = y & FLOOR_MASK;
	tiles[offsetX][offsetY].second = basicTile;
}

std::shared_ptr<BasicTile> Floor::getTileCache(uint16_t x, uint16_t y) const {
	uint32_t offsetX = x & FLOOR_MASK;
	uint32_t offsetY = y & FLOOR_MASK;
	return tiles[offsetX][offsetY].second;
}

// ============================================================================
// Map::setBasicTile - store tile in cache during map load
// ============================================================================

void Map::setBasicTile(uint16_t x, uint16_t y, uint8_t z, const std::shared_ptr<BasicTile>& basicTile) {
	if (z >= MAP_MAX_LAYERS) {
		LOG_ERROR(fmt::format("ERROR: Attempt to set tile cache on invalid coordinate {}!", Position(x, y, z)));
		return;
	}

	QTreeLeafNode::newLeaf = false;
	QTreeLeafNode* leaf = root.createLeaf(x, y, 15);

	if (QTreeLeafNode::newLeaf) {
		// update north
		QTreeLeafNode* northLeaf = root.getLeaf(x, y - FLOOR_SIZE);
		if (northLeaf) {
			northLeaf->leafS = leaf;
		}

		// update west leaf
		QTreeLeafNode* westLeaf = root.getLeaf(x - FLOOR_SIZE, y);
		if (westLeaf) {
			westLeaf->leafE = leaf;
		}

		// update south
		QTreeLeafNode* southLeaf = root.getLeaf(x, y + FLOOR_SIZE);
		if (southLeaf) {
			leaf->leafS = southLeaf;
		}

		// update east
		QTreeLeafNode* eastLeaf = root.getLeaf(x + FLOOR_SIZE, y);
		if (eastLeaf) {
			leaf->leafE = eastLeaf;
		}
	}

	Floor* floor = leaf->createFloor(z);
	floor->setTileCache(x, y, basicTile);
}
