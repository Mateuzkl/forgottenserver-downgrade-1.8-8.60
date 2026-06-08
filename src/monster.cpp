// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "monster.h"

#include "configmanager.h"
#include "events.h"
#include "game.h"
#include "iologindata.h"
#include "logger.h"
#include "player.h"
#include "scriptmanager.h"
#include "spells.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <limits>

extern Game g_game;
extern Monsters g_monsters;

int32_t Monster::despawnRange;
int32_t Monster::despawnRadius;

uint32_t Monster::monsterAutoID = 0x40000000;

std::unique_ptr<Monster> Monster::createMonster(const std::string& name)
{
	auto mType = g_monsters.getSharedMonsterType(name);
	if (!mType) {
		return nullptr;
	}
	return std::make_unique<Monster>(mType);
}

namespace monster_level {
	struct Config {
		float bonusDmg = 0.0f;
		float bonusSpeed = 0.0f;
		float bonusHP = 0.0f;
		struct SkullRange { int32_t min = 0; int32_t max = 0; };
		SkullRange whiteRange = {1, 99};
		SkullRange redRange = {100, 499};
		SkullRange blackRange = {500, 2000};
	};

	static Config config;

	Skulls_t getSkullByLevel(int32_t lvl)
	{
		if (lvl >= config.whiteRange.min && lvl <= config.whiteRange.max) {
			return SKULL_WHITE;
		}
		if (lvl >= config.redRange.min && lvl <= config.redRange.max) {
			return SKULL_RED;
		}
		if (lvl >= config.blackRange.min && lvl <= config.blackRange.max) {
			return SKULL_BLACK;
		}
		return SKULL_NONE;
	}

	bool setSkullRange(Skulls_t skull, int32_t minLevel, int32_t maxLevel)
	{
		if (minLevel > maxLevel) {
			return false;
		}

		switch (skull) {
			case SKULL_WHITE:
				config.whiteRange = {minLevel, maxLevel};
				return true;
			case SKULL_RED:
				config.redRange = {minLevel, maxLevel};
				return true;
			case SKULL_BLACK:
				config.blackRange = {minLevel, maxLevel};
				return true;
			default:
				return false;
		}
	}

	bool setBonus(const std::string& type, float value)
	{
		if (!std::isfinite(value)) {
			return false;
		}

		if (type == "damage") {
			config.bonusDmg = value;
			return true;
		} else if (type == "speed") {
			config.bonusSpeed = value;
			return true;
		} else if (type == "health") {
			config.bonusHP = value;
			return true;
		}
		return false;
	}

	float getBonusDamage() { return config.bonusDmg; }
	float getBonusSpeed() { return config.bonusSpeed; }
	float getBonusHealth() { return config.bonusHP; }
}

Skulls_t Monster::getSkull() const
{
	if (fiendish) {
		return SKULL_RED;
	}
	if (influenced) {
		return SKULL_GREEN;
	}
	return Creature::getSkull();
}

void Monster::setInfluenced(bool v)
{
	influenced = v;
	if (fiendish) {
		setIcon("forge", CreatureIcon(CreatureIconModifications_Fiendish));
	} else if (influenced) {
		setIcon("forge", CreatureIcon(CreatureIconModifications_Influenced));
	} else {
		removeIcon("forge");
	}
	g_game.updateCreatureSkull(this);
}

void Monster::setFiendish(bool v)
{
	fiendish = v;
	if (fiendish) {
		setIcon("forge", CreatureIcon(CreatureIconModifications_Fiendish));
	} else if (influenced) {
		setIcon("forge", CreatureIcon(CreatureIconModifications_Influenced));
	} else {
		removeIcon("forge");
	}
	g_game.updateCreatureSkull(this);
}

Monster::Monster(const std::shared_ptr<MonsterType>& mType) : Creature(), nameDescription(mType->nameDescription), mType(mType)
{
	defaultOutfit = mType->info.outfit;
	currentOutfit = mType->info.outfit;
	skull = mType->info.skull;
	emblem = mType->info.emblem;
	health = mType->info.health;
	healthMax = mType->info.healthMax;
	baseSpeed = mType->info.baseSpeed;
	internalLight = mType->info.light;
	hiddenHealth = mType->info.hiddenHealth;
	targetList.reserve(24);

	if (ConfigManager::getBoolean(ConfigManager::MONSTER_LEVEL_ENABLED) && mType->info.minLevel > 0 &&
	    mType->info.maxLevel >= mType->info.minLevel) {
		level = uniform_random(mType->info.minLevel, mType->info.maxLevel);

		skull = monster_level::getSkullByLevel(level);

		float bonusHP = monster_level::getBonusHealth();
		if (bonusHP != 0.0f) {
			const int64_t newHealthMax = static_cast<int64_t>(healthMax) +
			                             static_cast<int64_t>(
			                                 std::round(static_cast<double>(healthMax) * bonusHP * level));
			healthMax = static_cast<int32_t>(std::clamp(newHealthMax, static_cast<int64_t>(1),
			                                            static_cast<int64_t>(std::numeric_limits<int32_t>::max())));
			health = healthMax;
		}

		float bonusSpeed = monster_level::getBonusSpeed();
		if (bonusSpeed != 0.0f) {
			const int64_t newSpeed = static_cast<int64_t>(baseSpeed) +
			                         static_cast<int64_t>(
			                             std::round(static_cast<double>(baseSpeed) * bonusSpeed * level));
			baseSpeed = static_cast<uint32_t>(std::clamp(newSpeed, static_cast<int64_t>(0),
			                                             static_cast<int64_t>(std::numeric_limits<uint32_t>::max())));
		}
	}

	// register creature events
	for (std::string_view scriptName : mType->info.scripts) {
		if (!registerCreatureEvent(scriptName)) {
			LOG_WARN(fmt::format("[Warning - Monster::Monster] Unknown event name: {}", scriptName));
		}
	}
}

Monster::~Monster()
{
	clearTargetList();
	spawn = std::weak_ptr<Spawn>();
}

uint64_t Monster::getLostExperience() const
{
	if (!skillLoss) {
		return 0;
	}
	uint64_t xp = mType->info.experience;
	if (caseInsensitiveEqual(mType->name, g_game.getBoostedCreature())) {
		float mult = ConfigManager::getFloat(ConfigManager::BOOSTED_EXP_MULTIPLIER);
		xp = static_cast<uint64_t>(xp * mult);
	}
	return xp;
}

void Monster::addList() { g_game.addMonster(this); }

void Monster::removeList() { g_game.removeMonster(this); }

const std::string& Monster::getName() const
{
	if (name.empty()) {
		return mType->name;
	}
	return name;
}

void Monster::setName(std::string_view name)
{
	if (getName() == name) {
		return;
	}

	this->name = name;

	// NOTE: Due to how client caches known creatures,
	// it is not feasible to send creature update to everyone that has ever met it
	g_game.updateKnownCreature(this);
}

const std::string& Monster::getNameDescription() const
{
	if (nameDescription.empty()) {
		return mType->nameDescription;
	}
	return nameDescription;
}

bool Monster::canSee(const Position& pos) const
{
	if (pos.z != getPosition().z) {
		return false;
	}
	return Creature::canSee(getPosition(), pos, Map::maxClientViewportX + 1, Map::maxClientViewportY + 1);
}

bool Monster::canWalkOnFieldType(CombatType_t combatType) const
{
	switch (combatType) {
		case COMBAT_ENERGYDAMAGE:
			return mType->info.canWalkOnEnergy;
		case COMBAT_FIREDAMAGE:
			return mType->info.canWalkOnFire;
		case COMBAT_EARTHDAMAGE:
			return mType->info.canWalkOnPoison;
		default:
			return true;
	}
}

void Monster::onAttackedCreatureDisappear(bool) { attackTicks = 0; }

void Monster::onCreatureAppear(Creature* creature, bool isLogin)
{
	Creature::onCreatureAppear(creature, isLogin);

	if (mType->info.creatureAppearEvent != -1) {
		// onCreatureAppear(self, creature)
		LuaScriptInterface* scriptInterface = mType->info.scriptInterface;
		if (!scriptInterface->reserveScriptEnv()) {
			LOG_ERROR("[Error - Monster::onCreatureAppear] Call stack overflow");
			return;
		}

		ScriptEnvironment* env = scriptInterface->getScriptEnv();
		env->setScriptId(mType->info.creatureAppearEvent, scriptInterface);

		lua_State* L = scriptInterface->getLuaState();
		scriptInterface->pushFunction(mType->info.creatureAppearEvent);

		Lua::pushUserdata<Monster>(L, this);
		Lua::setMetatable(L, -1, "Monster");

		Lua::pushUserdata<Creature>(L, creature);
		Lua::setCreatureMetatable(L, -1, creature);

		scriptInterface->callFunction(2);
	}

	if (creature == this) {
		// We just spawned lets look around to see who is there.
		if (isSummon()) {
			auto master = getMaster();
			if (master) {
				const bool sameInstance = getInstanceID() == master->getInstanceID();
				isMasterInRange = sameInstance && canSee(master->getPosition());
			}
		}

		updateTargetList();
		updateIdleStatus();
	} else {
		onCreatureEnter(creature);
	}
}

void Monster::onRemoveCreature(Creature* creature, bool isLogout)
{
	Creature::onRemoveCreature(creature, isLogout);

	if (mType->info.creatureDisappearEvent != -1) {
		// onCreatureDisappear(self, creature)
		LuaScriptInterface* scriptInterface = mType->info.scriptInterface;
		if (!scriptInterface->reserveScriptEnv()) {
			LOG_ERROR("[Error - Monster::onCreatureDisappear] Call stack overflow");
			return;
		}

		ScriptEnvironment* env = scriptInterface->getScriptEnv();
		env->setScriptId(mType->info.creatureDisappearEvent, scriptInterface);

		lua_State* L = scriptInterface->getLuaState();
		scriptInterface->pushFunction(mType->info.creatureDisappearEvent);

		Lua::pushUserdata<Monster>(L, this);
		Lua::setMetatable(L, -1, "Monster");

		Lua::pushUserdata<Creature>(L, creature);
		Lua::setCreatureMetatable(L, -1, creature);

		scriptInterface->callFunction(2);
	}

	if (creature == this) {
		if (auto sp = spawn.lock()) {
			sp->startSpawnCheck();
		}

		setIdle(true);
	} else {
		onCreatureLeave(creature);
	}
}

