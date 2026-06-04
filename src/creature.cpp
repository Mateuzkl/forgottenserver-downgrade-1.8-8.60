// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "creature.h"

#include "configmanager.h"
#include "events.h"
#include "game.h"
#include "monster.h"
#include "scheduler.h"
#include "scriptmanager.h"
#include "instance_utils.h"
#include "tools.h"

extern Game g_game;

namespace {

std::shared_ptr<Creature> getSharedCreature(Creature* creature)
{
	return creature ? creature->weak_from_this().lock() : nullptr;
}

} // namespace

std::unordered_set<const Creature*> Creature::liveCreatures;

Creature::Creature()
{
	liveCreatures.insert(this);
	onIdleStatus();
}

Creature::~Creature()
{
	liveCreatures.erase(this);

	attackedCreature.reset();
	followCreature.reset();
	removeMaster();

	SummonList summonRefs;
	summonRefs.swap(summons);
	for (const auto& summonRef : summonRefs) {
		if (auto summon = summonRef.lock()) {
			summon->setAttackedCreature(nullptr);
			summon->removeMaster();
		}
	}

	conditions.clear();

	// clear damage for prevent memory leak
	damageMap.clear();

	// clear events list to prevent memory leak
	eventsList.clear();
}

bool Creature::canSee(const Position& myPos, const Position& pos, int32_t viewRangeX, int32_t viewRangeY)
{
	if (myPos.z <= 7) {
		// we are on ground level or above (7 -> 0)
		// view is from current floor ±2, capped within 0-7
		if (pos.z > 7 || myPos.getDistanceZ(pos) > 2) {
			return false;
		}
	} else if (myPos.z >= 8) {
		// we are underground (8 -> 15)
		// we can't see floors above 8
		if (pos.z < 8) {
			return false;
		}

		// view is +/- 2 from the floor we stand on
		if (myPos.getDistanceZ(pos) > 2) {
			return false;
		}
	}

	int32_t offsetz = myPos.getOffsetZ(pos);
	return (pos.getX() >= myPos.getX() - viewRangeX + offsetz) && (pos.getX() <= myPos.getX() + viewRangeX + offsetz) &&
	       (pos.getY() >= myPos.getY() - viewRangeY + offsetz) && (pos.getY() <= myPos.getY() + viewRangeY + offsetz);
}

bool Creature::canSee(const Position& pos) const
{
	return canSee(getPosition(), pos, Map::maxViewportX, Map::maxViewportY);
}

bool Creature::canSeeCreature(const Creature* creature) const
{
	if (!creature) {
		return false;
	}

	if (getInstanceID() != creature->getInstanceID()) {
		return false;
	}

	if (!canSeeGhostMode(creature) && creature->isInGhostMode()) {
		return false;
	}

	if (!canSeeInvisibility() && creature->isInvisible()) {
		return false;
	}
	return true;
}

void Creature::setSkull(Skulls_t newSkull)
{
	skull = newSkull;
	g_game.updateCreatureSkull(this);
}

void Creature::setGuildEmblem(GuildEmblems_t newEmblem)
{
	emblem = newEmblem;
	g_game.updateCreatureEmblem(this);
}

void Creature::setSkillLoss(bool _skillLoss) { skillLoss = _skillLoss; }

int64_t Creature::getTimeSinceLastMove() const
{
	if (lastStep) {
		return OTSYS_TIME() - lastStep;
	}
	return std::numeric_limits<int64_t>::max();
}

int32_t Creature::getWalkDelay(Direction dir) const
{
	if (lastStep == 0) {
		return 0;
	}

	int64_t ct = OTSYS_TIME();
	int64_t stepDuration = getStepDuration(dir);
	return stepDuration - (ct - lastStep);
}

int32_t Creature::getWalkDelay() const
{
	// Used for auto-walking
	if (lastStep == 0) {
		return 0;
	}

	int64_t ct = OTSYS_TIME();
	int64_t stepDuration = getStepDuration();
	return stepDuration - (ct - lastStep);
}

void Creature::onThink(uint32_t interval)
{
	if (auto fc = followCreature.lock()) {
		auto masterCreature = master.lock();
		if (fc->isRemoved() || (masterCreature.get() != fc.get() && !canSeeCreature(fc.get()))) {
			onCreatureDisappear(fc.get(), false);
		}
	}

	if (auto ac = attackedCreature.lock()) {
		auto masterCreature = master.lock();
		if (ac->isRemoved() || (masterCreature.get() != ac.get() && !canSeeCreature(ac.get()))) {
			onCreatureDisappear(ac.get(), false);
		}
	}

	blockTicks += interval;
	if (blockTicks >= 1000) {
		blockCount = std::min<uint32_t>(blockCount + 1, 2);
		blockTicks = 0;
	}

	if (!followCreature.expired()) {
		walkUpdateTicks += interval;
		if (forceUpdateFollowPath || walkUpdateTicks >= 2000) {
			walkUpdateTicks = 0;
			forceUpdateFollowPath = false;
			requestFollowPathUpdate();
		}
	}

	// scripting event - onThink
	const CreatureEventList& thinkEvents = getCreatureEvents(CREATURE_EVENT_THINK);
	for (CreatureEvent* thinkEvent : thinkEvents) {
		thinkEvent->executeOnThink(this, interval);
	}
}

void Creature::onAttacking(uint32_t interval)
{
	// OPTIMIZATION: Removed redundant isDead/isRemoved checks.
	// checkCreatures() already validates creature state before calling this.

	auto attacked = attackedCreature.lock();
	if (!attacked) {
		return;
	}

	if (attacked->isRemoved()) {
		setAttackedCreature(nullptr);
		return;
	}

	onAttacked();
	attacked->onAttacked();

	if (g_game.isSightClear(getPosition(), attacked->getPosition(), true)) {
		doAttacking(interval);
	}
}

void Creature::onIdleStatus()
{
	if (!isDead()) {
		damageMap.clear();
		lastAttacker.reset();
	}
}

void Creature::onWalk()
{
	if (getWalkDelay() <= 0) {
		Direction dir;
		uint32_t flags = FLAG_IGNOREFIELDDAMAGE;
		if (getNextStep(dir, flags)) {
			ReturnValue ret = g_game.internalMoveCreature(this, dir, flags);
			if (ret != RETURNVALUE_NOERROR) {
				if (Player* player = getPlayer()) {
					player->sendCancelMessage(ret);
					player->sendCancelWalk();
				}

				forceUpdateFollowPath = true;
			}
		} else {
			stopEventWalk();

			if (listWalkDir.empty()) {
				onWalkComplete();
			}
		}
	}

	if (cancelNextWalk) {
		listWalkDir.clear();
		onWalkAborted();
		cancelNextWalk = false;
	}

	if (eventWalk != 0) {
		eventWalk = 0;
		addEventWalk();
	}
}

