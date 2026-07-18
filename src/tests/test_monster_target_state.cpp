#include "../otpch.h"

#include "../chat.h"
#include "../configmanager.h"
#include "../events.h"
#include "../game.h"
#include "../globalevent.h"
#include "../groups.h"
#include "../item.h"
#include "../monster.h"
#include "../movement.h"
#include "../player.h"
#include "../scriptmanager.h"
#include "../tile.h"

#include "test_support.h"

#include <filesystem>

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

std::shared_ptr<Player> makePlayer()
{
	auto player = std::make_shared<Player>(nullptr);
	player->setName("Monster Target State Player");
	player->setGroup(std::make_shared<Group>());
	return player;
}

void ensureItemTypes()
{
	static const bool loaded = [] {
		const auto itemFile = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		                      "data/items/items.otb";
		return Item::items.loadFromOtb(itemFile.string());
	}();
	CHECK(loaded);
}

void ensureTile(const Position& position)
{
	if (!g_game.map.getTile(position)) {
		g_game.map.setTile(position.x, position.y, position.z,
		                   std::make_unique<StaticTile>(position.x, position.y, position.z));
	}

	Tile* tile = g_game.map.getTile(position);
	if (!tile->getGround()) {
		tile->setGround(std::make_shared<Item>(0));
	}
}

void addStairHeight(Tile* tile)
{
	static const uint16_t heightItemId = [] {
		for (size_t id = 1; id < Item::items.size(); ++id) {
			const ItemType& itemType = Item::items[id];
			if (itemType.id != 0 && !itemType.isGroundTile() && itemType.hasHeight &&
			    (!itemType.blockSolid || itemType.moveable)) {
				return static_cast<uint16_t>(id);
			}
		}
		return uint16_t{0};
	}();
	CHECK(heightItemId != 0);
	for (uint8_t i = 0; i < 3; ++i) {
		auto item = std::make_shared<Item>(heightItemId);
		tile->internalAddThing(item.get());
	}
	CHECK(tile->hasHeight(3));
}

class WorldFixture
{
public:
	WorldFixture()
	    : oldChat(g_chat), oldEvents(g_events), oldGlobalEvents(g_globalEvents),
	      oldFactionSystem(ConfigManager::getBoolean(ConfigManager::MONSTER_FACTION_SYSTEM)),
	      oldRequirePlayer(ConfigManager::getBoolean(ConfigManager::MONSTER_FACTION_REQUIRE_PLAYER_NEARBY))
	{
		ensureItemTypes();
		g_chat = &chat;
		g_events = &events;
		g_globalEvents = &globalEvents;
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
		g_globalEvents = oldGlobalEvents;
		g_events = oldEvents;
		g_chat = oldChat;
	}

	void place(const std::shared_ptr<Creature>& creature, const Position& position)
	{
		creatures.push_back(creature);
		ensureTile(position);
		CHECK(g_game.internalPlaceCreature(creature.get(), position, false, true));
	}

private:
	Chat* oldChat;
	Events* oldEvents;
	GlobalEvents* oldGlobalEvents;
	Chat chat;
	Events events;
	GlobalEvents globalEvents;
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

void moveCreatureForMonster(const std::shared_ptr<Creature>& creature, const Position& newPosition)
{
	ensureTile(newPosition);
	Tile* expectedTile = g_game.map.getTile(newPosition);
	CHECK(g_game.internalMoveCreature(*creature, *expectedTile, FLAG_NOLIMIT) == RETURNVALUE_NOERROR);
	CHECK(creature->getPosition() == newPosition);
	CHECK(creature->getTile() == expectedTile);
}

void moveCreatureByDirection(const std::shared_ptr<Creature>& creature, Direction direction,
                             const Position& expectedPosition)
{
	ensureTile(expectedPosition);
	Tile* expectedTile = g_game.map.getTile(expectedPosition);
	const ReturnValue result = g_game.internalMoveCreature(creature.get(), direction, FLAG_NOLIMIT);
	if (result != RETURNVALUE_NOERROR) {
		throw std::runtime_error("directional creature move failed with ReturnValue " +
		                         std::to_string(static_cast<uint8_t>(result)));
	}
	CHECK(creature->getPosition() == expectedPosition);
	CHECK(creature->getTile() == expectedTile);
}

void flushCreatureChecks()
{
	for (size_t index = 0; index < EVENT_CREATURECOUNT; ++index) {
		g_game.checkCreatures(index);
	}
}

bool hasTarget(const std::shared_ptr<Monster>& monster, const Creature* creature)
{
	return std::ranges::any_of(monster->getTargetList(), [creature](const auto& weakRef) {
		return weakRef.lock().get() == creature;
	});
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

	moveCreatureForMonster(master, newMasterPosition);

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

	moveCreatureForMonster(target, newPosition);

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
	const Position newPosition{600, 500, 6};
	world.place(monster, Position{599, 500, 7});
	world.place(target, oldPosition);
	selectTarget(monster, target);
	CHECK(monster->canSee(oldPosition));
	CHECK(Creature::canSee(monster->getPosition(), oldPosition, Map::maxClientViewportX + 1,
	                       Map::maxClientViewportY + 1));
	CHECK(Creature::canSee(monster->getPosition(), newPosition, Map::maxClientViewportX + 1,
	                       Map::maxClientViewportY + 1));
	CHECK(!monster->canSee(newPosition));

	moveCreatureForMonster(target, newPosition);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());

