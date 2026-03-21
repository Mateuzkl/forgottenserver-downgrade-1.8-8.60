// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "events.h"

#include "item.h"
#include "logger.h"
#include "player.h"
#include "imbuement.h"

#include <fmt/format.h>

Events::Events() : scriptInterface("Event Interface") { scriptInterface.initState(); }

bool Events::load()
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file("data/events/events.xml");
	if (!result) {
		printXMLError("Error - Events::load", "data/events/events.xml", result);
		return false;
	}

	info = {};

	std::set<std::string> classes;
	for (auto& eventNode : doc.child("events").children()) {
		if (!eventNode.attribute("enabled").as_bool()) {
			continue;
		}

		const std::string& className = eventNode.attribute("class").as_string();
		auto res = classes.insert(className);
		if (res.second) {
			const std::string& lowercase = boost::algorithm::to_lower_copy<std::string>(className);
			if (scriptInterface.loadFile("data/events/scripts/" + lowercase + ".lua") != 0) {
				LOG_WARN(fmt::format("[Warning - Events::load] Can not load script: {}.lua", lowercase));
				LOG_WARN(scriptInterface.getLastLuaError());
			}
		}

		const std::string& methodName = eventNode.attribute("method").as_string();
		const int32_t event = scriptInterface.getMetaEvent(className, methodName);
		if (className == "Creature") {
			if (methodName == "onChangeOutfit") {
				info.creatureOnChangeOutfit = event;
			} else if (methodName == "onAreaCombat") {
				info.creatureOnAreaCombat = event;
			} else if (methodName == "onTargetCombat") {
				info.creatureOnTargetCombat = event;
			} else if (methodName == "onHear") {
				info.creatureOnHear = event;
			} else if (methodName == "onChangeZone") {
				info.creatureOnChangeZone = event;
			} else if (methodName == "onUpdateStorage") {
				info.creatureOnUpdateStorage = event;
			} else {
				LOG_WARN(fmt::format("[Warning - Events::load] Unknown creature method: {}", methodName));
			}
		} else if (className == "Party") {
			if (methodName == "onJoin") {
				info.partyOnJoin = event;
			} else if (methodName == "onLeave") {
				info.partyOnLeave = event;
			} else if (methodName == "onDisband") {
				info.partyOnDisband = event;
			} else if (methodName == "onShareExperience") {
				info.partyOnShareExperience = event;
			} else if (methodName == "onInvite") {
				info.partyOnInvite = event;
			} else if (methodName == "onRevokeInvitation") {
				info.partyOnRevokeInvitation = event;
			} else if (methodName == "onPassLeadership") {
				info.partyOnPassLeadership = event;
			} else {
				LOG_WARN(fmt::format("[Warning - Events::load] Unknown party method: {}", methodName));
			}
		} else if (className == "Player") {
			if (methodName == "onLook") {
				info.playerOnLook = event;
			} else if (methodName == "onLookInBattleList") {
				info.playerOnLookInBattleList = event;
			} else if (methodName == "onLookInTrade") {
				info.playerOnLookInTrade = event;
			} else if (methodName == "onLookInShop") {
				info.playerOnLookInShop = event;
			} else if (methodName == "onTradeRequest") {
				info.playerOnTradeRequest = event;
			} else if (methodName == "onTradeAccept") {
				info.playerOnTradeAccept = event;
			} else if (methodName == "onTradeCompleted") {
				info.playerOnTradeCompleted = event;
			} else if (methodName == "onMoveItem") {
				info.playerOnMoveItem = event;
			} else if (methodName == "onItemMoved") {
				info.playerOnItemMoved = event;
			} else if (methodName == "onMoveCreature") {
				info.playerOnMoveCreature = event;
			} else if (methodName == "onStepTile") {
				info.playerOnStepTile = event;
			} else if (methodName == "onReportRuleViolation") {
				info.playerOnReportRuleViolation = event;
			} else if (methodName == "onReportBug") {
				info.playerOnReportBug = event;
			} else if (methodName == "onTurn") {
				info.playerOnTurn = event;
			} else if (methodName == "onGainExperience") {
				info.playerOnGainExperience = event;
			} else if (methodName == "onLoseExperience") {
				info.playerOnLoseExperience = event;
			} else if (methodName == "onGainSkillTries") {
				info.playerOnGainSkillTries = event;
			} else if (methodName == "onNetworkMessage") {
				info.playerOnNetworkMessage = event;
			} else if (methodName == "onUpdateInventory") {
				info.playerOnUpdateInventory = event;
			} else if (methodName == "onRotateItem") {
				info.playerOnRotateItem = event;
			} else if (methodName == "onSpellCheck") {
				info.playerOnSpellCheck = event;
			} else {
				LOG_WARN(fmt::format("[Warning - Events::load] Unknown player method: {}", methodName));
			}
		} else if (className == "Monster") {
			if (methodName == "onDropLoot") {
				info.monsterOnDropLoot = event;
			} else if (methodName == "onSpawn") {
				info.monsterOnSpawn = event;
			} else {
				LOG_WARN(fmt::format("[Warning - Events::load] Unknown monster method: {}", methodName));
			}
		} else if (className == "Item") {
			if (methodName == "onImbue") {
				info.itemOnImbue = event;
			} else if (methodName == "onRemoveImbue") {
				info.itemOnRemoveImbue = event;
			} else {
				LOG_WARN(fmt::format("[Warning - Events::load] Unknown item method: {}", methodName));
			}
		} else {
			LOG_WARN(fmt::format("[Warning - Events::load] Unknown class: {}", className));
		}
	}
	return true;
}

