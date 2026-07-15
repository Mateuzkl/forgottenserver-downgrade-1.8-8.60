#include "../otpch.h"

#include "../configmanager.h"
#include "../game.h"
#include "../monster.h"
#include "../movement.h"
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
	void markDead() { health = 0; }

private:
	inline static uint32_t nextId = 0x50000000;
	std::string name = "monster target state test creature";
};

std::shared_ptr<Monster> makeMonster(bool target = false)
{
	auto type = std::make_shared<MonsterType>();
	type->name = target ? "Target State Enemy" : "Target State Test Monster";
	type->nameDescription = target ? "a target state enemy" : "a target state test monster";
	type->info.isHostile = true;
	type->info.faction = target ? FACTION_LIONUSURPERS : FACTION_LION;
	type->info.enemyFactions.insert(target ? FACTION_LION : FACTION_LIONUSURPERS);
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
	    : oldFactionSystem(ConfigManager::getBoolean(ConfigManager::MONSTER_FACTION_SYSTEM)),
	      oldRequirePlayer(ConfigManager::getBoolean(ConfigManager::MONSTER_FACTION_REQUIRE_PLAYER_NEARBY))
	{
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
	}

	void place(const std::shared_ptr<Creature>& creature, const Position& position)
	{
		creatures.push_back(creature);
		ensureTile(position);
		CHECK(g_game.internalPlaceCreature(creature.get(), position, false, true));
	}

private:
	bool oldFactionSystem;
	bool oldRequirePlayer;
	std::vector<std::weak_ptr<Creature>> creatures;
};

void selectTarget(const std::shared_ptr<Monster>& monster, const std::shared_ptr<Creature>& target)
{
	monster->addTarget(target.get());
	CHECK(monster->getTargetList().size() == 1);
	CHECK(monster->setAttackedCreature(target.get()));
	CHECK(monster->setFollowCreature(target.get()));
	monster->setIdle(false);
}

void moveCreatureForMonster(const std::shared_ptr<Monster>& monster, const std::shared_ptr<Creature>& creature,
                            const Position& oldPosition, const Position& newPosition)
{
	ensureTile(newPosition);
	monster->onCreatureMove(creature.get(), g_game.map.getTile(newPosition), newPosition,
	                        g_game.map.getTile(oldPosition), oldPosition, true);
}

void flushCreatureChecks()
{
	for (size_t index = 0; index < EVENT_CREATURECOUNT; ++index) {
		g_game.checkCreatures(index);
	}
}

} // namespace

TEST_CASE(monster_removes_leaving_creature_by_identity)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = std::make_shared<TestCreature>();
	world.place(monster, Position{500, 500, 7});
	world.place(target, Position{501, 500, 7});
	selectTarget(monster, target);
	target->markDead();

	monster->onRemoveCreature(target.get(), false);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
}

TEST_CASE(monster_keeps_other_valid_target_after_one_leaves)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto firstTarget = std::make_shared<TestCreature>();
	auto secondTarget = makeMonster(true);
	world.place(monster, Position{510, 500, 7});
	world.place(firstTarget, Position{511, 500, 7});
	world.place(secondTarget, Position{512, 500, 7});
	selectTarget(monster, firstTarget);
	monster->addTarget(secondTarget.get());
	CHECK(monster->getTargetList().size() == 2);

	monster->onRemoveCreature(firstTarget.get(), false);

	CHECK(monster->getTargetList().size() == 1);
	CHECK(monster->getTargetList().front().lock() == secondTarget);
	CHECK(!monster->getIdleStatus());
}

TEST_CASE(monster_prunes_target_removed_without_leave_callback)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	world.place(monster, Position{520, 500, 7});
	world.place(target, Position{521, 500, 7});
	selectTarget(monster, target);

	// A different instance prevents the normal removal callback from reaching
	// the monster, reproducing stale state after an external transition.
	target->setInstanceID(1);
	CHECK(g_game.removeCreature(target.get(), false));
	std::weak_ptr<Monster> expiredTarget = target;
	target.reset();
	flushCreatureChecks();
	CHECK(expiredTarget.expired());
	monster->onThink(EVENT_CREATURE_THINK_INTERVAL);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
}

