// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "creature.h"
#include "events.h"
#include "game.h"
#include "luascript.h"
#include "player.h"

extern Events* g_events;
extern Game g_game;

namespace {
using namespace Lua;

// Creature
int luaCreatureCreate(lua_State* L)
{
	// Creature(id or name or userdata)
	Creature* creature;
	if (isInteger(L, 2)) {
		creature = g_game.getCreatureByID(getInteger<uint32_t>(L, 2));
	} else if (isString(L, 2)) {
		creature = g_game.getCreatureByName(getString(L, 2));
	} else if (isUserdata(L, 2)) {
		LuaDataType type = getUserdataType(L, 2);
		if (type != LuaData_Player && type != LuaData_Monster && type != LuaData_Npc) {
			lua_pushnil(L);
			return 1;
		}
		creature = getUserdata<Creature>(L, 2);
	} else {
		creature = nullptr;
	}

	if (creature) {
		pushUserdata<Creature>(L, creature);
		setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetEvents(lua_State* L)
{
	// creature:getEvents(type)
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	CreatureEventType_t eventType = getInteger<CreatureEventType_t>(L, 2);
	const auto& eventList = creature->getCreatureEvents(eventType);
	lua_createtable(L, eventList.size(), 0);

	int index = 0;
	for (CreatureEvent* event : eventList) {
		pushString(L, event->getName());
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaCreatureRegisterEvent(lua_State* L)
{
	// creature:registerEvent(name)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		const std::string& name = getString(L, 2);
		pushBoolean(L, creature->registerCreatureEvent(name));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureUnregisterEvent(lua_State* L)
{
	// creature:unregisterEvent(name)
	const std::string& name = getString(L, 2);
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		pushBoolean(L, creature->unregisterCreatureEvent(name));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureIsRemoved(lua_State* L)
{
	// creature:isRemoved()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		pushBoolean(L, creature->isRemoved());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureIsCreature(lua_State* L)
{
	// creature:isCreature()
	pushBoolean(L, getUserdata<const Creature>(L, 1) != nullptr);
	return 1;
}

int luaCreatureIsInGhostMode(lua_State* L)
{
	// creature:isInGhostMode()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		pushBoolean(L, creature->isInGhostMode());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureIsHealthHidden(lua_State* L)
{
	// creature:isHealthHidden()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		pushBoolean(L, creature->isHealthHidden());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureIsMovementBlocked(lua_State* L)
{
	// creature:isMovementBlocked()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		pushBoolean(L, creature->isMovementBlocked());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureCanSee(lua_State* L)
{
	// creature:canSee(position)
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		const Position& position = getPosition(L, 2);
		pushBoolean(L, creature->canSee(position));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureCanSeeCreature(lua_State* L)
{
	// creature:canSeeCreature(creature)
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		const Creature* otherCreature = getCreature(L, 2);
		if (!otherCreature) {
			reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}

		pushBoolean(L, creature->canSeeCreature(otherCreature));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureCanSeeGhostMode(lua_State* L)
{
	// creature:canSeeGhostMode(creature)
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		const Creature* otherCreature = getCreature(L, 2);
		if (!otherCreature) {
			reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}

		pushBoolean(L, creature->canSeeGhostMode(otherCreature));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureCanSeeInvisibility(lua_State* L)
{
	// creature:canSeeInvisibility()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		pushBoolean(L, creature->canSeeInvisibility());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetParent(lua_State* L)
{
	// creature:getParent()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	Cylinder* parent = creature->getParent();
	if (!parent) {
		lua_pushnil(L);
		return 1;
	}

	pushCylinder(L, parent);
	return 1;
}

int luaCreatureGetId(lua_State* L)
{
	// creature:getId()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		lua_pushinteger(L, creature->getID());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetName(lua_State* L)
{
	// creature:getName()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		pushString(L, creature->getName());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetTarget(lua_State* L)
{
	// creature:getTarget()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	Creature* target = creature->getAttackedCreature();
	if (target) {
		pushUserdata<Creature>(L, target);
		setCreatureMetatable(L, -1, target);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetTarget(lua_State* L)
{
	// creature:setTarget(target)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		pushBoolean(L, creature->setAttackedCreature(getCreature(L, 2)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetFollowCreature(lua_State* L)
{
	// creature:getFollowCreature()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	Creature* followCreature = creature->getFollowCreature();
	if (followCreature) {
		pushUserdata<Creature>(L, followCreature);
		setCreatureMetatable(L, -1, followCreature);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetFollowCreature(lua_State* L)
{
	// creature:setFollowCreature(followedCreature)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		pushBoolean(L, creature->setFollowCreature(getCreature(L, 2)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetMaster(lua_State* L)
{
	// creature:getMaster()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	Creature* master = creature->getMaster();
	if (!master) {
		lua_pushnil(L);
		return 1;
	}

	pushUserdata<Creature>(L, master);
	setCreatureMetatable(L, -1, master);
	return 1;
}

int luaCreatureSetMaster(lua_State* L)
{
	// creature:setMaster(master)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	pushBoolean(L, creature->setMaster(getCreature(L, 2)));
	return 1;
}

int luaCreatureGetLight(lua_State* L)
{
	// creature:getLight()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	LightInfo lightInfo = creature->getCreatureLight();
	lua_pushinteger(L, lightInfo.level);
	lua_pushinteger(L, lightInfo.color);
	return 2;
}

int luaCreatureSetLight(lua_State* L)
{
	// creature:setLight(color, level)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	LightInfo light;
	light.color = getInteger<uint8_t>(L, 2);
	light.level = getInteger<uint8_t>(L, 3);
	creature->setCreatureLight(light);
	g_game.changeLight(creature);
	pushBoolean(L, true);
	return 1;
}

int luaCreatureGetSpeed(lua_State* L)
{
	// creature:getSpeed()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		lua_pushinteger(L, creature->getSpeed());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetSpeed(lua_State* L)
{
	// creature:setSpeed(speed)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (!creature) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	int32_t speed = getInteger<int32_t>(L, 2);
	g_game.setCreatureSpeed(creature, speed);
	pushBoolean(L, true);
	return 1;
}

int luaCreatureGetBaseSpeed(lua_State* L)
{
	// creature:getBaseSpeed()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		lua_pushinteger(L, creature->getBaseSpeed());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureChangeSpeed(lua_State* L)
{
	// creature:changeSpeed(delta)
	Creature* creature = getCreature(L, 1);
	if (!creature) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	int32_t delta = getInteger<int32_t>(L, 2);
	g_game.changeSpeed(creature, delta);
	pushBoolean(L, true);
	return 1;
}

int luaCreatureSetDropLoot(lua_State* L)
{
	// creature:setDropLoot(doDrop)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		creature->setDropLoot(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetSkillLoss(lua_State* L)
{
	// creature:setSkillLoss(skillLoss)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		creature->setSkillLoss(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetPosition(lua_State* L)
{
	// creature:getPosition()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		pushPosition(L, creature->getPosition());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetTile(lua_State* L)
{
	// creature:getTile()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	const Tile* tile = creature->getTile();
	if (tile) {
		pushUserdata<const Tile>(L, tile);
		setMetatable(L, -1, "Tile");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetDirection(lua_State* L)
{
	// creature:getDirection()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		lua_pushinteger(L, creature->getDirection());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetDirection(lua_State* L)
{
	// creature:setDirection(direction)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		pushBoolean(L, g_game.internalCreatureTurn(creature, getInteger<Direction>(L, 2)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetHealth(lua_State* L)
{
	// creature:getHealth()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		lua_pushinteger(L, creature->getHealth());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetHealth(lua_State* L)
{
	// creature:setHealth(health[, actor = nil])
	Creature* creature = getUserdata<Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	auto health = getInteger<int32_t>(L, 2);
	if (health > 0) {
		creature->setHealth(health);
		g_game.addCreatureHealth(creature);
	} else {
		creature->drainHealth(getCreature(L, 3), creature->getHealth());
	}

	if (!creature->isDead()) {
		if (dynamic_cast<Player*>(creature) != nullptr) {
			static_cast<Player*>(creature)->sendStats();
		}
	}

	pushBoolean(L, true);
	return 1;
}

int luaCreatureAddHealth(lua_State* L)
{
	// creature:addHealth(healthChange)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	CombatDamage damage;
	damage.primary.value = getInteger<int32_t>(L, 2);
	if (damage.primary.value >= 0) {
		damage.primary.type = COMBAT_HEALING;
	} else {
		damage.primary.type = COMBAT_UNDEFINEDDAMAGE;
	}
	pushBoolean(L, g_game.combatChangeHealth(nullptr, creature, damage));
	return 1;
}

int luaCreatureGetMaxHealth(lua_State* L)
{
	// creature:getMaxHealth()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		lua_pushinteger(L, creature->getMaxHealth());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetMaxHealth(lua_State* L)
{
	// creature:setMaxHealth(maxHealth)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	creature->setMaxHealth(getInteger<uint32_t>(L, 2));
	creature->setHealth(creature->getHealth());
	g_game.addCreatureHealth(creature);

	Player* player = creature->getPlayer();
	if (player) {
		player->sendStats();
	}
	pushBoolean(L, true);
	return 1;
}

int luaCreatureSetHiddenHealth(lua_State* L)
{
	// creature:setHiddenHealth(hide)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		creature->setHiddenHealth(getBoolean(L, 2));
		g_game.addCreatureHealth(creature);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetMovementBlocked(lua_State* L)
{
	// creature:setMovementBlocked(state)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		creature->setMovementBlocked(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetSkull(lua_State* L)
{
	// creature:getSkull()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		lua_pushinteger(L, creature->getSkull());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetSkull(lua_State* L)
{
	// creature:setSkull(skull)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		creature->setSkull(getNumber<Skulls_t>(L, 2));
		g_game.updateCreatureSkull(creature);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetEmblem(lua_State* L)
{
	// creature:getEmblem()
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		lua_pushnumber(L, creature->getEmblem());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetEmblem(lua_State* L)
{
	// creature:setEmblem(emblem)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		creature->setGuildEmblem(getNumber<GuildEmblems_t>(L, 2));
		// g_game.updatePlayerShield(creature->getPlayer()); // already handled by creature->setGuildEmblem ->
		// game->updateCreatureEmblem
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetOutfit(lua_State* L)
{
	// creature:getOutfit()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		pushOutfit(L, creature->getCurrentOutfit());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetOutfit(lua_State* L)
{
	// creature:setOutfit(outfit)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (creature) {
		creature->setDefaultOutfit(getOutfit(L, 2));
		g_game.internalCreatureChangeOutfit(creature, creature->getDefaultOutfit());
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetCondition(lua_State* L)
{
	// creature:getCondition(conditionType[, conditionId = CONDITIONID_COMBAT[, subId = 0]])
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	ConditionType_t conditionType = getInteger<ConditionType_t>(L, 2);
	ConditionId_t conditionId = getInteger<ConditionId_t>(L, 3, CONDITIONID_COMBAT);
	uint32_t subId = getInteger<uint32_t>(L, 4, 0);

	Condition* condition = creature->getCondition(conditionType, conditionId, subId);
	if (condition) {
		pushUserdata<Condition>(L, condition);
		setWeakMetatable(L, -1, "Condition");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureAddCondition(lua_State* L)
{
	// creature:addCondition(condition[, force = false])
	Creature* creature = getUserdata<Creature>(L, 1);
	Condition* condition = getUserdata<Condition>(L, 2);
	if (creature && condition) {
		bool force = getBoolean(L, 3, false);
		pushBoolean(L, creature->addCondition(condition->clone(), force));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureRemoveCondition(lua_State* L)
{
	// creature:removeCondition(conditionType[, conditionId = CONDITIONID_COMBAT[, subId = 0[, force = false]]])
	// creature:removeCondition(condition[, force = false])
	Creature* const creature = getUserdata<Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	Condition* creatureCondition = nullptr;

	bool force = false;

	if (isUserdata(L, 2)) {
		const Condition* const condition = getUserdata<Condition>(L, 2);
		const ConditionType_t conditionType = condition->getType();
		const ConditionId_t conditionId = condition->getId();
		const uint32_t subId = condition->getSubId();
		creatureCondition = creature->getCondition(conditionType, conditionId, subId);
		force = getBoolean(L, 3, false);
	} else {
		const ConditionType_t conditionType = getNumber<ConditionType_t>(L, 2);
		const ConditionId_t conditionId = getNumber<ConditionId_t>(L, 3, CONDITIONID_COMBAT);
		const uint32_t subId = getNumber<uint32_t>(L, 4, 0);
		creatureCondition = creature->getCondition(conditionType, conditionId, subId);
		force = getBoolean(L, 5, false);
	}

	if (creatureCondition) {
		creature->removeCondition(creatureCondition, force);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureHasCondition(lua_State* L)
{
	// creature:hasCondition(conditionType[, subId = 0])
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	ConditionType_t conditionType = getInteger<ConditionType_t>(L, 2);
	uint32_t subId = getInteger<uint32_t>(L, 3, 0);
	pushBoolean(L, creature->hasCondition(conditionType, subId));
	return 1;
}

int luaCreatureIsImmune(lua_State* L)
{
	// creature:isImmune(condition or conditionType)
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	if (isInteger(L, 2)) {
		pushBoolean(L, creature->isImmune(getInteger<ConditionType_t>(L, 2)));
	} else if (Condition* condition = getUserdata<Condition>(L, 2)) {
		pushBoolean(L, creature->isImmune(condition->getType()));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureRemove(lua_State* L)
{
	// creature:remove()
	Creature** creaturePtr = getRawUserdata<Creature>(L, 1);
	if (!creaturePtr) {
		lua_pushnil(L);
		return 1;
	}

	Creature* creature = *creaturePtr;
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	if (dynamic_cast<Player*>(creature) != nullptr) {
		static_cast<Player*>(creature)->kickPlayer(true);
	} else {
		g_game.removeCreature(creature);
	}

	*creaturePtr = nullptr;
	pushBoolean(L, true);
	return 1;
}

int luaCreatureTeleportTo(lua_State* L)
{
	// creature:teleportTo(position[, pushMovement = false])
	bool pushMovement = getBoolean(L, 3, false);

	const Position& position = getPosition(L, 2);
	Creature* creature = getUserdata<Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	const Position oldPosition = creature->getPosition();
	if (g_game.internalTeleport(creature, position, pushMovement) != RETURNVALUE_NOERROR) {
		pushBoolean(L, false);
		return 1;
	}

	if (pushMovement) {
		if (oldPosition.x == position.x) {
			if (oldPosition.y < position.y) {
				g_game.internalCreatureTurn(creature, DIRECTION_SOUTH);
			} else {
				g_game.internalCreatureTurn(creature, DIRECTION_NORTH);
			}
		} else if (oldPosition.x > position.x) {
			g_game.internalCreatureTurn(creature, DIRECTION_WEST);
		} else if (oldPosition.x < position.x) {
			g_game.internalCreatureTurn(creature, DIRECTION_EAST);
		}
	}
	pushBoolean(L, true);
	return 1;
}

int luaCreatureSay(lua_State* L)
{
	// creature:say(text[, type = TALKTYPE_MONSTER_SAY[, ghost = false[, targets = {}[, position]]]])
	int parameters = lua_gettop(L);

	Position position;
	if (parameters >= 6) {
		position = getPosition(L, 6);
		if (!position.x || !position.y) {
			reportErrorFunc(L, "Invalid position specified.");
			pushBoolean(L, false);
			return 1;
		}
	}

	bool ghost = getBoolean(L, 4, false);

	SpeakClasses type = getInteger<SpeakClasses>(L, 3, TALKTYPE_MONSTER_SAY);
	const std::string& text = getString(L, 2);
	Creature* creature = getUserdata<Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	SpectatorVec spectators;
	if (parameters >= 5) {
		getSpectators<Creature>(L, 5, spectators);
	}

	// Prevent infinity echo on event onHear
	bool echo =
	    LuaScriptInterface::getScriptEnv()->getScriptId() == g_events->getScriptId(EventInfoId::CREATURE_ONHEAR);

	if (position.x != 0) {
		pushBoolean(L, g_game.internalCreatureSay(creature, type, text, ghost, &spectators, &position, echo));
	} else {
		pushBoolean(L, g_game.internalCreatureSay(creature, type, text, ghost, &spectators, nullptr, echo));
	}
	return 1;
}

int luaCreatureGetDamageMap(lua_State* L)
{
	// creature:getDamageMap()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	const auto& damageMap = creature->getDamageMap();
	lua_createtable(L, damageMap.size(), 0);
	for (const auto& damageEntry : damageMap) {
		lua_createtable(L, 0, 2);
		setField(L, "total", damageEntry.second.total);
		setField(L, "ticks", damageEntry.second.ticks);
		lua_rawseti(L, -2, damageEntry.first);
	}
	return 1;
}

int luaCreatureGetSummons(lua_State* L)
{
	// creature:getSummons()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, creature->getSummonCount(), 0);

	int index = 0;
	for (Creature* summon : creature->getSummons()) {
		pushUserdata<Creature>(L, summon);
		setCreatureMetatable(L, -1, summon);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaCreatureGetDescription(lua_State* L)
{
	// creature:getDescription(distance)
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		int32_t distance = getInteger<int32_t>(L, 2);
		pushString(L, creature->getDescription(distance));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetPathTo(lua_State* L)
{
	// creature:getPathTo(pos[, minTargetDist = 0[, maxTargetDist = 1[, fullPathSearch = true[, clearSight = true[,
	// maxSearchDist = 0]]]]])
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	const Position& position = getPosition(L, 2);

	FindPathParams fpp;
	fpp.minTargetDist = getInteger<int32_t>(L, 3, 0);
	fpp.maxTargetDist = getInteger<int32_t>(L, 4, 1);
	fpp.fullPathSearch = getBoolean(L, 5, fpp.fullPathSearch);
	fpp.clearSight = getBoolean(L, 6, fpp.clearSight);
	fpp.maxSearchDist = getInteger<int32_t>(L, 7, fpp.maxSearchDist);

	std::vector<Direction> dirList;
	if (creature->getPathTo(position, dirList, fpp)) {
		int len = dirList.size();
		lua_createtable(L, len, 0);

		for (auto dir : dirList) {
			lua_pushinteger(L, dir);
			lua_rawseti(L, -2, len--);
		}
	} else {
		pushBoolean(L, false);
	}
	return 1;
}

int luaCreatureMove(lua_State* L)
{
	// creature:move(direction)
	// creature:move(tile[, flags = 0])
	Creature* creature = getUserdata<Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	if (isInteger(L, 2)) {
		Direction direction = getInteger<Direction>(L, 2);
		if (direction > DIRECTION_LAST) {
			lua_pushnil(L);
			return 1;
		}
		lua_pushinteger(L, g_game.internalMoveCreature(creature, direction, FLAG_NOLIMIT));
	} else {
		Tile* tile = getUserdata<Tile>(L, 2);
		if (!tile) {
			lua_pushnil(L);
			return 1;
		}
		lua_pushinteger(L, g_game.internalMoveCreature(*creature, *tile, getInteger<uint32_t>(L, 3)));
	}
	return 1;
}

int luaCreatureGetZone(lua_State* L)
{
	// creature:getZone()
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		lua_pushinteger(L, creature->getZone());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureGetStorageValue(lua_State* L)
{
	// creature:getStorageValue(key[, defaultValue = 0])
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	uint32_t key = getInteger<uint32_t>(L, 2);
	const auto& value = creature->getStorageValue(key);
	if (value) {
		lua_pushinteger(L, value.value());
	} else if (isInteger(L, 3)) {
		lua_pushinteger(L, getInteger<int64_t>(L, 3));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaCreatureSetStorageValue(lua_State* L)
{
	// creature:setStorageValue(key, value)
	Creature* creature = getUserdata<Creature>(L, 1);
	if (!creature) {
		lua_pushnil(L);
		return 1;
	}

	if (!isInteger(L, 2)) {
		reportErrorFunc(L, "Invalid storage key.");
		lua_pushnil(L);
		return 1;
	}

	uint32_t key = getInteger<uint32_t>(L, 2);
	if (isInteger(L, 3)) {
		int64_t value = getInteger<int64_t>(L, 3);
		creature->setStorageValue(key, value);
	} else {
		creature->setStorageValue(key, std::nullopt);
	}

	pushBoolean(L, true);
	return 1;
}

int luaCreatureSendCreatureSquare(lua_State* L)
{
	// creature:sendCreatureSquare(color[, players = {}])
	const Creature* creature = getUserdata<const Creature>(L, 1);
	if (creature) {
		SpectatorVec spectators;
		if (lua_gettop(L) >= 3) {
			getSpectators<Player>(L, 3, spectators);
		} else {
			g_game.map.getSpectators(spectators, creature->getPosition(), false, true);
		}

		for (Creature* spectator : spectators) {
			assert(dynamic_cast<Player*>(spectator) != nullptr);
			static_cast<Player*>(spectator)->sendCreatureSquare(creature, getInteger<SquareColor_t>(L, 2));
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}
} // namespace

void LuaScriptInterface::registerCreature()
{
	// Creature
	registerClass("Creature", "", luaCreatureCreate);
	registerMetaMethod("Creature", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("Creature", "getEvents", luaCreatureGetEvents);
	registerMethod("Creature", "registerEvent", luaCreatureRegisterEvent);
	registerMethod("Creature", "unregisterEvent", luaCreatureUnregisterEvent);

	registerMethod("Creature", "isRemoved", luaCreatureIsRemoved);
	registerMethod("Creature", "isCreature", luaCreatureIsCreature);
	registerMethod("Creature", "isInGhostMode", luaCreatureIsInGhostMode);
	registerMethod("Creature", "isHealthHidden", luaCreatureIsHealthHidden);
	registerMethod("Creature", "isMovementBlocked", luaCreatureIsMovementBlocked);
	registerMethod("Creature", "isImmune", luaCreatureIsImmune);

	registerMethod("Creature", "canSee", luaCreatureCanSee);
	registerMethod("Creature", "canSeeCreature", luaCreatureCanSeeCreature);
	registerMethod("Creature", "canSeeGhostMode", luaCreatureCanSeeGhostMode);
	registerMethod("Creature", "canSeeInvisibility", luaCreatureCanSeeInvisibility);

	registerMethod("Creature", "getParent", luaCreatureGetParent);

	registerMethod("Creature", "getId", luaCreatureGetId);
	registerMethod("Creature", "getName", luaCreatureGetName);

	registerMethod("Creature", "getTarget", luaCreatureGetTarget);
	registerMethod("Creature", "setTarget", luaCreatureSetTarget);

	registerMethod("Creature", "getFollowCreature", luaCreatureGetFollowCreature);
	registerMethod("Creature", "setFollowCreature", luaCreatureSetFollowCreature);

	registerMethod("Creature", "getMaster", luaCreatureGetMaster);
	registerMethod("Creature", "setMaster", luaCreatureSetMaster);

	registerMethod("Creature", "getLight", luaCreatureGetLight);
	registerMethod("Creature", "setLight", luaCreatureSetLight);

	registerMethod("Creature", "getSpeed", luaCreatureGetSpeed);
	registerMethod("Creature", "setSpeed", luaCreatureSetSpeed);
	registerMethod("Creature", "getBaseSpeed", luaCreatureGetBaseSpeed);
	registerMethod("Creature", "changeSpeed", luaCreatureChangeSpeed);

	registerMethod("Creature", "setDropLoot", luaCreatureSetDropLoot);
	registerMethod("Creature", "setSkillLoss", luaCreatureSetSkillLoss);

	registerMethod("Creature", "getPosition", luaCreatureGetPosition);
	registerMethod("Creature", "getTile", luaCreatureGetTile);
	registerMethod("Creature", "getDirection", luaCreatureGetDirection);
	registerMethod("Creature", "setDirection", luaCreatureSetDirection);

	registerMethod("Creature", "getHealth", luaCreatureGetHealth);
	registerMethod("Creature", "setHealth", luaCreatureSetHealth);
	registerMethod("Creature", "addHealth", luaCreatureAddHealth);
	registerMethod("Creature", "getMaxHealth", luaCreatureGetMaxHealth);
	registerMethod("Creature", "setMaxHealth", luaCreatureSetMaxHealth);
	registerMethod("Creature", "setHiddenHealth", luaCreatureSetHiddenHealth);
	registerMethod("Creature", "setMovementBlocked", luaCreatureSetMovementBlocked);

	registerMethod("Creature", "getSkull", luaCreatureGetSkull);
	registerMethod("Creature", "setSkull", luaCreatureSetSkull);
	registerMethod("Creature", "getEmblem", luaCreatureGetEmblem);
	registerMethod("Creature", "setEmblem", luaCreatureSetEmblem);

	registerMethod("Creature", "getOutfit", luaCreatureGetOutfit);
	registerMethod("Creature", "setOutfit", luaCreatureSetOutfit);

	registerMethod("Creature", "getCondition", luaCreatureGetCondition);
	registerMethod("Creature", "addCondition", luaCreatureAddCondition);
	registerMethod("Creature", "removeCondition", luaCreatureRemoveCondition);
	registerMethod("Creature", "hasCondition", luaCreatureHasCondition);

	registerMethod("Creature", "remove", luaCreatureRemove);
	registerMethod("Creature", "teleportTo", luaCreatureTeleportTo);
	registerMethod("Creature", "say", luaCreatureSay);

	registerMethod("Creature", "getDamageMap", luaCreatureGetDamageMap);

	registerMethod("Creature", "getSummons", luaCreatureGetSummons);

	registerMethod("Creature", "getDescription", luaCreatureGetDescription);

	registerMethod("Creature", "getPathTo", luaCreatureGetPathTo);
	registerMethod("Creature", "move", luaCreatureMove);

	registerMethod("Creature", "getZone", luaCreatureGetZone);

	registerMethod("Creature", "getStorageValue", luaCreatureGetStorageValue);
	registerMethod("Creature", "setStorageValue", luaCreatureSetStorageValue);

	registerMethod("Creature", "sendCreatureSquare", luaCreatureSendCreatureSquare);
}