void Creature::onWalk(Direction& dir)
{
	if (!hasCondition(CONDITION_DRUNK)) {
		return;
	}

	uint16_t rand = static_cast<uint16_t>(uniform_random(0, 399));
	if (rand / 4 > getDrunkenness()) {
		return;
	}

	dir = static_cast<Direction>(rand % 4);
	g_game.internalCreatureSay(this, TALKTYPE_MONSTER_SAY, "Hicks!", false);
}

bool Creature::getNextStep(Direction& dir, uint32_t&)
{
	if (listWalkDir.empty()) {
		return false;
	}

	dir = listWalkDir.back();
	listWalkDir.pop_back();
	onWalk(dir);
	return true;
}

void Creature::startAutoWalk()
{
	if (isMovementBlocked()) {
		if (auto player = getPlayer()) {
			player->sendCancelWalk();
		}
		return;
	}

	addEventWalk(listWalkDir.size() == 1);
}

void Creature::startAutoWalk(Direction direction)
{
	if (isMovementBlocked()) {
		if (auto player = getPlayer()) {
			player->sendCancelWalk();
		}
		return;
	}

	listWalkDir.clear();
	listWalkDir.push_back(direction);
	addEventWalk(true);
}

void Creature::startAutoWalk(const std::vector<Direction>& listDir)
{
	if (isMovementBlocked()) {
		if (auto player = getPlayer()) {
			player->sendCancelWalk();
		}
		return;
	}

	listWalkDir = listDir;
	addEventWalk(listWalkDir.size() == 1);
}

void Creature::addEventWalk(bool firstStep)
{
	cancelNextWalk = false;

	if (getStepSpeed() <= 0) {
		return;
	}

	if (eventWalk != 0) {
		return;
	}

	int64_t ticks = getEventStepTicks(firstStep);
	if (ticks <= 0) {
		return;
	}

	const uint32_t safeTicks = static_cast<uint32_t>(std::max<int64_t>(1, ticks));
	const uint32_t cid = getID();

	eventWalk = g_scheduler.addEvent(createSchedulerTask(safeTicks,
	                                                     [cid]() { g_game.checkCreatureWalk(cid); }));
}

void Creature::stopEventWalk()
{
	if (eventWalk != 0) {
		g_scheduler.stopEvent(eventWalk);
		eventWalk = 0;
	}
}

void Creature::onCreatureAppear(Creature* creature, bool isLogin)
{
	if (creature == this) {
		if (isLogin) {
			setLastPosition(getPosition());
		}
	}
}

void Creature::onRemoveCreature(Creature* creature, bool isLogout)
{
	(void)isLogout;
	onCreatureDisappear(creature, true);
}

void Creature::onCreatureDisappear(const Creature* creature, bool isLogout)
{
	if (auto attacked = attackedCreature.lock(); attacked.get() == creature) {
		setAttackedCreature(nullptr);
		onAttackedCreatureDisappear(isLogout);
	}

	if (auto follow = followCreature.lock(); follow.get() == creature) {
		setFollowCreature(nullptr);
		onFollowCreatureDisappear(isLogout);
	}
}

void Creature::onChangeZone(ZoneType_t zone)
{
	if (auto ac = attackedCreature.lock(); ac && zone == ZONE_PROTECTION) {
		onCreatureDisappear(ac.get(), false);
	}
}

void Creature::onAttackedCreatureChangeZone(ZoneType_t zone)
{
	if (zone == ZONE_PROTECTION) {
		if (auto ac = attackedCreature.lock()) {
			onCreatureDisappear(ac.get(), false);
		}
	}
}

void Creature::onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos, const Tile* oldTile,
                              const Position& oldPos, bool teleport)
{
	if (creature == this) {
		lastStep = OTSYS_TIME();
		lastStepCost = 1;

		if (!teleport) {
			if (oldPos.z != newPos.z) {
				// floor change extra cost
				lastStepCost = 2;
			} else if (newPos.getDistanceX(oldPos) >= 1 && newPos.getDistanceY(oldPos) >= 1) {
				// diagonal extra cost
				lastStepCost = 3;
				if (getPlayer()) {
					lastStepCost -= 1;
				}
			}
		} else {
			stopEventWalk();
		}

		if (!summons.empty()) {
			std::erase_if(summons, [](const std::weak_ptr<Creature>& summon) { return summon.expired(); });

			// check if any of our summons is out of range (+/- 2 floors or 30 tiles away)
			std::forward_list<Creature*> despawnList;
			for (const auto& summonRef : summons) {
				auto summon = summonRef.lock();
				if (!summon) {
					continue;
				}

				const Position& pos = summon->getPosition();
				if (newPos.getDistanceZ(pos) > 2 || std::max(newPos.getDistanceX(pos), newPos.getDistanceY(pos)) > 30) {
					const Monster* summonMonster = summon->getMonster();
					const bool canTeleportPlayerSummon = ConfigManager::getBoolean(ConfigManager::TELEPORT_SUMMON) ||
					                                     (summonMonster && summonMonster->isFamiliar());
					if (getPlayer() && canTeleportPlayerSummon) {
						summon->setInstanceID(getInstanceID());
						g_game.internalTeleport(summon.get(), newPos, false, 0, CONST_ME_NONE);
						g_game.addMagicEffect(newPos, CONST_ME_TELEPORT, summon->getInstanceID());
					} else if (!getPlayer()) {
						despawnList.push_front(summon.get());
					}
				}
			}

			for (Creature* despawnCreature : despawnList) {
				g_game.removeCreature(despawnCreature, true);
			}
		}

		// protection time - only check for the creature that is actually moving
		if (creature == this) {
			if (Player *player = creature->getPlayer()) {
				if (player->getProtectionTime() > 0) {
					player->setProtectionTime(0);
				}
			}
		}

		if (newTile->getZone() != oldTile->getZone()) {
			g_events->eventCreatureOnChangeZone(this, oldTile->getZone(), newTile->getZone());
			onChangeZone(getZone());
		}
	}

	if (auto fc = followCreature.lock(); creature == fc.get() || (creature == this && fc)) {
		if (hasFollowPath) {
			requestFollowPathUpdate();
		}

		if (newPos.z != oldPos.z || !canSee(fc->getPosition())) {
			onCreatureDisappear(fc.get(), false);
		}
	}

	if (auto ac = attackedCreature.lock(); creature == ac.get() || (creature == this && ac)) {
		if (newPos.z != oldPos.z || !canSee(ac->getPosition())) {
			onCreatureDisappear(ac.get(), false);
		} else {
			if (hasExtraSwing()) {
				// our target is moving lets see if we can get in hit
				g_dispatcher.addTask(createTask([id = getID()]() { g_game.checkCreatureAttack(id); }));
			}

			if (newTile->getZone() != oldTile->getZone()) {
				onAttackedCreatureChangeZone(ac->getZone());
			}
		}
	}
}

