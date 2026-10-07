#include "../otpch.h"

#include "../creature.h"
#include "../map.h"
#include "../monster.h"
#include "../npc.h"
#include "../tile.h"
#include "test_support.h"

namespace {

class TestCreature : public Creature
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
	std::string name = "map spectator test creature";
};

class TestPlayer final : public TestCreature
{
public:
	CreatureType_t getType() const override { return CREATURETYPE_PLAYER; }
	Player* getPlayer() override { return reinterpret_cast<Player*>(this); }
	const Player* getPlayer() const override { return reinterpret_cast<const Player*>(this); }
};

class TestMonster final : public TestCreature
{
public:
	Monster* getMonster() override { return reinterpret_cast<Monster*>(this); }
	const Monster* getMonster() const override { return reinterpret_cast<const Monster*>(this); }
};

class TestNpc final : public TestCreature
{
public:
	CreatureType_t getType() const override { return CREATURETYPE_NPC; }
	Npc* getNpc() override { return reinterpret_cast<Npc*>(this); }
	const Npc* getNpc() const override { return reinterpret_cast<const Npc*>(this); }
};

void addCreature(Map& map, const Position& position, const std::shared_ptr<TestCreature>& creature)
{
	if (!map.getTile(position)) {
		map.setTile(position.x, position.y, position.z,
		            std::make_unique<StaticTile>(position.x, position.y, position.z));
	}

	Tile* tile = map.getTile(position);
	tile->internalAddThing(creature.get());
	map.getQTNode(position.x, position.y)->addCreature(creature.get());
}

void removeCreature(Map& map, const std::shared_ptr<TestCreature>& creature)
{
	Tile* tile = creature->getTile();
	const Position position = tile->getPosition();
	map.getQTNode(position.x, position.y)->removeCreature(creature.get());
	tile->removeThing(creature.get(), 0);
	creature->setParent(nullptr);
}

bool contains(const SpectatorVec& spectators, const Creature* expected)
{
	return std::ranges::any_of(spectators, [expected](const auto& spectator) { return spectator.get() == expected; });
}

} // namespace

TEST_CASE(spectator_queries_are_live_after_add_and_remove)
{
	Map map;
	const Position center{100, 100, 7};

	SpectatorVec beforeAdd;
	map.getSpectators(beforeAdd, center);
	CHECK(beforeAdd.empty());

	auto creature = std::make_shared<TestMonster>();
	addCreature(map, center, creature);

	SpectatorVec afterAdd;
	map.getSpectators(afterAdd, center);
	CHECK(contains(afterAdd, creature.get()));

	afterAdd = {};
	std::weak_ptr<TestMonster> weakCreature = creature;
	removeCreature(map, creature);

	SpectatorVec afterRemove;
	map.getSpectators(afterRemove, center);
	CHECK(!contains(afterRemove, creature.get()));

	creature.reset();
	CHECK(weakCreature.expired());
}

TEST_CASE(spectator_floor_rules_match_surface_and_underground_visibility)
{
	Map map;
	auto surfaceAbove = std::make_shared<TestMonster>();
	auto surfaceSame = std::make_shared<TestMonster>();
	auto underground = std::make_shared<TestMonster>();
	addCreature(map, Position{120, 120, 6}, surfaceAbove);
	addCreature(map, Position{120, 120, 7}, surfaceSame);
	addCreature(map, Position{120, 120, 8}, underground);

	SpectatorVec sameFloor;
	map.getSpectators(sameFloor, Position{120, 120, 7});
	CHECK(!contains(sameFloor, surfaceAbove.get()));
	CHECK(contains(sameFloor, surfaceSame.get()));
	CHECK(!contains(sameFloor, underground.get()));

	SpectatorVec surfaceMultiFloor;
	map.getSpectators(surfaceMultiFloor, Position{120, 120, 7}, true);
	CHECK(contains(surfaceMultiFloor, surfaceAbove.get()));
	CHECK(contains(surfaceMultiFloor, surfaceSame.get()));
	CHECK(!contains(surfaceMultiFloor, underground.get()));

	SpectatorVec undergroundMultiFloor;
	map.getSpectators(undergroundMultiFloor, Position{120, 120, 8}, true);
	CHECK(contains(undergroundMultiFloor, surfaceAbove.get()));
	CHECK(contains(undergroundMultiFloor, surfaceSame.get()));
	CHECK(contains(undergroundMultiFloor, underground.get()));

	removeCreature(map, surfaceAbove);
	removeCreature(map, surfaceSame);
	removeCreature(map, underground);
}

TEST_CASE(spectator_type_filters_return_only_requested_categories)
{
	Map map;
	const Position center{140, 140, 7};
	auto player = std::make_shared<TestPlayer>();
	auto monster = std::make_shared<TestMonster>();
	auto npc = std::make_shared<TestNpc>();
	addCreature(map, center, player);
	addCreature(map, center, monster);
	addCreature(map, center, npc);

	SpectatorVec players;
	map.getSpectators(players, center, false, true);
	CHECK(players.size() == 1);
	CHECK(contains(players, player.get()));

	SpectatorVec monsters;
	map.getSpectators(monsters, center, false, false, 0, 0, 0, 0, true);
	CHECK(monsters.size() == 1);
	CHECK(contains(monsters, monster.get()));

	SpectatorVec npcs;
	map.getSpectators(npcs, center, false, false, 0, 0, 0, 0, false, true);
	CHECK(npcs.size() == 1);
	CHECK(contains(npcs, npc.get()));

	removeCreature(map, player);
	removeCreature(map, monster);
	removeCreature(map, npc);
}

