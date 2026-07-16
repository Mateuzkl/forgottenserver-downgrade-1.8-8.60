#include "../otpch.h"

#include "../configmanager.h"
#include "../events.h"
#include "../game.h"
#include "../monster.h"
#include "../movement.h"
#include "../performance_metrics.h"
#include "../scriptmanager.h"
#include "../tile.h"

#include "test_support.h"

namespace {

class TestCreature final : public Creature
{
public:
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void removeList() override {}
	void addList() override {}
	void setID() override
	{
		if (id == 0) {
			id = nextId++;
		}
	}

private:
	inline static uint32_t nextId = 0x51000000;
	std::string name = "idle event test creature";
};

std::shared_ptr<Monster> makeMonster(bool enemy = false)
{
	auto type = std::make_shared<MonsterType>();
	type->name = enemy ? "Idle Event Enemy" : "Idle Event Monster";
	type->nameDescription = enemy ? "an idle event enemy" : "an idle event monster";
	type->info.isHostile = true;
	type->info.faction = enemy ? FACTION_LIONUSURPERS : FACTION_LION;
	type->info.enemyFactions.insert(enemy ? FACTION_LION : FACTION_LIONUSURPERS);
	return std::make_shared<Monster>(type);
}

void ensureTile(const Position& position)
{
	if (!g_game.map.getTile(position)) {
		g_game.map.setTile(position.x, position.y, position.z,
		                   std::make_unique<StaticTile>(position.x, position.y, position.z));
	}
}

class WorldFixture
{
public:
	WorldFixture()
	    : oldEvents(g_events),
	      oldFactionSystem(ConfigManager::getBoolean(ConfigManager::MONSTER_FACTION_SYSTEM)),
	      oldRequirePlayer(ConfigManager::getBoolean(ConfigManager::MONSTER_FACTION_REQUIRE_PLAYER_NEARBY))
	{
		g_events = &events;
		ConfigManager::setBoolean(ConfigManager::MONSTER_FACTION_SYSTEM, true);
		ConfigManager::setBoolean(ConfigManager::MONSTER_FACTION_REQUIRE_PLAYER_NEARBY, false);
		if (!g_moveEvents) {
			g_moveEvents = std::make_unique<MoveEvents>();
		}
	}

	~WorldFixture()
	{
		for (auto it = creatures.rbegin(); it != creatures.rend(); ++it) {
			if (auto creature = it->lock(); creature && !creature->isRemoved()) {
				g_game.removeCreature(creature.get(), false);
			}
		}
		ConfigManager::setBoolean(ConfigManager::MONSTER_FACTION_SYSTEM, oldFactionSystem);
		ConfigManager::setBoolean(ConfigManager::MONSTER_FACTION_REQUIRE_PLAYER_NEARBY, oldRequirePlayer);
		g_events = oldEvents;
	}

	void place(const std::shared_ptr<Creature>& creature, const Position& position)
	{
		creatures.push_back(creature);
		ensureTile(position);
		CHECK(g_game.internalPlaceCreature(creature.get(), position, false, true));
	}

private:
	Events* oldEvents;
	Events events;
	bool oldFactionSystem;
	bool oldRequirePlayer;
	std::vector<std::weak_ptr<Creature>> creatures;
};

class MetricsFixture
{
public:
	MetricsFixture() : wasEnabled(g_performanceMetrics.isEnabled()) { g_performanceMetrics.setEnabled(true); }
	~MetricsFixture() { g_performanceMetrics.setEnabled(wasEnabled); }

	uint64_t get(MonsterIdleMetric metric) const { return g_performanceMetrics.getMonsterIdleMetric(metric); }

private:
	bool wasEnabled;
};

void selectTarget(const std::shared_ptr<Monster>& monster, const std::shared_ptr<Monster>& target)
{
	monster->addTarget(target.get());
	CHECK(monster->setAttackedCreature(target.get()));
	CHECK(monster->setFollowCreature(target.get()));
	monster->setIdle(false);
}

} // namespace