CreatureVector Creature::getKillers() const
{
	CreatureVector killers;
	killers.reserve(damageMap.size());
	const int64_t timeNow = OTSYS_TIME();
	const int64_t inFightTicks = getInteger(ConfigManager::PZ_LOCKED);
	for (const auto& it : damageMap) {
		Creature* attacker = g_game.getCreatureByID(it.first);
		if (attacker && attacker != this && timeNow - it.second.ticks <= inFightTicks) {
			if (auto attackerRef = getSharedCreature(attacker)) {
				killers.push_back(std::move(attackerRef));
			}
		}
	}
	return killers;
}

void Creature::onDeath()
{
	bool lastHitUnjustified = false;
	bool mostDamageUnjustified = false;
	auto self = getSharedCreature(this);
	if (!self) {
		return;
	}
	auto lastHitCreature = lastAttacker.lock();
	std::shared_ptr<Creature> lastHitCreatureMaster;
	if (lastHitCreature) {
		lastHitUnjustified = lastHitCreature->onKilledCreature(self);
		lastHitCreatureMaster = lastHitCreature->getMasterShared();
	}

	std::shared_ptr<Creature> mostDamageCreature;

	const int64_t timeNow = OTSYS_TIME();
	const int64_t inFightTicks = getInteger(ConfigManager::PZ_LOCKED);
	int32_t mostDamage = 0;
	std::unordered_map<std::shared_ptr<Creature>, uint64_t> experienceMap;
	for (const auto& it : damageMap) {
		if (Creature* attackerRaw = g_game.getCreatureByID(it.first)) {
			auto attacker = getSharedCreature(attackerRaw);
			if (!attacker) {
				continue;
			}
			CountBlock_t cb = it.second;
			if ((cb.total > mostDamage && (timeNow - cb.ticks <= inFightTicks))) {
				mostDamage = cb.total;
				mostDamageCreature = attacker;
			}

			if (attacker.get() != this) {
				uint64_t gainExp = getGainedExperience(attacker);
				if (Player* attackerPlayer = attacker->getPlayer()) {
					attackerPlayer->removeAttacked(getPlayer());

					Party* party = attackerPlayer->getParty();
					auto partyLeader = party ? party->getLeader() : nullptr;
					if (partyLeader && party->isSharedExperienceActive() && party->isSharedExperienceEnabled()) {
						attacker = std::move(partyLeader);
					}
				}

				auto tmpIt = experienceMap.find(attacker);
				if (tmpIt == experienceMap.end()) {
					experienceMap[attacker] = gainExp;
				} else {
					tmpIt->second += gainExp;
				}
			}
		}
	}

	for (const auto& it : experienceMap) {
		it.first->onGainExperience(it.second, self);
	}

	if (mostDamageCreature) {
		if (mostDamageCreature != lastHitCreature && mostDamageCreature != lastHitCreatureMaster) {
			auto mostDamageCreatureMaster = mostDamageCreature->getMasterShared();
			if (lastHitCreature != mostDamageCreatureMaster &&
			    (!lastHitCreatureMaster || mostDamageCreatureMaster != lastHitCreatureMaster)) {
				mostDamageUnjustified = mostDamageCreature->onKilledCreature(self, false);
			}
		}
	}

	bool droppedCorpse = dropCorpse(lastHitCreature.get(), mostDamageCreature.get(), lastHitUnjustified,
	                               mostDamageUnjustified);
	death(lastHitCreature.get());

	if (!master.expired()) {
		setMaster(nullptr);
	}

	if (droppedCorpse) {
		g_game.removeCreature(this, false);
	}
}