	moveCreatureForMonster(target, oldPosition);
	CHECK(hasTarget(monster, target.get()));
	CHECK(!monster->getIdleStatus());
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

	moveCreatureForMonster(target, newPosition);
	CHECK(target->getZone() == ZONE_PROTECTION);

	CHECK(monster->getTargetList().empty());
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());

	moveCreatureForMonster(target, oldPosition);
	CHECK(target->getZone() != ZONE_PROTECTION);
	CHECK(hasTarget(monster, target.get()));
	CHECK(!monster->getIdleStatus());
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

	moveCreatureForMonster(master, newMasterPosition);

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

TEST_CASE(monsters_clean_all_player_target_states_after_real_floor_transition)
{
	WorldFixture world;
	auto monsterA = makeMonster();
	auto monsterB = makeMonster();
	auto monsterC = makeMonster();
	auto player = makePlayer();
	const Position oldPosition{900, 500, 7};
	const Position newPosition{901, 500, 6};
	world.place(monsterA, Position{899, 499, 7});
	world.place(monsterB, Position{899, 500, 7});
	world.place(monsterC, Position{899, 501, 7});
	world.place(player, oldPosition);
	ensureTile(newPosition);
	addStairHeight(player->getTile());

	CHECK(monsterA->canSee(oldPosition));
	CHECK(Creature::canSee(monsterA->getPosition(), newPosition, Map::maxClientViewportX + 1,
	                       Map::maxClientViewportY + 1));
	CHECK(!monsterA->canSee(newPosition));
	selectTarget(monsterA, player);
	CHECK(monsterB->addTarget(player.get()));
	CHECK(monsterB->setFollowCreature(player.get()));
	monsterB->setAttackedCreature(nullptr);
	CHECK(monsterC->addTarget(player.get()));
	monsterC->setAttackedCreature(nullptr);
	monsterC->setFollowCreature(nullptr);
	CHECK(hasTarget(monsterA, player.get()));
	CHECK(hasTarget(monsterB, player.get()));
	CHECK(hasTarget(monsterC, player.get()));

	moveCreatureByDirection(player, DIRECTION_EAST, newPosition);

	for (const auto& monster : {monsterA, monsterB, monsterC}) {
		CHECK(!hasTarget(monster, player.get()));
		CHECK(!monster->getAttackedCreatureShared());
		CHECK(!monster->getFollowCreatureShared());
		CHECK(monster->getIdleStatus());
		CHECK(!monster->isCreatureCheckEnabled());
	}
	// Execute the work an already queued follow task would perform. The stale
	// task must observe the cleared follow pointer and stop before pathfinding.
	g_game.updateCreatureWalk(monsterA->getID());
	CHECK(monsterA->getIdleStatus());
	CHECK(!monsterA->getFollowCreatureShared());

	moveCreatureByDirection(player, DIRECTION_WEST, oldPosition);
	for (const auto& monster : {monsterA, monsterB, monsterC}) {
		CHECK(hasTarget(monster, player.get()));
		CHECK(!monster->getIdleStatus());
		CHECK(monster->isCreatureCheckEnabled());
	}
}

TEST_CASE(monster_does_not_readd_player_during_same_position_instance_change)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto newInstanceMonster = makeMonster();
	auto player = makePlayer();
	const Position position{920, 500, 7};
	world.place(monster, Position{919, 500, 7});
	newInstanceMonster->setInstanceID(7);
	world.place(newInstanceMonster, Position{921, 500, 7});
	world.place(player, position);
	selectTarget(monster, player);

	player->setInstanceID(7);

	CHECK(player->getPosition() == position);
	CHECK(player->getTile() == g_game.map.getTile(position));
	CHECK(player->getInstanceID() == 7);
	CHECK(!hasTarget(monster, player.get()));
	CHECK(!monster->getAttackedCreatureShared());
	CHECK(!monster->getFollowCreatureShared());
	CHECK(monster->getIdleStatus());
	CHECK(hasTarget(newInstanceMonster, player.get()));
	CHECK(!newInstanceMonster->getIdleStatus());
}

TEST_CASE(moving_monster_discovers_new_stationary_target)
{
	WorldFixture world;
	auto monster = makeMonster();
	auto followedTarget = makeMonster(true);
	auto stationaryTarget = makeMonster(true);
	const Position oldPosition{940, 500, 7};
	const Position newPosition{941, 500, 7};
	world.place(monster, oldPosition);
	world.place(followedTarget, Position{940, 501, 7});
	world.place(stationaryTarget, Position{950, 500, 7});
	selectTarget(monster, followedTarget);
	CHECK(!hasTarget(monster, stationaryTarget.get()));
	CHECK(!monster->canSee(stationaryTarget->getPosition()));

	moveCreatureForMonster(monster, newPosition);

	CHECK(monster->canSee(stationaryTarget->getPosition()));
	CHECK(hasTarget(monster, followedTarget.get()));
	CHECK(hasTarget(monster, stationaryTarget.get()));
}

TFS_TEST_MAIN()