TEST_CASE(player_only_multifloor_query_matches_the_all_creature_player_set)
{
	// A future non-player item callback must revisit the tile optimization.
	static_assert(std::is_same_v<decltype(&Monster::onUpdateTileItem), decltype(&Creature::onUpdateTileItem)>);
	static_assert(std::is_same_v<decltype(&Npc::onUpdateTileItem), decltype(&Creature::onUpdateTileItem)>);
	static_assert(std::is_same_v<decltype(&Monster::onRemoveTileItem), decltype(&Creature::onRemoveTileItem)>);
	static_assert(std::is_same_v<decltype(&Npc::onRemoveTileItem), decltype(&Creature::onRemoveTileItem)>);
	Map map;
	const Position center{200, 200, 7};
	auto player = std::make_shared<TestPlayer>();
	auto upstairs = std::make_shared<TestPlayer>();
	auto distant = std::make_shared<TestPlayer>();
	auto monster = std::make_shared<TestMonster>();
	auto npc = std::make_shared<TestNpc>();
	player->setInstanceID(42);
	upstairs->setInstanceID(43);
	addCreature(map, center, player);
	addCreature(map, Position{201, 201, 6}, upstairs);
	addCreature(map, Position{250, 250, 7}, distant);
	addCreature(map, Position{202, 202, 7}, monster);
	addCreature(map, Position{203, 203, 7}, npc);

	SpectatorVec all, players;
	map.getSpectators(all, center, true);
	map.getSpectators(players, center, true, true);
	CHECK(players.size() == all.players().size());
	CHECK(players.size() == 2);
	CHECK(players.monsters().empty());
	CHECK(players.npcs().empty());
	for (const auto& spectator : all.players()) {
		CHECK(contains(players, spectator.get()));
	}
	CHECK(!contains(players, distant.get()));
	// Map collection remains instance-agnostic; each tile notification keeps its
	// existing InstanceUtils visibility checks rather than changing them here.
	CHECK(contains(players, player.get()));
	CHECK(contains(players, upstairs.get()));

	removeCreature(map, player);
	removeCreature(map, upstairs);
	removeCreature(map, distant);
	removeCreature(map, monster);
	removeCreature(map, npc);
}

TEST_CASE(movement_snapshot_matches_two_live_queries_at_viewport_edges_and_floors)
{
	Map map;
	std::vector<std::shared_ptr<TestCreature>> creatures;
	// Dense boundary grid includes the diagonal bounding rectangle's extra corners.
	for (uint8_t z = 5; z <= 10; ++z) {
		for (uint16_t x = 88; x <= 113; ++x) {
			for (uint16_t y = 88; y <= 113; ++y) {
				auto creature = std::make_shared<TestMonster>();
				addCreature(map, Position{x, y, z}, creature);
				creatures.push_back(std::move(creature));
			}
		}
	}
	for (uint8_t z : {uint8_t{7}, uint8_t{8}}) {
		const Position oldPos{100, 100, z};
		for (int dx = -1; dx <= 1; ++dx) {
			for (int dy = -1; dy <= 1; ++dy) {
				const Position newPos{static_cast<uint16_t>(100 + dx), static_cast<uint16_t>(100 + dy), z};
				SpectatorVec expected, next, actual;
				map.getSpectators(expected, oldPos, true);
				map.getSpectators(next, newPos, true);
				expected.addSpectators(next);
				expected.partitionByType();
				map.getMovementSpectators(actual, oldPos, newPos, false);
				CHECK(actual.size() == expected.size());
				CHECK(std::ranges::equal(actual, expected));
			}
		}
	}
	for (const auto& creature : creatures) {
		removeCreature(map, creature);
	}
}

TEST_CASE(movement_snapshot_keeps_teleport_floor_instance_and_lifetime_semantics)
{
	Map map;
	auto player = std::make_shared<TestPlayer>();
	auto monster = std::make_shared<TestMonster>();
	auto npc = std::make_shared<TestNpc>();
	player->setInstanceID(42);
	monster->setInstanceID(43);
	addCreature(map, Position{0, 0, 7}, player);
	addCreature(map, Position{65535, 65535, 8}, monster);
	addCreature(map, Position{1, 1, 6}, npc);
	for (const Position newPos : {Position{1, 1, 7}, Position{65535, 65535, 8}, Position{0, 0, 8}}) {
		SpectatorVec expected, next, actual;
		map.getSpectators(expected, Position{0, 0, 7}, true);
		map.getSpectators(next, newPos, true);
		expected.addSpectators(next);
		expected.partitionByType();
		map.getMovementSpectators(actual, Position{0, 0, 7}, newPos, newPos.z != 7);
		CHECK(std::ranges::equal(actual, expected));
		CHECK(actual.players().size() == expected.players().size());
		CHECK(actual.npcs().size() == expected.npcs().size());
	}
	SpectatorVec pinned;
	map.getMovementSpectators(pinned, Position{0, 0, 7}, Position{1, 1, 7}, false);
	std::weak_ptr<TestPlayer> weak = player;
	removeCreature(map, player);
	player.reset();
	CHECK(!weak.expired());
	pinned = {};
	CHECK(weak.expired());
	removeCreature(map, monster);
	removeCreature(map, npc);
}

TFS_TEST_MAIN()