bool Creature::dropCorpse(Creature* lastHitCreature, Creature* mostDamageCreature, bool lastHitUnjustified,
                          bool mostDamageUnjustified)
{
	if (!lootDrop && getMonster()) {
		if (!master.expired()) {
			// scripting event - onDeath
			const CreatureEventList& deathEvents = getCreatureEvents(CREATURE_EVENT_DEATH);
			for (CreatureEvent* deathEvent : deathEvents) {
				deathEvent->executeOnDeath(this, nullptr, lastHitCreature, mostDamageCreature, lastHitUnjustified,
				                           mostDamageUnjustified);
			}
		}

		InstanceUtils::sendMagicEffectToInstance(getPosition(), getInstanceID(), CONST_ME_POFF);
	} else {
		std::shared_ptr<Item> splash;
		switch (getRace()) {
			case RACE_VENOM:
				splash = Item::CreateItem(ITEM_FULLSPLASH, FLUID_SLIME);
				break;

			case RACE_BLOOD:
				splash = Item::CreateItem(ITEM_FULLSPLASH, FLUID_BLOOD);
				break;

			case RACE_INK:
				splash = Item::CreateItem(ITEM_FULLSPLASH, FLUID_INK);
				break;

			default:
				break;
		}

		Tile* tile = getTile();

		if (splash) {
			splash->setInstanceID(getInstanceID());
			if (!tile || g_game.internalAddItem(tile, splash.get(), INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
				splash.reset();
			} else {
				g_game.startDecay(splash.get());
			}
		}

		auto corpse = getCorpse(lastHitCreature, mostDamageCreature);
		if (corpse) {
			uint32_t corpseInstanceId = getInstanceID();
			if (corpseInstanceId == 0) {
				if (mostDamageCreature && mostDamageCreature->getInstanceID() != 0) {
					corpseInstanceId = mostDamageCreature->getInstanceID();
				} else if (lastHitCreature && lastHitCreature->getInstanceID() != 0) {
					corpseInstanceId = lastHitCreature->getInstanceID();
				}
			}
			corpse->setInstanceID(corpseInstanceId);
			if (!tile || g_game.internalAddItem(tile, corpse.get(), INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
				corpse.reset();
			} else {
				g_game.startDecay(corpse.get());
			}
		}

		// MonsterType onDeath
		if (auto monster = getMonster()) {
			const MonsterType* mType = monster->getMonsterType();
			if (mType->info.deathEvent != -1) {
				LuaScriptInterface* scriptInterface = mType->info.scriptInterface;
				if (!scriptInterface->reserveScriptEnv()) {
					LOG_ERROR("[Error - Creature::dropCorpse] Call stack overflow");
				} else {
					ScriptEnvironment* env = scriptInterface->getScriptEnv();
					env->setScriptId(mType->info.deathEvent, scriptInterface);

					lua_State* L = scriptInterface->getLuaState();
					scriptInterface->pushFunction(mType->info.deathEvent);
					Lua::pushUserdata<Creature>(L, this);
					Lua::setCreatureMetatable(L, -1, this);

					Lua::pushThing(L, corpse.get());

					if (lastHitCreature) {
						Lua::pushUserdata<Creature>(L, lastHitCreature);
						Lua::setCreatureMetatable(L, -1, lastHitCreature);
					} else {
						lua_pushnil(L);
					}

					if (mostDamageCreature) {
						Lua::pushUserdata<Creature>(L, mostDamageCreature);
						Lua::setCreatureMetatable(L, -1, mostDamageCreature);
					} else {
						lua_pushnil(L);
					}

					Lua::pushBoolean(L, lastHitUnjustified);
					Lua::pushBoolean(L, mostDamageUnjustified);

					scriptInterface->callFunction(6);
				}
			}
		}

		// scripting event - onDeath
		for (CreatureEvent* deathEvent : getCreatureEvents(CREATURE_EVENT_DEATH)) {
			deathEvent->executeOnDeath(this, corpse.get(), lastHitCreature, mostDamageCreature, lastHitUnjustified,
			                           mostDamageUnjustified);
		}

		if (corpse) {
			if (Container* corpseContainer = corpse->getContainer()) {
				dropLoot(corpseContainer, lastHitCreature);

				uint32_t corpseOwnerId = corpse->getCorpseOwner();
				if (corpseOwnerId != 0) {
					if (auto corpseOwner = g_game.getPlayerByID(corpseOwnerId)) {
						if (ConfigManager::getBoolean(ConfigManager::LOOT_GROUPING_ENABLED)) {
							corpseOwner->addPendingLoot(getNameDescription(), corpseContainer);
						}
						corpseOwner->lootCorpse(corpseContainer);
					}
				}

				// Start loot highlight — shows the sparkling effect on the corpse
				// so the owner (and later everyone) knows there's loot inside
				if (!corpseContainer->empty() && corpseOwnerId != 0) {
					g_game.startLootHighlight(corpseContainer, corpseOwnerId);
				}
			}
		}
	}

	return true;
}

bool Creature::hasBeenAttacked(uint32_t attackerId)
{
	auto it = damageMap.find(attackerId);
	if (it == damageMap.end()) {
		return false;
	}
	return (OTSYS_TIME() - it->second.ticks) <= getInteger(ConfigManager::PZ_LOCKED);
}

std::shared_ptr<Item> Creature::getCorpse(Creature*, Creature*) { return Item::CreateItem(getLookCorpse()); }

void Creature::changeHealth(int32_t healthChange, bool sendHealthChange /* = true*/)
{
	int32_t oldHealth = health;

	if (healthChange > 0) {
		health += std::min<int32_t>(healthChange, getMaxHealth() - health);
	} else {
		health = std::max<int32_t>(0, health + healthChange);
	}

	if (sendHealthChange && oldHealth != health) {
		g_game.addCreatureHealth(this);
	}

	if (isDead()) {
		g_dispatcher.addTask([id = getID()]() { g_game.executeDeath(id); });
	}
}

void Creature::gainHealth(const std::shared_ptr<Creature>& healer, int32_t healthGain)
{
	changeHealth(healthGain);
	if (healer) {
		if (auto self = getSharedCreature(this)) {
			healer->onTargetCreatureGainHealth(self, healthGain);
		}
	}
}

void Creature::drainHealth(const std::shared_ptr<Creature>& attacker, int32_t damage)
{
	changeHealth(-damage, false);

	if (attacker) {
		if (auto self = getSharedCreature(this)) {
			attacker->onAttackedCreatureDrainHealth(self, damage);
		}
	} else {
		lastAttacker.reset();
	}
}

BlockType_t Creature::blockHit(const std::shared_ptr<Creature>& attacker, CombatType_t combatType, int32_t& damage,
                               bool checkDefense /* = false */, bool checkArmor /* = false */, bool /* field = false */,
                               bool /* ignoreResistances = false */, CombatOrigin /* origin = ORIGIN_NONE */)
{
	BlockType_t blockType = BLOCK_NONE;

	if (attacker && combatType != COMBAT_HEALING) {
		Player* attackerPlayer = attacker->getPlayer();
		Player* targetPlayer = getPlayer();
		if (attackerPlayer && targetPlayer) {
			damage = std::round(damage * attackerPlayer->getVocation()->pvpDamageDealtMultiplier);
			damage = std::round(damage * targetPlayer->getVocation()->pvpDamageReceivedMultiplier);
		}
	}

	if (isImmune(combatType)) {
		damage = 0;
		blockType = BLOCK_IMMUNITY;
	} else if (combatType != COMBAT_HEALING && (checkDefense || checkArmor)) {
		bool hasDefense = false;

		if (blockCount > 0) {
			--blockCount;
			hasDefense = true;
		}

		if (checkDefense && hasDefense && canUseDefense) {
			int32_t defense = getDefense();
			damage -= uniform_random(defense / 2, defense);
			if (damage <= 0) {
				damage = 0;
				blockType = BLOCK_DEFENSE;
				checkArmor = false;
			}
		}

		if (checkArmor) {
			int32_t armor = getArmor();
			if (armor > 3) {
				damage -= uniform_random(armor / 2, armor - (armor % 2 + 1));
			} else if (armor > 0) {
				--damage;
			}

			if (damage <= 0) {
				damage = 0;
				blockType = BLOCK_ARMOR;
			}
		}

		if (hasDefense && blockType != BLOCK_NONE) {
			onBlockHit();
		}
	}

	if (attacker) {
		if (Player* attackerPlayer = attacker->getPlayer()) {
			for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
				if (!attackerPlayer->isItemAbilityEnabled(static_cast<slots_t>(slot))) {
					continue;
				}

				Item* item = attackerPlayer->getInventoryItem(static_cast<slots_t>(slot));
				if (!item) {
					continue;
				}

				const uint16_t boostPercent = item->getBoostPercent(combatType);
				if (boostPercent != 0) {
					damage += std::round(damage * (boostPercent / 100.));
				}
			}
		}

		if (damage <= 0) {
			damage = 0;
			blockType = BLOCK_ARMOR;
		}

		if (combatType != COMBAT_HEALING) {
			if (auto self = getSharedCreature(this)) {
				attacker->onAttackedCreature(self);
			}
			attacker->onAttackedCreatureBlockHit(blockType);
			// OPTIMIZATION: Removed per-hit master notification for summons.
			// onAttackedCreature on master was called every single hit which
			// adds 3 virtual calls per hit per summon. PZ/skull logic is
			// already handled in combat/death paths.
		}
	}

	if (combatType != COMBAT_HEALING) {
		float mitigation = getMitigation();
		if (mitigation > 0.0f) {
			damage -= std::round(damage * (mitigation / 100.0f));
			if (damage <= 0) {
				damage = 0;
				blockType = BLOCK_ARMOR;
			}
		}

		onAttacked();
	}
	return blockType;
}

bool Creature::setAttackedCreature(Creature* creature)
{
	if (creature) {
		assert(!creature->isRemoved() && "Setting removed creature as attack target");
		const Position& creaturePos = creature->getPosition();
		if (creaturePos.z != getPosition().z || !canSee(creaturePos)) {
			attackedCreature.reset();
			return false;
		}

		auto creatureRef = getSharedCreature(creature);
		if (!creatureRef) {
			attackedCreature.reset();
			return false;
		}
		attackedCreature = creatureRef;
		onAttackedCreature(creatureRef);
		creature->onAttacked();
	} else {
		attackedCreature.reset();
	}

	for (const auto& summonRef : summons) {
		if (auto summon = summonRef.lock()) {
			summon->setAttackedCreature(creature);
		}
	}
	return true;
}

void Creature::getPathSearchParams(const Creature*, FindPathParams& fpp) const
{
	const Monster* monster = getMonster();
	if (monster && monster->getMaster()) {
		fpp.fullPathSearch = false;
		fpp.clearSight = false;
		fpp.maxSearchDist = 8;
		fpp.minTargetDist = 1;
		fpp.maxTargetDist = 2;
	} else if (monster) {
		fpp.fullPathSearch = false;
		fpp.clearSight = true;
		fpp.maxSearchDist = 10;
		fpp.minTargetDist = 1;
		fpp.maxTargetDist = 1;
	} else {
		fpp.fullPathSearch = !hasFollowPath;
		fpp.clearSight = true;
		fpp.maxSearchDist = 12;
		fpp.minTargetDist = 1;
		fpp.maxTargetDist = 1;
	}
}

void Creature::goToFollowCreature()
{
	if (auto fc = followCreature.lock()) {
		FindPathParams fpp;
		getPathSearchParams(fc.get(), fpp);

		listWalkDir.clear();

		if (getPathTo(fc->getPosition(), listWalkDir, fpp)) {
			hasFollowPath = true;
			startAutoWalk(listWalkDir);
		} else
			hasFollowPath = false;
	}

	auto follow = followCreature.lock();
	onFollowCreatureComplete(follow.get());
}

void Creature::requestFollowPathUpdate()
{
	if (isUpdatingPath || followCreature.expired()) {
		return;
	}

	isUpdatingPath = true;
	g_dispatcher.addTask(createTask([id = getID()] { g_game.updateCreatureWalk(id); }));
}

bool Creature::setFollowCreature(Creature* creature)
{
	if (creature) {
		if (auto follow = followCreature.lock(); follow.get() == creature) {
			requestFollowPathUpdate();
			return true;
		}

		const Position& creaturePos = creature->getPosition();
		if (creaturePos.z != getPosition().z || !canSee(creaturePos)) {
			isUpdatingPath = false;
			hasFollowPath = false;
			followCreature.reset();
			return false;
		}

		if (!listWalkDir.empty()) {
			listWalkDir.clear();
			onWalkAborted();
		}

		hasFollowPath = false;
		forceUpdateFollowPath = false;
		auto creatureRef = getSharedCreature(creature);
		if (!creatureRef) {
			isUpdatingPath = false;
			hasFollowPath = false;
			followCreature.reset();
			return false;
		}
		followCreature = creatureRef;
		requestFollowPathUpdate();
	} else {
		isUpdatingPath = false;
		hasFollowPath = false;
		followCreature.reset();
	}

	onFollowCreature(creature);
	return true;
}

size_t Creature::getSummonCount() const
{
	return std::count_if(summons.begin(), summons.end(),
	                     [](const std::weak_ptr<Creature>& summon) { return !summon.expired(); });
}

double Creature::getDamageRatio(const std::shared_ptr<Creature>& attacker) const
{
	uint32_t totalDamage = 0;
	uint32_t attackerDamage = 0;

	for (const auto& it : damageMap) {
		const CountBlock_t& cb = it.second;
		totalDamage += cb.total;
		if (attacker && it.first == attacker->getID()) {
			attackerDamage += cb.total;
		}
	}

	if (totalDamage == 0) {
		return 0;
	}

	return (static_cast<double>(attackerDamage) / totalDamage);
}

uint64_t Creature::getGainedExperience(const std::shared_ptr<Creature>& attacker) const
{
	return std::floor(getDamageRatio(attacker) * getLostExperience());
}

void Creature::addDamagePoints(const std::shared_ptr<Creature>& attacker, int32_t damagePoints)
{
	if (damagePoints <= 0 || !attacker) {
		return;
	}

	uint32_t attackerId = attacker->id;

	auto& cb = damageMap[attackerId];
	cb.ticks = OTSYS_TIME();
	cb.total += damagePoints;

	lastAttacker = attacker;
}

void Creature::onAddCondition(ConditionType_t type)
{
	if ((type & CONDITION_PARALYZE) && hasCondition(CONDITION_HASTE)) {
		removeCondition(CONDITION_HASTE);
	} else if ((type & CONDITION_HASTE) && hasCondition(CONDITION_PARALYZE)) {
		removeCondition(CONDITION_PARALYZE);
	}
}

void Creature::onAddCombatCondition(ConditionType_t)
{
	//
}

void Creature::onEndCondition(ConditionType_t)
{
	//
}

void Creature::onTickCondition(ConditionType_t type, bool& bRemove)
{
	const Tile* tile = getTile();
	if (!tile) {
		return;
	}

	const MagicField* field = tile->getFieldItem(getInstanceID());
	if (!field) {
		return;
	}

	switch (type) {
		case CONDITION_FIRE:
			bRemove = (field->getCombatType() != COMBAT_FIREDAMAGE);
			break;
		case CONDITION_ENERGY:
			bRemove = (field->getCombatType() != COMBAT_ENERGYDAMAGE);
			break;
		case CONDITION_POISON:
			bRemove = (field->getCombatType() != COMBAT_EARTHDAMAGE);
			break;
		case CONDITION_FREEZING:
			bRemove = (field->getCombatType() != COMBAT_ICEDAMAGE);
			break;
		case CONDITION_DAZZLED:
			bRemove = (field->getCombatType() != COMBAT_HOLYDAMAGE);
			break;
		case CONDITION_CURSED:
			bRemove = (field->getCombatType() != COMBAT_DEATHDAMAGE);
			break;
		case CONDITION_AGONY:
			bRemove = (field->getCombatType() != COMBAT_AGONYDAMAGE);
			break;
		case CONDITION_DROWN:
			bRemove = (field->getCombatType() != COMBAT_DROWNDAMAGE);
			break;
		case CONDITION_BLEEDING:
			bRemove = (field->getCombatType() != COMBAT_PHYSICALDAMAGE);
			break;
		default:
			break;
	}
}

void Creature::onCombatRemoveCondition(Condition* condition) { removeCondition(condition); }

void Creature::onAttacked()
{
	//
}

void Creature::onAttackedCreatureDrainHealth(const std::shared_ptr<Creature>& target, int32_t points)
{
	if (auto self = getSharedCreature(this)) {
		target->addDamagePoints(self, points);
	}
}

bool Creature::onKilledCreature(const std::shared_ptr<Creature>& target, bool)
{
	if (auto m = master.lock()) {
		m->onKilledCreature(target);
	}

	// scripting event - onKill
	const CreatureEventList& killEvents = getCreatureEvents(CREATURE_EVENT_KILL);
	for (CreatureEvent* killEvent : killEvents) {
		killEvent->executeOnKill(this, target.get());
	}
	return false;
}

void Creature::onGainExperience(uint64_t gainExp, const std::shared_ptr<Creature>& target)
{
	auto m = master.lock();
	if (gainExp == 0 || !m) {
		return;
	}

	gainExp /= 2;
	m->onGainExperience(gainExp, target);

	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, position, false, true);
	InstanceUtils::filterByInstanceInPlace(spectators, getInstanceID());
	if (spectators.empty()) {
		return;
	}

	std::string expText = std::to_string(gainExp);
	if (getBoolean(ConfigManager::MODIFY_EXP_IN_K)) {
		expText = formatValueK(static_cast<int64_t>(gainExp));
	}

	TextMessage textMessage(MESSAGE_STATUS_DEFAULT,
	                        fmt::format("{:s} gained {:s} {:s}.", ucfirst(getNameDescription()), expText,
	                                    gainExp != 1 ? "experience points" : "experience point"));
	for (const auto& spectator : spectators) {
		static_cast<Player*>(spectator.get())->sendTextMessage(textMessage);
	}

	std::string animatedText = expText;
	TextColor_t animatedColor = TEXTCOLOR_WHITE;
	if (getBoolean(ConfigManager::MODIFY_EXP_IN_K)) {
		if (const auto storedColor = m->getStorageValue(STORAGE_EXP_COLOR); storedColor && storedColor.value() > 0 &&
		                                                         storedColor.value() <= std::numeric_limits<uint8_t>::max()) {
			animatedColor = static_cast<TextColor_t>(storedColor.value());
		} else {
			animatedColor = static_cast<TextColor_t>(getInteger(ConfigManager::DEFAULT_EXP_COLOR));
		}
	}

	g_game.addAnimatedText(spectators, animatedText, position, animatedColor);
}

