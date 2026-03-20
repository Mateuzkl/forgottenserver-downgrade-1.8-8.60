// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_MAP_H
#define FS_MAP_H

#include "otpch.h"

#include "house.h"
#include "mapcache.h"
#include "position.h"
#include "spawn.h"
#include "town.h"

#include <absl/container/flat_hash_map.h>
#include <cstring>
#include <unordered_map>

class Creature;

struct SpectatorsCache
{
	struct FloorData
	{
		bool hasFloor{false};
		bool hasMultiFloor{false};
		SpectatorVec floor;
		SpectatorVec multiFloor;
	};

	int32_t minRangeX{0};
	int32_t maxRangeX{0};
	int32_t minRangeY{0};
	int32_t maxRangeY{0};

	FloorData creatures;
	FloorData monsters;
	FloorData npcs;
	FloorData players;
};

struct PositionHasher
{
	std::size_t operator()(const Position& pos) const
	{
		std::size_t h = 0;
		hash_combine(h, pos.x);
		hash_combine(h, pos.y);
		hash_combine(h, pos.z);
		return h;
	}
};

inline constexpr int32_t MAP_MAX_LAYERS = 16;

struct FindPathParams;

struct AStarNode
{
	AStarNode* parent;
	int_fast32_t f;
	uint16_t x, y;
};

inline constexpr int32_t MAX_NODES = 512;
inline constexpr int32_t MAP_NORMALWALKCOST = 10;
inline constexpr int32_t MAP_DIAGONALWALKCOST = 25;

class AStarNodes
{
public:
	AStarNodes(uint32_t x, uint32_t y);

	AStarNode* createOpenNode(AStarNode* parent, uint32_t x, uint32_t y, int_fast32_t f);
	AStarNode* getBestNode();
	void closeNode(AStarNode* node);
	void openNode(AStarNode* node);
	int_fast32_t getClosedNodes() const;
	AStarNode* getNodeByPosition(uint32_t x, uint32_t y);

	static int_fast32_t getMapWalkCost(AStarNode* node, const Position& neighborPos);
	static int_fast32_t getTileWalkCost(const Creature& creature, const Tile* tile);

private:
	AStarNode nodes[MAX_NODES];
	bool openNodes[MAX_NODES];
	std::unordered_map<uint32_t, AStarNode*> nodeTable;
	size_t curNode;
	int_fast32_t closedNodes;
};

using SpectatorCache = absl::flat_hash_map<Position, SpectatorsCache, PositionHasher>;

inline constexpr int32_t FLOOR_BITS = 3;
inline constexpr int32_t FLOOR_SIZE = (1 << FLOOR_BITS);
inline constexpr int32_t FLOOR_MASK = (FLOOR_SIZE - 1);

struct Floor
{
	Floor() = default;
	~Floor() = default;

	// non-copyable
	Floor(const Floor&) = delete;
	Floor& operator=(const Floor&) = delete;

	// Pair: <Tile (real), BasicTile (cache)>
	// BasicTile is stored during map load, real Tile is created on first access (lazy loading)
	std::pair<std::unique_ptr<Tile>, std::shared_ptr<BasicTile>> tiles[FLOOR_SIZE][FLOOR_SIZE];

	// Get tile, creating from cache if needed (z is needed for tile creation)
	Tile* getTile(uint16_t x, uint16_t y, uint8_t z);

	// Set tile cache (during map load)
	void setTileCache(uint16_t x, uint16_t y, const std::shared_ptr<BasicTile>& basicTile);

	// Get tile cache
	std::shared_ptr<BasicTile> getTileCache(uint16_t x, uint16_t y) const;
};

class FrozenPathingConditionCall;
class QTreeLeafNode;

class QTreeNode
{
public:
	QTreeNode() = default;
	virtual ~QTreeNode() = default;

	// non-copyable
	QTreeNode(const QTreeNode&) = delete;
	QTreeNode& operator=(const QTreeNode&) = delete;

	bool isLeaf() const { return leaf; }

	QTreeLeafNode* getLeaf(uint32_t x, uint32_t y);

	template <typename Leaf, typename Node>
	static Leaf getLeafStatic(Node node, uint32_t x, uint32_t y)
	{
		do {
			node = node->child[((x & 0x8000) >> 15) | ((y & 0x8000) >> 14)].get();
			if (!node) {
				return nullptr;
			}

			x <<= 1;
			y <<= 1;
		} while (!node->leaf);
		return static_cast<Leaf>(node);
	}

	QTreeLeafNode* createLeaf(uint32_t x, uint32_t y, uint32_t level);

protected:
	bool leaf = false;

private:
	std::unique_ptr<QTreeNode> child[4];
	friend class Map;
};

class QTreeLeafNode final : public QTreeNode
{
public:
	QTreeLeafNode()
	{
		leaf = true;
		newLeaf = true;
	}

	~QTreeLeafNode() = default;

	// non-copyable
	QTreeLeafNode(const QTreeLeafNode&) = delete;
	QTreeLeafNode& operator=(const QTreeLeafNode&) = delete;

	Floor* createFloor(uint32_t z);
	Floor* getFloor(uint8_t z) const { return array[z].get(); }

	void addCreature(Creature* c);
	void removeCreature(Creature* c);

private:
	static bool newLeaf;
	QTreeLeafNode* leafS = nullptr;
	QTreeLeafNode* leafE = nullptr;
	std::unique_ptr<Floor> array[MAP_MAX_LAYERS];
	CreatureVector creature_list;
	CreatureVector player_list;

	friend class Map;
	friend class QTreeNode;
};

