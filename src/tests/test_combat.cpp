#include "../otpch.h"

#include "../combat.h"
#include "../events.h"
#include "../game.h"
#include "../matrixarea.h"
#include "../scriptmanager.h"
#include "../tile.h"

#include "test_support.h"

namespace {

class EventsGuard
{
public:
	EventsGuard() : previous(g_events) { g_events = &events; }
	~EventsGuard() { g_events = previous; }

private:
	Events* previous;
	Events events;
};

class TestTarget final : public Creature
{
public:
	explicit TestTarget(BlockType_t blockType) : blockType(blockType) {}

	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void setID() override {}
	void removeList() override {}
	void addList() override {}

	BlockType_t blockHit(const std::shared_ptr<Creature>&, CombatType_t, int32_t& damage, bool, bool, bool, bool,
	                     CombatOrigin) override
	{
		if (blockType != BLOCK_NONE) {
			damage = 0;
		}
		return blockType;
	}

private:
	std::string name = "area combat test target";
	BlockType_t blockType;
};

void addCreature(const Position& position, const std::shared_ptr<TestTarget>& creature)
{
	if (!g_game.map.getTile(position)) {
		g_game.map.setTile(position.x, position.y, position.z,
		                   std::make_unique<StaticTile>(position.x, position.y, position.z));
	}

	Tile* tile = g_game.map.getTile(position);
	tile->internalAddThing(creature.get());
	g_game.map.getQTNode(position.x, position.y)->addCreature(creature.get());
}

void removeCreature(const std::shared_ptr<TestTarget>& creature)
{
	Tile* tile = creature->getTile();
	const Position position = tile->getPosition();
	g_game.map.getQTNode(position.x, position.y)->removeCreature(creature.get());
	tile->removeThing(creature.get(), 0);
	creature->setParent(nullptr);
}

} // namespace

TEST_CASE(area_conditions_use_each_targets_block_state)
{
	EventsGuard eventsGuard;
	const Position blockedPosition{300, 300, 7};
	const Position unblockedPosition{301, 300, 7};
	auto blockedTarget = std::make_shared<TestTarget>(BLOCK_DEFENSE);
	auto unblockedTarget = std::make_shared<TestTarget>(BLOCK_NONE);
	addCreature(blockedPosition, blockedTarget);
	addCreature(unblockedPosition, unblockedTarget);

	AreaCombat area;
	area.setupArea({3, 1}, 1);

	CombatDamage damage;
	damage.primary.type = COMBAT_PHYSICALDAMAGE;
	damage.primary.value = -100;
	damage.origin = ORIGIN_SPELL;

	CombatParams params;
	params.aggressive = false;
	params.conditionList.push_front(
	    Condition::createCondition(CONDITIONID_COMBAT, CONDITION_INFIGHT, 10'000));

	Combat::doAreaCombat(nullptr, unblockedPosition, &area, damage, params);

	CHECK(!blockedTarget->hasCondition(CONDITION_INFIGHT));
	CHECK(unblockedTarget->hasCondition(CONDITION_INFIGHT));

	removeCreature(blockedTarget);
	removeCreature(unblockedTarget);
}

TFS_TEST_MAIN()