void Monster::onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos, const Tile* oldTile,
                             const Position& oldPos, bool teleport)
{
	Creature::onCreatureMove(creature, newTile, newPos, oldTile, oldPos, teleport);

	if (mType->info.creatureMoveEvent != -1) {
		// onCreatureMove(self, creature, oldPosition, newPosition)
		LuaScriptInterface* scriptInterface = mType->info.scriptInterface;
		if (!scriptInterface->reserveScriptEnv()) {
			LOG_ERROR("[Error - Monster::onCreatureMove] Call stack overflow");
			return;
		}

		ScriptEnvironment* env = scriptInterface->getScriptEnv();
		env->setScriptId(mType->info.creatureMoveEvent, scriptInterface);

		lua_State* L = scriptInterface->getLuaState();
		scriptInterface->pushFunction(mType->info.creatureMoveEvent);

		Lua::pushUserdata<Monster>(L, this);
		Lua::setMetatable(L, -1, "Monster");

		Lua::pushUserdata<Creature>(L, creature);
		Lua::setCreatureMetatable(L, -1, creature);

		Lua::pushPosition(L, oldPos);
		Lua::pushPosition(L, newPos);

		scriptInterface->callFunction(4);
	}

	if (creature == this) {
		if (isSummon()) {
			auto master = getMaster();
			if (master) {
				const bool sameInstance = getInstanceID() == master->getInstanceID();
				isMasterInRange = sameInstance && canSee(master->getPosition());
			}
			updateTargetList();
		} else {
			if (followCreature.expired()) {
				updateTargetList();
			}
		}
		updateIdleStatus();
	} else {
		bool canSeeNewPos = canSee(newPos);
		bool canSeeOldPos = canSee(oldPos);

		if (canSeeNewPos && !canSeeOldPos) {
			onCreatureEnter(creature);
		} else if (!canSeeNewPos && canSeeOldPos) {
			onCreatureLeave(creature);
		} else if (canSeeNewPos && canSeeOldPos) {
			// Handle PZ entry/exit while creature remains visible
			bool oldInPZ = oldTile && oldTile->getZone() == ZONE_PROTECTION;
			bool newInPZ = newTile && newTile->getZone() == ZONE_PROTECTION;
			if (!oldInPZ && newInPZ) {
				onCreatureLeave(creature);
			} else if (oldInPZ && !newInPZ) {
				onCreatureEnter(creature);
			}
		}

		auto master = getMaster();
		if (canSeeNewPos && master && master.get() == creature && getInstanceID() == master->getInstanceID()) {
			isMasterInRange = true; // Follow master again.
		}

		if (isIdle) {
			updateIdleStatus();
		}

		if (!isSummon()) {
			if (auto fc = followCreature.lock()) {
				if (!hasFollowPath && isOpponent(creature) && creature != fc.get()) {
					auto ac = attackedCreature.lock();
					if (!ac || !canSeeCreature(ac.get())) {
						selectTarget(creature);
					}
				} else {
					const Position& followPosition = fc->getPosition();
					const Position& position = getPosition();

					int32_t offset_x = followPosition.getDistanceX(position);
					int32_t offset_y = followPosition.getDistanceY(position);
					if (offset_x > 1 || offset_y > 1) {
						selectBlockerTarget();
					}
				}
			} else if (isOpponent(creature)) {
				// we have no target lets try pick this one
				selectTarget(creature);
			}
		}
	}
}

void Monster::onCreatureSay(Creature* creature, SpeakClasses type, std::string_view text)
{
	Creature::onCreatureSay(creature, type, text);

	if (mType->info.creatureSayEvent != -1) {
		// onCreatureSay(self, creature, type, message)
		LuaScriptInterface* scriptInterface = mType->info.scriptInterface;
		if (!scriptInterface->reserveScriptEnv()) {
			LOG_ERROR("[Error - Monster::onCreatureSay] Call stack overflow");
			return;
		}

		ScriptEnvironment* env = scriptInterface->getScriptEnv();
		env->setScriptId(mType->info.creatureSayEvent, scriptInterface);

		lua_State* L = scriptInterface->getLuaState();
		scriptInterface->pushFunction(mType->info.creatureSayEvent);

		Lua::pushUserdata<Monster>(L, this);
		Lua::setMetatable(L, -1, "Monster");

		Lua::pushUserdata<Creature>(L, creature);
		Lua::setCreatureMetatable(L, -1, creature);

		lua_pushinteger(L, type);
		Lua::pushString(L, text);

		scriptInterface->callVoidFunction(4);
	}
}

void Monster::addFriend(Creature* creature)
{
	assert(creature != this);
	auto weakRef = g_game.getCreatureWeakRef(creature);
	if (!weakRef.expired()) {
		friendList.insert(std::move(weakRef));
	}
}

bool Monster::setType(const std::shared_ptr<MonsterType>& newType, bool restoreHealth)
{
	// Adapted from Canary's luaMonsterSetType
	if (!newType) {
		return false;
	}

	// Unregister creature events (current MonsterType)
	for (const std::string& scriptName : mType->info.scripts) {
		if (!unregisterCreatureEvent(scriptName)) {
			LOG_WARN(fmt::format("[Warning - Monster::setType] Unknown event name: {}", scriptName));
		}
	}

	// Assign new MonsterType
	mType = newType;
	nameDescription = asLowerCaseString(newType->nameDescription);
	defaultOutfit = newType->info.outfit;
	currentOutfit = newType->info.outfit;
	skull = newType->info.skull;

	// Update stats (adapted from Canary)
	float multiplier = 1.0f;
	healthMax = newType->info.healthMax * multiplier;
	baseSpeed = newType->info.baseSpeed;
	internalLight = newType->info.light;
	hiddenHealth = newType->info.hiddenHealth;

	// Handle health based on restoreHealth parameter
	if (restoreHealth) {
		// Reset health to new type's max health
		health = newType->info.health * multiplier;
	} else {
		// Preserve current health, capping at new max
		health = std::min(health, healthMax);
	}

	// Register creature events (new MonsterType)
	for (const std::string& scriptName : newType->info.scripts) {
		if (!registerCreatureEvent(scriptName)) {
			LOG_WARN(fmt::format("[Warning - Monster::setType] Unknown event name: {}", scriptName));
		}
	}

	// Reload creature on spectators
	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, getPosition(), true, true);
	for (const auto& spectator : spectators.players()) {
		static_cast<Player*>(spectator.get())->sendUpdateTileCreature(this);
	}

	return true;
}

void Monster::removeFriend(Creature* creature)
{
	auto weakRef = g_game.getCreatureWeakRef(creature);
	auto it = friendList.find(weakRef);
	if (it != friendList.end()) {
		friendList.erase(it);
	}
}

void Monster::addTarget(Creature* creature, bool pushFront /* = false*/)
{
	assert(creature != this);
	if (!creature || !canSeeCreature(creature)) {
		return;
	}

	auto weakRef = g_game.getCreatureWeakRef(creature);
	if (weakRef.expired()) return;

	// Check if already present
	for (const auto& w : targetList) {
		if (w.lock().get() == creature) return;
	}

	if (pushFront) {
		targetList.insert(targetList.begin(), std::move(weakRef));
	} else {
		targetList.push_back(std::move(weakRef));
	}
}

void Monster::removeTarget(Creature* creature)
{
	auto it = std::find_if(targetList.begin(), targetList.end(), [creature](const auto& weakRef) {
		return weakRef.lock().get() == creature;
	});
	if (it != targetList.end()) {
		targetList.erase(it);
	}
}

void Monster::updateTargetList()
{
	std::erase_if(friendList, [this](const auto& weakRef) {
		auto creature = weakRef.lock();
		return !creature || creature->isDead() || !canSee(creature->getPosition());
	});

	std::erase_if(targetList, [this](const auto& weakRef) {
		auto creature = weakRef.lock();
		return !creature || creature->isDead() || !canSee(creature->getPosition()) || !canSeeCreature(creature.get()) ||
		       (!isFamiliar() && creature->getZone() == ZONE_PROTECTION) || !isOpponent(creature.get());
	});

	SpectatorVec spectators;
	// OPTIMIZATION: Use multifloor=true (like upstream) to reduce spectator count
	g_game.map.getSpectators(spectators, position, true);
	spectators.erase(this);
	for (const auto& spectator : spectators) {
		onCreatureFound(spectator.get());
	}
}

void Monster::clearTargetList()
{
	targetList.clear();
}

void Monster::clearFriendList()
{
	friendList.clear();
}

void Monster::onCreatureFound(Creature* creature, bool pushFront /* = false*/)
{
	if (!creature) {
		return;
	}

	if (!canSee(creature->getPosition())) {
		return;
	}

	if (!canSeeCreature(creature)) {
		return;
	}

	if (isFriend(creature)) {
		addFriend(creature);
	}

	if (isOpponent(creature)) {
		if (!isFamiliar() && creature->getZone() == ZONE_PROTECTION) {
			return;
		}
		addTarget(creature, pushFront);
	}

	updateIdleStatus();
}

void Monster::onCreatureEnter(Creature* creature)
{
	// LOG_INFO(fmt::format("onCreatureEnter - {}", creature->getName()));

	auto master = getMaster();
	if (master && master.get() == creature) {
		// Follow master again
		isMasterInRange = true;
	}

	onCreatureFound(creature, true);

	if (creature->isPlayer()) {
		// A player entered, we might need to notice other monsters now
		updateTargetList();
	}
}


bool Monster::hasPlayerNearby(int32_t range /* = 20*/) const
{
	if (g_game.getPlayersOnline() == 0) {
		return false;
	}

	uint64_t currentTime = OTSYS_TIME();
	if (lastPlayerNearbyCheck == currentTime) {
		return cachedPlayerNearby;
	}

	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, position, true, true, range, range, range, range);
	cachedPlayerNearby = !spectators.empty();
	lastPlayerNearbyCheck = currentTime;
	return cachedPlayerNearby;
}

