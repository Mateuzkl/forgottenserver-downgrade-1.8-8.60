// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_MONSTER_H
#define FS_MONSTER_H

#include "monsters.h"
#include "tile.h"

class Creature;
class Game;
class Spawn;

using CreatureWeakHashSet = std::set<std::weak_ptr<Creature>, std::owner_less<std::weak_ptr<Creature>>>;
using CreatureWeakList = std::vector<std::weak_ptr<Creature>>;

enum TargetSearchType_t
{
	TARGETSEARCH_DEFAULT,
	TARGETSEARCH_RANDOM,
	TARGETSEARCH_ATTACKRANGE,
	TARGETSEARCH_NEAREST,
	TARGETSEARCH_HEALTH,
	TARGETSEARCH_DAMAGE,
};

class Monster final : public Creature
{
public:
	static std::unique_ptr<Monster> createMonster(const std::string& name);
	static int32_t despawnRange;
	static int32_t despawnRadius;

	explicit Monster(const std::shared_ptr<MonsterType>& mType);
	~Monster();

	using Creature::onWalk;

	// non-copyable
	Monster(const Monster&) = delete;
	Monster& operator=(const Monster&) = delete;

	Monster* getMonster() override { return this; }
	const Monster* getMonster() const override { return this; }

	void setID() override
	{
		if (id == 0) {
			id = monsterAutoID++;
		}
	}

	void addList() override;
	void removeList() override;

	const std::string& getName() const override;
	void setName(std::string_view name);

	const std::string& getNameDescription() const override;
	void setNameDescription(std::string_view nameDescription) { this->nameDescription = nameDescription; };

	std::string getDescription(int32_t) const override
	{
		if (level > 0) {
			return nameDescription + " [Level " + std::to_string(level) + "].";
		}
		return nameDescription + '.';
	}

	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }

	const Position& getMasterPos() const { return masterPos; }
	void setMasterPos(Position pos) { masterPos = pos; }
	Faction_t getFaction() const override { return mType->info.faction; }

	RaceType_t getRace() const override { return mType->info.race; }
	int32_t getArmor() const override { return mType->info.armor; }
	int32_t getDefense() const override { return mType->info.defense; }
	float getMitigation() const override { return mType->info.mitigation; }
	bool isPushable() const override { return mType->info.pushable && baseSpeed != 0; }
	bool isAttackable() const override { return mType->info.isAttackable; }

	bool canPushItems() const;
	bool canPushCreatures() const { return mType->info.canPushCreatures; }
	bool isHostile() const { return mType->info.isHostile; }
	bool isRewardBoss() const { return mType->info.isRewardBoss; }
	bool isBoss() const { return isRewardBoss(); }
	bool canSee(const Position& pos) const override;
	bool canSeeInvisibility() const override { return isImmune(CONDITION_INVISIBLE); }

	// Influenced creature system
	bool isInfluenced() const { return influenced; }
	void setInfluenced(bool v);
	uint8_t getInfluencedLevel() const { return influencedLevel; }
	void setInfluencedLevel(uint8_t level) { influencedLevel = level; }
	bool isFiendish() const { return fiendish; }
	void setFiendish(bool v);
	Skulls_t getSkull() const override;
	int32_t getLevel() const { return level; }

	uint32_t getManaCost() const { return mType->info.manaCost; }
	void setSpawn(std::shared_ptr<Spawn> newSpawn) { spawn = newSpawn; }
	bool canWalkOnFieldType(CombatType_t combatType) const;

	void onAttackedCreatureDisappear(bool isLogout) override;

	void onCreatureAppear(Creature* creature, bool isLogin) override;
	void onRemoveCreature(Creature* creature, bool isLogout) override;
	void onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos, const Tile* oldTile,
	                    const Position& oldPos, bool teleport) override;
	void onCreatureSay(Creature* creature, SpeakClasses type, std::string_view text) override;

	void drainHealth(const std::shared_ptr<Creature>& attacker, int32_t damage) override;
	void changeHealth(int32_t healthChange, bool sendHealthChange = true) override;

	bool isWalkingToSpawn() const { return walkingToSpawn; }
	bool walkToSpawn();
	void onWalk() override;
	void onWalkComplete() override;
	bool getNextStep(Direction& direction, uint32_t& flags) override;
	void onFollowCreatureComplete(const Creature* creature) override;

	void onThink(uint32_t interval) override;

	bool challengeCreature(Creature* creature, bool force = false) override;

	void setNormalCreatureLight() override;
	bool getCombatValues(int32_t& min, int32_t& max) override;

	void doAttacking(uint32_t interval) override;
	bool hasExtraSwing() override { return lastMeleeAttack == 0; }

	bool searchTarget(TargetSearchType_t searchType = TARGETSEARCH_DEFAULT);
	bool selectTarget(Creature* creature);

	const CreatureWeakList& getTargetList() const { return targetList; }
	const CreatureWeakHashSet& getFriendList() const { return friendList; }

	bool isTarget(const Creature* creature) const;
	bool isFleeing() const
	{
		return !isSummon() && getHealth() <= mType->info.runAwayHealth && challengeFocusDuration <= 0 &&
		       OTSYS_TIME() >= fleeBlockUntil;
	}
	void blockFleeing(uint32_t durationMs) { fleeBlockUntil = std::max<int64_t>(fleeBlockUntil, OTSYS_TIME() + durationMs); }

	bool getDistanceStep(const Position& targetPos, Direction& direction, bool flee = false);
	bool isTargetNearby() const { return stepDuration >= 1; }
	bool isIgnoringFieldDamage() const { return ignoreFieldDamage; }

	BlockType_t blockHit(const std::shared_ptr<Creature>& attacker, CombatType_t combatType, int32_t& damage, bool checkDefense = false,
	                     bool checkArmor = false, bool field = false, bool ignoreResistances = false, CombatOrigin origin = ORIGIN_NONE) override;

	static uint32_t monsterAutoID;

	// for lua module
	auto getMonsterType() const { return mType.get(); }

	bool isInSpawnRange(const Position& pos) const;

	bool getIdleStatus() const { return isIdle; }
	void setIdle(bool idle);

	bool isFriend(const Creature* creature) const;
	bool isOpponent(const Creature* creature) const;
	bool isFamiliar() const;
	bool hasPlayerNearby(int32_t range = 20) const;

	void addFriend(Creature* creature);
	bool setType(const std::shared_ptr<MonsterType>& newType, bool restoreHealth = false);
	void removeFriend(Creature* creature);
	void addTarget(Creature* creature, bool pushFront = false);
	void removeTarget(Creature* creature);

	void callPlayerAttackEvent(Player* player);

