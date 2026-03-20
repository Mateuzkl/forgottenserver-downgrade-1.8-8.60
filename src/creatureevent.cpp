// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "creatureevent.h"

#include "logger.h"
#include "player.h"
#include "tools.h"

CreatureEvents::CreatureEvents() : scriptInterface("CreatureScript Interface") { scriptInterface.initState(); }

void CreatureEvents::clear(bool fromLua)
{
	for (auto it = creatureEvents.begin(); it != creatureEvents.end(); ++it) {
		if (fromLua == it->second.fromLua) {
			it->second.clearEvent();
		}
	}

	reInitState(fromLua);
}

void CreatureEvents::removeInvalidEvents()
{
	for (auto it = creatureEvents.begin(); it != creatureEvents.end();) {
		if (it->second.getScriptId() == 0) {
			it = creatureEvents.erase(it);
		} else {
			++it;
		}
	}
}

LuaScriptInterface& CreatureEvents::getScriptInterface() { return scriptInterface; }

std::string_view CreatureEvents::getScriptBaseName() const { return "creaturescripts"; }

Event_ptr CreatureEvents::getEvent(std::string_view nodeName)
{
	if (!caseInsensitiveEqual(nodeName, "event")) {
		return nullptr;
	}
	return Event_ptr(new CreatureEvent(&scriptInterface));
}

bool CreatureEvents::registerEvent(Event_ptr event, const pugi::xml_node&)
{
	CreatureEvent_ptr creatureEvent{
	    static_cast<CreatureEvent*>(event.release())}; // event is guaranteed to be a CreatureEvent
	if (creatureEvent->getEventType() == CREATURE_EVENT_NONE) {
		LOG_ERROR("[Error - CreatureEvents::registerEvent] Trying to register event without type!");
		return false;
	}

	CreatureEvent* oldEvent = getEventByName(creatureEvent->getName(), false);
	if (oldEvent) {
		// if there was an event with the same that is not loaded
		//(happens when reloading), it is reused
		if (!oldEvent->isLoaded() && oldEvent->getEventType() == creatureEvent->getEventType()) {
			oldEvent->copyEvent(creatureEvent.get());
		}
		return false;
	}

	// if not, register it normally
	creatureEvents.emplace(boost::algorithm::to_lower_copy(creatureEvent->getName()), std::move(*creatureEvent));
	return true;
}

bool CreatureEvents::registerLuaEvent(CreatureEvent* event)
{
	CreatureEvent_ptr creatureEvent{event};
	if (creatureEvent->getEventType() == CREATURE_EVENT_NONE) {
		LOG_ERROR("[Error - CreatureEvents::registerLuaEvent] Trying to register event without type!");
		return false;
	}

	CreatureEvent* oldEvent = getEventByName(creatureEvent->getName(), false);
	if (oldEvent) {
		// if there was an event with the same that is not loaded
		//(happens when reloading), it is reused
		if (!oldEvent->isLoaded() && oldEvent->getEventType() == creatureEvent->getEventType()) {
			oldEvent->copyEvent(creatureEvent.get());
		}
		return false;
	}

	// if not, register it normally
	creatureEvents.emplace(boost::algorithm::to_lower_copy(creatureEvent->getName()), std::move(*creatureEvent));
	return true;
}

CreatureEvent* CreatureEvents::getEventByName(std::string_view name, bool forceLoaded /*= true*/)
{
	std::string key = boost::algorithm::to_lower_copy(std::string{name});

	auto it = creatureEvents.find(key);
	if (it != creatureEvents.end()) {
		if (!forceLoaded || it->second.isLoaded()) {
			return &it->second;
		}
	}
	return nullptr;
}

bool CreatureEvents::playerLogin(Player* player) const
{
	// fire global event if is registered
	for (const auto& it : creatureEvents) {
		if (it.second.getEventType() == CREATURE_EVENT_LOGIN) {
			if (!it.second.executeOnLogin(player)) {
				return false;
			}
		}
	}
	return true;
}

