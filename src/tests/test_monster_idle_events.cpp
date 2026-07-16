#include "../otpch.h"

#include "../configmanager.h"
#include "../game.h"
#include "../monster.h"
#include "../movement.h"
#include "../scriptmanager.h"
#include "../tile.h"

#include "test_support.h"

namespace {

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
	WorldFixture() : oldFactionSystem(ConfigManager::getBoolean(ConfigManager::MONSTER_FACTION_SYSTEM))
	{
		ConfigManager::setBoolean(ConfigManager::MONSTER_FACTION_SYSTEM, true);
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
	}

	void place(const std::shared_ptr<Creature>& creature, const Position& position)
	{
		creatures.push_back(creature);
		ensureTile(position);
		CHECK(g_game.internalPlaceCreature(creature.get(), position, false, true));
	}

private:
	bool oldFactionSystem;
	std::vector<std::weak_ptr<Creature>> creatures;
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

TFS_TEST_MAIN()
