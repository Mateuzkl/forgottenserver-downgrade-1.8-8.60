// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_MONSTERS_H
#define FS_MONSTERS_H

#include "creature.h"
#include "observer_ptr.h"

const uint32_t MAX_LOOTCHANCE = 100000;

struct LootBlock
{
	uint16_t id;
	uint32_t countmin;
	uint32_t countmax;
	uint32_t chance;

	// optional
	int32_t subType;
	int32_t actionId;
	std::string text;
	bool unique;

	std::vector<LootBlock> childLoot;
	LootBlock()
	{
		id = 0;
		countmin = 1;
		countmax = 1;
		chance = 0;

		subType = -1;
		actionId = -1;
		unique = false;
	}
};

class Loot
{
public:
	Loot() = default;

	// non-copyable
	Loot(const Loot&) = delete;
	Loot& operator=(const Loot&) = delete;

	LootBlock lootBlock;
};

struct summonBlock_t
{
	std::string name;
	uint32_t chance;
	uint32_t speed;
	uint32_t max;
	MagicEffectClasses effect = CONST_ME_TELEPORT;
	MagicEffectClasses masterEffect = CONST_ME_NONE;
	bool force = false;
};

class BaseSpell;
struct spellBlock_t
{
	spellBlock_t();
	~spellBlock_t();
	spellBlock_t(const spellBlock_t& other) = delete;
	spellBlock_t& operator=(const spellBlock_t& other) = delete;
	spellBlock_t(spellBlock_t&& other) noexcept;
	spellBlock_t& operator=(spellBlock_t&& other) noexcept;

	std::unique_ptr<BaseSpell> ownedSpell;
	ObserverPtr<BaseSpell> spell = nullptr;
	uint32_t chance = 100;
	uint32_t speed = 2000;
	uint32_t range = 0;
	int32_t minCombatValue = 0;
	int32_t maxCombatValue = 0;
	bool combatSpell = false;
	bool isMelee = false;
};

struct voiceBlock_t
{
	std::string text;
	bool yellText;
};

struct targetStrategies_t
{
	uint32_t nearest = 0;
	uint32_t health = 0;
	uint32_t damage = 0;
	uint32_t random = 0;
};

class MonsterType
{
	struct MonsterInfo
	{
		ObserverPtr<LuaScriptInterface> scriptInterface;

		std::map<CombatType_t, int32_t> elementMap;
		std::map<CombatType_t, int32_t> reflectMap;
		std::map<CombatType_t, int32_t> healingMap;

		std::vector<voiceBlock_t> voiceVector;

		std::vector<LootBlock> lootItems;
		std::vector<std::string> scripts;
		std::vector<spellBlock_t> attackSpells;
		std::vector<spellBlock_t> defenseSpells;
		std::vector<summonBlock_t> summons;

		Skulls_t skull = SKULL_NONE;
		GuildEmblems_t emblem = GUILDEMBLEM_NONE;
		Outfit_t outfit = {};
		RaceType_t race = RACE_BLOOD;

		LightInfo light = {};
		uint16_t lookcorpse = 0;

		uint64_t experience = 0;

		uint32_t manaCost = 0;
		uint32_t yellChance = 0;
		uint32_t yellSpeedTicks = 0;
		uint32_t staticAttackChance = 95;
		uint16_t critChance = 0;
		uint32_t maxSummons = 0;
		uint32_t changeTargetSpeed = 0;
		uint64_t conditionImmunities = 0;
		uint32_t damageImmunities = 0;
		uint32_t baseSpeed = 200;

		int32_t creatureAppearEvent = -1;
		int32_t creatureDisappearEvent = -1;
		int32_t creatureMoveEvent = -1;
		int32_t creatureSayEvent = -1;
		int32_t thinkEvent = -1;
		int32_t deathEvent = -1;
		int32_t playerAttackEvent = -1;
		int32_t targetDistance = 1;
		int32_t runAwayHealth = 0;
		int32_t health = 100;
		int32_t healthMax = 100;
		int32_t changeTargetChance = 0;
		int32_t defense = 0;
		int32_t armor = 0;
		float mitigation = 0.0f;
		int32_t minLevel = 0;
		int32_t maxLevel = 0;