bool CreatureEvents::playerLogout(Player* player) const
{
	// fire global event if is registered
	for (const auto& it : creatureEvents) {
		if (it.second.getEventType() == CREATURE_EVENT_LOGOUT) {
			if (!it.second.executeOnLogout(player)) {
				return false;
			}
		}
	}
	return true;
}

void CreatureEvents::playerReconnect(Player* player) const
{
	// fire global event if is registered
	for (const auto& it : creatureEvents) {
		if (it.second.getEventType() == CREATURE_EVENT_RECONNECT) {
			it.second.executeOnReconnect(player);
		}
	}
}

bool CreatureEvents::playerAdvance(Player* player, skills_t skill, uint32_t oldLevel, uint32_t newLevel)
{
	for (auto& it : creatureEvents) {
		if (it.second.getEventType() == CREATURE_EVENT_ADVANCE) {
			if (!it.second.executeAdvance(player, skill, oldLevel, newLevel)) {
				return false;
			}
		}
	}
	return true;
}

/////////////////////////////////////

CreatureEvent::CreatureEvent(LuaScriptInterface* interface) : Event(interface), type(CREATURE_EVENT_NONE), loaded(false)
{}

bool CreatureEvent::configureEvent(const pugi::xml_node& node)
{
	// Name that will be used in monster xml files and
	// lua function to register events to reference this event
	pugi::xml_attribute nameAttribute = node.attribute("name");
	if (!nameAttribute) {
		LOG_ERROR("[Error - CreatureEvent::configureEvent] Missing name for creature event");
		return false;
	}

	eventName = nameAttribute.as_string();

	pugi::xml_attribute typeAttribute = node.attribute("type");
	if (!typeAttribute) {
		LOG_ERROR(
		    fmt::format("[Error - CreatureEvent::configureEvent] Missing type for creature event: {}", eventName));
		return false;
	}

	std::string tmpStr = boost::algorithm::to_lower_copy<std::string>(typeAttribute.as_string());
	if (tmpStr == "login") {
		type = CREATURE_EVENT_LOGIN;
	} else if (tmpStr == "logout") {
		type = CREATURE_EVENT_LOGOUT;
	} else if (tmpStr == "reconnect") {
		type = CREATURE_EVENT_RECONNECT;
	} else if (tmpStr == "think") {
		type = CREATURE_EVENT_THINK;
	} else if (tmpStr == "preparedeath") {
		type = CREATURE_EVENT_PREPAREDEATH;
	} else if (tmpStr == "death") {
		type = CREATURE_EVENT_DEATH;
	} else if (tmpStr == "kill") {
		type = CREATURE_EVENT_KILL;
	} else if (tmpStr == "advance") {
		type = CREATURE_EVENT_ADVANCE;
	} else if (tmpStr == "modalwindow") {
		type = CREATURE_EVENT_MODALWINDOW;
	} else if (tmpStr == "textedit") {
		type = CREATURE_EVENT_TEXTEDIT;
	} else if (tmpStr == "healthchange") {
		type = CREATURE_EVENT_HEALTHCHANGE;
	} else if (tmpStr == "manachange") {
		type = CREATURE_EVENT_MANACHANGE;
	} else if (tmpStr == "extendedopcode") {
		type = CREATURE_EVENT_EXTENDED_OPCODE;
	} else {
		LOG_ERROR(
		    fmt::format("[Error - CreatureEvent::configureEvent] Invalid type for creature event: {}", eventName));
		return false;
	}

	loaded = true;
	return true;
}