bool Monster::isFriend(const Creature* creature) const
{
	if (creature == this) {
		return true;
	}

	if (mType->info.faction != FACTION_DEFAULT && mType->info.faction == creature->getFaction()) {
		return true;
	}

	auto master = getMaster();
	if (isSummon() && master && master->getPlayer()) {
		const Player* masterPlayer = master->getPlayer();
		const Player* tmpPlayer = nullptr;

		if (creature->getPlayer()) {
			tmpPlayer = creature->getPlayer();
		} else {
			auto creatureMaster = creature->getMaster();

			if (creatureMaster && creatureMaster->getPlayer()) {
				tmpPlayer = creatureMaster->getPlayer();
			}
		}

		if (tmpPlayer && (tmpPlayer == master.get() || masterPlayer->isPartner(tmpPlayer))) {
			return true;
		}
	} else if (creature->isMonster() && !creature->isSummon()) {
		if (isOpponent(creature)) {
			return false;
		}
		return true;
	}

	return false;
}

bool Monster::isOpponent(const Creature* creature) const
{
	if (creature == this) {
		return false;
	}

	auto creatureMaster = creature->getMaster();
	if ((creature->getPlayer() && !creature->getPlayer()->hasFlag(PlayerFlag_IgnoredByMonsters)) ||
	    (creatureMaster && creatureMaster->getPlayer())) {
		return true;
	}

	if (mType->info.enemyFactions.count(creature->getFaction()) > 0) {
		if (creature->isMonster() && !creature->isSummon() && !hasPlayerNearby(20)) {
			return false;
		}
		return true;
	}

	auto master = getMaster();
	if (isSummon() && master && master->getPlayer()) {
		if (creature != master.get()) {
			return true;
		}
	}

	return false;
}

bool Monster::isFamiliar() const
{
	return isSummon() && getEmblem() == GUILDEMBLEM_ALLY;
}

void Monster::onCreatureLeave(Creature* creature)
{
	// LOG_INFO(fmt::format("onCreatureLeave - {}", creature->getName()));

	auto master = getMaster();
	if (master && master.get() == creature) {
		// Take random steps and only use defense abilities (e.g. heal) until its master comes back
		isMasterInRange = false;
	}

	// update friendList
	if (isFriend(creature)) {
		removeFriend(creature);
	}

	// update targetList
	if (isOpponent(creature)) {
		removeTarget(creature);
		updateIdleStatus();

		if (!isSummon() && targetList.empty()) {
			const int64_t walkToSpawnRadius = getInteger(ConfigManager::DEFAULT_WALKTOSPAWNRADIUS);
			if (walkToSpawnRadius > 0 && !position.isInRange(masterPos, walkToSpawnRadius, walkToSpawnRadius)) {
				walkToSpawn();
			}
		}
	}

	if (creature->isPlayer()) {
		// A player left, we might need to stop fighting other monsters
		updateTargetList();
	}
}

bool Monster::searchTarget(TargetSearchType_t searchType /*= TARGETSEARCH_DEFAULT*/)
{
	if (isRemoved()) {
		return false;
	}

	std::vector<Creature*> resultList;
	resultList.reserve(targetList.size());
	const Position& myPos = getPosition();

	for (const auto& weakRef : targetList) {
		auto creature = weakRef.lock();
		if (!creature || creature->isRemoved() || !creature->getTile()) {
			continue;
		}

		auto follow = followCreature.lock();
		if (follow.get() != creature.get() && isTarget(creature.get())) {
			if (searchType == TARGETSEARCH_RANDOM || canUseAttack(myPos, creature.get())) {
				resultList.push_back(creature.get());
			}
		}
	}

	switch (searchType) {
		case TARGETSEARCH_NEAREST: {
			Creature* target = nullptr;
			if (!resultList.empty()) {
				auto it = resultList.begin();
				target = *it;

				if (++it != resultList.end()) {
					const Position& targetPosition = target->getPosition();
					int32_t minRange = myPos.getDistanceX(targetPosition) + myPos.getDistanceY(targetPosition);
					do {
						Creature* creature = *it;
						if (!creature || creature->isRemoved() || !creature->getTile()) {
							continue;
						}

						const Position& pos = creature->getPosition();
						if (int32_t distance = myPos.getDistanceX(pos) + myPos.getDistanceY(pos); distance < minRange) {
							target = creature;
							minRange = distance;
						}
					} while (++it != resultList.end());
				}
			} else {
				int32_t minRange = std::numeric_limits<int32_t>::max();
				for (const auto& weakRef : targetList) {
					auto creature = weakRef.lock();
					if (!creature || creature->isRemoved() || !creature->getTile()) {
						continue;
					}

					if (!isTarget(creature.get())) {
						continue;
					}

					const Position& pos = creature->getPosition();
					if (int32_t distance = myPos.getDistanceX(pos) + myPos.getDistanceY(pos); distance < minRange) {
						target = creature.get();
						minRange = distance;
					}
				}
			}

			if (target && selectTarget(target)) {
				return true;
			}
			break;
		}

		case TARGETSEARCH_HEALTH: {
			Creature* target = nullptr;
			int32_t minHealth = std::numeric_limits<int32_t>::max();
			if (!resultList.empty()) {
				for (Creature* creature : resultList) {
					if (int32_t health = creature->getHealth(); health < minHealth) {
						target = creature;
						minHealth = health;
					}
				}
			} else {
				for (const auto& weakRef : targetList) {
					auto creature = weakRef.lock();
					if (!creature || creature->isRemoved() || !creature->getTile() || !isTarget(creature.get())) {
						continue;
					}

					if (int32_t health = creature->getHealth(); health < minHealth) {
						target = creature.get();
						minHealth = health;
					}
				}
			}

			if (target && selectTarget(target)) {
				return true;
			}
			break;
		}

		case TARGETSEARCH_DAMAGE: {
			Creature* target = nullptr;
			int32_t maxDamage = -1;
			if (!resultList.empty()) {
				for (Creature* creature : resultList) {
					auto it = damageMap.find(creature->getID());
					if (it != damageMap.end()) {
						if (it->second.total > maxDamage) {
							target = creature;
							maxDamage = it->second.total;
						}
					}
				}
			} else {
				for (const auto& weakRef : targetList) {
					auto creature = weakRef.lock();
					if (!creature || creature->isRemoved() || !creature->getTile() || !isTarget(creature.get())) {
						continue;
					}

					auto it = damageMap.find(creature->getID());
					if (it != damageMap.end()) {
						if (it->second.total > maxDamage) {
							target = creature.get();
							maxDamage = it->second.total;
						}
					}
				}
			}

			if (target && selectTarget(target)) {
				return true;
			}
			break;
		}

		case TARGETSEARCH_DEFAULT:
		case TARGETSEARCH_ATTACKRANGE:
		case TARGETSEARCH_RANDOM:
		default: {
			if (!resultList.empty()) {
				return selectTarget(resultList[uniform_random(0, resultList.size() - 1)]);
			}

			if (searchType == TARGETSEARCH_ATTACKRANGE) {
				return false;
			}

			break;
		}
	}

	// lets just pick the first target in the list
	for (const auto& weakRef : targetList) {
		auto target = weakRef.lock();
		if (!target || target->isRemoved() || !target->getTile()) {
			continue;
		}

		auto follow = followCreature.lock();
		if (follow.get() != target.get() && isTarget(target.get()) && selectTarget(target.get())) {
			return true;
		}
	}
	return false;
}

bool Monster::selectBlockerTarget()
{
	if (isRemoved() || isDead() || isSummon() || challengeFocusDuration > 0) {
		return false;
	}

	auto fc = followCreature.lock();
	if (!fc) {
		return false;
	}

	const Position& myPos = getPosition();
	const Position& targetPos = fc->getPosition();

	if (myPos.z != targetPos.z || !canSee(targetPos)) {
		return false;
	}

	// Only for monsters that want to be close (melee)
	if (mType->info.targetDistance > 1) {
		return false;
	}

	Direction dir = getDirectionTo(myPos, targetPos);
	Position nextPos = getNextPosition(dir, myPos);

	Tile* tile = g_game.map.getTile(nextPos);
	if (!tile) {
		return false;
	}

	Creature* blocker = tile->getTopCreature();
	if (blocker && blocker != fc.get() && isOpponent(blocker)) {
		return selectTarget(blocker);
	}

	return false;
}

void Monster::onFollowCreatureComplete(const Creature* creature)
{
	if (creature) {
		auto it = std::find_if(targetList.begin(), targetList.end(), [creature](const auto& weakRef) {
			return weakRef.lock().get() == creature;
		});
		if (it != targetList.end()) {
			auto weakRef = std::move(*it);
			targetList.erase(it);

			if (hasFollowPath) {
				targetList.insert(targetList.begin(), std::move(weakRef));
			} else if (!isSummon()) {
				targetList.push_back(std::move(weakRef));
			}
			// if summon and !hasFollowPath: just drop — weak_ptr expires naturally
		}
	}
}

BlockType_t Monster::blockHit(const std::shared_ptr<Creature>& attacker, CombatType_t combatType, int32_t& damage,
                              bool checkDefense /* = false*/, bool checkArmor /* = false*/, bool field /* = false */,
                              bool ignoreResistances /* = false */, CombatOrigin origin /* = ORIGIN_NONE */)
{
	BlockType_t blockType = Creature::blockHit(attacker, combatType, damage, checkDefense, checkArmor, field, ignoreResistances, origin);

	if (damage != 0) {
		int32_t elementMod = 0;
		auto it = mType->info.elementMap.find(combatType);
		if (it != mType->info.elementMap.end()) {
			elementMod = it->second;
		}

		if (elementMod != 0) {
			damage = static_cast<int32_t>(std::round(damage * ((100 - elementMod) / 100.)));
			if (damage <= 0) {
				damage = 0;
				blockType = BLOCK_ARMOR;
			}
		}
	}

	if (field) {
		ignoreFieldDamage = true;
	} else if (damage > 0 && combatType != COMBAT_HEALING && attacker) {
		const auto master = attacker->getMaster();
		if (attacker->isPlayer() || (master && master->isPlayer())) {
			ignoreFieldDamage = true;
		}
	}

	return blockType;
}

bool Monster::isTarget(const Creature* creature) const
{
	// Debug: detect programming errors early
	assert(creature != nullptr);

	if (!creature) {
		return false;
	}

	if (creature->isRemoved() || !creature->isAttackable() || creature->getZone() == ZONE_PROTECTION ||
	    !canSeeCreature(creature)) {
		return false;
	}

	if (creature->getPosition().z != getPosition().z) {
		return false;
	}
	return true;
}