// Monster
bool Events::eventMonsterOnSpawn(Monster* monster, const Position& position, bool startup, bool artificial)
{
	// Monster:onSpawn(position, startup, artificial)
	if (info.monsterOnSpawn == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::monsterOnSpawn] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.monsterOnSpawn, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.monsterOnSpawn);

	Lua::pushUserdata<Monster>(L, monster);
	Lua::setMetatable(L, -1, "Monster");
	Lua::pushPosition(L, position);
	Lua::pushBoolean(L, startup);
	Lua::pushBoolean(L, artificial);

	return scriptInterface.callFunction(4);
}

// Creature
bool Events::eventCreatureOnChangeOutfit(Creature* creature, const Outfit_t& outfit)
{
	// Creature:onChangeOutfit(outfit) or Creature.onChangeOutfit(self, outfit)
	if (info.creatureOnChangeOutfit == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventCreatureOnChangeOutfit] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.creatureOnChangeOutfit, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.creatureOnChangeOutfit);

	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);

	Lua::pushOutfit(L, outfit);

	return scriptInterface.callFunction(2);
}

ReturnValue Events::eventCreatureOnAreaCombat(Creature* creature, Tile* tile, bool aggressive)
{
	// Creature:onAreaCombat(tile, aggressive) or Creature.onAreaCombat(self, tile, aggressive)
	if (info.creatureOnAreaCombat == -1) {
		return RETURNVALUE_NOERROR;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventCreatureOnAreaCombat] Call stack overflow");
		return RETURNVALUE_NOTPOSSIBLE;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.creatureOnAreaCombat, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.creatureOnAreaCombat);

	if (creature) {
		Lua::pushUserdata<Creature>(L, creature);
		Lua::setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}

	Lua::pushUserdata<Tile>(L, tile);
	Lua::setMetatable(L, -1, "Tile");

	Lua::pushBoolean(L, aggressive);

	ReturnValue returnValue;
	if (scriptInterface.protectedCall(L, 3, 1) != 0) {
		returnValue = RETURNVALUE_NOTPOSSIBLE;
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		returnValue = Lua::getInteger<ReturnValue>(L, -1);
		lua_pop(L, 1);
	}

	scriptInterface.resetScriptEnv();
	return returnValue;
}