bool Creature::setMaster(Creature* newMaster)
{
	auto oldMaster = master.lock();
	if (!newMaster) {
		if (!oldMaster) {
			return false;
		}
		removeMaster();
		return true;
	}

	if (oldMaster.get() == newMaster) {
		return true;
	}

	if (oldMaster) {
		removeMaster();
	}

	auto& newMasterSummons = newMaster->summons;
	std::erase_if(newMasterSummons, [](const std::weak_ptr<Creature>& summon) { return summon.expired(); });

	const bool alreadySummoned = std::any_of(newMasterSummons.begin(), newMasterSummons.end(),
	                                         [this](const std::weak_ptr<Creature>& summon) {
		                                         auto lockedSummon = summon.lock();
		                                         return lockedSummon && lockedSummon.get() == this;
	                                         });
	auto self = getSharedCreature(this);
	auto masterRef = getSharedCreature(newMaster);
	if (!self || !masterRef) {
		return false;
	}

	if (!alreadySummoned) {
		newMasterSummons.emplace_back(self);
	}

	if (getInstanceID() != newMaster->getInstanceID()) {
		setInstanceID(newMaster->getInstanceID());
	}

	master = std::move(masterRef);
	return true;
}

void Creature::removeMaster()
{
	auto oldMaster = master.lock();
	master.reset();
	if (!oldMaster) {
		return;
	}

	std::erase_if(oldMaster->summons, [this](const std::weak_ptr<Creature>& summon) {
		auto lockedSummon = summon.lock();
		return !lockedSummon || lockedSummon.get() == this;
	});
}