bool Monster::selectTarget(Creature* creature)
{
	if (!isTarget(creature)) {
		return false;
	}

	if (creature->getPlayer()) {
		if (creature->getPlayer()->getProtectionTime() > 0) {
			return false;
		}
	}

	auto it = std::find_if(targetList.begin(), targetList.end(),
		[creature](const std::weak_ptr<Creature>& w) { return w.lock().get() == creature; });
	if (it == targetList.end()) {
		// Target not found in our target list.
		return false;
	}

	if (isHostile() || isSummon()) {
		if (setAttackedCreature(creature) && !isSummon()) {
			g_dispatcher.addTask([id = getID()]() { g_game.checkCreatureAttack(id); });
		}
	}
	return setFollowCreature(creature);
}

void Monster::setIdle(bool idle)
{
	if (isRemoved() || isDead()) {
		return;
	}

	isIdle = idle;

	if (!isIdle) {
		g_game.addCreatureCheck(this);
	} else {
		onIdleStatus();
		clearTargetList();
		clearFriendList();
		Game::removeCreatureCheck(this);
	}
}

void Monster::updateIdleStatus()
{
	bool idle = false;
	if (!isSummon() && targetList.empty()) {
		idle = std::find_if(conditions.begin(), conditions.end(),
		                    [](const auto& condition) { return condition->isAggressive(); }) == conditions.end();
	}

	setIdle(idle);
}

void Monster::onAddCondition(ConditionType_t)
{
	updateIdleStatus();
}

void Monster::onEndCondition(ConditionType_t type)
{
	if (type == CONDITION_FIRE || type == CONDITION_ENERGY || type == CONDITION_POISON) {
		ignoreFieldDamage = false;
	}

	updateIdleStatus();
}

void Monster::onThink(uint32_t interval)
{
	Creature::onThink(interval);

	if (mType->info.thinkEvent != -1) {
		// onThink(self, interval)
		LuaScriptInterface* scriptInterface = mType->info.scriptInterface;
		if (!scriptInterface->reserveScriptEnv()) {
			LOG_ERROR("[Error - Monster::onThink] Call stack overflow");
			return;
		}

		ScriptEnvironment* env = scriptInterface->getScriptEnv();
		env->setScriptId(mType->info.thinkEvent, scriptInterface);

		lua_State* L = scriptInterface->getLuaState();
		scriptInterface->pushFunction(mType->info.thinkEvent);

		Lua::pushUserdata<Monster>(L, this);
		Lua::setMetatable(L, -1, "Monster");

		lua_pushinteger(L, interval);

		scriptInterface->callFunction(2);
	}

	if (!isInSpawnRange(position)) {
		g_game.addMagicEffect(this->getPosition(), CONST_ME_POFF, getInstanceID());
		if (getBoolean(ConfigManager::REMOVE_ON_DESPAWN)) {
			g_game.removeCreature(this, false);
		} else {
			g_game.internalTeleport(this, masterPos);
			setIdle(true);
		}
	} else {
		updateIdleStatus();

		if (!isIdle) {
			addEventWalk();

			if (getAttackedCreatureShared() && getTimeSinceLastMove() >= 3000) {
				ignoreFieldDamage = true;
			}

			if (isSummon()) {
				auto master = getMaster();
				if (master) {
					const Tile* masterTile = master->getTile();
					const bool masterInProtectionZone = masterTile && masterTile->hasFlag(TILESTATE_PROTECTIONZONE);
					const bool differentInstance = getInstanceID() != master->getInstanceID();

					if (isFamiliar()) {
						const int32_t familiarTeleportRange = std::max<int32_t>(
						    1, static_cast<int32_t>(ConfigManager::getInteger(ConfigManager::FAMILIAR_TELEPORT_RANGE)));
						const bool tooFar = !getPosition().isInRange(master->getPosition(), familiarTeleportRange,
						                                             familiarTeleportRange, 3);
						if ((!masterInProtectionZone || ConfigManager::getBoolean(ConfigManager::FAMILIAR_ENTER_PZ)) &&
						    (differentInstance || tooFar)) {
							setInstanceID(master->getInstanceID());
							g_game.internalTeleport(this, master->getPosition(), false, 0, CONST_ME_NONE);
							g_game.addMagicEffect(master->getPosition(), CONST_ME_TELEPORT, getInstanceID());
							isMasterInRange = true;
						}
					} else if (masterInProtectionZone) {
						if (ConfigManager::getBoolean(ConfigManager::REMOVE_SUMMONS_ON_PZ)) {
							g_game.removeCreature(this, false);
							g_game.addMagicEffect(getPosition(), CONST_ME_POFF, getInstanceID());
							return;
						}
					} else if (ConfigManager::getBoolean(ConfigManager::TELEPORT_SUMMON)) {
						const bool tooFar = !getPosition().isInRange(master->getPosition(), 7, 7, 0);
						if (differentInstance || tooFar) {
							setInstanceID(master->getInstanceID());
							g_game.internalTeleport(this, master->getPosition(), false, 0, CONST_ME_NONE);
							g_game.addMagicEffect(master->getPosition(), CONST_ME_TELEPORT, getInstanceID());
							isMasterInRange = true;
						}
					}
				}

				auto ac = attackedCreature.lock();
				if (!ac) {
					auto masterTarget = master ? master->getAttackedCreatureShared() : nullptr;
					if (masterTarget) {
						// This happens if the monster is summoned during combat
						selectTarget(masterTarget.get());
					} else {
						auto follow = followCreature.lock();
						if (master != follow) {
							// Our master has not ordered us to attack anything, lets follow him around instead.
							setFollowCreature(master.get());
						}
					}
				} else if (ac.get() == this) {
					setFollowCreature(nullptr);
				} else if (followCreature.lock() != ac) {
					// This happens just after a master orders an attack, so lets follow it as well.
					setFollowCreature(ac.get());
				}
			} else if (!targetList.empty()) {
				// Clear stale followCreature (dead, removed, or in PZ)
				if (auto fc = followCreature.lock(); fc && !isTarget(fc.get())) {
					setFollowCreature(nullptr);
					setAttackedCreature(nullptr);
				}
				if (followCreature.expired() || !hasFollowPath) {
					searchTarget();
				} else if (isFleeing()) {
					if (auto ac = attackedCreature.lock(); ac && !canUseAttack(getPosition(), ac.get())) {
						searchTarget(TARGETSEARCH_ATTACKRANGE);
					}
				}
			} else if (!isSummon() && (!followCreature.expired() || !attackedCreature.expired())) {
				setAttackedCreature(nullptr);
				setFollowCreature(nullptr);
			}

			onThinkTarget(interval);
			onThinkYell(interval);
			onThinkDefense(interval);
		}
	}
}

void Monster::doAttacking(uint32_t interval)
{
	if (isRemoved() || isDead()) {
		return;
	}

	// Early exit: no target means no attacking
	auto ac = attackedCreature.lock();
	if (!ac || ac.get() == this) {
		attackedCreature.reset();
		return;
	}

	if (!mType || mType->info.attackSpells.empty()) {
		return;
	}

	if (isSummon()) {
		auto masterPtr = getMaster();
		if (!masterPtr || masterPtr->isRemoved() || masterPtr->isDead()) {
			attackedCreature.reset();
			return;
		}
	}

	if (ac->isRemoved() || ac->isDead()) {
		attackedCreature.reset();
		return;
	}

	bool updateLook = true;
	bool resetTicks = interval != 0;
	attackTicks += interval;

	const Position& myPos = getPosition();

	for (const spellBlock_t& spellBlock : mType->info.attackSpells) {
		// OPTIMIZATION: Single validity check per spell iteration instead of
		// 5+ redundant isDead/isRemoved checks that were here before.
		auto victim = attackedCreature.lock();
		if (!victim || victim->isRemoved() || victim->isDead()) {
			attackedCreature.reset();
			return;
		}

		bool inRange = false;

		if (!spellBlock.spell) {
			continue;
		}

		const Position& targetPos = victim->getPosition();

		if (canUseSpell(myPos, targetPos, spellBlock, interval, inRange, resetTicks)) {
			if (spellBlock.chance >= static_cast<uint32_t>(uniform_random(1, 100))) {
				if (updateLook) {
					updateLookDirection();
					updateLook = false;
				}

				int32_t minVal = spellBlock.minCombatValue;
				int32_t maxVal = spellBlock.maxCombatValue;
				if (minVal > maxVal) {
					std::swap(minVal, maxVal);
				}

				minCombatValue = minVal;
				maxCombatValue = maxVal;

				spellBlock.spell->castSpell(this, victim.get());

				// Check after cast — this is the only place state can actually change
				auto currentTarget = attackedCreature.lock();
				if (isRemoved() || isDead() || !currentTarget || currentTarget->isRemoved() || currentTarget->isDead()) {
					attackedCreature.reset();
					return;
				}

				if (spellBlock.isMelee) {
					lastMeleeAttack = OTSYS_TIME();
				}
			}
		}

		if (!inRange && spellBlock.isMelee) {
			// melee swing out of reach
			lastMeleeAttack = 0;
		}
	}

	if (updateLook) {
		updateLookDirection();
	}

	if (resetTicks) {
		attackTicks = 0;
	}
}

bool Monster::canUseAttack(const Position& pos, const Creature* target) const
{
	if (isHostile()) {
		if (!target) {
			return false;
		}

		if (!mType) {
			return false;
		}

		const Position& targetPos = target->getPosition();
		uint32_t distance = std::max<uint32_t>(pos.getDistanceX(targetPos), pos.getDistanceY(targetPos));
		for (const spellBlock_t& spellBlock : mType->info.attackSpells) {
			if (spellBlock.range != 0 && distance <= spellBlock.range) {
				return g_game.isSightClear(pos, targetPos, true);
			}
		}
		return false;
	}
	return true;
}

bool Monster::canUseSpell(const Position& pos, const Position& targetPos, const spellBlock_t& sb, uint32_t interval,
                          bool& inRange, bool& resetTicks) const
{
	inRange = true;

	if (sb.isMelee) {
		if (isFleeing() || (OTSYS_TIME() - lastMeleeAttack) < sb.speed) {
			return false;
		}
	} else {
		if (sb.speed > attackTicks) {
			resetTicks = false;
			return false;
		}

		if (sb.speed == 0 || attackTicks % sb.speed >= interval) {
			// already used this spell for this round
			return false;
		}
	}

	if (sb.range != 0 && std::max<uint32_t>(pos.getDistanceX(targetPos), pos.getDistanceY(targetPos)) > sb.range) {
		inRange = false;
		return false;
	}
	return true;
}