ReturnValue Events::eventCreatureOnTargetCombat(Creature* creature, Creature* target)
{
	// Creature:onTargetCombat(target) or Creature.onTargetCombat(self, target)
	if (info.creatureOnTargetCombat == -1) {
		return RETURNVALUE_NOERROR;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventCreatureOnTargetCombat] Call stack overflow");
		return RETURNVALUE_NOTPOSSIBLE;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.creatureOnTargetCombat, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.creatureOnTargetCombat);

	if (creature) {
		Lua::pushUserdata<Creature>(L, creature);
		Lua::setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}

	Lua::pushUserdata<Creature>(L, target);
	Lua::setCreatureMetatable(L, -1, target);

	ReturnValue returnValue;
	if (scriptInterface.protectedCall(L, 2, 1) != 0) {
		returnValue = RETURNVALUE_NOTPOSSIBLE;
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		returnValue = Lua::getInteger<ReturnValue>(L, -1);
		lua_pop(L, 1);
	}

	scriptInterface.resetScriptEnv();
	return returnValue;
}

void Events::eventCreatureOnHear(Creature* creature, Creature* speaker, std::string_view words, SpeakClasses type)
{
	// Creature:onHear(speaker, words, type)
	if (info.creatureOnHear == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventCreatureOnHear] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.creatureOnHear, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.creatureOnHear);

	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);

	Lua::pushUserdata<Creature>(L, speaker);
	Lua::setCreatureMetatable(L, -1, speaker);

	Lua::pushString(L, words);
	lua_pushinteger(L, type);

	scriptInterface.callVoidFunction(4);
}

void Events::eventCreatureOnChangeZone(Creature* creature, ZoneType_t fromZone, ZoneType_t toZone)
{
	// Creature:onChangeZone(fromZone, toZone)
	if (info.creatureOnChangeZone == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventCreatureOnChangeZone] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.creatureOnChangeZone, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.creatureOnChangeZone);

	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);

	lua_pushinteger(L, fromZone);
	lua_pushinteger(L, toZone);

	scriptInterface.callVoidFunction(3);
}

void Events::eventCreatureOnUpdateStorage(Creature* creature, const uint32_t key, const std::optional<int32_t> value,
                                          const std::optional<int32_t> oldValue, bool isSpawn)
{
	// Creature:onUpdateStorage(key, value, oldValue, isSpawn)
	if (info.creatureOnUpdateStorage == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventCreatureOnUpdateStorage] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.creatureOnUpdateStorage, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.creatureOnUpdateStorage);

	Lua::pushUserdata<Creature>(L, creature);
	Lua::setMetatable(L, -1, "Creature");

	lua_pushinteger(L, key);
	if (value) {
		lua_pushinteger(L, value.value());
	} else {
		lua_pushnil(L);
	}

	if (oldValue) {
		lua_pushinteger(L, oldValue.value());
	} else {
		lua_pushnil(L);
	}

	Lua::pushBoolean(L, isSpawn);

	scriptInterface.callVoidFunction(5);
}

// Party
bool Events::eventPartyOnJoin(Party* party, Player* player)
{
	// Party:onJoin(player) or Party.onJoin(self, player)
	if (info.partyOnJoin == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPartyOnJoin] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.partyOnJoin, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.partyOnJoin);

	Lua::pushUserdata<Party>(L, party);
	Lua::setMetatable(L, -1, "Party");

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	return scriptInterface.callFunction(2);
}

bool Events::eventPartyOnLeave(Party* party, Player* player)
{
	// Party:onLeave(player) or Party.onLeave(self, player)
	if (info.partyOnLeave == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPartyOnLeave] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.partyOnLeave, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.partyOnLeave);

	Lua::pushUserdata<Party>(L, party);
	Lua::setMetatable(L, -1, "Party");

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	return scriptInterface.callFunction(2);
}

bool Events::eventPartyOnDisband(Party* party)
{
	// Party:onDisband() or Party.onDisband(self)
	if (info.partyOnDisband == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPartyOnDisband] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.partyOnDisband, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.partyOnDisband);

	Lua::pushUserdata<Party>(L, party);
	Lua::setMetatable(L, -1, "Party");

	return scriptInterface.callFunction(1);
}