bool Creature::addCondition(Condition_ptr condition, bool force /* = false*/)
{
	if (!condition) {
		return false;
	}

	if (!force && (condition->getType() == CONDITION_ROOTED || condition->getType() == CONDITION_FEARED) &&
	    isImmune(condition->getType())) {
		return false;
	}

	if (!force && condition->getType() == CONDITION_HASTE && hasCondition(CONDITION_PARALYZE)) {
		int64_t walkDelay = getWalkDelay();
		if (walkDelay > 0) {
			auto condWrapper = std::make_shared<Condition_ptr>(std::move(condition));
			g_scheduler.addEvent(
			    createSchedulerTask(walkDelay, ([id = getID(), condWrapper]() {
				    g_game.forceAddCondition(id, condWrapper->release());
			    })));
			return false;
		}
	}

	Condition* prevCond = getCondition(condition->getType(), condition->getId(), condition->getSubId());
	if (prevCond) {
		prevCond->addCondition(this, condition.get());
		return true;
	}

	if (condition->startCondition(this)) {
		const ConditionType_t type = condition->getType();
		conditions.push_back(std::move(condition));
		onAddCondition(type);
		return true;
	}

	return false;
}

bool Creature::addCombatCondition(Condition_ptr condition)
{
	// Caution: condition variable could be deleted after the call to addCondition
	ConditionType_t type = condition->getType();

	if (!addCondition(std::move(condition))) {
		return false;
	}

	onAddCombatCondition(type);
	return true;
}

