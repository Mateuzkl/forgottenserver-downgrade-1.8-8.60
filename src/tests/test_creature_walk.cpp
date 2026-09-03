#include "../otpch.h"

#include "../creature.h"
#include "../events.h"
#include "../game.h"
#include "../reactor.h"
#include "../scheduler.h"
#include "../scriptmanager.h"
#include "../tasks.h"
#include "../tile.h"

#include "test_support.h"

namespace {

class WalkCreature final : public Creature
{
public:
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void setID() override
	{
		if (id == 0) {
			id = nextId++;
		}
	}
	void addList() override {}
	void removeList() override {}

	uint32_t generation() const { return walkGeneration; }
	uint32_t eventId() const { return eventWalk; }

	void onWalk() override
	{
		++walkCalls;
		Creature::onWalk();
	}

	bool getNextStep(Direction&, uint32_t&) override
	{
		if (clearEventBeforeStop) {
			// Match Monster::getNextStep() when movement is blocked or idle.
			eventWalk = 0;
		}
		return false; // exhaust the current path without needing map movement
	}

	void onWalkComplete() override
	{
		++completionCalls;
		if (rearmOnComplete) {
			// Match Monster::onWalkComplete -> walkToSpawn -> startAutoWalk.
			startAutoWalk();
			replacementId = eventWalk;
		}
	}

	bool rearmOnComplete = false;
	bool clearEventBeforeStop = false;
	int walkCalls = 0;
	int completionCalls = 0;
	uint32_t replacementId = 0;

private:
	inline static uint32_t nextId = 0x51000000;
	std::string name = "walk scheduler test creature";
};

class WalkFixture
{
public:
	WalkFixture() : creature(std::make_shared<WalkCreature>()), oldEvents(g_events)
	{
		g_events = &events;
		g_dispatcher.start();
		g_scheduler.start();
		const Position position{950, 950, 7};
		if (!g_game.map.getTile(position)) {
			g_game.map.setTile(position.x, position.y, position.z,
			                   std::make_unique<StaticTile>(position.x, position.y, position.z));
		}
		CHECK(g_game.internalPlaceCreature(creature.get(), position, false, true));
	}

	~WalkFixture()
	{
		creature->rearmOnComplete = false;
		creature->stopEventWalk();
		if (!creature->isRemoved()) {
			g_game.removeCreature(creature.get(), false);
		}
		// Drain cancelled scheduler callbacks while their referenced globals live.
		g_reactor.drain();
		g_scheduler.shutdown();
		g_dispatcher.shutdown();
		g_events = oldEvents;
	}

	std::shared_ptr<WalkCreature> creature;

private:
	Events* oldEvents;
	Events events;
};

} // namespace

TEST_CASE(creature_walk_rejects_stale_generation_after_stop_and_restart)
{
	WalkFixture world;
	auto& creature = *world.creature;
	creature.addEventWalk();
	CHECK(creature.eventId() != 0);
	const auto oldGeneration = creature.generation();
	creature.stopEventWalk();
	creature.addEventWalk();
	const auto replacementId = creature.eventId();
	CHECK(replacementId != 0);
	CHECK(creature.generation() != oldGeneration);

	g_game.checkCreatureWalk(creature.getID(), oldGeneration);
	CHECK(creature.walkCalls == 0);
	CHECK(creature.eventId() == replacementId);
}

TEST_CASE(creature_walk_current_generation_executes_and_completes)
{
	WalkFixture world;
	auto& creature = *world.creature;
	creature.addEventWalk();
	CHECK(creature.eventId() != 0);

	g_game.checkCreatureWalk(creature.getID(), creature.generation());
	CHECK(creature.walkCalls == 1);
	CHECK(creature.completionCalls == 1);
	CHECK(creature.eventId() == 0);
}

TEST_CASE(creature_walk_completion_rearm_keeps_its_single_tracked_event)
{
	WalkFixture world;
	auto& creature = *world.creature;
	creature.rearmOnComplete = true;
	creature.addEventWalk();
	const auto initialGeneration = creature.generation();

	g_game.checkCreatureWalk(creature.getID(), initialGeneration);
	CHECK(creature.walkCalls == 1);
	CHECK(creature.completionCalls == 1);
	CHECK(creature.generation() != initialGeneration);
	CHECK(creature.replacementId != 0);
	CHECK(creature.eventId() == creature.replacementId);
}

TEST_CASE(creature_walk_completion_rearm_after_event_id_was_cleared)
{
	WalkFixture world;
	auto& creature = *world.creature;
	creature.clearEventBeforeStop = true;
	creature.rearmOnComplete = true;
	creature.addEventWalk();
	const auto initialGeneration = creature.generation();

	g_game.checkCreatureWalk(creature.getID(), initialGeneration);
	CHECK(creature.generation() != initialGeneration);
	CHECK(creature.replacementId != 0);
	CHECK(creature.eventId() == creature.replacementId);
}

TEST_CASE(creature_walk_callback_ignores_removed_creature)
{
	WalkFixture world;
	auto& creature = *world.creature;
	const auto generation = creature.generation();
	CHECK(g_game.removeCreature(&creature, false));

	g_game.checkCreatureWalk(creature.getID(), generation);
	CHECK(creature.walkCalls == 0);
}

TFS_TEST_MAIN()