void Events::eventPartyOnShareExperience(Party* party, uint64_t& exp)
{
	// Party:onShareExperience(exp) or Party.onShareExperience(self, exp)
	if (info.partyOnShareExperience == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPartyOnShareExperience] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.partyOnShareExperience, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.partyOnShareExperience);

	Lua::pushUserdata<Party>(L, party);
	Lua::setMetatable(L, -1, "Party");

	lua_pushinteger(L, exp);

	if (scriptInterface.protectedCall(L, 2, 1) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		exp = Lua::getInteger<uint64_t>(L, -1);
		lua_pop(L, 1);
	}

	scriptInterface.resetScriptEnv();
}

bool Events::eventPartyOnInvite(Party* party, Player* player)
{
	// Party:onInvite(player) or Party.onInvite(self, player)
	if (info.partyOnInvite == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPartyOnInvite] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.partyOnInvite, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.partyOnInvite);

	Lua::pushUserdata<Party>(L, party);
	Lua::setMetatable(L, -1, "Party");

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	return scriptInterface.callFunction(2);
}

bool Events::eventPartyOnRevokeInvitation(Party* party, Player* player)
{
	// Party:onRevokeInvitation(player) or Party.onRevokeInvitation(self, player)
	if (info.partyOnRevokeInvitation == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPartyOnRevokeInvitation] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.partyOnRevokeInvitation, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.partyOnRevokeInvitation);

	Lua::pushUserdata<Party>(L, party);
	Lua::setMetatable(L, -1, "Party");

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	return scriptInterface.callFunction(2);
}

bool Events::eventPartyOnPassLeadership(Party* party, Player* player)
{
	// Party:onPassLeadership(player) or Party.onPassLeadership(self, player)
	if (info.partyOnPassLeadership == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPartyOnPassLeadership] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.partyOnPassLeadership, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.partyOnPassLeadership);

	Lua::pushUserdata<Party>(L, party);
	Lua::setMetatable(L, -1, "Party");

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	return scriptInterface.callFunction(2);
}

// Player
void Events::eventPlayerOnLook(Player* player, const Position& position, Thing* thing, uint8_t stackpos,
                               int32_t lookDistance)
{
	// Player:onLook(thing, position, distance) or Player.onLook(self, thing, position, distance)
	if (info.playerOnLook == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnLook] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnLook, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnLook);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	if (Creature* creature = thing->getCreature()) {
		Lua::pushUserdata<Creature>(L, creature);
		Lua::setCreatureMetatable(L, -1, creature);
	} else if (Item* item = thing->getItem()) {
		Lua::pushUserdata<Item>(L, item);
		Lua::setItemMetatable(L, -1, item);
	} else {
		lua_pushnil(L);
	}

	Lua::pushPosition(L, position, stackpos);
	lua_pushinteger(L, lookDistance);

	scriptInterface.callVoidFunction(4);
}

void Events::eventPlayerOnLookInBattleList(Player* player, Creature* creature, int32_t lookDistance)
{
	// Player:onLookInBattleList(creature, distance) or Player.onLookInBattleList(self, creature, distance)
	if (info.playerOnLookInBattleList == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnLookInBattleList] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnLookInBattleList, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnLookInBattleList);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);

	lua_pushinteger(L, lookDistance);

	scriptInterface.callVoidFunction(3);
}

void Events::eventPlayerOnLookInTrade(Player* player, Player* partner, Item* item, int32_t lookDistance)
{
	// Player:onLookInTrade(partner, item, distance) or Player.onLookInTrade(self, partner, item, distance)
	if (info.playerOnLookInTrade == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnLookInTrade] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnLookInTrade, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnLookInTrade);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Player>(L, partner);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Item>(L, item);
	Lua::setItemMetatable(L, -1, item);

	lua_pushinteger(L, lookDistance);

	scriptInterface.callVoidFunction(4);
}

bool Events::eventPlayerOnLookInShop(Player* player, const ItemType* itemType, uint8_t count)
{
	// Player:onLookInShop(itemType, count) or Player.onLookInShop(self, itemType, count)
	if (info.playerOnLookInShop == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnLookInShop] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnLookInShop, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnLookInShop);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<const ItemType>(L, itemType);
	Lua::setMetatable(L, -1, "ItemType");

	lua_pushinteger(L, count);

	return scriptInterface.callFunction(3);
}