void Creature::removeCondition(ConditionType_t type, bool force /* = false*/)
{
	auto it = conditions.begin();
	while (it != conditions.end()) {
		if ((*it)->getType() != type) {
			++it;
			continue;
		}

		if (!force && type == CONDITION_PARALYZE) {
			int64_t walkDelay = getWalkDelay();
			if (walkDelay > 0) {
				g_scheduler.addEvent(
				    createSchedulerTask(walkDelay, ([=, id = getID()]() { g_game.forceRemoveCondition(id, type); })));
				return;
			}
		}

		auto owned = std::move(*it);
		it = conditions.erase(it);

		owned->endCondition(this);
		onEndCondition(type);
	}
}

void Creature::removeCondition(ConditionType_t type, ConditionId_t conditionId, bool force /* = false*/)
{
	auto it = conditions.begin();
	while (it != conditions.end()) {
		if ((*it)->getType() != type || (*it)->getId() != conditionId) {
			++it;
			continue;
		}

		if (!force && type == CONDITION_PARALYZE) {
			int64_t walkDelay = getWalkDelay();
			if (walkDelay > 0) {
				g_scheduler.addEvent(
				    createSchedulerTask(walkDelay, ([=, id = getID()]() { g_game.forceRemoveCondition(id, type); })));
				return;
			}
		}

		auto owned = std::move(*it);
		it = conditions.erase(it);
		owned->endCondition(this);
		onEndCondition(type);
	}
}

void Creature::removeCombatCondition(ConditionType_t type)
{
	std::vector<Condition*> removeConditions;
	for (const auto& condition : conditions) {
		if (condition->getType() == type) {
			removeConditions.push_back(condition.get());
		}
	}

	for (Condition* condition : removeConditions) {
		onCombatRemoveCondition(condition);
	}
}

void Creature::removeCondition(Condition* condition, bool force /* = false*/)
{
	auto it = std::find_if(conditions.begin(), conditions.end(),
	                        [condition](const auto& c) { return c.get() == condition; });
	if (it == conditions.end()) {
		return;
	}

	if (!force && condition->getType() == CONDITION_PARALYZE) {
		int64_t walkDelay = getWalkDelay();
		if (walkDelay > 0) {
			g_scheduler.addEvent(createSchedulerTask(
			    walkDelay, ([id = getID(), type = condition->getType()]() { g_game.forceRemoveCondition(id, type); })));
			return;
		}
	}

	auto owned = std::move(*it);
	conditions.erase(it);

	owned->endCondition(this);
	onEndCondition(owned->getType());
}

Condition* Creature::getCondition(ConditionType_t type) const
{
	for (const auto& condition : conditions) {
		if (condition->getType() == type) {
			return condition.get();
		}
	}
	return nullptr;
}

Condition* Creature::getCondition(ConditionType_t type, ConditionId_t conditionId, uint32_t subId /* = 0*/) const
{
	for (const auto& condition : conditions) {
		if (condition->getType() == type && condition->getId() == conditionId && condition->getSubId() == subId) {
			return condition.get();
		}
	}
	return nullptr;
}

void Creature::executeConditions(uint32_t interval)
{
	std::vector<Condition*> tempConditions;
	tempConditions.reserve(conditions.size());
	for (const auto& c : conditions) {
		tempConditions.push_back(c.get());
	}

	for (Condition* condition : tempConditions) {
		if (isDead() || isRemoved()) {
			break;
		}

		auto it = std::find_if(conditions.begin(), conditions.end(),
		                        [condition](const auto& c) { return c.get() == condition; });
		if (it == conditions.end()) {
			continue;
		}

		if (!condition->executeCondition(this, interval)) {
			auto it2 = std::find_if(conditions.begin(), conditions.end(),
			                         [condition](const auto& c) { return c.get() == condition; });
			if (it2 != conditions.end()) {
				const ConditionType_t condType = condition->getType();
				auto owned = std::move(*it2);
				conditions.erase(it2);
				owned->endCondition(this);
				onEndCondition(condType);
			}
		}
	}
}

bool Creature::hasCondition(ConditionType_t type, uint32_t subId /* = 0*/) const
{
	if (isSuppress(type)) {
		return false;
	}

	int64_t timeNow = OTSYS_TIME();
	for (const auto& condition : conditions) {
		if (condition->getType() != type || condition->getSubId() != subId) {
			continue;
		}

		if (condition->getEndTime() >= timeNow || condition->getTicks() == -1) {
			return true;
		}
	}
	return false;
}

bool Creature::isImmune(CombatType_t type) const
{
	return hasBitSet(static_cast<uint32_t>(type), getDamageImmunities());
}

bool Creature::isImmune(ConditionType_t type) const
{
	return hasBitSet(static_cast<uint32_t>(type), getConditionImmunities());
}

bool Creature::isSuppress(ConditionType_t type) const
{
	return hasBitSet(static_cast<uint32_t>(type), getConditionSuppressions());
}

int64_t Creature::getStepDuration(Direction dir) const
{
	int64_t stepDuration = getStepDuration();
	if ((dir & DIRECTION_DIAGONAL_MASK) != 0) {
		stepDuration *= 2;
	}
	return stepDuration;
}

int64_t Creature::getStepDuration() const
{
	if (isRemoved()) {
		return 0;
	}

	const int32_t stepSpeed = getStepSpeed();
	if (stepSpeed <= 0) {
		return 0;
	}

	uint32_t groundSpeed = 100;
	if (const Tile* tile = getTile()) {
		if (const Item* ground = tile->getGround()) {
			groundSpeed = Item::items[ground->getID()].speed;
			if (groundSpeed == 0) {
				groundSpeed = 100;
			}
		}
	}

	int64_t stepDuration = (1000 * static_cast<int64_t>(groundSpeed)) / stepSpeed;

	if (stepDuration < SCHEDULER_MINTICKS) {
		stepDuration = SCHEDULER_MINTICKS;
	}

	return stepDuration * lastStepCost;
}

int64_t Creature::getEventStepTicks(bool onlyDelay) const
{
	int64_t ret = getWalkDelay();
	if (ret > 0) {
		return ret;
	}

	if (onlyDelay) {
		return 1;
	}

	return 1;
}

LightInfo Creature::getCreatureLight() const { return internalLight; }

void Creature::setCreatureLight(LightInfo lightInfo) { internalLight = std::move(lightInfo); }

void Creature::setNormalCreatureLight() { internalLight = {}; }