std::string_view CreatureEvent::getScriptEventName() const
{
	// Depending on the type script event name is different
	switch (type) {
		case CREATURE_EVENT_LOGIN:
			return "onLogin";

		case CREATURE_EVENT_LOGOUT:
			return "onLogout";

		case CREATURE_EVENT_RECONNECT:
			return "onReconnect";

		case CREATURE_EVENT_THINK:
			return "onThink";

		case CREATURE_EVENT_PREPAREDEATH:
			return "onPrepareDeath";

		case CREATURE_EVENT_DEATH:
			return "onDeath";

		case CREATURE_EVENT_KILL:
			return "onKill";

		case CREATURE_EVENT_ADVANCE:
			return "onAdvance";

		case CREATURE_EVENT_MODALWINDOW:
			return "onModalWindow";

		case CREATURE_EVENT_TEXTEDIT:
			return "onTextEdit";

		case CREATURE_EVENT_HEALTHCHANGE:
			return "onHealthChange";

		case CREATURE_EVENT_MANACHANGE:
			return "onManaChange";

		case CREATURE_EVENT_EXTENDED_OPCODE:
			return "onExtendedOpcode";

		case CREATURE_EVENT_NONE:
		default:
			return "";
	}
}

void CreatureEvent::copyEvent(CreatureEvent* creatureEvent)
{
	scriptId = creatureEvent->scriptId;
	scriptInterface = creatureEvent->scriptInterface;
	scripted = creatureEvent->scripted;
	loaded = creatureEvent->loaded;
}

void CreatureEvent::clearEvent()
{
	scriptId = 0;
	scriptInterface = nullptr;
	scripted = false;
	loaded = false;
}

bool CreatureEvent::executeOnLogin(Player* player) const
{
	// onLogin(player)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeOnLogin] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	Lua::pushUserdata(L, player);
	Lua::setMetatable(L, -1, "Player");
	return scriptInterface->callFunction(1);
}

bool CreatureEvent::executeOnLogout(Player* player) const
{
	// onLogout(player)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeOnLogout] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	Lua::pushUserdata(L, player);
	Lua::setMetatable(L, -1, "Player");
	return scriptInterface->callFunction(1);
}

void CreatureEvent::executeOnReconnect(Player* player) const
{
	// onReconnect(player)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeOnReconnect] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	Lua::pushUserdata(L, player);
	Lua::setMetatable(L, -1, "Player");
	scriptInterface->callFunction(1);
}

bool CreatureEvent::executeOnThink(Creature* creature, uint32_t interval)
{
	// onThink(creature, interval)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeOnThink] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);
	lua_pushinteger(L, interval);

	return scriptInterface->callFunction(2);
}

bool CreatureEvent::executeOnPrepareDeath(Creature* creature, Creature* killer)
{
	// onPrepareDeath(creature, killer)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeOnPrepareDeath] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);

	if (killer) {
		Lua::pushUserdata<Creature>(L, killer);
		Lua::setCreatureMetatable(L, -1, killer);
	} else {
		lua_pushnil(L);
	}

	return scriptInterface->callFunction(2);
}

bool CreatureEvent::executeOnDeath(Creature* creature, Item* corpse, Creature* killer, Creature* mostDamageKiller,
                                   bool lastHitUnjustified, bool mostDamageUnjustified)
{
	// onDeath(creature, corpse, killer, mostDamageKiller, lastHitUnjustified, mostDamageUnjustified)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeOnDeath] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);

	Lua::pushThing(L, corpse);

	if (killer) {
		Lua::pushUserdata<Creature>(L, killer);
		Lua::setCreatureMetatable(L, -1, killer);
	} else {
		lua_pushnil(L);
	}

	if (mostDamageKiller) {
		Lua::pushUserdata<Creature>(L, mostDamageKiller);
		Lua::setCreatureMetatable(L, -1, mostDamageKiller);
	} else {
		lua_pushnil(L);
	}

	Lua::pushBoolean(L, lastHitUnjustified);
	Lua::pushBoolean(L, mostDamageUnjustified);

	return scriptInterface->callFunction(6);
}