ReturnValue Events::eventPlayerOnMoveItem(Player* player, Item* item, uint16_t count, const Position& fromPosition,
                                          const Position& toPosition, Cylinder* fromCylinder, Cylinder* toCylinder)
{
	// Player:onMoveItem(item, count, fromPosition, toPosition) or Player.onMoveItem(self, item, count, fromPosition,
	// toPosition, fromCylinder, toCylinder)
	if (info.playerOnMoveItem == -1) {
		return RETURNVALUE_NOERROR;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnMoveItem] Call stack overflow");
		return RETURNVALUE_NOTPOSSIBLE;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnMoveItem, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnMoveItem);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Item>(L, item);
	Lua::setItemMetatable(L, -1, item);

	lua_pushinteger(L, count);
	Lua::pushPosition(L, fromPosition);
	Lua::pushPosition(L, toPosition);

	Lua::pushCylinder(L, fromCylinder);
	Lua::pushCylinder(L, toCylinder);

	ReturnValue returnValue;
	if (scriptInterface.protectedCall(L, 7, 1) != 0) {
		returnValue = RETURNVALUE_NOTPOSSIBLE;
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		returnValue = Lua::getInteger<ReturnValue>(L, -1);
		lua_pop(L, 1);
	}

	scriptInterface.resetScriptEnv();
	return returnValue;
}

void Events::eventPlayerOnItemMoved(Player* player, Item* item, uint16_t count, const Position& fromPosition,
                                    const Position& toPosition, Cylinder* fromCylinder, Cylinder* toCylinder)
{
	// Player:onItemMoved(item, count, fromPosition, toPosition) or Player.onItemMoved(self, item, count, fromPosition,
	// toPosition, fromCylinder, toCylinder)
	if (info.playerOnItemMoved == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnItemMoved] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnItemMoved, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnItemMoved);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Item>(L, item);
	Lua::setItemMetatable(L, -1, item);

	lua_pushinteger(L, count);
	Lua::pushPosition(L, fromPosition);
	Lua::pushPosition(L, toPosition);

	Lua::pushCylinder(L, fromCylinder);
	Lua::pushCylinder(L, toCylinder);

	scriptInterface.callVoidFunction(7);
}

bool Events::eventPlayerOnMoveCreature(Player* player, Creature* creature, const Position& fromPosition,
                                       const Position& toPosition)
{
	// Player:onMoveCreature(creature, fromPosition, toPosition) or Player.onMoveCreature(self, creature, fromPosition,
	// toPosition)
	if (info.playerOnMoveCreature == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnMoveCreature] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnMoveCreature, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnMoveCreature);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);

	Lua::pushPosition(L, fromPosition);
	Lua::pushPosition(L, toPosition);

	return scriptInterface.callFunction(4);
}

bool Events::eventPlayerOnStepTile(Player* player, const Position& fromPosition, const Position& toPosition)
{
	// Player:onStepTile(fromPosition, toPosition) or Player.onStepTile(self, fromPosition, toPosition)
	if (info.playerOnStepTile == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnStepTile] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnStepTile, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnStepTile);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");
	Lua::pushPosition(L, fromPosition);
	Lua::pushPosition(L, toPosition);

	bool result = scriptInterface.callFunction(3);

	// Incremental GC to reduce memory pressure from frequent step events
	static uint32_t stepCounter = 0;
	if (++stepCounter >= 100) {
		stepCounter = 0;
		lua_gc(L, LUA_GCSTEP, 50);
	}

	return result;
}

void Events::eventPlayerOnReportRuleViolation(Player* player, std::string_view targetName, uint8_t reportType,
                                              uint8_t reportReason, std::string_view comment,
                                              std::string_view translation)
{
	// Player:onReportRuleViolation(targetName, reportType, reportReason, comment, translation)
	if (info.playerOnReportRuleViolation == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnReportRuleViolation] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnReportRuleViolation, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnReportRuleViolation);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushString(L, targetName);

	lua_pushinteger(L, reportType);
	lua_pushinteger(L, reportReason);

	Lua::pushString(L, comment);
	Lua::pushString(L, translation);

	scriptInterface.callVoidFunction(6);
}