		targetStrategies_t targetStrategies = {};
		Faction_t faction = FACTION_DEFAULT;
		std::set<Faction_t> enemyFactions;

		bool canPushItems = false;
		bool canPushCreatures = false;
		bool pushable = true;
		bool isAttackable = true;
		bool isBoss = false;
		bool isRewardBoss = false;
		bool isChallengeable = true;
		bool isConvinceable = false;
		bool isHostile = true;
		bool isBlockable = true;
		bool isIllusionable = false;
		bool isSummonable = false;
		bool hiddenHealth = false;
		bool canWalkOnEnergy = true;
		bool canWalkOnFire = true;
		bool canWalkOnPoison = true;

		MonstersEvent_t eventType = MONSTERS_EVENT_NONE;
	};

public:
	MonsterType() = default;

	// non-copyable
	MonsterType(const MonsterType&) = delete;
	MonsterType& operator=(const MonsterType&) = delete;

	bool loadCallback(LuaScriptInterface* scriptInterface);

	std::string name;
	std::string nameDescription;
	uint32_t raceId = 0;

	MonsterInfo info{};

	void loadLoot(MonsterType* monsterType, LootBlock lootBlock);
};

class MonsterSpell
{
public:
	MonsterSpell() = default;

	MonsterSpell(const MonsterSpell&) = delete;
	MonsterSpell& operator=(const MonsterSpell&) = delete;

	std::string name = "";
	std::string scriptName = "";

	uint8_t chance = 100;
	uint8_t range = 0;
	uint8_t drunkenness = 0;

	uint16_t interval = 2000;

	int32_t minCombatValue = 0;
	int32_t maxCombatValue = 0;
	int32_t attack = 0;
	int32_t skill = 0;
	int32_t length = 0;
	int32_t spread = 0;
	int32_t radius = 0;
	int32_t ring = 0;
	int32_t conditionMinDamage = 0;
	int32_t conditionMaxDamage = 0;
	int32_t conditionStartDamage = 0;
	int32_t tickInterval = 0;
	int32_t minSpeedChange = 0;
	int32_t maxSpeedChange = 0;
	int32_t duration = 0;

	bool isScripted = false;
	bool needTarget = false;
	bool needDirection = false;
	bool combatSpell = false;
	bool isMelee = false;

	Outfit_t outfit = {};
	ShootType_t shoot = CONST_ANI_NONE;
	MagicEffectClasses effect = CONST_ME_NONE;
	ConditionType_t conditionType = CONDITION_NONE;
	CombatType_t combatType = COMBAT_UNDEFINEDDAMAGE;
};

class Monsters
{
public:
	Monsters();
	~Monsters();

	// non-copyable
	Monsters(const Monsters&) = delete;
	Monsters& operator=(const Monsters&) = delete;

	bool isLoaded() const { return loaded; }
	bool reload();

	MonsterType* getMonsterType(const std::string &name);
	MonsterType* getMonsterType(uint32_t raceId);
	std::shared_ptr<MonsterType> getSharedMonsterType(const std::string &name);
	bool deserializeSpell(MonsterSpell* spell, spellBlock_t& sb, const std::string& description = "");
	bool registerBestiaryMonster(const MonsterType* mType);

	std::unique_ptr<LuaScriptInterface> scriptInterface;
	std::map<std::string, std::shared_ptr<MonsterType>> monsters;

private:
	Condition_ptr getDamageCondition(ConditionType_t conditionType, int32_t maxDamage, int32_t minDamage,
	                                 int32_t startDamage, uint32_t tickInterval);

	std::unordered_map<uint32_t, std::string> bestiaryMonsters;

	bool loaded = false;
};

#endif