void Monster::onThinkTarget(uint32_t interval)
{
	if (!isSummon()) {
		// protection time
		if (auto target = getAttackedCreatureShared();
		    target && target->getPlayer() && target->getPlayer()->getProtectionTime() > 0) {
			setAttackedCreature(nullptr);
			updateTargetList();
			followCreature.reset();
		}

		if (mType->info.changeTargetSpeed != 0) {
			bool canChangeTarget = true;

			if (challengeFocusDuration > 0) {
				challengeFocusDuration -= interval;

				if (challengeFocusDuration <= 0) {
					challengeFocusDuration = 0;
				}
			}

			if (targetChangeCooldown > 0) {
				targetChangeCooldown -= interval;

				if (targetChangeCooldown <= 0) {
					targetChangeCooldown = 0;
					targetChangeTicks = mType->info.changeTargetSpeed;
				} else {
					canChangeTarget = false;
				}
			}

			if (canChangeTarget) {
				targetChangeTicks += interval;

				if (targetChangeTicks >= mType->info.changeTargetSpeed) {
					targetChangeTicks = 0;
					targetChangeCooldown = mType->info.changeTargetSpeed;

					if (challengeFocusDuration > 0) {
						challengeFocusDuration = 0;
					}

					if (mType->info.changeTargetChance >= uniform_random(1, 100)) {
						const targetStrategies_t& strategies = mType->info.targetStrategies;
						uint32_t totalWeight = strategies.nearest + strategies.health + strategies.damage + strategies.random;
						if (totalWeight > 0) {
							uint32_t roll = uniform_random(1, totalWeight);
							uint32_t currentWeight = 0;

							if (roll <= (currentWeight += strategies.nearest)) {
								searchTarget(TARGETSEARCH_NEAREST);
							} else if (roll <= (currentWeight += strategies.health)) {
								searchTarget(TARGETSEARCH_HEALTH);
							} else if (roll <= (currentWeight += strategies.damage)) {
								searchTarget(TARGETSEARCH_DAMAGE);
							} else {
								searchTarget(TARGETSEARCH_RANDOM);
							}
						} else {
							if (mType->info.targetDistance <= 1) {
								searchTarget(TARGETSEARCH_RANDOM);
							} else {
								searchTarget(TARGETSEARCH_NEAREST);
							}
						}
					}
				}
			}
		}
	}
}

void Monster::onThinkDefense(uint32_t interval)
{
	bool resetTicks = true;
	defenseTicks += interval;

	for (const spellBlock_t& spellBlock : mType->info.defenseSpells) {
		if (spellBlock.speed > defenseTicks) {
			resetTicks = false;
			continue;
		}

		if (spellBlock.speed == 0 || defenseTicks % spellBlock.speed >= interval) {
			// already used this spell for this round
			continue;
		}

		if ((spellBlock.chance >= static_cast<uint32_t>(uniform_random(1, 100)))) {
			minCombatValue = spellBlock.minCombatValue;
			maxCombatValue = spellBlock.maxCombatValue;
			spellBlock.spell->castSpell(this, this);
		}
	}

	if (!isSummon() && getSummonCount() < mType->info.maxSummons && hasFollowPath) {
		std::unordered_map<std::string, uint32_t> summonCounts;
		for (const auto& summonRef : summons) {
			if (auto summon = summonRef.lock()) {
				++summonCounts[summon->getName()];
			}
		}

		for (const summonBlock_t& summonBlock : mType->info.summons) {
			if (summonBlock.speed > defenseTicks) {
				resetTicks = false;
				continue;
			}

			if (getSummonCount() >= mType->info.maxSummons) {
				continue;
			}

			if (summonBlock.speed == 0 || defenseTicks % summonBlock.speed >= interval) {
				// already used this spell for this round
				continue;
			}

			if (summonCounts[summonBlock.name] >= summonBlock.max) {
				continue;
			}

			if (summonBlock.chance < static_cast<uint32_t>(uniform_random(1, 100))) {
				continue;
			}

			auto summonUnique = Monster::createMonster(summonBlock.name);
			if (summonUnique) {
				std::shared_ptr<Monster> summon(std::move(summonUnique));
				summon->setInstanceID(getInstanceID());
				if (g_game.placeCreature(summon.get(), getPosition(), false, summonBlock.force, summonBlock.effect)) {
					auto summonRef = g_game.getCreatureSharedRef<Monster>(summon.get());
					summonRef->setDropLoot(false);
					summonRef->setSkillLoss(false);
					summonRef->setMaster(this);
					if (summonBlock.masterEffect != CONST_ME_NONE) {
						g_game.addMagicEffect(getPosition(), summonBlock.masterEffect, getInstanceID());
					}
					++summonCounts[summonBlock.name];
				}
			}
		}
	}

	if (resetTicks) {
		defenseTicks = 0;
	}
}

void Monster::onThinkYell(uint32_t interval)
{
	if (mType->info.yellSpeedTicks == 0) {
		return;
	}

	yellTicks += interval;
	if (yellTicks >= mType->info.yellSpeedTicks) {
		yellTicks = 0;

		if (!mType->info.voiceVector.empty() &&
		    (mType->info.yellChance >= static_cast<uint32_t>(uniform_random(1, 100)))) {
			uint32_t index = uniform_random(0, mType->info.voiceVector.size() - 1);
			const voiceBlock_t& vb = mType->info.voiceVector[index];

			if (vb.yellText) {
				g_game.internalCreatureSay(this, TALKTYPE_MONSTER_YELL, vb.text, false);
			} else {
				g_game.internalCreatureSay(this, TALKTYPE_MONSTER_SAY, vb.text, false);
			}
		}
	}
}

void Monster::callPlayerAttackEvent(Player* player)
{
	if (mType->info.playerAttackEvent == -1) {
		return;
	}

	// onPlayerAttack(self, attackerPlayer)
	LuaScriptInterface* scriptInterface = mType->info.scriptInterface;
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - Monster::callPlayerAttackEvent] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(mType->info.playerAttackEvent, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(mType->info.playerAttackEvent);

	Lua::pushUserdata<Monster>(L, this);
	Lua::setMetatable(L, -1, "Monster");

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	scriptInterface->callFunction(2);
}

bool Monster::walkToSpawn()
{
	if (walkingToSpawn || spawn.expired() || !targetList.empty()) {
		return false;
	}

	int32_t distance = std::max(position.getDistanceX(masterPos), position.getDistanceY(masterPos));
	if (distance == 0) {
		return false;
	}

	listWalkDir.clear();
	if (!getPathTo(masterPos, listWalkDir, 0, std::max(0, distance - 5), true, true, distance)) {
		return false;
	}

	walkingToSpawn = true;
	startAutoWalk();
	return true;
}

void Monster::onWalk() { Creature::onWalk(); }

void Monster::onWalkComplete()
{
	// Continue walking to spawn
	if (walkingToSpawn) {
		walkingToSpawn = false;
		walkToSpawn();
	}
}

bool Monster::pushItem(Item* item)
{
	const Position& centerPos = item->getPosition();

	static std::vector<std::pair<int32_t, int32_t>> relList{{-1, -1}, {0, -1}, {1, -1}, {-1, 0},
	                                                        {1, 0},   {-1, 1}, {0, 1},  {1, 1}};

	std::shuffle(relList.begin(), relList.end(), getRandomGenerator());

	for (const auto& it : relList) {
		Position tryPos(centerPos.x + it.first, centerPos.y + it.second, centerPos.z);
		Tile* tile = g_game.map.getTile(tryPos);
		if (tile && g_game.canThrowObjectTo(centerPos, tryPos, true, true)) {
			if (g_game.internalMoveItem(item->getParent(), tile, INDEX_WHEREEVER, item, item->getItemCount(),
			                            nullptr) == RETURNVALUE_NOERROR) {
				return true;
			}
		}
	}
	return false;
}

void Monster::pushItems(Tile* tile)
{
	// We can not use iterators here since we can push the item to another tile
	// which will invalidate the iterator.
	// start from the end to minimize the amount of traffic
	if (TileItemVector* items = tile->getItemList()) {
		uint32_t moveCount = 0;
		uint32_t removeCount = 0;

		int32_t downItemSize = tile->getDownItemCount();
		for (int32_t i = downItemSize; --i >= 0;) {
			Item* item = items->at(i).get();
			if (item && item->hasProperty(CONST_PROP_MOVEABLE) &&
			    (item->hasProperty(CONST_PROP_BLOCKPATH) || item->hasProperty(CONST_PROP_BLOCKSOLID))) {
				if (moveCount < 20 && Monster::pushItem(item)) {
					++moveCount;
				} else if (g_game.internalRemoveItem(item) == RETURNVALUE_NOERROR) {
					++removeCount;
				}
			}
		}

		if (removeCount > 0) {
			g_game.addMagicEffect(tile->getPosition(), CONST_ME_BLOCKHIT);
		}
	}
}

bool Monster::pushCreature(Creature* creature)
{
	for (Direction dir : getShuffleDirections()) {
		const Position& tryPos = Spells::getCasterPosition(creature, dir);
		Tile* toTile = g_game.map.getTile(tryPos);
		if (toTile && !toTile->hasFlag(TILESTATE_BLOCKPATH)) {
			if (g_game.internalMoveCreature(creature, dir) == RETURNVALUE_NOERROR) {
				return true;
			}
		}
	}
	return false;
}

void Monster::pushCreatures(Tile* tile)
{
	// We can not use iterators here since we can push a creature to another tile
	// which will invalidate the iterator.
	if (CreatureVector* creatures = tile->getCreatures()) {
		uint32_t removeCount = 0;
		Monster* lastPushedMonster = nullptr;

		for (size_t i = 0; i < creatures->size();) {
			Monster* monster = creatures->at(i)->getMonster();
			if (monster && monster->isPushable()) {
				if (monster != lastPushedMonster && Monster::pushCreature(monster)) {
					lastPushedMonster = monster;
					continue;
				}

				monster->changeHealth(-monster->getHealth());
				monster->setDropLoot(false);
				removeCount++;
			}

			++i;
		}

		if (removeCount > 0) {
			g_game.addMagicEffect(tile->getPosition(), CONST_ME_BLOCKHIT);
		}
	}
}