bool Events::eventPlayerOnReportBug(Player* player, std::string_view message)
{
	// Player:onReportBug(message)
	if (info.playerOnReportBug == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnReportBug] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnReportBug, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnReportBug);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushString(L, message);

	return scriptInterface.callFunction(2);
}

bool Events::eventPlayerOnTurn(Player* player, Direction direction)
{
	// Player:onTurn(direction) or Player.onTurn(self, direction)
	if (info.playerOnTurn == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnTurn] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnTurn, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnTurn);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	lua_pushinteger(L, direction);

	return scriptInterface.callFunction(2);
}

bool Events::eventPlayerOnTradeRequest(Player* player, Player* target, Item* item)
{
	// Player:onTradeRequest(target, item)
	if (info.playerOnTradeRequest == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnTradeRequest] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnTradeRequest, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnTradeRequest);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Player>(L, target);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Item>(L, item);
	Lua::setItemMetatable(L, -1, item);

	return scriptInterface.callFunction(3);
}

bool Events::eventPlayerOnTradeAccept(Player* player, Player* target, Item* item, Item* targetItem)
{
	// Player:onTradeAccept(target, item, targetItem)
	if (info.playerOnTradeAccept == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnTradeAccept] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnTradeAccept, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnTradeAccept);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Player>(L, target);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Item>(L, item);
	Lua::setItemMetatable(L, -1, item);

	Lua::pushUserdata<Item>(L, targetItem);
	Lua::setItemMetatable(L, -1, targetItem);

	return scriptInterface.callFunction(4);
}

void Events::eventPlayerOnTradeCompleted(Player* player, Player* target, Item* item, Item* targetItem, bool isSuccess)
{
	// Player:onTradeCompleted(target, item, targetItem, isSuccess)
	if (info.playerOnTradeCompleted == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnTradeCompleted] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnTradeCompleted, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnTradeCompleted);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Player>(L, target);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Item>(L, item);
	Lua::setItemMetatable(L, -1, item);

	Lua::pushUserdata<Item>(L, targetItem);
	Lua::setItemMetatable(L, -1, targetItem);

	Lua::pushBoolean(L, isSuccess);

	return scriptInterface.callVoidFunction(5);
}

void Events::eventPlayerOnGainExperience(Player* player, Creature* source, uint64_t& exp, uint64_t rawExp,
                                         bool sendText)
{
	// Player:onGainExperience(source, exp, rawExp, sendText)
	// rawExp gives the original exp which is not multiplied
	if (info.playerOnGainExperience == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnGainExperience] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnGainExperience, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnGainExperience);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	if (source) {
		Lua::pushUserdata<Creature>(L, source);
		Lua::setCreatureMetatable(L, -1, source);
	} else {
		lua_pushnil(L);
	}

	lua_pushinteger(L, exp);
	lua_pushinteger(L, rawExp);
	Lua::pushBoolean(L, sendText);

	if (scriptInterface.protectedCall(L, 5, 1) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		exp = Lua::getInteger<uint64_t>(L, -1);
		lua_pop(L, 1);
	}

	scriptInterface.resetScriptEnv();
}

void Events::eventPlayerOnLoseExperience(Player* player, uint64_t& exp)
{
	// Player:onLoseExperience(exp)
	if (info.playerOnLoseExperience == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnLoseExperience] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnLoseExperience, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnLoseExperience);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	lua_pushinteger(L, exp);

	if (scriptInterface.protectedCall(L, 2, 1) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		exp = Lua::getInteger<uint64_t>(L, -1);
		lua_pop(L, 1);
	}

	scriptInterface.resetScriptEnv();
}

void Events::eventPlayerOnGainSkillTries(Player* player, skills_t skill, uint64_t& tries, bool artificial)
{
	// Player:onGainSkillTries(skill, tries, artificial)
	if (info.playerOnGainSkillTries == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnGainSkillTries] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnGainSkillTries, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnGainSkillTries);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	lua_pushinteger(L, skill);
	lua_pushinteger(L, tries);

	Lua::pushBoolean(L, artificial);

	if (scriptInterface.protectedCall(L, 4, 1) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		tries = Lua::getInteger<uint64_t>(L, -1);
		lua_pop(L, 1);
	}

	scriptInterface.resetScriptEnv();
}