bool Creature::registerCreatureEvent(std::string_view name)
{
	CreatureEvent* event = g_creatureEvents->getEventByName(name);
	if (!event) {
		return false;
	}

	CreatureEventType_t type = event->getEventType();
	const std::string normalizedName = asLowerCaseString(std::string{name});
	if (hasEventRegistered(type)) {
		for (const auto& creatureEvent : eventsList) {
			if (creatureEvent.name == normalizedName && creatureEvent.type == type) {
				return false;
			}
		}
	} else {
		scriptEventsBitField |= static_cast<uint32_t>(1) << type;
	}

	eventsList.push_back({normalizedName, type});
	return true;
}

bool Creature::unregisterCreatureEvent(std::string_view name)
{
	CreatureEvent* event = g_creatureEvents->getEventByName(name);
	if (!event) {
		return false;
	}

	CreatureEventType_t type = event->getEventType();
	if (!hasEventRegistered(type)) {
		return false;
	}

	const std::string normalizedName = asLowerCaseString(std::string{name});

	std::erase_if(eventsList, [&normalizedName, type](const auto& event) {
		return event.name == normalizedName && event.type == type;
	});

	if (std::none_of(eventsList.begin(), eventsList.end(), [type](const auto& event) { return event.type == type; })) {
		scriptEventsBitField &= ~(static_cast<uint32_t>(1) << type);
	}
	return true;
}

CreatureEventList Creature::getCreatureEvents(CreatureEventType_t type) const
{
	CreatureEventList tmpEventList;

	if (!hasEventRegistered(type)) {
		return tmpEventList;
	}

	tmpEventList.reserve(eventsList.size());
	for (const auto& registeredEvent : eventsList) {
		if (registeredEvent.type != type) {
			continue;
		}

		CreatureEvent* creatureEvent = g_creatureEvents->getEventByName(registeredEvent.name);
		if (!creatureEvent) {
			continue;
		}

		if (!creatureEvent->isLoaded()) {
			continue;
		}

		if (creatureEvent->getEventType() == registeredEvent.type) {
			tmpEventList.push_back(creatureEvent);
		}
	}

	return tmpEventList;
}

bool FrozenPathingConditionCall::isInRange(const Position& startPos, const Position& testPos,
                                           const FindPathParams& fpp) const
{
	if (fpp.fullPathSearch) {
		if (testPos.x > targetPos.x + fpp.maxTargetDist) {
			return false;
		}

		if (testPos.x < targetPos.x - fpp.maxTargetDist) {
			return false;
		}

		if (testPos.y > targetPos.y + fpp.maxTargetDist) {
			return false;
		}

		if (testPos.y < targetPos.y - fpp.maxTargetDist) {
			return false;
		}
	} else {
		int32_t dx = startPos.getOffsetX(targetPos);

		int32_t dxMax = (dx >= 0 ? fpp.maxTargetDist : 0);
		if (testPos.x > targetPos.x + dxMax) {
			return false;
		}

		int32_t dxMin = (dx <= 0 ? fpp.maxTargetDist : 0);
		if (testPos.x < targetPos.x - dxMin) {
			return false;
		}

		int32_t dy = startPos.getOffsetY(targetPos);

		int32_t dyMax = (dy >= 0 ? fpp.maxTargetDist : 0);
		if (testPos.y > targetPos.y + dyMax) {
			return false;
		}

		int32_t dyMin = (dy <= 0 ? fpp.maxTargetDist : 0);
		if (testPos.y < targetPos.y - dyMin) {
			return false;
		}
	}
	return true;
}

bool FrozenPathingConditionCall::operator()(const Position& startPos, const Position& testPos,
                                            const FindPathParams& fpp, int32_t& bestMatchDist) const
{
	if (!isInRange(startPos, testPos, fpp)) {
		return false;
	}

	if (fpp.clearSight && !g_game.isSightClear(testPos, targetPos, true)) {
		return false;
	}

	int32_t testDist = std::max(targetPos.getDistanceX(testPos), targetPos.getDistanceY(testPos));
	if (fpp.maxTargetDist == 1) {
		if (testDist < fpp.minTargetDist || testDist > fpp.maxTargetDist) {
			return false;
		}

		return true;
	} else if (testDist <= fpp.maxTargetDist) {
		if (testDist < fpp.minTargetDist) {
			return false;
		}

		if (testDist == fpp.maxTargetDist) {
			bestMatchDist = 0;
			return true;
		} else if (testDist > bestMatchDist) {
			// not quite what we want, but the best so far
			bestMatchDist = testDist;
			return true;
		}
	}
	return false;
}

bool Creature::isInvisible() const
{
	return std::find_if(conditions.begin(), conditions.end(), [](const auto& condition) {
		       return condition->getType() == CONDITION_INVISIBLE;
	       }) != conditions.end();
}

bool Creature::getPathTo(const Position& targetPos, std::vector<Direction>& dirList, const FindPathParams& fpp) const
{
	return g_game.map.getPathMatching(*this, dirList, FrozenPathingConditionCall(targetPos), fpp);
}

bool Creature::getPathTo(const Position& targetPos, std::vector<Direction>& dirList, int32_t minTargetDist,
                         int32_t maxTargetDist, bool fullPathSearch /*= true*/, bool clearSight /*= true*/,
                         int32_t maxSearchDist /*= 0*/) const
{
	FindPathParams fpp;
	fpp.fullPathSearch = fullPathSearch;
	fpp.maxSearchDist = maxSearchDist;
	fpp.clearSight = clearSight;
	fpp.minTargetDist = minTargetDist;
	fpp.maxTargetDist = maxTargetDist;
	return getPathTo(targetPos, dirList, fpp);
}

const std::vector<ZoneId>& Creature::getZoneIds() const
{
	static const std::vector<ZoneId> emptyZoneIds;
	if (const Tile* currentTile = getTile()) {
		return currentTile->getZoneIds();
	}
	return emptyZoneIds;
}

void Creature::setStorageValue(uint32_t key, std::optional<int64_t> value, bool isSpawn)
{
	auto oldValue = getStorageValue(key);
	if (value && value.value() != -1) {
		storageMap.insert_or_assign(key, value.value());
	} else {
		storageMap.erase(key);
	}
	g_events->eventCreatureOnUpdateStorage(this, key, oldValue, value, isSpawn);
}

std::optional<int64_t> Creature::getStorageValue(uint32_t key) const
{
	auto it = storageMap.find(key);
	if (it == storageMap.end()) {
		return std::nullopt;
	}
	return std::make_optional(it->second);
}
