#include "../otpch.h"

#include "../game.h"
#include "../tile.h"

#include "test_support.h"

namespace {

class CheckCreature final : public Creature
{
public:
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void addList() override {}
	void removeList() override {}
	void setID() override
	{
		if (id == 0) {
			id = nextId++;
		}
	}
	void onThink(uint32_t) override { ++thinkCalls; }

	inline static size_t thinkCalls = 0;

private:
	inline static uint32_t nextId = 0x62000000;
	std::string name = "creature check test creature";
};

void ensureTile(const Position& position)
{
	if (!g_game.map.getTile(position)) {
		g_game.map.setTile(position.x, position.y, position.z,
		                   std::make_unique<StaticTile>(position.x, position.y, position.z));
	}
}

class CheckCreatureFixture
{
public:
	~CheckCreatureFixture()
	{
		for (auto it = creatures.rbegin(); it != creatures.rend(); ++it) {
			if (*it && !(*it)->isRemoved()) {
				g_game.removeCreature(it->get(), false);
			}
		}
		for (size_t index = 0; index < EVENT_CREATURECOUNT; ++index) {
			g_game.checkCreatures(index);
		}
	}

	std::shared_ptr<CheckCreature> add(size_t offset)
	{
		auto creature = std::make_shared<CheckCreature>();
		const Position position{static_cast<uint16_t>(900 + offset), 900, 7};
		ensureTile(position);
		CHECK(g_game.internalPlaceCreature(creature.get(), position, false, true));
		g_game.addCreatureCheck(creature.get());
		creatures.push_back(creature);
		return creature;
	}

private:
	std::vector<std::shared_ptr<CheckCreature>> creatures;
};

} // namespace

TEST_CASE(creature_checks_are_balanced_across_buckets)
{
	CheckCreatureFixture world;
	CheckCreature::thinkCalls = 0;

	constexpr size_t creatureCount = 250;
	for (size_t index = 0; index < creatureCount; ++index) {
		world.add(index);
	}

	std::array<size_t, EVENT_CREATURECOUNT> processedPerBucket{};
	for (size_t index = 0; index < EVENT_CREATURECOUNT; ++index) {
		const size_t before = CheckCreature::thinkCalls;
		g_game.checkCreatures(index);
		processedPerBucket[index] = CheckCreature::thinkCalls - before;
	}

	CHECK(CheckCreature::thinkCalls == creatureCount);
	const auto [minIt, maxIt] = std::minmax_element(processedPerBucket.begin(), processedPerBucket.end());
	CHECK(*maxIt - *minIt <= 1);
}

TEST_CASE(creature_check_removal_and_reactivation_do_not_duplicate_work)
{
	CheckCreatureFixture world;
	CheckCreature::thinkCalls = 0;
	auto creature = world.add(300);

	Game::removeCreatureCheck(creature.get());
	for (size_t index = 0; index < EVENT_CREATURECOUNT; ++index) {
		g_game.checkCreatures(index);
	}
	CHECK(!creature->isCreatureCheckEnabled());
	CHECK(CheckCreature::thinkCalls == 0);

	g_game.addCreatureCheck(creature.get());
	for (size_t index = 0; index < EVENT_CREATURECOUNT; ++index) {
		g_game.checkCreatures(index);
	}
	CHECK(creature->isCreatureCheckEnabled());
	CHECK(CheckCreature::thinkCalls == 1);
}

TFS_TEST_MAIN()