void Events::eventPlayerOnNetworkMessage(Player* player, uint8_t recvByte, NetworkMessage_ptr& msg)
{
	// Player:onNetworkMessage(recvByte, msg)
	if (info.playerOnNetworkMessage == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnNetworkMessage] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnNetworkMessage, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnNetworkMessage);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	lua_pushinteger(L, recvByte);

	Lua::pushUserdata<NetworkMessage>(L, msg.release());
	Lua::setMetatable(L, -1, "NetworkMessage");

	scriptInterface.callVoidFunction(3);
}

void Events::eventPlayerOnUpdateInventory(Player* player, Item* item, const slots_t slot, const bool equip)
{
	// Player:onUpdateInventory(item, slot, equip)
	if (info.playerOnUpdateInventory == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnUpdateInventory] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnUpdateInventory, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnUpdateInventory);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Item>(L, item);
	Lua::setItemMetatable(L, -1, item);

	lua_pushinteger(L, slot);
	Lua::pushBoolean(L, equip);

	scriptInterface.callVoidFunction(4);
}

void Events::eventPlayerOnRotateItem(Player* player, Item* item)
{
	// Player:onRotateItem(item)
	if (info.playerOnRotateItem == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnRotateItem] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnRotateItem, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnRotateItem);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushUserdata<Item>(L, item);
	Lua::setItemMetatable(L, -1, item);

	scriptInterface.callVoidFunction(2);
}

bool Events::eventPlayerOnSpellCheck(Player* player, const Spell* spell)
{
	// Player:onSpellCheck(spell)
	if (info.playerOnSpellCheck == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventPlayerOnSpellCheck] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.playerOnSpellCheck, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.playerOnSpellCheck);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushSpell(L, *spell);

	return scriptInterface.callFunction(2);
}

void Events::eventMonsterOnDropLoot(Monster* monster, Container* corpse)
{
	// Monster:onDropLoot(corpse)
	if (info.monsterOnDropLoot == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		LOG_ERROR("[Error - Events::eventMonsterOnDropLoot] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.monsterOnDropLoot, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.monsterOnDropLoot);

	Lua::pushUserdata<Monster>(L, monster);
	Lua::setMetatable(L, -1, "Monster");

	Lua::pushUserdata<Container>(L, corpse);
	Lua::setMetatable(L, -1, "Container");

	return scriptInterface.callVoidFunction(2);
}

bool Events::eventItemOnImbue(Item* item, std::shared_ptr<Imbuement> imbuement, bool created)
{
	if (info.itemOnImbue == -1) {
		return true;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		std::cout << "[Error - Events::eventItemOnImbue] Call stack overflow" << std::endl;
		return false;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.itemOnImbue, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.itemOnImbue);

	Lua::pushUserdata<Item>(L, item);
	Lua::setItemMetatable(L, -1, item);

	Lua::pushSharedPtr(L, imbuement);
	Lua::setMetatable(L, -1, "Imbuement");

	Lua::pushBoolean(L, created);

	return scriptInterface.callFunction(3);
}

void Events::eventItemOnRemoveImbue(Item* item, ImbuementType imbueType, bool decayed)
{
	if (info.itemOnRemoveImbue == -1) {
		return;
	}

	if (!scriptInterface.reserveScriptEnv()) {
		std::cout << "[Error - Events::eventItemOnRemoveImbue] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(info.itemOnRemoveImbue, &scriptInterface);

	lua_State* L = scriptInterface.getLuaState();
	scriptInterface.pushFunction(info.itemOnRemoveImbue);

	Lua::pushUserdata<Item>(L, item);
	Lua::setItemMetatable(L, -1, item);

	lua_pushnumber(L, static_cast<uint8_t>(imbueType));
	Lua::pushBoolean(L, decayed);

	return scriptInterface.callVoidFunction(3);
}