bool Monster::getNextStep(Direction& direction, uint32_t& flags)
{
	if (isMovementBlocked() || (!walkingToSpawn && (isIdle || isDead()))) {
		// we don't have anyone watching, might as well stop walking
		// or the creature movement is blocked
		eventWalk = 0;
		return false;
	}

	bool result = false;
	if (!walkingToSpawn && (followCreature.expired() || !hasFollowPath) && (!isSummon() || !isMasterInRange)) {
		if (getTimeSinceLastMove() >= EVENT_CREATURE_THINK_INTERVAL) {
			randomStepping = true;
			// choose a random direction
			result = getRandomStep(getPosition(), direction);
		}
	} else if ((isSummon() && isMasterInRange) || !followCreature.expired() || walkingToSpawn) {
		auto master = getMaster();
		if (!hasFollowPath && master && !master->isPlayer()) {
			randomStepping = true;
			result = getRandomStep(getPosition(), direction);
		} else {
			randomStepping = false;
			result = Creature::getNextStep(direction, flags);
			if (result) {
				flags |= FLAG_PATHFINDING;
			} else {
				if (selectBlockerTarget()) {
					return false;
				}

				if (ignoreFieldDamage) {
					ignoreFieldDamage = false;
				}
				// target dancing
				if (auto ac = attackedCreature.lock(); ac && ac == followCreature.lock()) {
					if (isFleeing()) {
						result = getDanceStep(getPosition(), direction, false, false);
					} else if (mType->info.staticAttackChance < static_cast<uint32_t>(uniform_random(1, 100))) {
						result = getDanceStep(getPosition(), direction);
					}
				}
			}
		}
	}

	if (result && (canPushItems() || canPushCreatures())) {
		const Position& pos = Spells::getCasterPosition(this, direction);
		Tile* tile = g_game.map.getTile(pos);
		if (tile) {
			if (canPushItems()) {
				Monster::pushItems(tile);
			}

			if (canPushCreatures()) {
				Monster::pushCreatures(tile);
			}
		}
	}

	return result;
}

bool Monster::getRandomStep(const Position& creaturePos, Direction& direction) const
{
	for (Direction dir : getShuffleDirections()) {
		if (canWalkTo(creaturePos, dir)) {
			direction = dir;
			return true;
		}
	}
	return false;
}

bool Monster::getDanceStep(const Position& creaturePos, Direction& direction, bool keepAttack /*= true*/,
                           bool keepDistance /*= true*/)
{
	auto attacked = attackedCreature.lock();
	if (!attacked) {
		return false;
	}

	bool canDoAttackNow = canUseAttack(creaturePos, attacked.get());

	assert(attacked != nullptr);
	const Position& centerPos = attacked->getPosition();

	int32_t offset_x = creaturePos.getOffsetX(centerPos);
	int32_t offset_y = creaturePos.getOffsetY(centerPos);

	int32_t distance_x = std::abs(offset_x);
	int32_t distance_y = std::abs(offset_y);

	int32_t centerToDist = std::max(distance_x, distance_y);

	std::vector<Direction> dirList;
	dirList.reserve(4);

	if (!keepDistance || offset_y >= 0) {
		int32_t tmpDist = std::max(distance_x, std::abs((creaturePos.getY() - 1) - centerPos.getY()));
		if (tmpDist == centerToDist && canWalkTo(creaturePos, DIRECTION_NORTH)) {
			bool result = true;

			if (keepAttack) {
				result =
				    (!canDoAttackNow || canUseAttack(Position(creaturePos.x, creaturePos.y - 1, creaturePos.z), attacked.get()));
			}

			if (result) {
				dirList.push_back(DIRECTION_NORTH);
			}
		}
	}

	if (!keepDistance || offset_y <= 0) {
		int32_t tmpDist = std::max(distance_x, std::abs((creaturePos.getY() + 1) - centerPos.getY()));
		if (tmpDist == centerToDist && canWalkTo(creaturePos, DIRECTION_SOUTH)) {
			bool result = true;

			if (keepAttack) {
				result =
				    (!canDoAttackNow || canUseAttack(Position(creaturePos.x, creaturePos.y + 1, creaturePos.z), attacked.get()));
			}

			if (result) {
				dirList.push_back(DIRECTION_SOUTH);
			}
		}
	}

	if (!keepDistance || offset_x <= 0) {
		int32_t tmpDist = std::max(std::abs((creaturePos.getX() + 1) - centerPos.getX()), distance_y);
		if (tmpDist == centerToDist && canWalkTo(creaturePos, DIRECTION_EAST)) {
			bool result = true;

			if (keepAttack) {
				result =
				    (!canDoAttackNow || canUseAttack(Position(creaturePos.x + 1, creaturePos.y, creaturePos.z), attacked.get()));
			}

			if (result) {
				dirList.push_back(DIRECTION_EAST);
			}
		}
	}

	if (!keepDistance || offset_x >= 0) {
		int32_t tmpDist = std::max(std::abs((creaturePos.getX() - 1) - centerPos.getX()), distance_y);
		if (tmpDist == centerToDist && canWalkTo(creaturePos, DIRECTION_WEST)) {
			bool result = true;

			if (keepAttack) {
				result =
				    (!canDoAttackNow || canUseAttack(Position(creaturePos.x - 1, creaturePos.y, creaturePos.z), attacked.get()));
			}

			if (result) {
				dirList.push_back(DIRECTION_WEST);
			}
		}
	}

	if (!dirList.empty()) {
		direction = dirList[uniform_random(0, dirList.size() - 1)];
		return true;
	}
	return false;
}

enum CommonIndex
{
	FIRST_ENTRY,
	SECOND_ENTRY,
	THIRD_ENTRY,
	FOURTH_ENTRY,
	FIFTH_ENTRY
};

using _FleeList = std::array<Direction, 5>;
static constexpr std::array<_FleeList, 8> FleeMap = {
    {// Index = direction of target relative to monster
     // NORTH(0): flee south, or west/east, or SW/SE
     {DIRECTION_SOUTH, DIRECTION_WEST, DIRECTION_EAST, DIRECTION_SOUTHWEST, DIRECTION_SOUTHEAST},
     // EAST(1): flee west, or north/south, or NW/SW
     {DIRECTION_WEST, DIRECTION_NORTH, DIRECTION_SOUTH, DIRECTION_NORTHWEST, DIRECTION_SOUTHWEST},
     // SOUTH(2): flee north, or west/east, or NW/NE
     {DIRECTION_NORTH, DIRECTION_WEST, DIRECTION_EAST, DIRECTION_NORTHWEST, DIRECTION_NORTHEAST},
     // WEST(3): flee east, or north/south, or NE/SE
     {DIRECTION_EAST, DIRECTION_NORTH, DIRECTION_SOUTH, DIRECTION_NORTHEAST, DIRECTION_SOUTHEAST},
     // SOUTHWEST(4): flee NE direction
     {DIRECTION_NORTH, DIRECTION_EAST, DIRECTION_NORTHEAST, DIRECTION_NORTHWEST, DIRECTION_SOUTHEAST},
     // SOUTHEAST(5): flee NW direction
     {DIRECTION_NORTH, DIRECTION_WEST, DIRECTION_NORTHWEST, DIRECTION_SOUTHWEST, DIRECTION_NORTHEAST},
     // NORTHWEST(6): flee SE direction
     {DIRECTION_SOUTH, DIRECTION_EAST, DIRECTION_SOUTHEAST, DIRECTION_SOUTHWEST, DIRECTION_NORTHEAST},
     // NORTHEAST(7): flee SW direction
     {DIRECTION_SOUTH, DIRECTION_WEST, DIRECTION_SOUTHWEST, DIRECTION_NORTHWEST, DIRECTION_SOUTHEAST}}};

using _ChargeList = std::array<Direction, 3>;
static constexpr std::array<_ChargeList, 8> ChaseMap = {{// NORTH(0): chase north, or west/east
                                                         {DIRECTION_NORTH, DIRECTION_WEST, DIRECTION_EAST},
                                                         // EAST(1): chase east, or north/south
                                                         {DIRECTION_EAST, DIRECTION_NORTH, DIRECTION_SOUTH},
                                                         // SOUTH(2): chase south, or west/east
                                                         {DIRECTION_SOUTH, DIRECTION_WEST, DIRECTION_EAST},
                                                         // WEST(3): chase west, or north/south
                                                         {DIRECTION_WEST, DIRECTION_NORTH, DIRECTION_SOUTH},
                                                         // SOUTHWEST(4): chase SW, or south/west
                                                         {DIRECTION_SOUTH, DIRECTION_WEST, DIRECTION_SOUTHWEST},
                                                         // SOUTHEAST(5): chase SE, or south/east
                                                         {DIRECTION_SOUTH, DIRECTION_EAST, DIRECTION_SOUTHEAST},
                                                         // NORTHWEST(6): chase NW, or north/west
                                                         {DIRECTION_NORTH, DIRECTION_WEST, DIRECTION_NORTHWEST},
                                                         // NORTHEAST(7): chase NE, or north/east
                                                         {DIRECTION_NORTH, DIRECTION_EAST, DIRECTION_NORTHEAST}}};

using _EscapeList = std::array<Direction, 3>;
static constexpr std::array<_EscapeList, 8> EscapeMap = {{{DIRECTION_NORTH, DIRECTION_NORTHWEST, DIRECTION_NORTHEAST},
                                                          {DIRECTION_EAST, DIRECTION_NORTHEAST, DIRECTION_SOUTHEAST},
                                                          {DIRECTION_SOUTH, DIRECTION_SOUTHWEST, DIRECTION_SOUTHEAST},
                                                          {DIRECTION_WEST, DIRECTION_NORTHWEST, DIRECTION_SOUTHWEST},
                                                          {DIRECTION_SOUTHWEST, DIRECTION_WEST, DIRECTION_SOUTH},
                                                          {DIRECTION_SOUTHEAST, DIRECTION_EAST, DIRECTION_SOUTH},
                                                          {DIRECTION_NORTHWEST, DIRECTION_WEST, DIRECTION_NORTH},
                                                          {DIRECTION_NORTHEAST, DIRECTION_EAST, DIRECTION_NORTH}}};

// Converts (offsetX, offsetY) to a Direction using a 3x3 lookup table.
inline static constexpr Direction getTargetDirection(int32_t x_offset, int32_t y_offset) noexcept
{
	int32_t x_norm = (x_offset > 0) - (x_offset < 0);
	int32_t y_norm = (y_offset > 0) - (y_offset < 0);

	constexpr Direction lookup[3][3] = {{DIRECTION_SOUTHEAST, DIRECTION_SOUTH, DIRECTION_SOUTHWEST},
	                                    {DIRECTION_EAST, DIRECTION_NONE, DIRECTION_WEST},
	                                    {DIRECTION_NORTHEAST, DIRECTION_NORTH, DIRECTION_NORTHWEST}};

	return lookup[y_norm + 1][x_norm + 1];
}