TEST_CASE(monster_cleans_target_when_instance_changes_at_same_position)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	world.place(monster, Position{700, 500, 7});
	world.place(target, Position{701, 500, 7});
	selectTarget(monster, target);

	target->setInstanceID(7);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
}

TEST_CASE(summon_keeps_master_while_adopting_master_instance)
{
	WorldFixture world;
	auto summon = makeMonster();
	auto master = std::make_shared<TestCreature>();
	world.place(summon, Position{705, 500, 7});
	world.place(master, Position{706, 500, 7});
	master->setInstanceID(11);

	CHECK(summon->setMaster(master.get()));

	CHECK(summon->getMaster() == master);
	CHECK(summon->getInstanceID() == master->getInstanceID());
	CHECK(!summon->getIdleStatus());
}

TEST_CASE(idle_monster_is_removed_from_creature_checks_after_placement)
{
	WorldFixture world;
	auto monster = makeMonster();
	world.place(monster, Position{710, 500, 7});

	CHECK(monster->getIdleStatus());
	CHECK(!monster->isCreatureCheckEnabled());
}

TEST_CASE(monster_idle_transition_is_idempotent_and_clears_damage_once)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto firstAttacker = std::make_shared<TestCreature>();
	auto secondAttacker = std::make_shared<TestCreature>();
	world.place(monster, Position{720, 500, 7});
	world.place(firstAttacker, Position{721, 500, 7});
	world.place(secondAttacker, Position{722, 500, 7});
	MetricsFixture metrics;

	const uint64_t transitionsToActive = metrics.get(MonsterIdleMetric::TransitionToActive);
	const uint64_t transitionsToIdle = metrics.get(MonsterIdleMetric::TransitionToIdle);
	const uint64_t sameStateCalls = metrics.get(MonsterIdleMetric::SameStateCalls);
	const uint64_t onIdleStatusCalls = metrics.get(MonsterIdleMetric::OnIdleStatusCalls);
	const uint64_t damageMapClears = metrics.get(MonsterIdleMetric::DamageMapClears);

	monster->setIdle(false);
	monster->setIdle(false);
	CHECK(monster->isCreatureCheckEnabled());
	firstAttacker->onAttackedCreatureDrainHealth(monster, 10);
	secondAttacker->onAttackedCreatureDrainHealth(monster, 20);
	CHECK(monster->getDamageMap().size() == 2);

	monster->setIdle(true);
	CHECK(!monster->isCreatureCheckEnabled());
	CHECK(monster->getDamageMap().empty());
	monster->setIdle(true);

	CHECK(metrics.get(MonsterIdleMetric::TransitionToActive) == transitionsToActive + 1);
	CHECK(metrics.get(MonsterIdleMetric::TransitionToIdle) == transitionsToIdle + 1);
	CHECK(metrics.get(MonsterIdleMetric::SameStateCalls) == sameStateCalls + 2);
	CHECK(metrics.get(MonsterIdleMetric::OnIdleStatusCalls) == onIdleStatusCalls + 1);
	CHECK(metrics.get(MonsterIdleMetric::DamageMapClears) == damageMapClears + 1);
}

TEST_CASE(monster_target_and_friend_mutations_report_real_changes)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto creature = std::make_shared<TestCreature>();
	world.place(monster, Position{730, 500, 7});
	world.place(creature, Position{731, 500, 7});

	CHECK(monster->addTarget(creature.get()));
	CHECK(!monster->addTarget(creature.get()));
	CHECK(monster->removeTarget(creature.get()));
	CHECK(!monster->removeTarget(creature.get()));
	CHECK(monster->addFriend(creature.get()));
	CHECK(!monster->addFriend(creature.get()));
	CHECK(monster->removeFriend(creature.get()));
	CHECK(!monster->removeFriend(creature.get()));
}

TEST_CASE(monster_does_not_refresh_idle_for_duplicate_creature_found)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	world.place(monster, Position{740, 500, 7});
	world.place(target, Position{741, 500, 7});
	monster->onCreatureAppear(target.get(), false);
	MetricsFixture metrics;
	const uint64_t refreshCalls = metrics.get(MonsterIdleMetric::RefreshCalls);

	monster->onCreatureAppear(target.get(), false);

	CHECK(metrics.get(MonsterIdleMetric::RefreshCalls) == refreshCalls);
}