TEST_CASE(monster_stays_active_while_aggressive_condition_exists)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = std::make_shared<TestCreature>();
	world.place(monster, Position{530, 500, 7});
	world.place(target, Position{531, 500, 7});
	selectTarget(monster, target);
	CHECK(monster->addCondition(
	    Condition::createCondition(CONDITIONID_COMBAT, CONDITION_INFIGHT, 10'000, 0, false, 0, true)));

	monster->onRemoveCreature(target.get(), false);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getIdleStatus());

	monster->removeCondition(CONDITION_INFIGHT, true);
	CHECK(monster->getIdleStatus());
}

TEST_CASE(summon_preserves_master_follow_across_floor_change)
{
	WorldFixture world;
	auto summon = makeMonster();
	auto master = std::make_shared<TestCreature>();
	const Position summonPosition{540, 500, 7};
	const Position oldMasterPosition{541, 500, 7};
	const Position newMasterPosition{541, 500, 8};
	world.place(summon, summonPosition);
	world.place(master, oldMasterPosition);
	ensureTile(newMasterPosition);
	CHECK(summon->setMaster(master.get()));
	CHECK(summon->setFollowCreature(master.get()));

	summon->onCreatureMove(master.get(), g_game.map.getTile(newMasterPosition), newMasterPosition,
	                       g_game.map.getTile(oldMasterPosition), oldMasterPosition, true);

	CHECK(summon->getMaster() == master);
	CHECK(summon->getFollowCreatureShared() == master);
}

TEST_CASE(monster_does_not_duplicate_known_target)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	world.place(monster, Position{550, 500, 7});
	world.place(target, Position{551, 500, 7});

	monster->addTarget(target.get());
	monster->addTarget(target.get());

	CHECK(monster->getTargetList().size() == 1);
}

TEST_CASE(monster_cleans_target_that_moves_out_of_view)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	const Position oldPosition{560, 500, 7};
	const Position newPosition{590, 500, 7};
	world.place(monster, Position{559, 500, 7});
	world.place(target, oldPosition);
	selectTarget(monster, target);

	moveCreatureForMonster(monster, target, oldPosition, newPosition);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
}

TEST_CASE(monster_cleans_target_that_changes_floor)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	const Position oldPosition{600, 500, 7};
	const Position newPosition{600, 500, 8};
	world.place(monster, Position{599, 500, 7});
	world.place(target, oldPosition);
	selectTarget(monster, target);

	moveCreatureForMonster(monster, target, oldPosition, newPosition);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
}

TEST_CASE(monster_cleans_target_that_enters_protection_zone)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto target = makeMonster(true);
	const Position oldPosition{610, 500, 7};
	const Position newPosition{612, 500, 7};
	world.place(monster, Position{609, 500, 7});
	world.place(target, oldPosition);
	selectTarget(monster, target);
	ensureTile(newPosition);
	g_game.map.getTile(newPosition)->setFlag(TILESTATE_PROTECTIONZONE);

	moveCreatureForMonster(monster, target, oldPosition, newPosition);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
}

TEST_CASE(monster_prunes_expired_friend_reference)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto friendMonster = makeMonster();
	world.place(monster, Position{620, 500, 7});
	world.place(friendMonster, Position{621, 500, 7});
	monster->addFriend(friendMonster.get());
	CHECK(!monster->getFriendList().empty());

	friendMonster->setInstanceID(1);
	CHECK(g_game.removeCreature(friendMonster.get(), false));
	std::weak_ptr<Monster> expiredFriend = friendMonster;
	friendMonster.reset();
	flushCreatureChecks();
	CHECK(expiredFriend.expired());
	monster->onThink(EVENT_CREATURE_THINK_INTERVAL);

	CHECK(monster->getFriendList().empty());
}

TEST_CASE(familiar_preserves_master_follow_across_floor_change)
{
	WorldFixture world;
	auto familiar = makeMonster();
	auto master = std::make_shared<TestCreature>();
	const Position oldMasterPosition{631, 500, 7};
	const Position newMasterPosition{631, 500, 8};
	world.place(familiar, Position{630, 500, 7});
	world.place(master, oldMasterPosition);
	familiar->setGuildEmblem(GUILDEMBLEM_ALLY);
	CHECK(familiar->setMaster(master.get()));
	CHECK(familiar->setFollowCreature(master.get()));

	moveCreatureForMonster(familiar, master, oldMasterPosition, newMasterPosition);

	CHECK(familiar->getMaster() == master);
	CHECK(familiar->getFollowCreatureShared() == master);
}

TEST_CASE(removing_master_definitively_removes_summon)
{
	WorldFixture world;
	auto master = std::make_shared<TestCreature>();
	auto summon = makeMonster();
	world.place(master, Position{640, 500, 7});
	world.place(summon, Position{641, 500, 7});
	CHECK(summon->setMaster(master.get()));
	CHECK(summon->setFollowCreature(master.get()));

	CHECK(g_game.removeCreature(master.get(), true));

	CHECK(master->isRemoved());
	CHECK(summon->isRemoved());
	CHECK(!summon->getMaster());
	CHECK(!summon->getFollowCreatureShared());
}

TEST_CASE(monster_can_return_to_combat_after_becoming_idle)
{
	WorldFixture world;
	auto monster = makeMonster();
	world.place(monster, Position{650, 500, 7});
	monster->setIdle(false);
	monster->setIdle(false);
	monster->onThink(EVENT_CREATURE_THINK_INTERVAL);
	CHECK(monster->getIdleStatus());

	auto target = makeMonster(true);
	world.place(target, Position{651, 500, 7});
	monster->addTarget(target.get());
	CHECK(monster->selectTarget(target.get()));
	monster->setIdle(false);

	CHECK(!monster->getIdleStatus());
	CHECK(monster->getAttackedCreatureShared() == target);
	CHECK(monster->getFollowCreatureShared() == target);
}

TFS_TEST_MAIN()
