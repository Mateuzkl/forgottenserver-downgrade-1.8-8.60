#include "../otpch.h"

#include "../creature.h"
#include "../map.h"
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

TFS_TEST_MAIN()