bool CreatureEvent::executeAdvance(Player* player, skills_t skill, uint32_t oldLevel, uint32_t newLevel)
{
	// onAdvance(player, skill, oldLevel, newLevel)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeAdvance] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	Lua::pushUserdata(L, player);
	Lua::setMetatable(L, -1, "Player");
	lua_pushinteger(L, static_cast<uint32_t>(skill));
	lua_pushinteger(L, oldLevel);
	lua_pushinteger(L, newLevel);

	return scriptInterface->callFunction(4);
}

void CreatureEvent::executeOnKill(Creature* creature, Creature* target)
{
	// onKill(creature, target)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeOnKill] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);
	Lua::pushUserdata<Creature>(L, target);
	Lua::setCreatureMetatable(L, -1, target);
	scriptInterface->callVoidFunction(2);
}

void CreatureEvent::executeModalWindow(Player* player, uint32_t modalWindowId, uint8_t buttonId, uint8_t choiceId)
{
	// onModalWindow(player, modalWindowId, buttonId, choiceId)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeModalWindow] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata(L, player);
	Lua::setMetatable(L, -1, "Player");

	lua_pushinteger(L, modalWindowId);
	lua_pushinteger(L, buttonId);
	lua_pushinteger(L, choiceId);

	scriptInterface->callVoidFunction(4);
}

bool CreatureEvent::executeTextEdit(Player* player, Item* item, std::string_view text, const uint32_t windowTextId)
{
	// onTextEdit(player, item, text, windowTextId)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeTextEdit] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushThing(L, item);
	Lua::pushString(L, text);

	lua_pushinteger(L, windowTextId);

	return scriptInterface->callFunction(4);
}

void CreatureEvent::executeHealthChange(Creature* creature, Creature* attacker, CombatDamage& damage)
{
	// onHealthChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType, origin)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeHealthChange] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);
	if (attacker) {
		Lua::pushUserdata(L, attacker);
		Lua::setCreatureMetatable(L, -1, attacker);
	} else {
		lua_pushnil(L);
	}

	Lua::pushCombatDamage(L, damage);

	if (scriptInterface->protectedCall(L, 7, 4) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		damage.primary.value = std::abs(Lua::getInteger<int32_t>(L, -4));
		damage.primary.type = Lua::getInteger<CombatType_t>(L, -3);
		damage.secondary.value = std::abs(Lua::getInteger<int32_t>(L, -2));
		damage.secondary.type = Lua::getInteger<CombatType_t>(L, -1);

		lua_pop(L, 4);
		if (damage.primary.type != COMBAT_HEALING) {
			damage.primary.value = -damage.primary.value;
			damage.secondary.value = -damage.secondary.value;
		}
	}

	scriptInterface->resetScriptEnv();
}

void CreatureEvent::executeManaChange(Creature* creature, Creature* attacker, CombatDamage& damage)
{
	// onManaChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType, origin)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeManaChange] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);
	if (attacker) {
		Lua::pushUserdata(L, attacker);
		Lua::setCreatureMetatable(L, -1, attacker);
	} else {
		lua_pushnil(L);
	}

	Lua::pushCombatDamage(L, damage);

	if (scriptInterface->protectedCall(L, 7, 4) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		damage.primary.value = Lua::getInteger<int32_t>(L, -4);
		damage.primary.type = Lua::getInteger<CombatType_t>(L, -3);
		damage.secondary.value = Lua::getInteger<int32_t>(L, -2);
		damage.secondary.type = Lua::getInteger<CombatType_t>(L, -1);
		lua_pop(L, 4);
	}

	scriptInterface->resetScriptEnv();
}

void CreatureEvent::executeExtendedOpcode(Player* player, uint8_t opcode, std::string_view buffer)
{
	// onExtendedOpcode(player, opcode, buffer)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CreatureEvent::executeExtendedOpcode] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	lua_pushinteger(L, opcode);
	Lua::pushString(L, buffer);

	scriptInterface->callVoidFunction(3);
}