// Flee from target using lookup tables.
void Monster::fleeFromTarget(const Position& targetPos, Direction& direction)
{
	const Position& creaturePos = getPosition();
	int32_t offsetx = creaturePos.getOffsetX(targetPos);
	int32_t offsety = creaturePos.getOffsetY(targetPos);
	Direction target_direction = getTargetDirection(offsetx, offsety);

	if ((offsetx == 0 && offsety == 0) || target_direction == DIRECTION_NONE) {
		getRandomStep(creaturePos, direction);
		return;
	}

	const auto& flee_list = FleeMap[target_direction];
	const bool diagonal = (offsetx != 0) && (offsety != 0);
	const int random_number = (rand() % 3);
	const bool chance = ((random_number + 1) > 1);

	if (!diagonal) {
		// Non-diagonal: primary direction has highest priority
		if (canWalkTo(creaturePos, flee_list[FIRST_ENTRY])) {
			direction = flee_list[FIRST_ENTRY];
			return;
		}

		// Secondary: 2nd and 3rd directions randomized
		auto secondary_first = chance ? flee_list[THIRD_ENTRY] : flee_list[SECOND_ENTRY];
		auto secondary_backup = chance ? flee_list[SECOND_ENTRY] : flee_list[THIRD_ENTRY];

		if (canWalkTo(creaturePos, secondary_first)) {
			direction = secondary_first;
			return;
		}
		if (canWalkTo(creaturePos, secondary_backup)) {
			direction = secondary_backup;
			return;
		}

		// Tertiary: 4th and 5th fallback directions
		auto tertiary_first = chance ? flee_list[FOURTH_ENTRY] : flee_list[FIFTH_ENTRY];
		auto tertiary_backup = chance ? flee_list[FIFTH_ENTRY] : flee_list[FOURTH_ENTRY];

		if (canWalkTo(creaturePos, tertiary_first)) {
			direction = tertiary_first;
			return;
		}
		if (canWalkTo(creaturePos, tertiary_backup)) {
			direction = tertiary_backup;
			return;
		}
	}

	if (diagonal) {
		// Diagonal: first two directions share primary priority
		auto primary_first = chance ? flee_list[SECOND_ENTRY] : flee_list[FIRST_ENTRY];
		auto primary_backup = chance ? flee_list[FIRST_ENTRY] : flee_list[SECOND_ENTRY];

		if (canWalkTo(creaturePos, primary_first)) {
			direction = primary_first;
			return;
		}
		if (canWalkTo(creaturePos, primary_backup)) {
			direction = primary_backup;
			return;
		}

		// Secondary: 3rd entry
		if (canWalkTo(creaturePos, flee_list[THIRD_ENTRY])) {
			direction = flee_list[THIRD_ENTRY];
			return;
		}

		// Tertiary fallback: 4th and 5th
		auto final_first = chance ? flee_list[FOURTH_ENTRY] : flee_list[FIFTH_ENTRY];
		auto final_last = chance ? flee_list[FIFTH_ENTRY] : flee_list[FOURTH_ENTRY];

		if (canWalkTo(creaturePos, final_first)) {
			direction = final_first;
			return;
		}
		if (canWalkTo(creaturePos, final_last)) {
			direction = final_last;
			return;
		}
	}

	const auto& escape_list = EscapeMap[target_direction];
	auto escape_one = escape_list[random_number];
	auto escape_two = escape_list[(random_number + 1) % 3];
	auto escape_three = escape_list[(random_number + 2) % 3];

	if (canWalkTo(creaturePos, escape_one)) {
		direction = escape_one;
		return;
	}
	if (canWalkTo(creaturePos, escape_two)) {
		direction = escape_two;
		return;
	}
	if (canWalkTo(creaturePos, escape_three)) {
		direction = escape_three;
		return;
	}
}

bool Monster::getDistanceStep(const Position& targetPos, Direction& direction, bool flee /* = false */)
{
	const Position& creaturePos = getPosition();

	int32_t dx = creaturePos.getDistanceX(targetPos);
	int32_t dy = creaturePos.getDistanceY(targetPos);
	int32_t distance = std::max(dx, dy);

	if (!flee && (distance > mType->info.targetDistance || !g_game.isSightClear(creaturePos, targetPos, true))) {
		return false; // let the A* calculate it
	} else if (!flee && distance == mType->info.targetDistance) {
		return true; // already at target distance, dance step handles position
	}

	if (dx <= 1 && dy <= 1) {
		if (stepDuration < 2) {
			stepDuration++;
		}
	} else if (stepDuration > 0) {
		stepDuration--;
	}

	if (flee) {
		fleeFromTarget(targetPos, direction);
		return true;
	}

	if (distance < mType->info.targetDistance) {
		fleeFromTarget(targetPos, direction);
		return true;
	}

	int32_t offsetx = creaturePos.getOffsetX(targetPos);
	int32_t offsety = creaturePos.getOffsetY(targetPos);
	Direction target_direction = getTargetDirection(offsetx, offsety);

	if ((offsetx == 0 && offsety == 0) || target_direction == DIRECTION_NONE) {
		return getRandomStep(creaturePos, direction);
	}

	const auto& chase_list = ChaseMap[target_direction];
	const bool diagonal = (offsetx != 0) && (offsety != 0);
	const bool chance = boolean_random();

	if (!diagonal) {
		// Primary: single best direction
		if (canWalkTo(creaturePos, chase_list[FIRST_ENTRY])) {
			direction = chase_list[FIRST_ENTRY];
			return true;
		}

		// Secondary: randomized fallback
		auto secondary_one = chance ? chase_list[SECOND_ENTRY] : chase_list[THIRD_ENTRY];
		auto secondary_two = chance ? chase_list[THIRD_ENTRY] : chase_list[SECOND_ENTRY];

		if (canWalkTo(creaturePos, secondary_one)) {
			direction = secondary_one;
			return true;
		}
		if (canWalkTo(creaturePos, secondary_two)) {
			direction = secondary_two;
			return true;
		}
	}

	// Diagonal: two primary options
	auto primary_one = chance ? chase_list[SECOND_ENTRY] : chase_list[FIRST_ENTRY];
	auto primary_two = chance ? chase_list[FIRST_ENTRY] : chase_list[SECOND_ENTRY];

	if (canWalkTo(creaturePos, primary_one)) {
		direction = primary_one;
		return true;
	}
	if (canWalkTo(creaturePos, primary_two)) {
		direction = primary_two;
		return true;
	}

	// Secondary: fallback
	if (canWalkTo(creaturePos, chase_list[THIRD_ENTRY])) {
		direction = chase_list[THIRD_ENTRY];
		return true;
	}

	return false;
}

bool Monster::canWalkTo(Position pos, Direction direction) const
{
	pos = getNextPosition(direction, pos);
	if (isInSpawnRange(pos)) {
		Tile* tile = g_game.map.getTile(pos);
		uint32_t pathFlags = FLAG_PATHFINDING;
		if (isFamiliar() || ignoreFieldDamage) {
			pathFlags |= FLAG_IGNOREFIELDDAMAGE;
		}
		if (isSummon() && !isFamiliar() && tile && tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
			return false;
		}
		if (tile && tile->getTopVisibleCreature(this) == nullptr &&
		    tile->queryAdd(0, *this, 1, pathFlags) == RETURNVALUE_NOERROR) {
			return true;
		}
	}
	return false;
}