private:
	CreatureWeakHashSet friendList;
	CreatureWeakList targetList;

	std::string name;
	std::string nameDescription;

	std::shared_ptr<MonsterType> mType;
	std::weak_ptr<Spawn> spawn;

	int64_t lastMeleeAttack = 0;

	uint32_t attackTicks = 0;
	uint32_t targetChangeTicks = 0;
	uint32_t defenseTicks = 0;
	uint32_t yellTicks = 0;
	int32_t minCombatValue = 0;
	int32_t maxCombatValue = 0;
	int32_t targetChangeCooldown = 0;
	int32_t challengeFocusDuration = 0;
	int32_t stepDuration = 0;
	int64_t fleeBlockUntil = 0;

	Position masterPos;

	bool ignoreFieldDamage = false;
	bool isIdle = true;
	bool isMasterInRange = false;
	bool randomStepping = false;
	bool walkingToSpawn = false;
	bool influenced = false;
	uint8_t influencedLevel = 0;
	bool fiendish = false;

	void onCreatureEnter(Creature* creature);
	void onCreatureLeave(Creature* creature);
	bool selectBlockerTarget();
	void onCreatureFound(Creature* creature, bool pushFront = false);

	void updateLookDirection();
	mutable uint64_t lastPlayerNearbyCheck = 0;
	mutable bool cachedPlayerNearby = false;

	void updateTargetList();
	void clearTargetList();
	void clearFriendList();

	void death(Creature* lastHitCreature) override;
	std::shared_ptr<Item> getCorpse(Creature* lastHitCreature, Creature* mostDamageCreature) override;

	void updateIdleStatus();

	void onAddCondition(ConditionType_t type) override;
	void onEndCondition(ConditionType_t type) override;

	bool canUseAttack(const Position& pos, const Creature* target) const;
	bool canUseSpell(const Position& pos, const Position& targetPos, const spellBlock_t& sb, uint32_t interval,
	                 bool& inRange, bool& resetTicks) const;
	bool getRandomStep(const Position& creaturePos, Direction& direction) const;
	bool getDanceStep(const Position& creaturePos, Direction& direction, bool keepAttack = true,
	                  bool keepDistance = true);
	bool canWalkTo(Position pos, Direction direction) const;
	void fleeFromTarget(const Position& targetPos, Direction& direction);

	static bool pushItem(Item* item);
	static void pushItems(Tile* tile);
	static bool pushCreature(Creature* creature);
	static void pushCreatures(Tile* tile);

	void onThinkTarget(uint32_t interval);
	void onThinkYell(uint32_t interval);
	void onThinkDefense(uint32_t interval);

	uint64_t getLostExperience() const override;
	uint16_t getLookCorpse() const override { return mType->info.lookcorpse; }
	void dropLoot(Container* corpse, Creature* lastHitCreature) override;
	uint32_t getDamageImmunities() const override { return mType->info.damageImmunities; }
	uint64_t getConditionImmunities() const override { return mType->info.conditionImmunities; }
	void getPathSearchParams(const Creature* creature, FindPathParams& fpp) const override;

	friend class LuaScriptInterface;
};

namespace monster_level {
	bool setSkullRange(Skulls_t skull, int32_t minLevel, int32_t maxLevel);
	bool setBonus(const std::string& type, float value);
	Skulls_t getSkullByLevel(int32_t level);
	float getBonusDamage();
	float getBonusSpeed();
	float getBonusHealth();
}

#endif
