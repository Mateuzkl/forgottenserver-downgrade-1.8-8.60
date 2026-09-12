#include "../otpch.h"

#include "../creature.h"
#include "../map.h"
#include "../performance_metrics.h"
#include "../tile.h"
#include "test_support.h"

#include <limits>

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

class PathMetricsFixture
{
public:
	PathMetricsFixture() : wasEnabled(g_performanceMetrics.isEnabled()) { g_performanceMetrics.setEnabled(true); }
	~PathMetricsFixture() { g_performanceMetrics.setEnabled(wasEnabled); }

	uint64_t getPathSteps() const { return g_performanceMetrics.getPathSteps(); }

private:
	bool wasEnabled;
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

FindPathParams summonPathParams()
{
	FindPathParams params;
	params.fullPathSearch = false;
	params.clearSight = false;
	params.maxSearchDist = 8;
	params.minTargetDist = 1;
	params.maxTargetDist = 2;
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

TEST_CASE(pathfinding_cross_floor_preserves_clear_sight_semantics)
{
	Map map;
	addGrid(map, 400, 410, 400, 404);

	auto params = exactPathParams(false);
	std::vector<Direction> clearSightDirections = {DIRECTION_NORTH};
	params.clearSight = true;
	CHECK(!findPath(map, Position{401, 402, 7}, Position{405, 402, 8}, params, clearSightDirections));
	CHECK(clearSightDirections == std::vector<Direction>{DIRECTION_NORTH});

	std::vector<Direction> noClearSightDirections;
	params.clearSight = false;
	CHECK(findPath(map, Position{401, 402, 7}, Position{405, 402, 8}, params, noClearSightDirections));
	CHECK(noClearSightDirections.size() == 4);

	std::vector<Direction> summonDirections;
	CHECK(findPath(map, Position{401, 402, 7}, Position{407, 402, 8}, summonPathParams(), summonDirections));
	CHECK(!summonDirections.empty());
}

TEST_CASE(pathfinding_handles_search_distance_boundaries_without_overflow)
{
	Map map;
	addGrid(map, 500, 508, 500, 504);

	auto params = exactPathParams(false);
	params.maxSearchDist = 0;
	std::vector<Direction> unlimitedDirections;
	CHECK(findPath(map, Position{501, 502, 7}, Position{505, 502, 7}, params, unlimitedDirections));

	params.maxSearchDist = 1;
	std::vector<Direction> limitedDirections;
	CHECK(!findPath(map, Position{501, 502, 7}, Position{503, 502, 7}, params, limitedDirections));

	params.maxSearchDist = -1;
	std::vector<Direction> negativeDirections;
	CHECK(!findPath(map, Position{501, 502, 7}, Position{502, 502, 7}, params, negativeDirections));

	params.maxSearchDist = 0;
	params.maxTargetDist = -1;
	std::vector<Direction> invalidTargetDistanceDirections;
	CHECK(!findPath(map, Position{501, 502, 7}, Position{502, 502, 7}, params,
	                invalidTargetDistanceDirections));

	params.maxSearchDist = std::numeric_limits<int32_t>::max();
	params.minTargetDist = 1;
	params.maxTargetDist = 1;
	std::vector<Direction> overflowBoundaryDirections;
	CHECK(findPath(map, Position{501, 502, 7}, Position{502, 502, 7}, params, overflowBoundaryDirections));
	CHECK(overflowBoundaryDirections.empty());
}

TEST_CASE(pathfinding_preserves_prefilled_direction_output_contract)
{
	Map map;
	addGrid(map, 600, 608, 600, 608, {{604, 600}, {604, 601}, {604, 602}, {604, 603}, {604, 604},
	                                                {604, 605}, {604, 606}, {604, 607}, {604, 608}});
	const std::vector<Direction> prefix = {DIRECTION_NORTH, DIRECTION_EAST};

	std::vector<Direction> successDirections = prefix;
	CHECK(findPath(map, Position{601, 601, 7}, Position{603, 601, 7}, exactPathParams(false), successDirections));
	CHECK(successDirections.size() == prefix.size() + 2);
	CHECK(std::equal(prefix.begin(), prefix.end(), successDirections.begin()));

	std::vector<Direction> searchFailureDirections = prefix;
	CHECK(!findPath(map, Position{601, 604, 7}, Position{607, 604, 7}, exactPathParams(false),
	                searchFailureDirections));
	CHECK(searchFailureDirections == prefix);

	std::vector<Direction> inRangeDirections = prefix;
	CHECK(findPath(map, Position{601, 601, 7}, Position{601, 601, 7}, exactPathParams(), inRangeDirections));
	CHECK(inRangeDirections == prefix);
}

TEST_CASE(pathfinding_metrics_count_only_steps_appended_by_successful_search)
{
	PathMetricsFixture metrics;
	const uint64_t initialPathSteps = metrics.getPathSteps();
	Map map;
	addGrid(map, 650, 654, 650, 652);
	std::vector<Direction> directions = {DIRECTION_NORTH, DIRECTION_EAST};

	CHECK(findPath(map, Position{651, 651, 7}, Position{653, 651, 7}, exactPathParams(false), directions));
	CHECK(directions.size() == 4);
	CHECK(metrics.getPathSteps() == initialPathSteps + 2);
}

TEST_CASE(pathfinding_metrics_count_zero_new_steps_when_already_in_range)
{
	PathMetricsFixture metrics;
	const uint64_t initialPathSteps = metrics.getPathSteps();
	Map map;
	addGrid(map, 660, 660, 660, 660);
	std::vector<Direction> directions = {DIRECTION_NORTH, DIRECTION_EAST};

	CHECK(findPath(map, Position{660, 660, 7}, Position{660, 660, 7}, exactPathParams(), directions));
	CHECK(directions.size() == 2);
	CHECK(metrics.getPathSteps() == initialPathSteps);
}

TEST_CASE(pathfinding_keeps_origin_tile_alive_for_const_creature_search)
{
	Map map;
	addGrid(map, 700, 702, 700, 702);
	auto creature = std::make_shared<PathCreature>();
	Tile* startTile = map.getTile(Position{700, 700, 7});
	startTile->internalAddThing(creature.get());

	const Creature& constCreature = *creature;
	const std::shared_ptr<const Tile> tileRef = constCreature.getTileShared();
	CHECK(tileRef.get() == startTile);

	std::vector<Direction> directions;
	CHECK(map.getPathMatching(constCreature, directions, FrozenPathingConditionCall(Position{702, 700, 7}),
	                          exactPathParams(false)));
	startTile->removeThing(creature.get(), 0);
	creature->setParent(nullptr);
}

TEST_CASE(pathfinding_map_walk_cost_keeps_generic_semantics)
{
	AStarNode node;
	node.x = 800;
	node.y = 800;
	CHECK(AStarNodes::GetMapWalkCost(node, Position{802, 802, 7}) == MAP_DIAGONALWALKCOST);
	CHECK(AStarNodes::GetMapWalkCost(node, Position{802, 801, 7}) == MAP_NORMALWALKCOST);
	CHECK(AStarNodes::GetMapWalkCost(node, Position{800, 802, 7}) == MAP_NORMALWALKCOST);
}

TFS_TEST_MAIN()