TEST_CASE(monster_cleans_target_after_real_teleport_out_of_view)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	world.place(monster, Position{750, 500, 7});
	world.place(target, Position{751, 500, 7});
	selectTarget(monster, target);
	const Position destination{780, 500, 7};
	ensureTile(destination);

	CHECK(g_game.internalTeleport(target.get(), destination) == RETURNVALUE_NOERROR);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
}

TEST_CASE(monster_cleans_target_after_real_teleport_to_another_floor)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	world.place(monster, Position{800, 500, 7});
	world.place(target, Position{801, 500, 7});
	selectTarget(monster, target);
	const Position destination{801, 500, 8};
	ensureTile(destination);

	CHECK(g_game.internalTeleport(target.get(), destination) == RETURNVALUE_NOERROR);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
	CHECK(!monster->isCreatureCheckEnabled());
}

TEST_CASE(monster_cleans_target_after_real_teleport_into_protection_zone)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	world.place(monster, Position{810, 500, 7});
	world.place(target, Position{811, 500, 7});
	selectTarget(monster, target);
	const Position destination{812, 500, 7};
	ensureTile(destination);
	g_game.map.getTile(destination)->setFlag(TILESTATE_PROTECTIONZONE);

	CHECK(g_game.internalTeleport(target.get(), destination) == RETURNVALUE_NOERROR);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
	CHECK(!monster->isCreatureCheckEnabled());
}

TEST_CASE(monster_cleans_target_after_logout_removal)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	world.place(monster, Position{820, 500, 7});
	world.place(target, Position{821, 500, 7});
	selectTarget(monster, target);

	CHECK(g_game.removeCreature(target.get(), true));

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
	CHECK(!monster->isCreatureCheckEnabled());
}

TEST_CASE(aggressive_condition_ends_with_one_idle_transition)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	world.place(monster, Position{830, 500, 7});
	world.place(target, Position{831, 500, 7});
	selectTarget(monster, target);
	CHECK(monster->addCondition(
	    Condition::createCondition(CONDITIONID_COMBAT, CONDITION_INFIGHT, 10'000, 0, false, 0, true)));
	monster->onRemoveCreature(target.get(), false);
	CHECK(!monster->getIdleStatus());
	MetricsFixture metrics;
	const uint64_t transitionsToIdle = metrics.get(MonsterIdleMetric::TransitionToIdle);
	const uint64_t onIdleStatusCalls = metrics.get(MonsterIdleMetric::OnIdleStatusCalls);

	monster->removeCondition(CONDITION_INFIGHT, true);

	CHECK(monster->getIdleStatus());
	CHECK(metrics.get(MonsterIdleMetric::TransitionToIdle) == transitionsToIdle + 1);
	CHECK(metrics.get(MonsterIdleMetric::OnIdleStatusCalls) == onIdleStatusCalls + 1);
}

TEST_CASE(active_monster_without_reason_converges_to_idle_on_think)
{
	WorldFixture world;
	auto monster = makeMonster();
	world.place(monster, Position{840, 500, 7});
	MetricsFixture metrics;
	const uint64_t activeWithoutReason = metrics.get(MonsterIdleMetric::ActiveWithoutReason);
	monster->setIdle(false);
	CHECK(monster->isCreatureCheckEnabled());

	monster->onThink(EVENT_CREATURE_THINK_INTERVAL);

	CHECK(monster->getIdleStatus());
	CHECK(!monster->isCreatureCheckEnabled());
	CHECK(metrics.get(MonsterIdleMetric::ActiveWithoutReason) == activeWithoutReason + 1);
}

TEST_CASE(monster_clears_faction_target_when_policy_is_disabled)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	world.place(monster, Position{790, 500, 7});
	world.place(target, Position{791, 500, 7});
	selectTarget(monster, target);

	ConfigManager::setBoolean(ConfigManager::MONSTER_FACTION_SYSTEM, false);
	monster->onThink(EVENT_CREATURE_THINK_INTERVAL);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
}

TFS_TEST_MAIN()