void Monster::death(Creature*)
{
	// rewardboss
	if (getMonster()->isRewardBoss()) {
		uint32_t monsterId = getMonster()->getID();
		auto& rewardBossContributionInfo = g_game.rewardBossTracking;
		auto it = rewardBossContributionInfo.find(monsterId);
		if (it == rewardBossContributionInfo.end()) {
			return;
		}
		auto& scoreInfo = it->second;
		uint32_t mostScoreContributor = 0;
		int32_t highestScore = 0;
		int32_t totalScore = 0;
		int32_t contributors = 0;
		int32_t totalDamageDone = 0;
		int32_t totalDamageTaken = 0;
		int32_t totalHealingDone = 0;
		for (const auto& [playerId, playerScoreInfo] : scoreInfo.playerScoreTable) {
			int32_t playerScore =
			    playerScoreInfo.damageDone + playerScoreInfo.damageTaken + playerScoreInfo.healingDone;
			totalScore += playerScore;
			totalDamageDone += playerScoreInfo.damageDone;
			totalDamageTaken += playerScoreInfo.damageTaken;
			totalHealingDone += playerScoreInfo.healingDone;
			contributors++;
			if (playerScore > highestScore) {
				highestScore = playerScore;
				mostScoreContributor = playerId;
			}
		}
		const auto& creatureLoot = mType->info.lootItems;
		int64_t currentTime = time(nullptr);
		for (const auto& [playerId, playerScoreInfo] : rewardBossContributionInfo[monsterId].playerScoreTable) {
			double damageDone = playerScoreInfo.damageDone;
			double damageTaken = playerScoreInfo.damageTaken;
			double healingDone = playerScoreInfo.healingDone;
			// Base loot rate calculation with zero checks
			double contrubutionScore = 0;
			if (damageDone > 0) {
				contrubutionScore += damageDone;
			}
			if (damageTaken > 0) {
				contrubutionScore += damageTaken;
			}
			if (healingDone > 0) {
				contrubutionScore += healingDone;
			}
			double expectedScore =
			    ((contrubutionScore / totalScore) * ConfigManager::getFloat(ConfigManager::REWARD_BASE_RATE));
			double lootRate = std::min(expectedScore, 1.0);
			auto player = g_game.getPlayerByGUID(playerId);
			auto rewardItem = Item::CreateItem(ITEM_REWARD_CONTAINER);
			if (!rewardItem) {
				return;
			}
			auto rewardContainer = rewardItem->getContainer();
			if (!rewardContainer) {
				return;
			}
			rewardContainer->setIntAttr(ITEM_ATTRIBUTE_DATE, currentTime);
			rewardContainer->setIntAttr(ITEM_ATTRIBUTE_REWARDID, getMonster()->getID());
			bool hasLoot = false;
			
			std::unordered_map<uint16_t, uint32_t> stackableCounts;
			std::vector<std::shared_ptr<Item>> pendingItems;

			for (const auto& lootBlock : creatureLoot) {
				// Skip items with invalid IDs — CreateItem would return a recycled/garbage pointer
				if (lootBlock.id == 0) {
					continue;
				}
				float adjustedChance =
				    (lootBlock.chance * lootRate) * ConfigManager::getInteger(ConfigManager::RATE_LOOT);
				if (lootBlock.unique && mostScoreContributor == playerId) {
					// Ensure that the mostScoreContributor can receive multiple unique items
					const ItemType& itemType = Item::items[lootBlock.id];
					if (itemType.stackable) {
						stackableCounts[lootBlock.id] += uniform_random(1, lootBlock.countmax);
						continue;
					}

					uint32_t count = uniform_random(1, lootBlock.countmax);
					auto lootItem = Item::CreateItem(lootBlock.id, count);
					if (!lootItem) {
						continue;
					}
					lootItem->setIntAttr(ITEM_ATTRIBUTE_DATE, currentTime);
					lootItem->setIntAttr(ITEM_ATTRIBUTE_REWARDID, getMonster()->getID());
					pendingItems.push_back(lootItem);
					rewardContainer->internalAddThing(lootItem.get());
					hasLoot = true;
				} else if (!lootBlock.unique) {
					// Normal loot distribution for non-unique items
					if (uniform_random(1, MAX_LOOTCHANCE) <= adjustedChance) {
						const ItemType& itemType = Item::items[lootBlock.id];
						if (itemType.stackable) {
							stackableCounts[lootBlock.id] += uniform_random(1, lootBlock.countmax);
							continue;
						}

						uint32_t count = uniform_random(1, lootBlock.countmax);
						auto lootItem = Item::CreateItem(lootBlock.id, count);
						if (!lootItem) {
							continue;
						}
						lootItem->setIntAttr(ITEM_ATTRIBUTE_DATE, currentTime);
						lootItem->setIntAttr(ITEM_ATTRIBUTE_REWARDID, getMonster()->getID());
						pendingItems.push_back(lootItem);
						rewardContainer->internalAddThing(lootItem.get());
						hasLoot = true;
					}
				}
			}

			// Flush accumulated stackable counts as batched items (max 100 per stack)
			for (auto& [itemId, totalCount] : stackableCounts) {
				while (totalCount > 0) {
					uint32_t batch = std::min<uint32_t>(totalCount, 100u);
					auto mergedItem = Item::CreateItem(itemId, batch);
					if (mergedItem) {
						pendingItems.push_back(mergedItem);
						rewardContainer->internalAddThing(mergedItem.get());
						hasLoot = true;
					}
					totalCount -= batch;
				}
			}
			if (hasLoot) {
				if (player) {
					std::string lootString;
					const auto& itemList = rewardContainer->getItemList();
					for (auto lootIt = itemList.begin(); lootIt != itemList.end(); ++lootIt) {
						if (lootIt != itemList.begin()) {
							lootString += ", ";
						}
						lootString += (*lootIt)->getNameDescription();
					}
					player->getRewardChest().internalAddThing(rewardContainer);
					player->sendTextMessage(MESSAGE_STATUS_DEFAULT,
					                        "The following items dropped by " + getMonster()->getName() +
					                            " are available in your reward chest: Reward Container (" + lootString +
					                            ").");
				} else {
					DBInsert rewardQuery(
					    "INSERT INTO `player_rewarditems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
					PropWriteStream propWriteStream;
					ItemBlockList itemList;
					int32_t currentPid = 1;
					for (const auto& subItem : rewardContainer->getItemList()) {
						itemList.emplace_back(currentPid, subItem.get());
					}
					IOLoginData::addRewardItems(playerId, itemList, rewardQuery, propWriteStream);
				}
			} else {
				if (player) {
					player->sendTextMessage(MESSAGE_STATUS_DEFAULT, "You did not receive any loot.");
				}
			}
		}
		g_game.resetDamageTracking(monsterId);
	}

	setAttackedCreature(nullptr);

	SummonList summonRefs;
	summonRefs.swap(summons);
	for (const auto& summonRef : summonRefs) {
		if (auto summon = summonRef.lock()) {
			summon->changeHealth(-summon->getHealth());
			summon->removeMaster();
		}
	}

	clearTargetList();
	clearFriendList();
}

std::shared_ptr<Item> Monster::getCorpse(Creature* lastHitCreature, Creature* mostDamageCreature)
{
	std::shared_ptr<Item> corpse;
	if (isRewardBoss()) {
		corpse = Item::CreateItemAsContainer(getLookCorpse(), 1);
	}

	if (!corpse) {
		corpse = Creature::getCorpse(lastHitCreature, mostDamageCreature);
	}

	if (corpse) {
		if (mostDamageCreature) {
			if (mostDamageCreature->getPlayer()) {
				corpse->setCorpseOwner(mostDamageCreature->getID());
			} else {
				auto mostDamageCreatureMaster = mostDamageCreature->getMaster();
				if (mostDamageCreatureMaster && mostDamageCreatureMaster->getPlayer()) {
					corpse->setCorpseOwner(mostDamageCreatureMaster->getID());
				}
			}
		}
	}
	return corpse;
}

bool Monster::isInSpawnRange(const Position& pos) const
{
	if (spawn.expired()) {
		return true;
	}

	if (Monster::despawnRadius == 0) {
		return true;
	}

	if (!Spawns::isInZone(masterPos, Monster::despawnRadius, pos)) {
		return false;
	}

	if (Monster::despawnRange == 0) {
		return true;
	}

	if (pos.getDistanceZ(masterPos) > Monster::despawnRange) {
		return false;
	}

	return true;
}

bool Monster::getCombatValues(int32_t& min, int32_t& max)
{
	if (minCombatValue == 0 && maxCombatValue == 0) {
		return false;
	}

	min = minCombatValue;
	max = maxCombatValue;

	// Influenced creature damage multiplier
	if (influenced && influencedLevel >= 1 && influencedLevel <= 5) {
		static constexpr double dmgMult[] = {0.0, 1.35, 1.45, 1.55, 1.65, 1.75};
		double mult = dmgMult[influencedLevel];
		min = static_cast<int32_t>(min * mult);
		max = static_cast<int32_t>(max * mult);
	}

	return true;
}

void Monster::updateLookDirection()
{
	auto ac = attackedCreature.lock();
	if (!ac) {
		return;
	}

	auto lookDirection = DIRECTION_NONE;

	const auto& currentPosition = getPosition();
	const auto& targetPosition = ac->getPosition();

	auto offsetX = targetPosition.getOffsetX(currentPosition);
	auto absOffsetX = std::abs(offsetX);

	auto offsetY = targetPosition.getOffsetY(currentPosition);
	auto absOffsetY = std::abs(offsetY);

	if (absOffsetX > absOffsetY) {
		lookDirection = (offsetX < 0) ? DIRECTION_WEST : DIRECTION_EAST;
	} else if (absOffsetY > absOffsetX) {
		lookDirection = (offsetY < 0) ? DIRECTION_NORTH : DIRECTION_SOUTH;
	} else {
		lookDirection = (offsetX < 0) ? DIRECTION_WEST : DIRECTION_EAST;
	}

	g_game.internalCreatureTurn(this, lookDirection);
}

void Monster::dropLoot(Container* corpse, Creature*)
{
	if (!corpse) {
		return;
	}

	if (getMonster()->isRewardBoss()) {
		int64_t currentTime = std::time(nullptr);
		auto rewardContainer = Item::CreateItem(ITEM_REWARD_CONTAINER);
		if (!rewardContainer) {
			return;
		}
		rewardContainer->setIntAttr(ITEM_ATTRIBUTE_DATE, currentTime);
		rewardContainer->setIntAttr(ITEM_ATTRIBUTE_REWARDID, getMonster()->getID());
		corpse->internalAddThing(rewardContainer.get());
	} else if (lootDrop) {
		g_events->eventMonsterOnDropLoot(this, corpse);
	}
}

void Monster::setNormalCreatureLight() { internalLight = mType->info.light; }

void Monster::drainHealth(const std::shared_ptr<Creature>& attacker, int32_t damage)
{
	Creature::drainHealth(attacker, damage);

	if (isInvisible()) {
		removeCondition(CONDITION_INVISIBLE);
	}
}

void Monster::changeHealth(int32_t healthChange, bool sendHealthChange /* = true*/)
{
	// In case a player with ignore flag set attacks the monster
	setIdle(false);
	Creature::changeHealth(healthChange, sendHealthChange);
}

bool Monster::challengeCreature(Creature* creature, bool force /* = false*/)
{
	if (isSummon()) {
		return false;
	}

	if (!mType->info.isChallengeable && !force) {
		return false;
	}

	bool result = selectTarget(creature);
	if (result) {
		targetChangeCooldown = 8000;
		challengeFocusDuration = targetChangeCooldown;
		targetChangeTicks = 0;
	}
	return result;
}

void Monster::getPathSearchParams(const Creature* creature, FindPathParams& fpp) const
{
	Creature::getPathSearchParams(creature, fpp);

	fpp.minTargetDist = 1;
	fpp.maxTargetDist = mType->info.targetDistance;

	if (isSummon()) {
		auto master = getMaster();
		if (master && master.get() == creature) {
			fpp.maxTargetDist = 2;
			fpp.fullPathSearch = true;
		} else if (mType->info.targetDistance <= 1) {
			fpp.fullPathSearch = true;
		} else {
			fpp.fullPathSearch = !canUseAttack(getPosition(), creature);
		}
	} else if (isFleeing()) {
		// Distance should be higher than the client view range (Map::maxClientViewportX/Map::maxClientViewportY)
		fpp.maxTargetDist = Map::maxViewportX;
		fpp.clearSight = false;
		fpp.keepDistance = true;
		fpp.fullPathSearch = false;
	} else if (mType->info.targetDistance <= 1) {
		fpp.fullPathSearch = true;
	} else {
		fpp.fullPathSearch = !canUseAttack(getPosition(), creature);
	}
}

bool Monster::canPushItems() const
{
	auto m = this->master.lock();
	Monster* masterMonster = m ? m->getMonster() : nullptr;
	if (masterMonster) {
		return masterMonster->mType->info.canPushItems;
	}

	return mType->info.canPushItems;
}
