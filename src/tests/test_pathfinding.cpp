#include "../otpch.h"

#include "../creature.h"
#include "../map.h"
#include "../tile.h"
#include "test_support.h"

namespace {

class PathCreature final : public Creature
{
public:
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void setID() override {}
	void removeList() override {}
	void addList() override {}

private:
	std::string name = "pathfinding test creature";
};

class PathTile final : public DynamicTile
{
public:
	PathTile(uint16_t x, uint16_t y, uint8_t z, bool walkable) : DynamicTile(x, y, z), walkable(walkable) {}

	ReturnValue queryAdd(int32_t, const Thing&, uint32_t, uint32_t, Creature* = nullptr) const override
	{
		return walkable ? RETURNVALUE_NOERROR : RETURNVALUE_NOTPOSSIBLE;
	}

private:
	bool walkable;
};

using BlockedPositions = std::set<std::pair<uint16_t, uint16_t>>;

void addGrid(Map& map, uint16_t minX, uint16_t maxX, uint16_t minY, uint16_t maxY, const BlockedPositions& blocked = {})
{
	for (uint16_t y = minY; y <= maxY; ++y) {
		for (uint16_t x = minX; x <= maxX; ++x) {
			const bool walkable = !blocked.contains({x, y});
			map.setTile(x, y, 7, std::make_unique<PathTile>(x, y, 7, walkable));
		}
	}
}

FindPathParams exactPathParams(bool allowDiagonal = true)
{
	FindPathParams params;
	params.clearSight = false;
	params.allowDiagonal = allowDiagonal;
	params.maxSearchDist = 32;
	params.minTargetDist = 0;
	params.maxTargetDist = 0;
	return params;
}

bool findPath(Map& map, const Position& start, const Position& target, const FindPathParams& params,
              std::vector<Direction>& directions)
{
	auto creature = std::make_shared<PathCreature>();
	Tile* startTile = map.getTile(start);
	startTile->internalAddThing(creature.get());
	const bool found = map.getPathMatching(*creature, directions, FrozenPathingConditionCall(target), params);
	startTile->removeThing(creature.get(), 0);
	creature->setParent(nullptr);
	return found;
}

} // namespace

TEST_CASE(pathfinding_crosses_quadtree_leaf_boundaries)
{
	Map map;
	addGrid(map, 96, 112, 96, 104);
	std::vector<Direction> directions;
	CHECK(findPath(map, Position{97, 100, 7}, Position{111, 100, 7}, exactPathParams(false), directions));
	CHECK(directions.size() == 14);
}

TEST_CASE(pathfinding_routes_around_a_wall_through_a_gap)
{
	Map map;
	BlockedPositions wall;
	for (uint16_t y = 96; y <= 104; ++y) {
		if (y != 102) {
			wall.emplace(104, y);
		}
	}
	addGrid(map, 96, 112, 96, 104, wall);

	std::vector<Direction> directions;
	CHECK(findPath(map, Position{100, 100, 7}, Position{108, 100, 7}, exactPathParams(false), directions));
	CHECK(directions.size() > 8);
}

TEST_CASE(pathfinding_fails_when_a_wall_has_no_gap)
{
	Map map;
	BlockedPositions wall;
	for (uint16_t y = 96; y <= 104; ++y) {
		wall.emplace(104, y);
	}
	addGrid(map, 96, 112, 96, 104, wall);

	std::vector<Direction> directions;
	CHECK(!findPath(map, Position{100, 100, 7}, Position{108, 100, 7}, exactPathParams(false), directions));
	CHECK(directions.empty());
}

TEST_CASE(pathfinding_respects_diagonal_setting)
{
	Map map;
	addGrid(map, 200, 202, 200, 202, {{201, 200}, {200, 201}});

	std::vector<Direction> diagonalDirections;
	CHECK(findPath(map, Position{200, 200, 7}, Position{201, 201, 7}, exactPathParams(true), diagonalDirections));
	CHECK(diagonalDirections.size() == 1);

	std::vector<Direction> cardinalDirections;
	// The existing algorithm always checks all eight neighbors for the start
	// node; allowDiagonal only narrows later expansions. Preserve that behavior.
	CHECK(findPath(map, Position{200, 200, 7}, Position{201, 201, 7}, exactPathParams(false), cardinalDirections));
	CHECK(cardinalDirections.size() == 1);
}

TEST_CASE(pathfinding_same_start_and_target_returns_empty_path)
{
	Map map;
	addGrid(map, 300, 300, 300, 300);
	std::vector<Direction> directions;
	CHECK(findPath(map, Position{300, 300, 7}, Position{300, 300, 7}, exactPathParams(), directions));
	CHECK(directions.empty());
}

TFS_TEST_MAIN()