/**
 * Map class.
 * Holds all the actual map-data
 */
class Map
{
public:
	static constexpr int32_t maxViewportX = 11; // min value: maxClientViewportX + 1
	static constexpr int32_t maxViewportY = 11; // min value: maxClientViewportY + 1
	static constexpr int32_t maxClientViewportX = 8;
	static constexpr int32_t maxClientViewportY = 6;

	uint32_t clean() const;

	bool loadMap(const std::string& identifier, bool loadHouses);

	static bool save();

	/**
	 * Get a single tile.
	 * \returns A pointer to that tile.
	 */
	Tile* getTile(uint16_t x, uint16_t y, uint8_t z) const;
	Tile* getTile(const Position& pos) const { return getTile(pos.x, pos.y, pos.z); }

	/**
	 * Set a single tile.
	 */
	void setTile(uint16_t x, uint16_t y, uint8_t z, std::unique_ptr<Tile> newTile);
	void setTile(const Position& pos, std::unique_ptr<Tile> newTile)
	{
		setTile(pos.x, pos.y, pos.z, std::move(newTile));
	}

	// Backward compatibility wrapper - takes ownership of raw pointer
	void setTile(uint16_t x, uint16_t y, uint8_t z, Tile* newTile) { setTile(x, y, z, std::unique_ptr<Tile>(newTile)); }
	void setTile(const Position& pos, Tile* newTile) { setTile(pos.x, pos.y, pos.z, std::unique_ptr<Tile>(newTile)); }

	/**
	 * Set a tile cache (for lazy loading during map load)
	 */

	void setBasicTile(uint16_t x, uint16_t y, uint8_t z, const std::shared_ptr<BasicTile>& basicTile);
	void setBasicTile(const Position& pos, const std::shared_ptr<BasicTile>& basicTile)
	{
		setBasicTile(pos.x, pos.y, pos.z, basicTile);
	}

	/**
	 * Removes a single tile.
	 */

	void removeTile(uint16_t x, uint16_t y, uint8_t z);
	void removeTile(const Position& pos) { removeTile(pos.x, pos.y, pos.z); }

	/**
	 * Place a creature on the map
	 * \param centerPos The position to place the creature
	 * \param creature Creature to place on the map
	 * \param extendedPos If true, the creature will in first-hand be placed 2 tiles away
	 * \param forceLogin If true, placing the creature will not fail because of obstacles (creatures/chests)
	 */

	bool placeCreature(const Position& centerPos, Creature* creature, bool extendedPos = false,
	                   bool forceLogin = false);

	void moveCreature(Creature& creature, Tile& newTile, bool forceTeleport = false);

	void getSpectators(SpectatorVec& spectators, const Position& centerPos, bool multifloor = false,
	                   bool onlyPlayers = false, int32_t minRangeX = 0, int32_t maxRangeX = 0, int32_t minRangeY = 0,
	                   int32_t maxRangeY = 0, bool onlyMonsters = false, bool onlyNpcs = false);

	void clearSpectatorCache();

	/**
	 * Checks if you can throw an object to that position
	 * \param fromPos from Source point
	 * \param toPos Destination point
	 * \param rangex maximum allowed range horizontally
	 * \param rangey maximum allowed range vertically
	 * \param checkLineOfSight checks if there is any blocking objects in the way
	 * \param sameFloor checks if the destination is on same floor
	 * \returns The result if you can throw there or not
	 */

	bool canThrowObjectTo(const Position& fromPos, const Position& toPos, bool checkLineOfSight = true,
	                      bool sameFloor = false, int32_t rangex = Map::maxClientViewportX,
	                      int32_t rangey = Map::maxClientViewportY) const;

	/**
	 * Checks if there are no obstacles on that position
	 * \param blockFloor counts the ground tile as an obstacle
	 * \returns The result if there is an obstacle or not
	 */

	bool isTileClear(uint16_t x, uint16_t y, uint8_t z, bool blockFloor = false) const;

	/**
	 * Checks if path is clear from fromPos to toPos
	 * Notice: This only checks a straight line if the path is clear, for pathfinding use getPathTo.
	 * \param fromPos from Source point
	 * \param toPos Destination point
	 * \param sameFloor checks if the destination is on same floor
	 * \returns The result if there is no obstacles
	 */

	bool isSightClear(const Position& fromPos, const Position& toPos, bool sameFloor = false) const;

	bool checkSightLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t z) const;

	const Tile* canWalkTo(const Creature& creature, const Position& pos) const;

	bool getPathMatching(const Creature& creature, std::vector<Direction>& dirList,
	                     const FrozenPathingConditionCall& pathCondition, const FindPathParams& fpp) const;

	std::map<std::string, Position> waypoints;

	QTreeLeafNode* getQTNode(uint16_t x, uint16_t y)
	{
		return QTreeNode::getLeafStatic<QTreeLeafNode*, QTreeNode*>(&root, x, y);
	}

	Spawns spawns;
	Towns towns;
	Houses houses;

private:
	SpectatorCache spectatorsCache;

	QTreeNode root;

	std::filesystem::path spawnfile;
	std::filesystem::path housefile;

	uint32_t width = 0;
	uint32_t height = 0;

	void getSpectatorsInternal(SpectatorVec& spectators, const Position& centerPos, int32_t minRangeX,
	                           int32_t maxRangeX, int32_t minRangeY, int32_t maxRangeY, int32_t minRangeZ,
	                           int32_t maxRangeZ, bool onlyPlayers, bool onlyMonsters, bool onlyNpcs) const;

	friend class Game;
	friend class IOMap;
};

#endif // FS_MAP_H
