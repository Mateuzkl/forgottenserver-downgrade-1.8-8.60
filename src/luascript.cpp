// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "luascript.h"

#include "bed.h"
#include "chat.h"
#include "configmanager.h"
#include "databasemanager.h"
#include "databasetasks.h"
#include "depotchest.h"
#include "events.h"
#include "game.h"
#include "globalevent.h"
#include "housetile.h"
#include "logger.h"
#include "luavariant.h"
#include "matrixarea.h"
#include "monster.h"
#include "npc.h"
#include "player.h"
#include "protocolstatus.h"
#include "scheduler.h"
#include "script.h"
#include "spectators.h"
#include "spells.h"
#include "teleport.h"

#include <fmt/format.h>

extern GlobalEvents* g_globalEvents;

#include <boost/range/adaptor/reversed.hpp>

extern Chat* g_chat;
extern Game g_game;
extern Vocations g_vocations;
extern Scripts* g_scripts;
extern Spells* g_spells;

ScriptEnvironment::DBResultMap ScriptEnvironment::tempResults;
uint32_t ScriptEnvironment::lastResultId = 0;

std::multimap<ScriptEnvironment*, Item*> ScriptEnvironment::tempItems;

LuaEnvironment g_luaEnvironment;

ScriptEnvironment::ScriptEnvironment() { resetEnv(); }

ScriptEnvironment::~ScriptEnvironment() { resetEnv(); }

void ScriptEnvironment::resetEnv()
{
	scriptId = 0;
	callbackId = 0;
	timerEvent = false;
	interface = nullptr;
	localMap.clear();
	tempResults.clear();

	auto pair = tempItems.equal_range(this);
	auto it = pair.first;
	while (it != pair.second) {
		Item* item = it->second;
		if (item && item->getParent() == VirtualCylinder::virtualCylinder) {
			g_game.ReleaseItem(item);
		}
		it = tempItems.erase(it);
	}
}

bool ScriptEnvironment::setCallbackId(int32_t callbackId, LuaScriptInterface* scriptInterface)
{
	if (this->callbackId != 0) {
		// nested callbacks are not allowed
		if (interface) {
			reportErrorFunc(interface->getLuaState(), "Nested callbacks!");
		}
		return false;
	}

	this->callbackId = callbackId;
	interface = scriptInterface;
	return true;
}

void ScriptEnvironment::getEventInfo(int32_t& scriptId, LuaScriptInterface*& scriptInterface, int32_t& callbackId,
                                     bool& timerEvent) const
{
	scriptId = this->scriptId;
	scriptInterface = interface;
	callbackId = this->callbackId;
	timerEvent = this->timerEvent;
}

uint32_t ScriptEnvironment::addThing(Thing* thing)
{
	if (!thing || thing->isRemoved()) {
		return 0;
	}

	Creature* creature = thing->getCreature();
	if (creature) {
		return creature->getID();
	}

	Item* item = thing->getItem();
	if (item && item->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		return item->getUniqueId();
	}

	for (const auto& it : localMap) {
		if (it.second == item) {
			return it.first;
		}
	}

	localMap[++lastUID] = item;
	return lastUID;
}

void ScriptEnvironment::insertItem(uint32_t uid, Item* item)
{
	auto result = localMap.emplace(uid, item);
	if (!result.second) {
		LOG_ERROR("Lua Script Error: Thing uid already taken.");
	}
}

Thing* ScriptEnvironment::getThingByUID(uint32_t uid)
{
	if (uid >= 0x10000000) {
		return g_game.getCreatureByID(uid);
	}

	if (uid <= std::numeric_limits<uint16_t>::max()) {
		Item* item = g_game.getUniqueItem(static_cast<uint16_t>(uid));
		if (item && !item->isRemoved()) {
			return item;
		}
		return nullptr;
	}

	auto it = localMap.find(uid);
	if (it != localMap.end()) {
		Item* item = it->second;
		if (!item->isRemoved()) {
			return item;
		}
	}
	return nullptr;
}

Item* ScriptEnvironment::getItemByUID(uint32_t uid)
{
	Thing* thing = getThingByUID(uid);
	if (!thing) {
		return nullptr;
	}
	return thing->getItem();
}

Container* ScriptEnvironment::getContainerByUID(uint32_t uid)
{
	Item* item = getItemByUID(uid);
	if (!item) {
		return nullptr;
	}
	return item->getContainer();
}

void ScriptEnvironment::removeItemByUID(uint32_t uid)
{
	if (uid <= std::numeric_limits<uint16_t>::max()) {
		g_game.removeUniqueItem(static_cast<uint16_t>(uid));
		return;
	}

	auto it = localMap.find(uid);
	if (it != localMap.end()) {
		localMap.erase(it);
	}
}

void ScriptEnvironment::addTempItem(Item* item) { tempItems.emplace(this, item); }

void ScriptEnvironment::removeTempItem(Item* item)
{
	for (auto it = tempItems.begin(), end = tempItems.end(); it != end; ++it) {
		if (it->second == item) {
			tempItems.erase(it);
			break;
		}
	}
}

uint32_t ScriptEnvironment::addResult(DBResult_ptr res)
{
	tempResults[++lastResultId] = res;
	return lastResultId;
}

bool ScriptEnvironment::removeResult(uint32_t id)
{
	auto it = tempResults.find(id);
	if (it == tempResults.end()) {
		return false;
	}

	tempResults.erase(it);
	return true;
}

DBResult_ptr ScriptEnvironment::getResultByID(uint32_t id)
{
	auto it = tempResults.find(id);
	if (it == tempResults.end()) {
		return nullptr;
	}
	return it->second;
}

std::string_view LuaScriptInterface::getErrorDesc(LuaErrorCode code)
{
	switch (code) {
		case LuaErrorCode::PLAYER_NOT_FOUND:
			return "Player not found";
		case LuaErrorCode::CREATURE_NOT_FOUND:
			return "Creature not found";
		case LuaErrorCode::ITEM_NOT_FOUND:
			return "Item not found";
		case LuaErrorCode::THING_NOT_FOUND:
			return "Thing not found";
		case LuaErrorCode::TILE_NOT_FOUND:
			return "Tile not found";
		case LuaErrorCode::HOUSE_NOT_FOUND:
			return "House not found";
		case LuaErrorCode::COMBAT_NOT_FOUND:
			return "Combat not found";
		case LuaErrorCode::CONDITION_NOT_FOUND:
			return "Condition not found";
		case LuaErrorCode::AREA_NOT_FOUND:
			return "Area not found";
		case LuaErrorCode::CONTAINER_NOT_FOUND:
			return "Container not found";
		case LuaErrorCode::VARIANT_NOT_FOUND:
			return "Variant not found";
		case LuaErrorCode::VARIANT_UNKNOWN:
			return "Unknown variant type";
		case LuaErrorCode::SPELL_NOT_FOUND:
			return "Spell not found";
		case LuaErrorCode::CALLBACK_NOT_FOUND:
			return "Callback not found";
		default:
			return "Bad error code";
	}
}

ScriptEnvironment LuaScriptInterface::scriptEnv[16];
int32_t LuaScriptInterface::scriptEnvIndex = -1;

LuaScriptInterface::LuaScriptInterface(std::string_view interfaceName) : interfaceName{interfaceName}
{
	// Don't initialize g_luaEnvironment here if we ARE g_luaEnvironment
	// This prevents infinite recursion during static initialization
	if (this != &g_luaEnvironment && !g_luaEnvironment.getLuaState()) {
		g_luaEnvironment.initState();
	}
}

LuaScriptInterface::~LuaScriptInterface() { closeState(); }

bool LuaScriptInterface::reInitState()
{
	g_luaEnvironment.clearCombatObjects(this);
	g_luaEnvironment.clearAreaObjects(this);

	closeState();
	return initState();
}

void LuaEnvironment::shutdown()
{
	// Force garbage collection before closing
	if (g_luaEnvironment.luaState) {
		lua_gc(g_luaEnvironment.luaState, LUA_GCCOLLECT, 0);
	}

	// Close the main Lua state
	g_luaEnvironment.closeState();
}

/// Same as lua_pcall, but adds stack trace to error strings in called function.
int LuaScriptInterface::protectedCall(lua_State* L, int nargs, int nresults)
{
	int error_index = lua_gettop(L) - nargs;
	lua_pushcfunction(L, luaErrorHandler);
	lua_insert(L, error_index);

	int ret = lua_pcall(L, nargs, nresults, error_index);
	lua_remove(L, error_index);
	return ret;
}

int32_t LuaScriptInterface::loadFile(std::string_view file, Npc* npc /* = nullptr*/)
{
	// loads file as a chunk at stack top
	int ret = luaL_loadfile(luaState, file.data());
	if (ret != 0) {
		lastLuaError = Lua::popString(luaState);
		return -1;
	}

	// check that it is loaded as a function
	if (!Lua::isFunction(luaState, -1)) {
		lua_pop(luaState, 1);
		return -1;
	}

	loadingFile = file;

	if (!reserveScriptEnv()) {
		lua_pop(luaState, 1);
		return -1;
	}

	ScriptEnvironment* env = getScriptEnv();
	env->setScriptId(EVENT_ID_LOADING, this);
	env->setNpc(npc);

	// execute it
	ret = protectedCall(luaState, 0, 0);
	if (ret != 0) {
		reportError(nullptr, Lua::popString(luaState));
		resetScriptEnv();
		return -1;
	}

	resetScriptEnv();
	return 0;
}

int32_t LuaScriptInterface::getEvent(std::string_view eventName)
{
	// get our events table
	lua_rawgeti(luaState, LUA_REGISTRYINDEX, eventTableRef);
	if (!Lua::isTable(luaState, -1)) {
		lua_pop(luaState, 1);
		return -1;
	}

	// get current event function pointer
	lua_getglobal(luaState, eventName.data());
	if (!Lua::isFunction(luaState, -1)) {
		lua_pop(luaState, 2);
		return -1;
	}

	// save in our events table
	lua_pushvalue(luaState, -1);
	lua_rawseti(luaState, -3, runningEventId);
	lua_pop(luaState, 2);

	// reset global value of this event
	lua_pushnil(luaState);
	lua_setglobal(luaState, eventName.data());

	cacheFiles[runningEventId] = fmt::format("{}:{}", loadingFile, eventName);
	return runningEventId++;
}

int32_t LuaScriptInterface::getEvent()
{
	// check if function is on the stack
	if (!Lua::isFunction(luaState, -1)) {
		return -1;
	}

	// get our events table
	lua_rawgeti(luaState, LUA_REGISTRYINDEX, eventTableRef);
	if (!Lua::isTable(luaState, -1)) {
		lua_pop(luaState, 1);
		return -1;
	}

	// save in our events table
	lua_pushvalue(luaState, -2);
	lua_rawseti(luaState, -2, runningEventId);
	lua_pop(luaState, 2);

	cacheFiles[runningEventId] = loadingFile + ":callback";
	return runningEventId++;
}

int32_t LuaScriptInterface::getMetaEvent(std::string_view globalName, std::string_view eventName)
{
	// get our events table
	lua_rawgeti(luaState, LUA_REGISTRYINDEX, eventTableRef);
	if (!Lua::isTable(luaState, -1)) {
		lua_pop(luaState, 1);
		return -1;
	}

	// get current event function pointer
	lua_getglobal(luaState, globalName.data());
	lua_getfield(luaState, -1, eventName.data());
	if (!Lua::isFunction(luaState, -1)) {
		lua_pop(luaState, 3);
		return -1;
	}

	// save in our events table
	lua_pushvalue(luaState, -1);
	lua_rawseti(luaState, -4, runningEventId);
	lua_pop(luaState, 1);

	// reset global value of this event
	lua_pushnil(luaState);
	lua_setfield(luaState, -2, eventName.data());
	lua_pop(luaState, 2);

	cacheFiles[runningEventId] = fmt::format("{}:{}@{}", loadingFile, globalName, eventName);
	return runningEventId++;
}

void LuaScriptInterface::removeEvent(int32_t scriptId)
{
	if (scriptId == -1) {
		return;
	}

	// get our events table
	lua_rawgeti(luaState, LUA_REGISTRYINDEX, eventTableRef);
	if (!Lua::isTable(luaState, -1)) {
		lua_pop(luaState, 1);
		return;
	}

	// remove event from table
	lua_pushnil(luaState);
	lua_rawseti(luaState, -2, scriptId);
	lua_pop(luaState, 1);

	cacheFiles.erase(scriptId);
}

std::string_view LuaScriptInterface::getFileById(int32_t scriptId)
{
	if (scriptId == EVENT_ID_LOADING) {
		return loadingFile;
	}

	auto it = cacheFiles.find(scriptId);
	if (it == cacheFiles.end()) {
		return "(Unknown scriptfile)";
	}
	return it->second;
}

const std::string& LuaScriptInterface::getFileByIdForStats(int32_t scriptId)
{
	auto it = cacheFiles.find(scriptId);
	if (it == cacheFiles.end()) {
		static const std::string& unk = "(Unknown scriptfile)";
		return unk;
	}
	return it->second;
}

std::string LuaScriptInterface::getStackTrace(lua_State* L, std::string_view error_desc)
{
	std::string errorStr(error_desc);
	luaL_traceback(L, L, errorStr.c_str(), 1);
	return Lua::popString(L);
}

void LuaScriptInterface::reportError(const char* function, std::string_view error_desc, lua_State* L /*= nullptr*/,
                                     bool stack_trace /*= false*/)
{
	int32_t scriptId;
	int32_t callbackId;
	bool timerEvent;
	LuaScriptInterface* scriptInterface;
	getScriptEnv()->getEventInfo(scriptId, scriptInterface, callbackId, timerEvent);

	LOG_ERROR("Lua Script Error: ");

	if (scriptInterface) {
		LOG_ERROR(fmt::format("[{}] ", scriptInterface->getInterfaceName()));

		if (timerEvent) {
			LOG_ERROR("in a timer event called from: ");
		}

		if (callbackId) {
			LOG_ERROR(fmt::format("in callback: {}", scriptInterface->getFileById(callbackId)));
		}

		LOG_ERROR(scriptInterface->getFileById(scriptId));
	}

	if (function) {
		LOG_ERROR(fmt::format("{}(). ", function));
	}

	if (L && stack_trace) {
		LOG_ERROR(getStackTrace(L, error_desc));
	} else {
		LOG_ERROR(error_desc);
	}
}

bool LuaScriptInterface::pushFunction(int32_t functionId)
{
	lua_rawgeti(luaState, LUA_REGISTRYINDEX, eventTableRef);
	if (!Lua::isTable(luaState, -1)) {
		return false;
	}

	lua_rawgeti(luaState, -1, functionId);
	lua_replace(luaState, -2);
	return Lua::isFunction(luaState, -1);
}

bool LuaScriptInterface::initState()
{
	luaState = g_luaEnvironment.getLuaState();
	if (!luaState) {
		return false;
	}

	lua_newtable(luaState);
	eventTableRef = luaL_ref(luaState, LUA_REGISTRYINDEX);
	runningEventId = EVENT_ID_USER;
	return true;
}

bool LuaScriptInterface::closeState()
{
	if (!g_luaEnvironment.getLuaState() || !luaState) {
		return false;
	}

	cacheFiles.clear();
	if (eventTableRef != -1) {
		luaL_unref(luaState, LUA_REGISTRYINDEX, eventTableRef);
		eventTableRef = -1;
	}

	luaState = nullptr;
	return true;
}

int LuaScriptInterface::luaErrorHandler(lua_State* L)
{
	const std::string& errorMessage = Lua::popString(L);
	Lua::pushString(L, LuaScriptInterface::getStackTrace(L, errorMessage));
	return 1;
}

bool LuaScriptInterface::callFunction(int params)
{
#ifdef STATS_ENABLED
	int32_t scriptId;
	int32_t callbackId;
	bool timerEvent;
	LuaScriptInterface* scriptInterface;
	getScriptEnv()->getEventInfo(scriptId, scriptInterface, callbackId, timerEvent);
	std::chrono::high_resolution_clock::time_point time_point = std::chrono::high_resolution_clock::now();
#endif

	bool result = false;
	int size = lua_gettop(luaState);
	if (protectedCall(luaState, params, 1) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::getString(luaState, -1));
	} else {
		result = Lua::getBoolean(luaState, -1);
	}

	lua_pop(luaState, 1);
	if ((lua_gettop(luaState) + params + 1) != size) {
		LuaScriptInterface::reportError(nullptr, "Stack size changed!");
	}

#ifdef STATS_ENABLED
	uint64_t ns =
	    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - time_point)
	        .count();
	g_stats.addLuaStats(new Stat(ns, getFileByIdForStats(scriptId), ""));
#endif

	resetScriptEnv();
	return result;
}

void LuaScriptInterface::callVoidFunction(int params)
{
	int size = lua_gettop(luaState);
	if (protectedCall(luaState, params, 0) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(luaState));
	}

	if ((lua_gettop(luaState) + params + 1) != size) {
		LuaScriptInterface::reportError(nullptr, "Stack size changed!");
	}

	resetScriptEnv();
}

ReturnValue LuaScriptInterface::callReturnValueFunction(int params)
{
	int size = lua_gettop(luaState);
	if (protectedCall(luaState, params, 0) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(luaState));
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if ((lua_gettop(luaState) + params + 1) != size) {
		LuaScriptInterface::reportError(nullptr, "Stack size changed!");
		return RETURNVALUE_NOTPOSSIBLE;
	}

	resetScriptEnv();
	return Lua::getInteger<ReturnValue>(luaState, -1);
}

void Lua::pushVariant(lua_State* L, const LuaVariant& var)
{
	lua_createtable(L, 0, 2);
	setField(L, "type", var.type());
	switch (var.type()) {
		case VARIANT_NUMBER:
			setField(L, "number", var.getNumber());
			break;
		case VARIANT_STRING:
			setField(L, "string", var.getString());
			break;
		case VARIANT_TARGETPOSITION:
			pushPosition(L, var.getTargetPosition());
			lua_setfield(L, -2, "pos");
			break;
		case VARIANT_POSITION: {
			pushPosition(L, var.getPosition());
			lua_setfield(L, -2, "pos");
			break;
		}
		default:
			break;
	}
	setMetatable(L, -1, "Variant");
}

void Lua::pushThing(lua_State* L, Thing* thing)
{
	if (!thing) {
		lua_createtable(L, 0, 4);
		setField(L, "uid", 0);
		setField(L, "itemid", 0);
		setField(L, "actionid", 0);
		setField(L, "type", 0);
		return;
	}

	if (Item* item = thing->getItem()) {
		pushUserdata<Item>(L, item);
		setItemMetatable(L, -1, item);
	} else if (Creature* creature = thing->getCreature()) {
		pushUserdata<Creature>(L, creature);
		setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}
}

void Lua::pushCylinder(lua_State* L, Cylinder* cylinder)
{
	if (Creature* creature = cylinder->getCreature()) {
		pushUserdata<Creature>(L, creature);
		setCreatureMetatable(L, -1, creature);
	} else if (Item* parentItem = cylinder->getItem()) {
		pushUserdata<Item>(L, parentItem);
		setItemMetatable(L, -1, parentItem);
	} else if (Tile* tile = cylinder->getTile()) {
		pushUserdata<Tile>(L, tile);
		setMetatable(L, -1, "Tile");
	} else if (cylinder == VirtualCylinder::virtualCylinder) {
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
}

void Lua::pushString(lua_State* L, std::string_view value) { lua_pushlstring(L, value.data(), value.length()); }

void Lua::pushCallback(lua_State* L, int32_t callback) { lua_rawgeti(L, LUA_REGISTRYINDEX, callback); }

std::string Lua::popString(lua_State* L)
{
	if (lua_gettop(L) == 0) {
		return {};
	}

	auto str = getString(L, -1);
	lua_pop(L, 1);
	return str;
}

int32_t Lua::popCallback(lua_State* L) { return luaL_ref(L, LUA_REGISTRYINDEX); }

// Metatables
void Lua::setMetatable(lua_State* L, int32_t index, std::string_view name)
{
	luaL_getmetatable(L, name.data());
	lua_setmetatable(L, index - 1);
}

void Lua::setWeakMetatable(lua_State* L, int32_t index, std::string_view name)
{
	static std::set<std::string> weakObjectTypes;
	const std::string& weakName = fmt::format("{}{}", name, "_weak");

	auto result = weakObjectTypes.emplace(name);
	if (result.second) {
		luaL_getmetatable(L, name.data());
		int childMetatable = lua_gettop(L);

		luaL_newmetatable(L, weakName.c_str());
		int metatable = lua_gettop(L);

		for (std::string_view metaKey : {"__index", "__metatable", "__eq"}) {
			lua_getfield(L, childMetatable, metaKey.data());
			lua_setfield(L, metatable, metaKey.data());
		}

		for (auto metaIndex : {'h', 'p', 't'}) {
			lua_rawgeti(L, childMetatable, metaIndex);
			lua_rawseti(L, metatable, metaIndex);
		}

		lua_pushnil(L);
		lua_setfield(L, metatable, "__gc");

		lua_remove(L, childMetatable);
	} else {
		luaL_getmetatable(L, weakName.c_str());
	}
	lua_setmetatable(L, index - 1);
}

void Lua::setItemMetatable(lua_State* L, int32_t index, const Item* item)
{
	if (item->getContainer()) {
		luaL_getmetatable(L, "Container");
	} else if (item->getTeleport()) {
		luaL_getmetatable(L, "Teleport");
	} else {
		luaL_getmetatable(L, "Item");
	}
	lua_setmetatable(L, index - 1);
}

void Lua::setCreatureMetatable(lua_State* L, int32_t index, const Creature* creature)
{
	if (creature->getPlayer()) {
		luaL_getmetatable(L, "Player");
	} else if (creature->getMonster()) {
		luaL_getmetatable(L, "Monster");
	} else {
		luaL_getmetatable(L, "Npc");
	}
	lua_setmetatable(L, index - 1);
}

// Is
bool Lua::isNone(lua_State* L, int32_t arg) { return lua_isnone(L, arg); }
bool Lua::isNumber(lua_State* L, int32_t arg) { return lua_type(L, arg) == LUA_TNUMBER; }
bool Lua::isInteger(lua_State* L, int32_t arg) { return lua_isinteger(L, arg) != 0; }
bool Lua::isString(lua_State* L, int32_t arg) { return lua_isstring(L, arg) != 0; }
bool Lua::isBoolean(lua_State* L, int32_t arg) { return lua_isboolean(L, arg); }
bool Lua::isTable(lua_State* L, int32_t arg) { return lua_istable(L, arg); }
bool Lua::isFunction(lua_State* L, int32_t arg) { return lua_isfunction(L, arg); }
bool Lua::isUserdata(lua_State* L, int32_t arg) { return lua_isuserdata(L, arg) != 0; }

// Get
bool Lua::getBoolean(lua_State* L, int32_t arg) { return lua_toboolean(L, arg) != 0; }
bool Lua::getBoolean(lua_State* L, int32_t arg, bool defaultValue)
{
	const auto parameters = lua_gettop(L);
	if (parameters == 0 || arg > parameters) {
		return defaultValue;
	}
	return lua_toboolean(L, arg) != 0;
}

std::string Lua::getString(lua_State* L, int32_t arg)
{
	size_t len;
	const char* c_str = lua_tolstring(L, arg, &len);
	if (!c_str || len == 0) {
		return {};
	}
	return {c_str, len};
}

Position Lua::getPosition(lua_State* L, int32_t arg, int32_t& stackpos)
{
	Position position;
	position.x = getField<uint16_t>(L, arg, "x");
	position.y = getField<uint16_t>(L, arg, "y");
	position.z = getField<uint8_t>(L, arg, "z");

	lua_getfield(L, arg, "stackpos");
	if (lua_isnil(L, -1) == 1) {
		stackpos = 0;
	} else {
		stackpos = getInteger<int32_t>(L, -1);
	}

	lua_pop(L, 4);
	return position;
}

Position Lua::getPosition(lua_State* L, int32_t arg)
{
	Position position;
	position.x = getField<uint16_t>(L, arg, "x");
	position.y = getField<uint16_t>(L, arg, "y");
	position.z = getField<uint8_t>(L, arg, "z");

	lua_pop(L, 3);
	return position;
}

Outfit_t Lua::getOutfit(lua_State* L, int32_t arg)
{
	Outfit_t outfit;
	outfit.lookAddons = getField<uint8_t>(L, arg, "lookAddons");

	outfit.lookFeet = getField<uint8_t>(L, arg, "lookFeet");
	outfit.lookLegs = getField<uint8_t>(L, arg, "lookLegs");
	outfit.lookBody = getField<uint8_t>(L, arg, "lookBody");
	outfit.lookHead = getField<uint8_t>(L, arg, "lookHead");

	outfit.lookTypeEx = getField<uint16_t>(L, arg, "lookTypeEx");
	outfit.lookType = getField<uint16_t>(L, arg, "lookType");

	lua_pop(L, 8);
	return outfit;
}

Outfit Lua::getOutfitClass(lua_State* L, int32_t arg)
{
	uint16_t lookType = getField<uint16_t>(L, arg, "lookType");
	PlayerSex_t sex = getField<PlayerSex_t>(L, arg, "sex");
	auto name = getFieldString(L, arg, "name");
	bool premium = getField<uint8_t>(L, arg, "premium") == 1;
	bool unlocked = getField<uint8_t>(L, arg, "unlocked") == 1;
	lua_pop(L, 5);
	return {name, lookType, sex, premium, unlocked};
}

LuaVariant Lua::getVariant(lua_State* L, int32_t arg)
{
	LuaVariant var;
	switch (getField<LuaVariantType_t>(L, arg, "type")) {
		case VARIANT_NUMBER: {
			var.setNumber(getField<uint32_t>(L, arg, "number"));
			lua_pop(L, 2);
			break;
		}

		case VARIANT_STRING: {
			var.setString(getFieldString(L, arg, "string"));
			lua_pop(L, 2);
			break;
		}

		case VARIANT_POSITION:
			lua_getfield(L, arg, "pos");
			var.setPosition(getPosition(L, lua_gettop(L)));
			lua_pop(L, 2);
			break;

		case VARIANT_TARGETPOSITION: {
			lua_getfield(L, arg, "pos");
			var.setTargetPosition(getPosition(L, lua_gettop(L)));
			lua_pop(L, 2);
			break;
		}

		default: {
			var = {};
			lua_pop(L, 1);
			break;
		}
	}
	return var;
}

InstantSpell* Lua::getInstantSpell(lua_State* L, int32_t arg)
{
	InstantSpell* spell = g_spells->getInstantSpellByName(getFieldString(L, arg, "name"));
	lua_pop(L, 1);
	return spell;
}

Reflect Lua::getReflect(lua_State* L, int32_t arg)
{
	uint16_t percent = getField<uint16_t>(L, arg, "percent");
	uint16_t chance = getField<uint16_t>(L, arg, "chance");
	lua_pop(L, 2);
	return Reflect(percent, chance);
}

Thing* Lua::getThing(lua_State* L, int32_t arg)
{
	Thing* thing;
	if (lua_getmetatable(L, arg) != 0) {
		lua_rawgeti(L, -1, 't');
		switch (getInteger<uint32_t>(L, -1)) {
			case LuaData_Item:
				thing = getUserdata<Item>(L, arg);
				break;
			case LuaData_Container:
				thing = getUserdata<Container>(L, arg);
				break;
			case LuaData_Teleport:
				thing = getUserdata<Teleport>(L, arg);
				break;
			case LuaData_Player:
				thing = getUserdata<Player>(L, arg);
				break;
			case LuaData_Monster:
				thing = getUserdata<Monster>(L, arg);
				break;
			case LuaData_Npc:
				thing = getUserdata<Npc>(L, arg);
				break;
			default:
				thing = nullptr;
				break;
		}
		lua_pop(L, 2);
	} else {
		thing = LuaScriptInterface::getScriptEnv()->getThingByUID(getInteger<uint32_t>(L, arg));
	}
	return thing;
}

Creature* Lua::getCreature(lua_State* L, int32_t arg)
{
	if (isUserdata(L, arg)) {
		return getUserdata<Creature>(L, arg);
	}
	return g_game.getCreatureByID(getInteger<uint32_t>(L, arg));
}

Player* Lua::getPlayer(lua_State* L, int32_t arg)
{
	if (isUserdata(L, arg)) {
		return getUserdata<Player>(L, arg);
	}
	return g_game.getPlayerByID(getInteger<uint32_t>(L, arg));
}

std::string Lua::getFieldString(lua_State* L, int32_t arg, std::string_view key)
{
	lua_getfield(L, arg, key.data());
	return getString(L, -1);
}

LuaDataType Lua::getUserdataType(lua_State* L, int32_t arg)
{
	if (lua_getmetatable(L, arg) == 0) {
		return LuaData_Unknown;
	}
	lua_rawgeti(L, -1, 't');

	LuaDataType type = getInteger<LuaDataType>(L, -1);
	lua_pop(L, 2);

	return type;
}

std::optional<uint8_t> Lua::getBlessingId(lua_State* L, int32_t arg)
{
	int8_t blessing = getInteger<int8_t>(L, arg) - 1;
	if (blessing < 0 || blessing >= PLAYER_MAX_BLESSINGS) {
		reportErrorFunc(
		    L, fmt::format("Invalid blessing id: {} (must be between 1 and {})", blessing + 1, PLAYER_MAX_BLESSINGS));
		return std::nullopt;
	}

	return std::make_optional(blessing);
}

// Push
void Lua::pushBoolean(lua_State* L, bool value) { lua_pushboolean(L, value ? 1 : 0); }

void Lua::pushCombatDamage(lua_State* L, const CombatDamage& damage)
{
	lua_pushinteger(L, damage.primary.value);
	lua_pushinteger(L, damage.primary.type);
	lua_pushinteger(L, damage.secondary.value);
	lua_pushinteger(L, damage.secondary.type);
	lua_pushinteger(L, damage.origin);
}

void Lua::pushInstantSpell(lua_State* L, const InstantSpell& spell)
{
	lua_createtable(L, 0, 7);

	setField(L, "name", spell.getName());
	setField(L, "words", spell.getWords());
	setField(L, "level", spell.getLevel());
	setField(L, "mlevel", spell.getMagicLevel());
	setField(L, "mana", spell.getMana());
	setField(L, "manapercent", spell.getManaPercent());
	setField(L, "params", spell.getHasParam());

	setMetatable(L, -1, "Spell");
}

void Lua::pushSpell(lua_State* L, const Spell& spell)
{
	lua_createtable(L, 0, 5);

	setField(L, "name", spell.getName());
	setField(L, "level", spell.getLevel());
	setField(L, "mlevel", spell.getMagicLevel());
	setField(L, "mana", spell.getMana());
	setField(L, "manapercent", spell.getManaPercent());

	setMetatable(L, -1, "Spell");
}

void Lua::pushPosition(lua_State* L, const Position& position, int32_t stackpos /* = 0*/)
{
	lua_createtable(L, 0, 4);

	setField(L, "x", position.x);
	setField(L, "y", position.y);
	setField(L, "z", position.z);
	setField(L, "stackpos", stackpos);

	setMetatable(L, -1, "Position");
}

void Lua::pushOutfit(lua_State* L, const Outfit_t& outfit)
{
	lua_createtable(L, 0, 9);
	setField(L, "lookType", outfit.lookType);
	setField(L, "lookTypeEx", outfit.lookTypeEx);
	setField(L, "lookMount", outfit.lookMount);
	setField(L, "lookHead", outfit.lookHead);
	setField(L, "lookBody", outfit.lookBody);
	setField(L, "lookLegs", outfit.lookLegs);
	setField(L, "lookFeet", outfit.lookFeet);
	setField(L, "lookAddons", outfit.lookAddons);
}

void Lua::pushOutfit(lua_State* L, const Outfit* outfit)
{
	lua_createtable(L, 0, 5);
	setField(L, "lookType", outfit->lookType);
	setField(L, "sex", outfit->sex);
	setField(L, "name", outfit->name);
	setField(L, "premium", outfit->premium);
	setField(L, "unlocked", outfit->unlocked);
	setMetatable(L, -1, "Outfit");
}

void Lua::pushMount(lua_State* L, const Mount* mount)
{
	lua_createtable(L, 0, 5);
	setField(L, "name", mount->name);
	setField(L, "speed", mount->speed);
	setField(L, "clientId", mount->clientId);
	setField(L, "id", mount->id);
	setField(L, "premium", mount->premium);
}

void Lua::pushLoot(lua_State* L, const std::vector<LootBlock>& lootList)
{
	lua_createtable(L, lootList.size(), 0);

	int index = 0;
	for (const auto& lootBlock : lootList) {
		lua_createtable(L, 0, 7);

		setField(L, "itemId", lootBlock.id);
		setField(L, "chance", lootBlock.chance);
		setField(L, "subType", lootBlock.subType);
		setField(L, "minCount", lootBlock.countmin);
		setField(L, "maxCount", lootBlock.countmax);
		setField(L, "actionId", lootBlock.actionId);
		setField(L, "text", lootBlock.text);

		pushLoot(L, lootBlock.childLoot);
		lua_setfield(L, -2, "childLoot");

		lua_rawseti(L, -2, ++index);
	}
}

void Lua::pushReflect(lua_State* L, const Reflect& reflect)
{
	lua_createtable(L, 0, 2);
	setField(L, "percent", reflect.percent);
	setField(L, "chance", reflect.chance);
}

#define registerEnum(value) \
	{ \
		std::string enumName = #value; \
		registerGlobalVariable(enumName.substr(enumName.find_last_of(':') + 1), value); \
	}
#define registerEnumIn(tableName, value) \
	{ \
		std::string enumName = #value; \
		registerVariable(tableName, enumName.substr(enumName.find_last_of(':') + 1), static_cast<int64_t>(value)); \
	}
#define registerEnumClass(value) \
	{ \
		const std::string enumClassName = #value; \
		const size_t found = enumClassName.find_last_of(':'); \
		registerVariable(enumClassName.substr(0, found - 1), enumClassName.substr(found + 1), \
		                 static_cast<int64_t>(value)); \
	}

void LuaScriptInterface::registerFunctions()
{
	// doPlayerAddItem(uid, itemid, <optional: default: 1> count/subtype)
	// doPlayerAddItem(cid, itemid, <optional: default: 1> count, <optional: default: 1> canDropOnMap, <optional:
	// default: 1>subtype) Returns uid of the created item
	lua_register(luaState, "doPlayerAddItem", LuaScriptInterface::luaDoPlayerAddItem);

	// transformToSHA1(text)
	lua_register(luaState, "transformToSHA1", LuaScriptInterface::luaTransformToSHA1);

	// isValidUID(uid)
	lua_register(luaState, "isValidUID", LuaScriptInterface::luaIsValidUID);

	// isDepot(uid)
	lua_register(luaState, "isDepot", LuaScriptInterface::luaIsDepot);

	// isMovable(uid)
	lua_register(luaState, "isMovable", LuaScriptInterface::luaIsMoveable);

	// doAddContainerItem(uid, itemid, <optional> count/subtype)
	lua_register(luaState, "doAddContainerItem", LuaScriptInterface::luaDoAddContainerItem);

	// getDepotId(uid)
	lua_register(luaState, "getDepotId", LuaScriptInterface::luaGetDepotId);

	// getWorldTime()
	lua_register(luaState, "getWorldTime", LuaScriptInterface::luaGetWorldTime);

	// getWorldLight()
	lua_register(luaState, "getWorldLight", LuaScriptInterface::luaGetWorldLight);

	// setWorldLight(level, color)
	lua_register(luaState, "setWorldLight", LuaScriptInterface::luaSetWorldLight);

	// getWorldUpTime()
	lua_register(luaState, "getWorldUpTime", LuaScriptInterface::luaGetWorldUpTime);

	// getSubTypeName(subType)
	lua_register(luaState, "getSubTypeName", LuaScriptInterface::luaGetSubTypeName);

	// createCombatArea( {area}, <optional> {extArea} )
	lua_register(luaState, "createCombatArea", LuaScriptInterface::luaCreateCombatArea);

	// doAreaCombat(cid, type, pos, area, min, max, effect[, origin = ORIGIN_SPELL[, blockArmor = false[, blockShield =
	// false[, ignoreResistances = false]]]])
	lua_register(luaState, "doAreaCombat", LuaScriptInterface::luaDoAreaCombat);

	// doTargetCombat(cid, target, type, min, max, effect[, origin = ORIGIN_SPELL[, blockArmor = false[, blockShield =
	// false[, ignoreResistances = false]]]])
	lua_register(luaState, "doTargetCombat", LuaScriptInterface::luaDoTargetCombat);

	// doChallengeCreature(cid, target[, force = false])
	lua_register(luaState, "doChallengeCreature", LuaScriptInterface::luaDoChallengeCreature);

	// addEvent(callback, delay, ...)
	lua_register(luaState, "addEvent", LuaScriptInterface::luaAddEvent);

	// stopEvent(eventid)
	lua_register(luaState, "stopEvent", LuaScriptInterface::luaStopEvent);

	// saveServer()
	lua_register(luaState, "saveServer", LuaScriptInterface::luaSaveServer);

	// cleanMap()
	lua_register(luaState, "cleanMap", LuaScriptInterface::luaCleanMap);

	// debugPrint(text)
	lua_register(luaState, "debugPrint", LuaScriptInterface::luaDebugPrint);

	// logInfo(text)
	lua_register(luaState, "logInfo", LuaScriptInterface::luaLogInfo);

	// logMigration(text)
	lua_register(luaState, "logMigration", LuaScriptInterface::luaLogMigration);

	// logWarning(text)
	lua_register(luaState, "logWarning", LuaScriptInterface::luaLogWarning);

	// logError(text)
	lua_register(luaState, "logError", LuaScriptInterface::luaLogError);

	// isInWar(cid, target)
	lua_register(luaState, "isInWar", LuaScriptInterface::luaIsInWar);

	// getWaypointPosition(name)
	lua_register(luaState, "getWaypointPositionByName", LuaScriptInterface::luaGetWaypointPositionByName);

	// sendChannelMessage(channelId, type, message)
	lua_register(luaState, "sendChannelMessage", LuaScriptInterface::luaSendChannelMessage);

	// sendGuildChannelMessage(guildId, type, message)
	lua_register(luaState, "sendGuildChannelMessage", LuaScriptInterface::luaSendGuildChannelMessage);

	// isScriptsInterface()
	lua_register(luaState, "isScriptsInterface", LuaScriptInterface::luaIsScriptsInterface);

	// configManager table
	luaL_register(luaState, "configManager", LuaScriptInterface::luaConfigManagerTable);
	lua_pop(luaState, 1);

	// db table
	luaL_register(luaState, "db", LuaScriptInterface::luaDatabaseTable);
	lua_pop(luaState, 1);

	// result table
	luaL_register(luaState, "result", LuaScriptInterface::luaResultTable);
	lua_pop(luaState, 1);

	/* New functions */
	// registerClass(className, baseClass, newFunction)
	// registerTable(tableName)
	// registerMethod(className, functionName, function)
	// registerMetaMethod(className, functionName, function)
	// registerGlobalMethod(functionName, function)
	// registerVariable(tableName, name, value)
	// registerGlobalVariable(name, value)
	// registerEnum(value)
	// registerEnumIn(tableName, value)
	// registerEnumClass(value)

	// Enums
	registerEnum(ACCOUNT_TYPE_NORMAL);
	registerEnum(ACCOUNT_TYPE_TUTOR);
	registerEnum(ACCOUNT_TYPE_SENIORTUTOR);
	registerEnum(ACCOUNT_TYPE_GAMEMASTER);
	registerEnum(ACCOUNT_TYPE_COMMUNITYMANAGER);
	registerEnum(ACCOUNT_TYPE_GOD);

	registerGlobalVariable("RESET_REDUCTION_PERCENTAGE", ConfigManager::RESET_REDUCTION_PERCENTAGE);
	registerGlobalVariable("RESET_SYSTEM_ENABLED", ConfigManager::RESET_SYSTEM_ENABLED);
	registerGlobalVariable("RESET_LEVEL", ConfigManager::RESET_LEVEL);

	registerGlobalVariable("AUTOLOOT_MAXITEMS_FREE", ConfigManager::AUTOLOOT_MAXITEMS_FREE);
	registerGlobalVariable("AUTOLOOT_MAXITEMS_PREMIUM", ConfigManager::AUTOLOOT_MAXITEMS_PREMIUM);

	registerGlobalVariable("ACCOUNT_MANAGER_NONE", static_cast<uint8_t>(AccountManagerMode::ACCOUNT_MANAGER_NONE));
	registerGlobalVariable("ACCOUNT_MANAGER_NEW", static_cast<uint8_t>(AccountManagerMode::ACCOUNT_MANAGER_NEW));
	registerGlobalVariable("ACCOUNT_MANAGER_ACCOUNT",
	                       static_cast<uint8_t>(AccountManagerMode::ACCOUNT_MANAGER_ACCOUNT));
	registerGlobalVariable("ACCOUNT_MANAGER_NAMELOCK",
	                       static_cast<uint8_t>(AccountManagerMode::ACCOUNT_MANAGER_NAMELOCK));

	registerEnum(AMMO_NONE);
	registerEnum(AMMO_BOLT);
	registerEnum(AMMO_ARROW);
	registerEnum(AMMO_SPEAR);
	registerEnum(AMMO_THROWINGSTAR);
	registerEnum(AMMO_THROWINGKNIFE);
	registerEnum(AMMO_STONE);
	registerEnum(AMMO_SNOWBALL);

	registerEnum(BUG_CATEGORY_MAP);
	registerEnum(BUG_CATEGORY_TYPO);
	registerEnum(BUG_CATEGORY_TECHNICAL);
	registerEnum(BUG_CATEGORY_OTHER);

	// CallBackParam
	registerTable("CallBackParam");

	registerEnumClass(CallBackParam::LEVELMAGICVALUE);
	registerEnumClass(CallBackParam::SKILLVALUE);
	registerEnumClass(CallBackParam::TARGETTILE);
	registerEnumClass(CallBackParam::TARGETCREATURE);

	// Global aliases for backwards compatibility
	registerGlobalVariable("CALLBACK_PARAM_LEVELMAGICVALUE", static_cast<int64_t>(CallBackParam::LEVELMAGICVALUE));
	registerGlobalVariable("CALLBACK_PARAM_SKILLVALUE", static_cast<int64_t>(CallBackParam::SKILLVALUE));
	registerGlobalVariable("CALLBACK_PARAM_TARGETTILE", static_cast<int64_t>(CallBackParam::TARGETTILE));
	registerGlobalVariable("CALLBACK_PARAM_TARGETCREATURE", static_cast<int64_t>(CallBackParam::TARGETCREATURE));

	// ExperienceRateType
	registerTable("ExperienceRateType");

	registerEnumClass(ExperienceRateType::BASE);
	registerEnumClass(ExperienceRateType::LOW_LEVEL);
	registerEnumClass(ExperienceRateType::BONUS);
	registerEnumClass(ExperienceRateType::STAMINA);

	// Combat Formula
	registerEnum(COMBAT_FORMULA_UNDEFINED);
	registerEnum(COMBAT_FORMULA_LEVELMAGIC);
	registerEnum(COMBAT_FORMULA_SKILL);
	registerEnum(COMBAT_FORMULA_DAMAGE);

	// Direction
	registerEnum(DIRECTION_NORTH);
	registerEnum(DIRECTION_EAST);
	registerEnum(DIRECTION_SOUTH);
	registerEnum(DIRECTION_WEST);
	registerEnum(DIRECTION_SOUTHWEST);
	registerEnum(DIRECTION_SOUTHEAST);
	registerEnum(DIRECTION_NORTHWEST);
	registerEnum(DIRECTION_NORTHEAST);

	registerEnum(COMBAT_NONE);
	registerEnum(COMBAT_PHYSICALDAMAGE);
	registerEnum(COMBAT_ENERGYDAMAGE);
	registerEnum(COMBAT_EARTHDAMAGE);
	registerEnum(COMBAT_FIREDAMAGE);
	registerEnum(COMBAT_UNDEFINEDDAMAGE);
	registerEnum(COMBAT_LIFEDRAIN);
	registerEnum(COMBAT_MANADRAIN);
	registerEnum(COMBAT_HEALING);
	registerEnum(COMBAT_DROWNDAMAGE);
	registerEnum(COMBAT_ICEDAMAGE);
	registerEnum(COMBAT_HOLYDAMAGE);
	registerEnum(COMBAT_DEATHDAMAGE);

	registerEnum(COMBAT_PARAM_TYPE);
	registerEnum(COMBAT_PARAM_EFFECT);
	registerEnum(COMBAT_PARAM_DISTANCEEFFECT);
	registerEnum(COMBAT_PARAM_BLOCKSHIELD);
	registerEnum(COMBAT_PARAM_BLOCKARMOR);
	registerEnum(COMBAT_PARAM_TARGETCASTERORTOPMOST);
	registerEnum(COMBAT_PARAM_CREATEITEM);
	registerEnum(COMBAT_PARAM_AGGRESSIVE);
	registerEnum(COMBAT_PARAM_DISPEL);
	registerEnum(COMBAT_PARAM_USECHARGES);

	registerEnum(CONDITION_NONE);
	registerEnum(CONDITION_POISON);
	registerEnum(CONDITION_FIRE);
	registerEnum(CONDITION_ENERGY);
	registerEnum(CONDITION_BLEEDING);
	registerEnum(CONDITION_HASTE);
	registerEnum(CONDITION_PARALYZE);
	registerEnum(CONDITION_OUTFIT);
	registerEnum(CONDITION_INVISIBLE);
	registerEnum(CONDITION_LIGHT);
	registerEnum(CONDITION_MANASHIELD);
	registerEnum(CONDITION_INFIGHT);
	registerEnum(CONDITION_DRUNK);
	registerEnum(CONDITION_EXHAUST_WEAPON);
	registerEnum(CONDITION_REGENERATION);
	registerEnum(CONDITION_SOUL);
	registerEnum(CONDITION_DROWN);
	registerEnum(CONDITION_MUTED);
	registerEnum(CONDITION_CHANNELMUTEDTICKS);
	registerEnum(CONDITION_YELLTICKS);
	registerEnum(CONDITION_ATTRIBUTES);
	registerEnum(CONDITION_FREEZING);
	registerEnum(CONDITION_DAZZLED);
	registerEnum(CONDITION_CURSED);
	registerEnum(CONDITION_EXHAUST_COMBAT);
	registerEnum(CONDITION_EXHAUST_HEAL);
	registerEnum(CONDITION_PACIFIED);
	registerEnum(CONDITION_CLIPORT);
	registerEnum(CONDITION_SPELLCOOLDOWN);
	registerEnum(CONDITION_SPELLGROUPCOOLDOWN);

	registerEnum(CONDITIONID_DEFAULT);
	registerEnum(CONDITIONID_COMBAT);
	registerEnum(CONDITIONID_HEAD);
	registerEnum(CONDITIONID_NECKLACE);
	registerEnum(CONDITIONID_BACKPACK);
	registerEnum(CONDITIONID_ARMOR);
	registerEnum(CONDITIONID_RIGHT);
	registerEnum(CONDITIONID_LEFT);
	registerEnum(CONDITIONID_LEGS);
	registerEnum(CONDITIONID_FEET);
	registerEnum(CONDITIONID_RING);
	registerEnum(CONDITIONID_AMMO);

	registerEnum(CONDITION_PARAM_OWNER);
	registerEnum(CONDITION_PARAM_TICKS);
	registerEnum(CONDITION_PARAM_DRUNKENNESS);
	registerEnum(CONDITION_PARAM_HEALTHGAIN);
	registerEnum(CONDITION_PARAM_HEALTHTICKS);
	registerEnum(CONDITION_PARAM_MANAGAIN);
	registerEnum(CONDITION_PARAM_MANATICKS);
	registerEnum(CONDITION_PARAM_DELAYED);
	registerEnum(CONDITION_PARAM_SPEED);
	registerEnum(CONDITION_PARAM_LIGHT_LEVEL);
	registerEnum(CONDITION_PARAM_LIGHT_COLOR);
	registerEnum(CONDITION_PARAM_SOULGAIN);
	registerEnum(CONDITION_PARAM_SOULTICKS);
	registerEnum(CONDITION_PARAM_MINVALUE);
	registerEnum(CONDITION_PARAM_MAXVALUE);
	registerEnum(CONDITION_PARAM_STARTVALUE);
	registerEnum(CONDITION_PARAM_TICKINTERVAL);
	registerEnum(CONDITION_PARAM_FORCEUPDATE);
	registerEnum(CONDITION_PARAM_SKILL_MELEE);
	registerEnum(CONDITION_PARAM_SKILL_FIST);
	registerEnum(CONDITION_PARAM_SKILL_CLUB);
	registerEnum(CONDITION_PARAM_SKILL_SWORD);
	registerEnum(CONDITION_PARAM_SKILL_AXE);
	registerEnum(CONDITION_PARAM_SKILL_DISTANCE);
	registerEnum(CONDITION_PARAM_SKILL_SHIELD);
	registerEnum(CONDITION_PARAM_SKILL_FISHING);
	registerEnum(CONDITION_PARAM_STAT_MAXHITPOINTS);
	registerEnum(CONDITION_PARAM_STAT_MAXMANAPOINTS);
	registerEnum(CONDITION_PARAM_STAT_MAGICPOINTS);
	registerEnum(CONDITION_PARAM_STAT_MAXHITPOINTSPERCENT);
	registerEnum(CONDITION_PARAM_STAT_MAXMANAPOINTSPERCENT);
	registerEnum(CONDITION_PARAM_STAT_MAGICPOINTSPERCENT);
	registerEnum(CONDITION_PARAM_PERIODICDAMAGE);
	registerEnum(CONDITION_PARAM_SKILL_MELEEPERCENT);
	registerEnum(CONDITION_PARAM_SKILL_FISTPERCENT);
	registerEnum(CONDITION_PARAM_SKILL_CLUBPERCENT);
	registerEnum(CONDITION_PARAM_SKILL_SWORDPERCENT);
	registerEnum(CONDITION_PARAM_SKILL_AXEPERCENT);
	registerEnum(CONDITION_PARAM_SKILL_DISTANCEPERCENT);
	registerEnum(CONDITION_PARAM_SKILL_SHIELDPERCENT);
	registerEnum(CONDITION_PARAM_SKILL_FISHINGPERCENT);
	registerEnum(CONDITION_PARAM_BUFF_SPELL);
	registerEnum(CONDITION_PARAM_SUBID);
	registerEnum(CONDITION_PARAM_FIELD);
	registerEnum(CONDITION_PARAM_DISABLE_DEFENSE);
	registerEnum(CONDITION_PARAM_SPECIALSKILL_CRITICALHITCHANCE);
	registerEnum(CONDITION_PARAM_SPECIALSKILL_CRITICALHITAMOUNT);
	registerEnum(CONDITION_PARAM_SPECIALSKILL_LIFELEECHCHANCE);
	registerEnum(CONDITION_PARAM_SPECIALSKILL_LIFELEECHAMOUNT);
	registerEnum(CONDITION_PARAM_SPECIALSKILL_MANALEECHCHANCE);
	registerEnum(CONDITION_PARAM_SPECIALSKILL_MANALEECHAMOUNT);
	registerEnum(CONDITION_PARAM_AGGRESSIVE);

	registerEnum(RETURNVALUE_CANNOTMOVEEXERCISEWEAPON);
	registerEnum(RETURNVALUE_CANNOTMOVEGOLDPOUCH);
	registerEnum(RETURNVALUE_QUIVERAMMOONLY);

	registerEnum(CONST_ME_NONE);
	registerEnum(CONST_ME_DRAWBLOOD);
	registerEnum(CONST_ME_LOSEENERGY);
	registerEnum(CONST_ME_POFF);
	registerEnum(CONST_ME_BLOCKHIT);
	registerEnum(CONST_ME_EXPLOSIONAREA);
	registerEnum(CONST_ME_EXPLOSIONHIT);
	registerEnum(CONST_ME_FIREAREA);
	registerEnum(CONST_ME_YELLOW_RINGS);
	registerEnum(CONST_ME_GREEN_RINGS);
	registerEnum(CONST_ME_HITAREA);
	registerEnum(CONST_ME_TELEPORT);
	registerEnum(CONST_ME_ENERGYHIT);
	registerEnum(CONST_ME_MAGIC_BLUE);
	registerEnum(CONST_ME_MAGIC_RED);
	registerEnum(CONST_ME_MAGIC_GREEN);
	registerEnum(CONST_ME_HITBYFIRE);
	registerEnum(CONST_ME_HITBYPOISON);
	registerEnum(CONST_ME_MORTAREA);
	registerEnum(CONST_ME_SOUND_GREEN);
	registerEnum(CONST_ME_SOUND_RED);
	registerEnum(CONST_ME_POISONAREA);
	registerEnum(CONST_ME_SOUND_YELLOW);
	registerEnum(CONST_ME_SOUND_PURPLE);
	registerEnum(CONST_ME_SOUND_BLUE);
	registerEnum(CONST_ME_SOUND_WHITE);
	registerEnum(CONST_ME_BUBBLES);
	registerEnum(CONST_ME_CRAPS);
	registerEnum(CONST_ME_GIFT_WRAPS);
	registerEnum(CONST_ME_FIREWORK_YELLOW);
	registerEnum(CONST_ME_FIREWORK_RED);
	registerEnum(CONST_ME_FIREWORK_BLUE);
	registerEnum(CONST_ME_STUN);
	registerEnum(CONST_ME_SLEEP);
	registerEnum(CONST_ME_WATERCREATURE);
	registerEnum(CONST_ME_GROUNDSHAKER);
	registerEnum(CONST_ME_HEARTS);
	registerEnum(CONST_ME_FIREATTACK);
	registerEnum(CONST_ME_ENERGYAREA);
	registerEnum(CONST_ME_SMALLCLOUDS);
	registerEnum(CONST_ME_HOLYDAMAGE);
	registerEnum(CONST_ME_BIGCLOUDS);
	registerEnum(CONST_ME_ICEAREA);
	registerEnum(CONST_ME_ICETORNADO);
	registerEnum(CONST_ME_ICEATTACK);
	registerEnum(CONST_ME_STONES);
	registerEnum(CONST_ME_SMALLPLANTS);
	registerEnum(CONST_ME_CARNIPHILA);
	registerEnum(CONST_ME_PURPLEENERGY);
	registerEnum(CONST_ME_YELLOWENERGY);
	registerEnum(CONST_ME_HOLYAREA);
	registerEnum(CONST_ME_BIGPLANTS);
	registerEnum(CONST_ME_CAKE);
	registerEnum(CONST_ME_GIANTICE);
	registerEnum(CONST_ME_WATERSPLASH);
	registerEnum(CONST_ME_PLANTATTACK);
	registerEnum(CONST_ME_TUTORIALARROW);
	registerEnum(CONST_ME_TUTORIALSQUARE);
	registerEnum(CONST_ME_MIRRORHORIZONTAL);
	registerEnum(CONST_ME_MIRRORVERTICAL);
	registerEnum(CONST_ME_SKULLHORIZONTAL);
	registerEnum(CONST_ME_SKULLVERTICAL);
	registerEnum(CONST_ME_ASSASSIN);
	registerEnum(CONST_ME_STEPSHORIZONTAL);
	registerEnum(CONST_ME_BLOODYSTEPS);
	registerEnum(CONST_ME_STEPSVERTICAL);
	registerEnum(CONST_ME_YALAHARIGHOST);
	registerEnum(CONST_ME_BATS);
	registerEnum(CONST_ME_SMOKE);
	registerEnum(CONST_ME_INSECTS);
	registerEnum(CONST_ME_DRAGONHEAD);

	registerEnum(CONST_ME_ORCSHAMAN);
	registerEnum(CONST_ME_ORCSHAMAN_FIRE);
	registerEnum(CONST_ME_THUNDER);
	registerEnum(CONST_ME_FERUMBRAS);
	registerEnum(CONST_ME_CONFETTI_HORIZONTAL);
	registerEnum(CONST_ME_CONFETTI_VERTICAL);
	registerEnum(CONST_ME_BLACKSMOKE);
	registerEnum(CONST_ME_REDSMOKE);
	registerEnum(CONST_ME_YELLOWSMOKE);
	registerEnum(CONST_ME_GREENSMOKE);
	registerEnum(CONST_ME_PURPLESMOKE);
	registerEnum(CONST_ME_LIGHTNING);
	registerEnum(CONST_ME_RAGIAZ_BONE_CAPSULE);
	registerEnum(CONST_ME_CRITICAL_HIT);
	registerEnum(CONST_ME_PLUNGING_FISH);
	registerEnum(CONST_ME_BLUE_ENERGY_SPARK);
	registerEnum(CONST_ME_ORANGE_ENERGY_SPARK) registerEnum(CONST_ME_GREEN_ENERGY_SPARK);
	registerEnum(CONST_ME_PINK_ENERGY_SPARK);
	registerEnum(CONST_ME_WHITE_ENERGY_SPARK);
	registerEnum(CONST_ME_YELLOW_ENERGY_SPARK);
	registerEnum(CONST_ME_MAGIC_POWDER);
	registerEnum(CONST_ME_PIXIE_EXPLOSION);
	registerEnum(CONST_ME_PIXIE_COMING);
	registerEnum(CONST_ME_PIXIE_GOING);
	registerEnum(CONST_ME_STORM);
	registerEnum(CONST_ME_STONE_STORM);
	registerEnum(CONST_ME_BLUE_GHOST);
	registerEnum(CONST_ME_PINK_VORTEX);
	registerEnum(CONST_ME_TREASURE_MAP);
	registerEnum(CONST_ME_PINK_BEAM);
	registerEnum(CONST_ME_GREEN_FIREWORKS);
	registerEnum(CONST_ME_ORANGE_FIREWORKS);
	registerEnum(CONST_ME_PINK_FIREWORKS);
	registerEnum(CONST_ME_BLUE_FIREWORKS);
	registerEnum(CONST_ME_BLACK_BLOOD);
	registerEnum(CONST_ME_SUPREME_CUBE);
	registerEnum(CONST_ME_PRISMATIC_SPARK);
	registerEnum(CONST_ME_THAIAN);
	registerEnum(CONST_ME_THAIAN_GHOST);
	registerEnum(CONST_ME_GHOST_SMOKE);
	registerEnum(CONST_ME_WATER_BLOCK_FLOATING);
	registerEnum(CONST_ME_WATER_BLOCK);
	registerEnum(CONST_ME_ROOTS);
	registerEnum(CONST_ME_GHOSTLY_SCRATCH);
	registerEnum(CONST_ME_GHOSTLY_BITE);
	registerEnum(CONST_ME_BIG_SCRATCH);
	registerEnum(CONST_ME_SLASH);
	registerEnum(CONST_ME_BITE);
	registerEnum(CONST_ME_CHIVALRIOUS_CHALLENGE);
	registerEnum(CONST_ME_DIVINE_DAZZLE);
	registerEnum(CONST_ME_ELECTRICALSPARK);
	registerEnum(CONST_ME_PURPLETELEPORT);
	registerEnum(CONST_ME_REDTELEPORT);
	registerEnum(CONST_ME_ORANGETELEPORT);
	registerEnum(CONST_ME_GREYTELEPORT);
	registerEnum(CONST_ME_LIGHTBLUETELEPORT);
	registerEnum(CONST_ME_FATAL);
	registerEnum(CONST_ME_DODGE);
	registerEnum(CONST_ME_HOURGLASS);
	registerEnum(CONST_ME_DAZZLING);
	registerEnum(CONST_ME_FERUMBRAS_1);
	registerEnum(CONST_ME_GAZHARAGOTH);
	registerEnum(CONST_ME_MAD_MAGE);
	registerEnum(CONST_ME_HORESTIS);
	registerEnum(CONST_ME_DEVOVORGA);
	registerEnum(CONST_ME_FERUMBRAS_2);
	registerEnum(CONST_ME_WHITE_SMOKE);
	registerEnum(CONST_ME_WHITE_SMOKES);
	registerEnum(CONST_ME_WATER_DROP);
	registerEnum(CONST_ME_AVATAR_APPEAR);
	registerEnum(CONST_ME_DIVINE_GRENADE);
	registerEnum(CONST_ME_DIVINE_EMPOWERMENT);
	registerEnum(CONST_ME_WATER_FLOATING_THRASH);
	registerEnum(CONST_ME_AGONY);
	registerEnum(CONST_ME_LOOT_HIGHLIGHT);
	registerEnum(CONST_ME_MELTING_CREAM);
	registerEnum(CONST_ME_REAPER);
	registerEnum(CONST_ME_POWERFUL_HEARTS);
	registerEnum(CONST_ME_CREAM);
	registerEnum(CONST_ME_GENTLE_BUBBLE);
	registerEnum(CONST_ME_STARBURST);
	registerEnum(CONST_ME_SIURP);
	registerEnum(CONST_ME_CACAO);
	registerEnum(CONST_ME_CANDY_FLOSS);

	// Avatars
	registerEnum(AVATAR_LOOKTYPE_STEEL);
	registerEnum(AVATAR_LOOKTYPE_LIGHT);
	registerEnum(AVATAR_LOOKTYPE_STORM);
	registerEnum(AVATAR_LOOKTYPE_NATURE);
	registerEnum(AVATAR_LOOKTYPE_BALANCE);

	// 15.00 effects
	registerEnum(CONST_ME_GREEN_HITAREA);
	registerEnum(CONST_ME_RED_HITAREA);
	registerEnum(CONST_ME_BLUE_HITAREA);
	registerEnum(CONST_ME_YELLOW_HITAREA);
	registerEnum(CONST_ME_WHITE_FLURRYOFBLOWS);
	registerEnum(CONST_ME_GREEN_FLURRYOFBLOWS);
	registerEnum(CONST_ME_PINK_FLURRYOFBLOWS);
	registerEnum(CONST_ME_WHITE_ENERGYPULSE);
	registerEnum(CONST_ME_GREEN_ENERGYPULSE);
	registerEnum(CONST_ME_PINK_ENERGYPULSE);
	registerEnum(CONST_ME_WHITE_TIGERCLASH);
	registerEnum(CONST_ME_GREEN_TIGERCLASH);
	registerEnum(CONST_ME_PINK_TIGERCLASH);
	registerEnum(CONST_ME_WHITE_EXPLOSIONHIT);
	registerEnum(CONST_ME_GREEN_EXPLOSIONHIT);
	registerEnum(CONST_ME_BLUE_EXPLOSIONHIT);
	registerEnum(CONST_ME_PINK_EXPLOSIONHIT);
	registerEnum(CONST_ME_WHITE_ENERGYSHOCK);
	registerEnum(CONST_ME_GREEN_ENERGYSHOCK);
	registerEnum(CONST_ME_YELLOW_ENERGYSHOCK);

	registerEnum(CONST_ANI_NONE);
	registerEnum(CONST_ANI_SPEAR);
	registerEnum(CONST_ANI_BOLT);
	registerEnum(CONST_ANI_ARROW);
	registerEnum(CONST_ANI_FIRE);
	registerEnum(CONST_ANI_ENERGY);
	registerEnum(CONST_ANI_POISONARROW);
	registerEnum(CONST_ANI_BURSTARROW);
	registerEnum(CONST_ANI_THROWINGSTAR);
	registerEnum(CONST_ANI_THROWINGKNIFE);
	registerEnum(CONST_ANI_SMALLSTONE);
	registerEnum(CONST_ANI_DEATH);
	registerEnum(CONST_ANI_LARGEROCK);
	registerEnum(CONST_ANI_SNOWBALL);
	registerEnum(CONST_ANI_POWERBOLT);
	registerEnum(CONST_ANI_POISON);
	registerEnum(CONST_ANI_INFERNALBOLT);
	registerEnum(CONST_ANI_HUNTINGSPEAR);
	registerEnum(CONST_ANI_ENCHANTEDSPEAR);
	registerEnum(CONST_ANI_REDSTAR);
	registerEnum(CONST_ANI_GREENSTAR);
	registerEnum(CONST_ANI_ROYALSPEAR);
	registerEnum(CONST_ANI_SNIPERARROW);
	registerEnum(CONST_ANI_ONYXARROW);
	registerEnum(CONST_ANI_PIERCINGBOLT);
	registerEnum(CONST_ANI_WHIRLWINDSWORD);
	registerEnum(CONST_ANI_WHIRLWINDAXE);
	registerEnum(CONST_ANI_WHIRLWINDCLUB);
	registerEnum(CONST_ANI_ETHEREALSPEAR);
	registerEnum(CONST_ANI_ICE);
	registerEnum(CONST_ANI_EARTH);
	registerEnum(CONST_ANI_HOLY);
	registerEnum(CONST_ANI_SUDDENDEATH);
	registerEnum(CONST_ANI_FLASHARROW);
	registerEnum(CONST_ANI_FLAMMINGARROW);
	registerEnum(CONST_ANI_SHIVERARROW);
	registerEnum(CONST_ANI_ENERGYBALL);
	registerEnum(CONST_ANI_SMALLICE);
	registerEnum(CONST_ANI_SMALLHOLY);
	registerEnum(CONST_ANI_SMALLEARTH);
	registerEnum(CONST_ANI_EARTHARROW);
	registerEnum(CONST_ANI_EXPLOSION);
	registerEnum(CONST_ANI_CAKE);
	registerEnum(CONST_ANI_ENVENOMEDARROW);
	registerEnum(CONST_ANI_GLOOTHSPEAR);
	registerEnum(CONST_ANI_SIMPLEARROW);
	registerEnum(CONST_ANI_LEAFSTAR);
	registerEnum(CONST_ANI_PRISMATICBOLT);
	registerEnum(CONST_ANI_DRILLBOLT);
	registerEnum(CONST_ANI_VORTEXBOLT);
	registerEnum(CONST_ANI_TARSALARROW);
	registerEnum(CONST_ANI_CRYSTALLINEARROW);
	registerEnum(CONST_ANI_DIAMONDARROW);
	registerEnum(CONST_ANI_SPECTRALBOLT);
	registerEnum(CONST_ANI_ROYALSTAR);
	registerEnum(CONST_ANI_CANDYCANE);
	registerEnum(CONST_ANI_CHERRYBOMB);
	registerEnum(CONST_ANI_WEAPONTYPE);

	registerEnum(CONST_PROP_BLOCKSOLID);
	registerEnum(CONST_PROP_HASHEIGHT);
	registerEnum(CONST_PROP_BLOCKPROJECTILE);
	registerEnum(CONST_PROP_BLOCKPATH);
	registerEnum(CONST_PROP_ISVERTICAL);
	registerEnum(CONST_PROP_ISHORIZONTAL);
	registerEnum(CONST_PROP_MOVEABLE);
	registerEnum(CONST_PROP_IMMOVABLEBLOCKSOLID);
	registerEnum(CONST_PROP_IMMOVABLEBLOCKPATH);
	registerEnum(CONST_PROP_IMMOVABLENOFIELDBLOCKPATH);
	registerEnum(CONST_PROP_NOFIELDBLOCKPATH);
	registerEnum(CONST_PROP_SUPPORTHANGABLE);

	registerEnum(CONST_SLOT_HEAD);
	registerEnum(CONST_SLOT_NECKLACE);
	registerEnum(CONST_SLOT_BACKPACK);
	registerEnum(CONST_SLOT_ARMOR);
	registerEnum(CONST_SLOT_RIGHT);
	registerEnum(CONST_SLOT_LEFT);
	registerEnum(CONST_SLOT_LEGS);
	registerEnum(CONST_SLOT_FEET);
	registerEnum(CONST_SLOT_RING);
	registerEnum(CONST_SLOT_AMMO);

	registerEnum(CREATURE_EVENT_NONE);
	registerEnum(CREATURE_EVENT_LOGIN);
	registerEnum(CREATURE_EVENT_LOGOUT);
	registerEnum(CREATURE_EVENT_RECONNECT);
	registerEnum(CREATURE_EVENT_THINK);
	registerEnum(CREATURE_EVENT_PREPAREDEATH);
	registerEnum(CREATURE_EVENT_DEATH);
	registerEnum(CREATURE_EVENT_KILL);
	registerEnum(CREATURE_EVENT_ADVANCE);
	registerEnum(CREATURE_EVENT_TEXTEDIT);
	registerEnum(CREATURE_EVENT_HEALTHCHANGE);
	registerEnum(CREATURE_EVENT_MANACHANGE);
	registerEnum(CREATURE_EVENT_EXTENDED_OPCODE);

	registerEnum(GAME_STATE_STARTUP);
	registerEnum(GAME_STATE_INIT);
	registerEnum(GAME_STATE_NORMAL);
	registerEnum(GAME_STATE_CLOSED);
	registerEnum(GAME_STATE_SHUTDOWN);
	registerEnum(GAME_STATE_CLOSING);
	registerEnum(GAME_STATE_MAINTAIN);

	registerEnum(MESSAGE_STATUS_CONSOLE_BLUE);
	registerEnum(MESSAGE_STATUS_CONSOLE_RED);
	registerEnum(MESSAGE_STATUS_DEFAULT);
	registerEnum(MESSAGE_STATUS_WARNING);
	registerEnum(MESSAGE_EVENT_ADVANCE);
	registerEnum(MESSAGE_STATUS_SMALL);
	registerEnum(MESSAGE_INFO_DESCR);
	registerEnum(MESSAGE_EVENT_DEFAULT);
	registerEnum(MESSAGE_EVENT_ORANGE);
	registerEnum(MESSAGE_STATUS_CONSOLE_ORANGE);

	registerEnum(CREATURETYPE_PLAYER);
	registerEnum(CREATURETYPE_MONSTER);
	registerEnum(CREATURETYPE_NPC);
	registerEnum(CREATURETYPE_SUMMON_OWN);
	registerEnum(CREATURETYPE_SUMMON_OTHERS);

	registerEnum(CLIENTOS_LINUX);
	registerEnum(CLIENTOS_WINDOWS);
	registerEnum(CLIENTOS_FLASH);
	registerEnum(CLIENTOS_OTCLIENT_LINUX);
	registerEnum(CLIENTOS_OTCLIENT_WINDOWS);
	registerEnum(CLIENTOS_OTCLIENT_MAC);

	registerEnum(CLIENTOS_OTCLIENTV8_LINUX);
	registerEnum(CLIENTOS_OTCLIENTV8_WINDOWS);
	registerEnum(CLIENTOS_OTCLIENTV8_MAC);
	registerEnum(CLIENTOS_OTCLIENTV8_ANDROID);
	registerEnum(CLIENTOS_OTCLIENTV8_IOS);
	registerEnum(CLIENTOS_OTCLIENTV8_WEB);

	registerEnum(FIGHTMODE_ATTACK);
	registerEnum(FIGHTMODE_BALANCED);
	registerEnum(FIGHTMODE_DEFENSE);

	registerEnum(ITEM_ATTRIBUTE_NONE);
	registerEnum(ITEM_ATTRIBUTE_ACTIONID);
	registerEnum(ITEM_ATTRIBUTE_UNIQUEID);
	registerEnum(ITEM_ATTRIBUTE_DESCRIPTION);
	registerEnum(ITEM_ATTRIBUTE_TEXT);
	registerEnum(ITEM_ATTRIBUTE_DATE);
	registerEnum(ITEM_ATTRIBUTE_WRITER);
	registerEnum(ITEM_ATTRIBUTE_NAME);
	registerEnum(ITEM_ATTRIBUTE_ARTICLE);
	registerEnum(ITEM_ATTRIBUTE_PLURALNAME);
	registerEnum(ITEM_ATTRIBUTE_WEIGHT);
	registerEnum(ITEM_ATTRIBUTE_ATTACK);
	registerEnum(ITEM_ATTRIBUTE_DEFENSE);
	registerEnum(ITEM_ATTRIBUTE_EXTRADEFENSE);
	registerEnum(ITEM_ATTRIBUTE_ARMOR);
	registerEnum(ITEM_ATTRIBUTE_HITCHANCE);
	registerEnum(ITEM_ATTRIBUTE_SHOOTRANGE);
	registerEnum(ITEM_ATTRIBUTE_OWNER);
	registerEnum(ITEM_ATTRIBUTE_DURATION);
	registerEnum(ITEM_ATTRIBUTE_DECAYSTATE);
	registerEnum(ITEM_ATTRIBUTE_CORPSEOWNER);
	registerEnum(ITEM_ATTRIBUTE_CHARGES);
	registerEnum(ITEM_ATTRIBUTE_FLUIDTYPE);
	registerEnum(ITEM_ATTRIBUTE_DOORID);
	registerEnum(ITEM_ATTRIBUTE_DURATION_TIMESTAMP);
	registerEnum(ITEM_ATTRIBUTE_WRAPID);
	registerEnum(ITEM_ATTRIBUTE_STOREITEM);
	registerEnum(ITEM_ATTRIBUTE_ATTACK_SPEED);
	registerEnum(ITEM_ATTRIBUTE_CLASSIFICATION);
	registerEnum(ITEM_ATTRIBUTE_TIER);
	registerEnum(ITEM_ATTRIBUTE_REWARDID);
	registerEnum(ITEM_ATTRIBUTE_DURATION_MIN);
	// registerEnum(ITEM_ATTRIBUTE_DURATION_MAX); // Removed due to overflow

	registerEnum(ITEM_TYPE_DEPOT);
	registerEnum(ITEM_TYPE_MAILBOX);
	registerEnum(ITEM_TYPE_TRASHHOLDER);
	registerEnum(ITEM_TYPE_CONTAINER);
	registerEnum(ITEM_TYPE_DOOR);
	registerEnum(ITEM_TYPE_MAGICFIELD);
	registerEnum(ITEM_TYPE_TELEPORT);
	registerEnum(ITEM_TYPE_BED);
	registerEnum(ITEM_TYPE_KEY);
	registerEnum(ITEM_TYPE_RUNE);

	registerEnum(ITEM_GROUP_GROUND);
	registerEnum(ITEM_GROUP_CONTAINER);
	registerEnum(ITEM_GROUP_WEAPON);
	registerEnum(ITEM_GROUP_AMMUNITION);
	registerEnum(ITEM_GROUP_ARMOR);
	registerEnum(ITEM_GROUP_CHARGES);
	registerEnum(ITEM_GROUP_TELEPORT);
	registerEnum(ITEM_GROUP_MAGICFIELD);
	registerEnum(ITEM_GROUP_WRITEABLE);
	registerEnum(ITEM_GROUP_KEY);
	registerEnum(ITEM_GROUP_SPLASH);
	registerEnum(ITEM_GROUP_FLUID);
	registerEnum(ITEM_GROUP_DOOR);
	registerEnum(ITEM_GROUP_DEPRECATED);

	registerEnum(ITEM_BAG);
	registerEnum(ITEM_GOLD_COIN);
	registerEnum(ITEM_PLATINUM_COIN);
	registerEnum(ITEM_CRYSTAL_COIN);
	registerEnum(ITEM_GOLD_NUGGET);
	registerEnum(ITEM_AMULETOFLOSS);
	registerEnum(ITEM_PARCEL);
	registerEnum(ITEM_LABEL);
	registerEnum(ITEM_INBOX);
	registerEnum(ITEM_FIREFIELD_PVP_FULL);
	registerEnum(ITEM_FIREFIELD_PVP_MEDIUM);
	registerEnum(ITEM_FIREFIELD_PVP_SMALL);
	registerEnum(ITEM_FIREFIELD_PERSISTENT_FULL);
	registerEnum(ITEM_FIREFIELD_PERSISTENT_MEDIUM);
	registerEnum(ITEM_FIREFIELD_PERSISTENT_SMALL);
	registerEnum(ITEM_FIREFIELD_NOPVP);
	registerEnum(ITEM_POISONFIELD_PVP);
	registerEnum(ITEM_POISONFIELD_PERSISTENT);
	registerEnum(ITEM_POISONFIELD_NOPVP);
	registerEnum(ITEM_ENERGYFIELD_PVP);
	registerEnum(ITEM_ENERGYFIELD_PERSISTENT);
	registerEnum(ITEM_ENERGYFIELD_NOPVP);
	registerEnum(ITEM_MAGICWALL);
	registerEnum(ITEM_MAGICWALL_PERSISTENT);
	registerEnum(ITEM_MAGICWALL_SAFE);
	registerEnum(ITEM_WILDGROWTH);
	registerEnum(ITEM_WILDGROWTH_PERSISTENT);
	registerEnum(ITEM_WILDGROWTH_SAFE);
	registerEnum(ITEM_REWARD_CONTAINER);
	registerEnum(ITEM_LETTER);
	registerEnum(ITEM_LETTER_STAMPED);
	registerEnum(ITEM_DOCUMENT_RO);
	registerEnum(ITEM_RECEIPT_SUCCESS);
	registerEnum(ITEM_RECEIPT_FAIL);

	registerEnum(WIELDINFO_NONE);
	registerEnum(WIELDINFO_LEVEL);
	registerEnum(WIELDINFO_MAGLV);
	registerEnum(WIELDINFO_VOCREQ);
	registerEnum(WIELDINFO_PREMIUM);

	registerEnum(PlayerFlag_CannotUseCombat);
	registerEnum(PlayerFlag_CannotAttackPlayer);
	registerEnum(PlayerFlag_CannotAttackMonster);
	registerEnum(PlayerFlag_CannotBeAttacked);
	registerEnum(PlayerFlag_CanConvinceAll);
	registerEnum(PlayerFlag_CanSummonAll);
	registerEnum(PlayerFlag_CanIllusionAll);
	registerEnum(PlayerFlag_CanSenseInvisibility);
	registerEnum(PlayerFlag_IgnoredByMonsters);
	registerEnum(PlayerFlag_NotGainInFight);
	registerEnum(PlayerFlag_HasInfiniteMana);
	registerEnum(PlayerFlag_HasInfiniteSoul);
	registerEnum(PlayerFlag_HasNoExhaustion);
	registerEnum(PlayerFlag_CannotUseSpells);
	registerEnum(PlayerFlag_CannotPickupItem);
	registerEnum(PlayerFlag_CanAlwaysLogin);
	registerEnum(PlayerFlag_CanBroadcast);
	registerEnum(PlayerFlag_CanEditHouses);
	registerEnum(PlayerFlag_CannotBeBanned);
	registerEnum(PlayerFlag_CannotBePushed);
	registerEnum(PlayerFlag_HasInfiniteCapacity);
	registerEnum(PlayerFlag_CanPushAllCreatures);
	registerEnum(PlayerFlag_CanTalkRedPrivate);
	registerEnum(PlayerFlag_CanTalkRedChannel);
	registerEnum(PlayerFlag_TalkOrangeHelpChannel);
	registerEnum(PlayerFlag_NotGainExperience);
	registerEnum(PlayerFlag_NotGainMana);
	registerEnum(PlayerFlag_NotGainHealth);
	registerEnum(PlayerFlag_NotGainSkill);
	registerEnum(PlayerFlag_SetMaxSpeed);
	registerEnum(PlayerFlag_SpecialVIP);
	registerEnum(PlayerFlag_NotGenerateLoot);
	registerEnum(PlayerFlag_IgnoreProtectionZone);
	registerEnum(PlayerFlag_IgnoreSpellCheck);
	registerEnum(PlayerFlag_IgnoreWeaponCheck);
	registerEnum(PlayerFlag_CannotBeMuted);
	registerEnum(PlayerFlag_IsAlwaysPremium);
	registerEnum(PlayerFlag_IgnoreYellCheck);
	registerEnum(PlayerFlag_IgnoreSendPrivateCheck);
	registerEnum(PlayerFlag_CanThrowFar);

	registerEnum(PLAYERSEX_FEMALE);
	registerEnum(PLAYERSEX_MALE);

	registerEnum(REPORT_REASON_NAMEINAPPROPRIATE);
	registerEnum(REPORT_REASON_NAMEPOORFORMATTED);
	registerEnum(REPORT_REASON_NAMEADVERTISING);
	registerEnum(REPORT_REASON_NAMEUNFITTING);
	registerEnum(REPORT_REASON_NAMERULEVIOLATION);
	registerEnum(REPORT_REASON_INSULTINGSTATEMENT);
	registerEnum(REPORT_REASON_SPAMMING);
	registerEnum(REPORT_REASON_ADVERTISINGSTATEMENT);
	registerEnum(REPORT_REASON_UNFITTINGSTATEMENT);
	registerEnum(REPORT_REASON_LANGUAGESTATEMENT);
	registerEnum(REPORT_REASON_DISCLOSURE);
	registerEnum(REPORT_REASON_RULEVIOLATION);
	registerEnum(REPORT_REASON_STATEMENT_BUGABUSE);
	registerEnum(REPORT_REASON_UNOFFICIALSOFTWARE);
	registerEnum(REPORT_REASON_PRETENDING);
	registerEnum(REPORT_REASON_HARASSINGOWNERS);
	registerEnum(REPORT_REASON_FALSEINFO);
	registerEnum(REPORT_REASON_ACCOUNTSHARING);
	registerEnum(REPORT_REASON_STEALINGDATA);
	registerEnum(REPORT_REASON_SERVICEATTACKING);
	registerEnum(REPORT_REASON_SERVICEAGREEMENT);

	registerEnum(REPORT_TYPE_NAME);
	registerEnum(REPORT_TYPE_STATEMENT);
	registerEnum(REPORT_TYPE_BOT);

	registerEnum(VOCATION_NONE);

	registerEnum(SKILL_FIST);
	registerEnum(SKILL_CLUB);
	registerEnum(SKILL_SWORD);
	registerEnum(SKILL_AXE);
	registerEnum(SKILL_DISTANCE);
	registerEnum(SKILL_SHIELD);
	registerEnum(SKILL_FISHING);
	registerEnum(SKILL_MAGLEVEL);
	registerEnum(SKILL_LEVEL);

	registerEnum(SPECIALSKILL_CRITICALHITCHANCE);
	registerEnum(SPECIALSKILL_CRITICALHITAMOUNT);
	registerEnum(SPECIALSKILL_LIFELEECHCHANCE);
	registerEnum(SPECIALSKILL_LIFELEECHAMOUNT);
	registerEnum(SPECIALSKILL_MANALEECHCHANCE);
	registerEnum(SPECIALSKILL_MANALEECHAMOUNT);

	registerEnum(STAT_MAXHITPOINTS);
	registerEnum(STAT_MAXMANAPOINTS);
	registerEnum(STAT_SOULPOINTS);
	registerEnum(STAT_MAGICPOINTS);

	registerEnum(SKULL_NONE);
	registerEnum(SKULL_YELLOW);
	registerEnum(SKULL_GREEN);
	registerEnum(SKULL_WHITE);
	registerEnum(SKULL_RED);
	registerEnum(SKULL_BLACK);

	registerEnum(FLUID_NONE);
	registerEnum(FLUID_WATER);
	registerEnum(FLUID_BLOOD);
	registerEnum(FLUID_BEER);
	registerEnum(FLUID_SLIME);
	registerEnum(FLUID_LEMONADE);
	registerEnum(FLUID_MILK);
	registerEnum(FLUID_MANA);
	registerEnum(FLUID_LIFE);
	registerEnum(FLUID_OIL);
	registerEnum(FLUID_URINE);
	registerEnum(FLUID_COCONUTMILK);
	registerEnum(FLUID_WINE);
	registerEnum(FLUID_MUD);
	registerEnum(FLUID_FRUITJUICE);
	registerEnum(FLUID_LAVA);
	registerEnum(FLUID_RUM);
	registerEnum(FLUID_SWAMP);
	registerEnum(FLUID_TEA);
	registerEnum(FLUID_MEAD);

	registerEnum(GUILDEMBLEM_NONE) registerEnum(GUILDEMBLEM_ALLY) registerEnum(GUILDEMBLEM_ENEMY)
	    registerEnum(GUILDEMBLEM_NEUTRAL)

	        registerEnum(TALKTYPE_SAY);
	registerEnum(TALKTYPE_WHISPER);
	registerEnum(TALKTYPE_YELL);
	registerEnum(TALKTYPE_CHANNEL_Y);
	registerEnum(TALKTYPE_CHANNEL_O);
	registerEnum(TALKTYPE_PRIVATE_NP);
	registerEnum(TALKTYPE_PRIVATE_PN);
	registerEnum(TALKTYPE_BROADCAST);
	registerEnum(TALKTYPE_CHANNEL_R1);
	registerEnum(TALKTYPE_MONSTER_SAY);
	registerEnum(TALKTYPE_MONSTER_YELL);

	registerEnum(TEXTCOLOR_BLUE);
	registerEnum(TEXTCOLOR_LIGHTGREEN);
	registerEnum(TEXTCOLOR_LIGHTBLUE);
	registerEnum(TEXTCOLOR_MAYABLUE);
	registerEnum(TEXTCOLOR_DARKRED);
	registerEnum(TEXTCOLOR_PURPLE);
	registerEnum(TEXTCOLOR_RED);
	registerEnum(TEXTCOLOR_ORANGE);
	registerEnum(TEXTCOLOR_YELLOW);
	registerEnum(TEXTCOLOR_NONE);

	registerEnum(TILESTATE_NONE);
	registerEnum(TILESTATE_PROTECTIONZONE);
	registerEnum(TILESTATE_NOPVPZONE);
	registerEnum(TILESTATE_NOLOGOUT);
	registerEnum(TILESTATE_PVPZONE);
	registerEnum(TILESTATE_FLOORCHANGE);
	registerEnum(TILESTATE_FLOORCHANGE_DOWN);
	registerEnum(TILESTATE_FLOORCHANGE_NORTH);
	registerEnum(TILESTATE_FLOORCHANGE_SOUTH);
	registerEnum(TILESTATE_FLOORCHANGE_EAST);
	registerEnum(TILESTATE_FLOORCHANGE_WEST);
	registerEnum(TILESTATE_TELEPORT);
	registerEnum(TILESTATE_MAGICFIELD);
	registerEnum(TILESTATE_MAILBOX);
	registerEnum(TILESTATE_TRASHHOLDER);
	registerEnum(TILESTATE_BED);
	registerEnum(TILESTATE_DEPOT);
	registerEnum(TILESTATE_BLOCKSOLID);
	registerEnum(TILESTATE_BLOCKPATH);
	registerEnum(TILESTATE_IMMOVABLEBLOCKSOLID);
	registerEnum(TILESTATE_IMMOVABLEBLOCKPATH);
	registerEnum(TILESTATE_IMMOVABLENOFIELDBLOCKPATH);
	registerEnum(TILESTATE_NOFIELDBLOCKPATH);
	registerEnum(TILESTATE_FLOORCHANGE_SOUTH_ALT);
	registerEnum(TILESTATE_FLOORCHANGE_EAST_ALT);
	registerEnum(TILESTATE_SUPPORTS_HANGABLE);

	registerEnum(WEAPON_NONE);
	registerEnum(WEAPON_SWORD);
	registerEnum(WEAPON_CLUB);
	registerEnum(WEAPON_AXE);
	registerEnum(WEAPON_SHIELD);
	registerEnum(WEAPON_DISTANCE);
	registerEnum(WEAPON_WAND);
	registerEnum(WEAPON_AMMO);
	registerEnum(WEAPON_QUIVER);
	registerEnum(WEAPON_FIST);

	registerEnum(WORLD_TYPE_NO_PVP);
	registerEnum(WORLD_TYPE_PVP);
	registerEnum(WORLD_TYPE_PVP_ENFORCED);

	// Use with container:addItem, container:addItemEx and possibly other functions.
	registerEnum(FLAG_NOLIMIT);
	registerEnum(FLAG_IGNOREBLOCKITEM);
	registerEnum(FLAG_IGNOREBLOCKCREATURE);
	registerEnum(FLAG_CHILDISOWNER);
	registerEnum(FLAG_PATHFINDING);
	registerEnum(FLAG_IGNOREFIELDDAMAGE);
	registerEnum(FLAG_IGNORENOTMOVEABLE);
	registerEnum(FLAG_IGNOREAUTOSTACK);

	// Use with itemType:getSlotPosition
	registerEnum(SLOTP_WHEREEVER);
	registerEnum(SLOTP_HEAD);
	registerEnum(SLOTP_NECKLACE);
	registerEnum(SLOTP_BACKPACK);
	registerEnum(SLOTP_ARMOR);
	registerEnum(SLOTP_RIGHT);
	registerEnum(SLOTP_LEFT);
	registerEnum(SLOTP_LEGS);
	registerEnum(SLOTP_FEET);
	registerEnum(SLOTP_RING);
	registerEnum(SLOTP_AMMO);
	registerEnum(SLOTP_DEPOT);
	registerEnum(SLOTP_TWO_HAND);

	// Use with combat functions
	registerEnum(ORIGIN_NONE);
	registerEnum(ORIGIN_CONDITION);
	registerEnum(ORIGIN_SPELL);
	registerEnum(ORIGIN_MELEE);
	registerEnum(ORIGIN_RANGED);
	registerEnum(ORIGIN_WAND);

	// Use with house:getAccessList, house:setAccessList
	registerEnum(GUEST_LIST);
	registerEnum(SUBOWNER_LIST);
	// Use with house:getType
	registerEnum(HOUSE_TYPE_NORMAL);
	registerEnum(HOUSE_TYPE_GUILDHALL);

	// Use with player:getGuildLevel
	registerEnum(GUILDLEVEL_MEMBER);
	registerEnum(GUILDLEVEL_VICE);
	registerEnum(GUILDLEVEL_LEADER);

	// Use with player:addMapMark
	registerEnum(MAPMARK_TICK);
	registerEnum(MAPMARK_QUESTION);
	registerEnum(MAPMARK_EXCLAMATION);
	registerEnum(MAPMARK_STAR);
	registerEnum(MAPMARK_CROSS);
	registerEnum(MAPMARK_TEMPLE);
	registerEnum(MAPMARK_KISS);
	registerEnum(MAPMARK_SHOVEL);
	registerEnum(MAPMARK_SWORD);
	registerEnum(MAPMARK_FLAG);
	registerEnum(MAPMARK_LOCK);
	registerEnum(MAPMARK_BAG);
	registerEnum(MAPMARK_SKULL);
	registerEnum(MAPMARK_DOLLAR);
	registerEnum(MAPMARK_REDNORTH);
	registerEnum(MAPMARK_REDSOUTH);
	registerEnum(MAPMARK_REDEAST);
	registerEnum(MAPMARK_REDWEST);
	registerEnum(MAPMARK_GREENNORTH);
	registerEnum(MAPMARK_GREENSOUTH);

	// Use with Game.getReturnMessage
	registerEnum(RETURNVALUE_NOERROR);
	registerEnum(RETURNVALUE_NOTPOSSIBLE);
	registerEnum(RETURNVALUE_NOTENOUGHROOM);
	registerEnum(RETURNVALUE_PLAYERISPZLOCKED);
	registerEnum(RETURNVALUE_PLAYERISNOTINVITED);
	registerEnum(RETURNVALUE_ONLYGUILDMEMBERSMAYENTER);
	registerEnum(RETURNVALUE_CANNOTTHROW);
	registerEnum(RETURNVALUE_THEREISNOWAY);
	registerEnum(RETURNVALUE_DESTINATIONOUTOFREACH);
	registerEnum(RETURNVALUE_CREATUREBLOCK);
	registerEnum(RETURNVALUE_NOTMOVEABLE);
	registerEnum(RETURNVALUE_DROPTWOHANDEDITEM);
	registerEnum(RETURNVALUE_BOTHHANDSNEEDTOBEFREE);
	registerEnum(RETURNVALUE_CANONLYUSEONEWEAPON);
	registerEnum(RETURNVALUE_NEEDEXCHANGE);
	registerEnum(RETURNVALUE_CANNOTBEDRESSED);
	registerEnum(RETURNVALUE_PUTTHISOBJECTINYOURHAND);
	registerEnum(RETURNVALUE_PUTTHISOBJECTINBOTHHANDS);
	registerEnum(RETURNVALUE_TOOFARAWAY);
	registerEnum(RETURNVALUE_FIRSTGODOWNSTAIRS);
	registerEnum(RETURNVALUE_FIRSTGOUPSTAIRS);
	registerEnum(RETURNVALUE_CONTAINERNOTENOUGHROOM);
	registerEnum(RETURNVALUE_NOTENOUGHCAPACITY);
	registerEnum(RETURNVALUE_CANNOTPICKUP);
	registerEnum(RETURNVALUE_THISISIMPOSSIBLE);
	registerEnum(RETURNVALUE_DEPOTISFULL);
	registerEnum(RETURNVALUE_CREATUREDOESNOTEXIST);
	registerEnum(RETURNVALUE_CANNOTUSETHISOBJECT);
	registerEnum(RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE);
	registerEnum(RETURNVALUE_NOTREQUIREDLEVELTOUSERUNE);
	registerEnum(RETURNVALUE_YOUAREALREADYTRADING);
	registerEnum(RETURNVALUE_THISPLAYERISALREADYTRADING);
	registerEnum(RETURNVALUE_YOUMAYNOTLOGOUTDURINGAFIGHT);
	registerEnum(RETURNVALUE_DIRECTPLAYERSHOOT);
	registerEnum(RETURNVALUE_NOTENOUGHLEVEL);
	registerEnum(RETURNVALUE_NOTENOUGHMAGICLEVEL);
	registerEnum(RETURNVALUE_NOTENOUGHMANA);
	registerEnum(RETURNVALUE_NOTENOUGHSOUL);
	registerEnum(RETURNVALUE_YOUAREEXHAUSTED);
	registerEnum(RETURNVALUE_YOUCANNOTUSEOBJECTSTHATFAST);
	registerEnum(RETURNVALUE_PLAYERISNOTREACHABLE);
	registerEnum(RETURNVALUE_CANONLYUSETHISRUNEONCREATURES);
	registerEnum(RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE);
	registerEnum(RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER);
	registerEnum(RETURNVALUE_YOUMAYNOTATTACKAPERSONINPROTECTIONZONE);
	registerEnum(RETURNVALUE_YOUMAYNOTATTACKAPERSONWHILEINPROTECTIONZONE);
	registerEnum(RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE);
	registerEnum(RETURNVALUE_YOUCANONLYUSEITONCREATURES);
	registerEnum(RETURNVALUE_CREATUREISNOTREACHABLE);
	registerEnum(RETURNVALUE_TURNSECUREMODETOATTACKUNMARKEDPLAYERS);
	registerEnum(RETURNVALUE_YOUNEEDPREMIUMACCOUNT);
	registerEnum(RETURNVALUE_YOUNEEDTOLEARNTHISSPELL);
	registerEnum(RETURNVALUE_YOURVOCATIONCANNOTUSETHISSPELL);
	registerEnum(RETURNVALUE_YOUNEEDAWEAPONTOUSETHISSPELL);
	registerEnum(RETURNVALUE_PLAYERISPZLOCKEDLEAVEPVPZONE);
	registerEnum(RETURNVALUE_PLAYERISPZLOCKEDENTERPVPZONE);
	registerEnum(RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE);
	registerEnum(RETURNVALUE_YOUCANNOTLOGOUTHERE);
	registerEnum(RETURNVALUE_YOUNEEDAMAGICITEMTOCASTSPELL);
	registerEnum(RETURNVALUE_NAMEISTOOAMBIGUOUS);
	registerEnum(RETURNVALUE_CANONLYUSEONESHIELD);
	registerEnum(RETURNVALUE_NOPARTYMEMBERSINRANGE);
	registerEnum(RETURNVALUE_YOUARENOTTHEOWNER);
	registerEnum(RETURNVALUE_TRADEPLAYERFARAWAY);
	registerEnum(RETURNVALUE_YOUDONTOWNTHISHOUSE);
	registerEnum(RETURNVALUE_TRADEPLAYERALREADYOWNSAHOUSE);
	registerEnum(RETURNVALUE_TRADEPLAYERHIGHESTBIDDER);
	registerEnum(RETURNVALUE_YOUCANNOTTRADETHISHOUSE);
	registerEnum(RETURNVALUE_YOUDONTHAVEREQUIREDPROFESSION);
	registerEnum(RETURNVALUE_YOUCANNOTUSETHISBED);
	registerEnum(RETURNVALUE_TRADEPLAYERNOTINAGUILD);
	registerEnum(RETURNVALUE_TRADEGUILDALREADYOWNSAHOUSE);
	registerEnum(RETURNVALUE_TRADEPLAYERNOTGUILDLEADER);
	registerEnum(RETURNVALUE_YOUARENOTGUILDLEADER);

	registerEnum(RELOAD_TYPE_ALL);
	registerEnum(RELOAD_TYPE_ACTIONS);
	registerEnum(RELOAD_TYPE_CHAT);
	registerEnum(RELOAD_TYPE_CONFIG);
	registerEnum(RELOAD_TYPE_CREATURESCRIPTS);
	registerEnum(RELOAD_TYPE_EVENTS);
	registerEnum(RELOAD_TYPE_GLOBAL);
	registerEnum(RELOAD_TYPE_GLOBALEVENTS);
	registerEnum(RELOAD_TYPE_ITEMS);
	registerEnum(RELOAD_TYPE_MONSTERS);
	registerEnum(RELOAD_TYPE_MOUNTS);
	registerEnum(RELOAD_TYPE_MOVEMENTS);
	registerEnum(RELOAD_TYPE_NPCS);
	registerEnum(RELOAD_TYPE_QUESTS);
	registerEnum(RELOAD_TYPE_RAIDS);
	registerEnum(RELOAD_TYPE_SCRIPTS);
	registerEnum(RELOAD_TYPE_SPELLS);
	registerEnum(RELOAD_TYPE_TALKACTIONS);
	registerEnum(RELOAD_TYPE_WEAPONS);

	registerEnum(ZONE_PROTECTION);
	registerEnum(ZONE_NOPVP);
	registerEnum(ZONE_PVP);
	registerEnum(ZONE_NOLOGOUT);
	registerEnum(ZONE_NORMAL);

	registerEnum(MAX_LOOTCHANCE);

	registerEnum(SPELL_INSTANT);
	registerEnum(SPELL_RUNE);

	registerEnum(MONSTERS_EVENT_THINK);
	registerEnum(MONSTERS_EVENT_APPEAR);
	registerEnum(MONSTERS_EVENT_DISAPPEAR);
	registerEnum(MONSTERS_EVENT_MOVE);
	registerEnum(MONSTERS_EVENT_SAY);

	registerEnum(DECAYING_FALSE);
	registerEnum(DECAYING_TRUE);
	registerEnum(DECAYING_PENDING);

	registerEnum(VARIANT_NUMBER);
	registerEnum(VARIANT_POSITION);
	registerEnum(VARIANT_TARGETPOSITION);
	registerEnum(VARIANT_STRING);

	registerEnum(SPELLGROUP_NONE);
	registerEnum(SPELLGROUP_ATTACK);
	registerEnum(SPELLGROUP_HEALING);
	registerEnum(SPELLGROUP_SUPPORT);
	registerEnum(SPELLGROUP_SPECIAL);

	// _G
	registerGlobalVariable("INDEX_WHEREEVER", INDEX_WHEREEVER);
	registerGlobalBoolean("VIRTUAL_PARENT", true);

	registerGlobalMethod("isType", LuaScriptInterface::luaIsType);
	registerGlobalMethod("rawgetmetatable", LuaScriptInterface::luaRawGetMetatable);

	// configKeys
	registerTable("configKeys");

	registerEnumIn("configKeys", ConfigManager::ALLOW_CHANGEOUTFIT);
	registerEnumIn("configKeys", ConfigManager::ONE_PLAYER_ON_ACCOUNT);
	registerEnumIn("configKeys", ConfigManager::AIMBOT_HOTKEY_ENABLED);
	registerEnumIn("configKeys", ConfigManager::REMOVE_RUNE_CHARGES);
	registerEnumIn("configKeys", ConfigManager::REMOVE_WEAPON_AMMO);
	registerEnumIn("configKeys", ConfigManager::REMOVE_WEAPON_CHARGES);
	registerEnumIn("configKeys", ConfigManager::REMOVE_POTION_CHARGES);
	registerEnumIn("configKeys", ConfigManager::EXPERIENCE_FROM_PLAYERS);
	registerEnumIn("configKeys", ConfigManager::FREE_PREMIUM);
	registerEnumIn("configKeys", ConfigManager::REPLACE_KICK_ON_LOGIN);
	registerEnumIn("configKeys", ConfigManager::ALLOW_CLONES);
	registerEnumIn("configKeys", ConfigManager::BIND_ONLY_GLOBAL_ADDRESS);
	registerEnumIn("configKeys", ConfigManager::OPTIMIZE_DATABASE);
	registerEnumIn("configKeys", ConfigManager::MARKET_PREMIUM);
	registerEnumIn("configKeys", ConfigManager::EMOTE_SPELLS);
	registerEnumIn("configKeys", ConfigManager::STAMINA_SYSTEM);
	registerEnumIn("configKeys", ConfigManager::WARN_UNSAFE_SCRIPTS);
	registerEnumIn("configKeys", ConfigManager::CONVERT_UNSAFE_SCRIPTS);
	registerEnumIn("configKeys", ConfigManager::CLASSIC_EQUIPMENT_SLOTS);
	registerEnumIn("configKeys", ConfigManager::CLASSIC_ATTACK_SPEED);
	registerEnumIn("configKeys", ConfigManager::SERVER_SAVE_NOTIFY_MESSAGE);
	registerEnumIn("configKeys", ConfigManager::SERVER_SAVE_CLEAN_MAP);
	registerEnumIn("configKeys", ConfigManager::SERVER_SAVE_CLOSE);
	registerEnumIn("configKeys", ConfigManager::SERVER_SAVE_SHUTDOWN);
	registerEnumIn("configKeys", ConfigManager::ONLINE_OFFLINE_CHARLIST);
	registerEnumIn("configKeys", ConfigManager::HOUSE_DOOR_SHOW_PRICE);
	registerEnumIn("configKeys", ConfigManager::MONSTER_OVERSPAWN);
	registerEnumIn("configKeys", ConfigManager::REMOVE_ON_DESPAWN);
	registerEnumIn("configKeys", ConfigManager::PROTECTION_TIME);
	registerEnumIn("configKeys", ConfigManager::BED_OFFLINE_TRAINING);
	registerEnumIn("configKeys", ConfigManager::CHECK_DUPLICATE_STORAGE_KEYS);

	registerEnumIn("configKeys", ConfigManager::MAP_NAME);
	registerEnumIn("configKeys", ConfigManager::HOUSE_RENT_PERIOD);
	registerEnumIn("configKeys", ConfigManager::SERVER_NAME);
	registerEnumIn("configKeys", ConfigManager::OWNER_NAME);
	registerEnumIn("configKeys", ConfigManager::OWNER_EMAIL);
	registerEnumIn("configKeys", ConfigManager::URL);
	registerEnumIn("configKeys", ConfigManager::LOCATION);
	registerEnumIn("configKeys", ConfigManager::IP);
	registerEnumIn("configKeys", ConfigManager::MOTD);
	registerEnumIn("configKeys", ConfigManager::WORLD_TYPE);
	registerEnumIn("configKeys", ConfigManager::MYSQL_HOST);
	registerEnumIn("configKeys", ConfigManager::MYSQL_USER);
	registerEnumIn("configKeys", ConfigManager::MYSQL_PASS);
	registerEnumIn("configKeys", ConfigManager::MYSQL_DB);
	registerEnumIn("configKeys", ConfigManager::MYSQL_SOCK);
	registerEnumIn("configKeys", ConfigManager::DEFAULT_PRIORITY);
	registerEnumIn("configKeys", ConfigManager::MAP_AUTHOR);

	registerEnumIn("configKeys", ConfigManager::SERVER_SAVE_NOTIFY_DURATION);
	registerEnumIn("configKeys", ConfigManager::SQL_PORT);
	registerEnumIn("configKeys", ConfigManager::MAX_PLAYERS);
	registerEnumIn("configKeys", ConfigManager::PZ_LOCKED);
	registerEnumIn("configKeys", ConfigManager::DEFAULT_DESPAWNRANGE);
	registerEnumIn("configKeys", ConfigManager::DEFAULT_DESPAWNRADIUS);
	registerEnumIn("configKeys", ConfigManager::DEFAULT_WALKTOSPAWNRADIUS);
	registerEnumIn("configKeys", ConfigManager::RATE_EXPERIENCE);
	registerEnumIn("configKeys", ConfigManager::RATE_SKILL);
	registerEnumIn("configKeys", ConfigManager::RATE_LOOT);
	registerEnumIn("configKeys", ConfigManager::RATE_MAGIC);
	registerEnumIn("configKeys", ConfigManager::RATE_SPAWN);
	registerEnumIn("configKeys", ConfigManager::HOUSE_LEVEL);
	registerEnumIn("configKeys", ConfigManager::HOUSE_PRICE);
	registerEnumIn("configKeys", ConfigManager::KILLS_TO_RED);
	registerEnumIn("configKeys", ConfigManager::KILLS_TO_BLACK);
	registerEnumIn("configKeys", ConfigManager::MAX_MESSAGEBUFFER);
	registerEnumIn("configKeys", ConfigManager::ACTIONS_DELAY_INTERVAL);
	registerEnumIn("configKeys", ConfigManager::EX_ACTIONS_DELAY_INTERVAL);
	registerEnumIn("configKeys", ConfigManager::KICK_AFTER_MINUTES);
	registerEnumIn("configKeys", ConfigManager::PROTECTION_LEVEL);
	registerEnumIn("configKeys", ConfigManager::DEATH_LOSE_PERCENT);
	registerEnumIn("configKeys", ConfigManager::STATUSQUERY_TIMEOUT);
	registerEnumIn("configKeys", ConfigManager::FRAG_TIME);
	registerEnumIn("configKeys", ConfigManager::WHITE_SKULL_TIME);
	registerEnumIn("configKeys", ConfigManager::GAME_PORT);
	registerEnumIn("configKeys", ConfigManager::LOGIN_PORT);
	registerEnumIn("configKeys", ConfigManager::STATUS_PORT);
	registerEnumIn("configKeys", ConfigManager::STAIRHOP_DELAY);
	registerEnumIn("configKeys", ConfigManager::MARKET_OFFER_DURATION);
	registerEnumIn("configKeys", ConfigManager::EXP_SHARE_RANGE);
	registerEnumIn("configKeys", ConfigManager::EXP_SHARE_FLOORS);
	registerEnumIn("configKeys", ConfigManager::EXP_FROM_PLAYERS_LEVEL_RANGE);
	registerEnumIn("configKeys", ConfigManager::MAX_PACKETS_PER_SECOND);
	registerEnumIn("configKeys", ConfigManager::STAMINA_REGEN_MINUTE);
	registerEnumIn("configKeys", ConfigManager::STAMINA_REGEN_PREMIUM);
	registerEnumIn("configKeys", ConfigManager::STAMINA_TRAINER);
	registerEnumIn("configKeys", ConfigManager::STAMINA_PZ);
	registerEnumIn("configKeys", ConfigManager::STAMINA_PZ_GAIN);
	registerEnumIn("configKeys", ConfigManager::STAMINA_ORANGE_DELAY);
	registerEnumIn("configKeys", ConfigManager::STAMINA_GREEN_DELAY);
	registerEnumIn("configKeys", ConfigManager::STAMINA_TRAINER_DELAY);
	registerEnumIn("configKeys", ConfigManager::STAMINA_TRAINER_GAIN);
	registerEnumIn("configKeys", ConfigManager::REWARD_BASE_RATE);
	registerEnumIn("configKeys", ConfigManager::REWARD_RATE_DAMAGE_DONE);
	registerEnumIn("configKeys", ConfigManager::REWARD_RATE_DAMAGE_TAKEN);
	registerEnumIn("configKeys", ConfigManager::REWARD_RATE_HEALING_DONE);
	registerEnumIn("configKeys", ConfigManager::MAX_ALLOWED_ON_A_DUMMY);
	registerEnumIn("configKeys", ConfigManager::RATE_EXERCISE_TRAINING_SPEED);
	registerEnumIn("configKeys", ConfigManager::NPCS_USING_BANK_MONEY);

	// os
	registerMethod("os", "mtime", LuaScriptInterface::luaSystemTime);
	registerMethod("os", "ntime", LuaScriptInterface::luaSystemNanoTime);

	// table
	registerMethod("table", "create", LuaScriptInterface::luaTableCreate);

	// lua modules
	registerGame();
	registerVariant();
	registerPosition();
	registerTile();
	registerNetworkMessage();
	registerItem();
	registerContainer();
	registerTeleport();
	registerCreature();
	registerPlayer();
	registerModalWindow();
	registerMonster();
	registerNpc();
	registerGuild();
	registerGroup();
	registerVocation();
	registerTown();
	registerHouse();
	registerItemType();
	registerCombat();
	registerCondition();
	registerOutfit();
	registerMonsterType();
	registerLoot();
	registerMonsterSpell();
	registerParty();
	registerSpells();
	registerActions();
	registerTalkActions();
	registerCreatureEvents();
	registerMoveEvents();
	registerGlobalEvents();
	registerWeapons();
	registerXML();

	registerMethod("Player", "getReset", LuaScriptInterface::luaPlayerGetReset); // reset system
	registerMethod("Player", "doReset", LuaScriptInterface::luaPlayerDoReset);   // reset system
	registerMethod("Player", "setReset", LuaScriptInterface::luaPlayerSetReset); // reset system
	registerMethod("Player", "reloadWarList", LuaScriptInterface::luaPlayerReloadWarList);
}

#undef registerEnum
#undef registerEnumIn

void LuaScriptInterface::registerClass(const std::string& className, const std::string& baseClass,
                                       lua_CFunction newFunction /* = nullptr*/)
{
	// className = {}
	lua_newtable(luaState);
	lua_pushvalue(luaState, -1);
	lua_setglobal(luaState, className.c_str());
	int methods = lua_gettop(luaState);

	// methodsTable = {}
	lua_newtable(luaState);
	int methodsTable = lua_gettop(luaState);

	if (newFunction) {
		// className.__call = newFunction
		lua_pushcfunction(luaState, newFunction);
		lua_setfield(luaState, methodsTable, "__call");
	}

	uint32_t parents = 0;
	if (!baseClass.empty()) {
		lua_getglobal(luaState, baseClass.c_str());
		lua_rawgeti(luaState, -1, 'p');
		parents = Lua::getInteger<uint32_t>(luaState, -1) + 1;
		lua_pop(luaState, 1);
		lua_setfield(luaState, methodsTable, "__index");
	}

	// setmetatable(className, methodsTable)
	lua_setmetatable(luaState, methods);

	// className.metatable = {}
	luaL_newmetatable(luaState, className.c_str());
	int metatable = lua_gettop(luaState);

	// className.metatable.__metatable = className
	lua_pushvalue(luaState, methods);
	lua_setfield(luaState, metatable, "__metatable");

	// className.metatable.__index = className
	lua_pushvalue(luaState, methods);
	lua_setfield(luaState, metatable, "__index");

	// className.metatable['h'] = hash
	lua_pushinteger(luaState, std::hash<std::string>()(className));
	lua_rawseti(luaState, metatable, 'h');

	// className.metatable['p'] = parents
	lua_pushinteger(luaState, parents);
	lua_rawseti(luaState, metatable, 'p');

	static std::unordered_map<std::string_view, LuaDataType> LuaDataTypeByClassName = {
	    {"Item", LuaData_Item},
	    {"Container", LuaData_Container},
	    {"Teleport", LuaData_Teleport},
	    {"Creature", LuaData_Creature},
	    {"Player", LuaData_Player},
	    {"Monster", LuaData_Monster},
	    {"Npc", LuaData_Npc},
	    {"Tile", LuaData_Tile},
	    {"Condition", LuaData_Condition},

	    {"Combat", LuaData_Combat},
	    {"Group", LuaData_Group},
	    {"Guild", LuaData_Guild},
	    {"House", LuaData_House},
	    {"ItemType", LuaData_ItemType},
	    {"ModalWindow", LuaData_ModalWindow},
	    {"MonsterType", LuaData_MonsterType},
	    {"NetworkMessage", LuaData_NetworkMessage},
	    {"Party", LuaData_Party},
	    {"Vocation", LuaData_Vocation},
	    {"Town", LuaData_Town},
	    {"LuaVariant", LuaData_LuaVariant},
	    {"Position", LuaData_Position},

	    {"Outfit", LuaData_Outfit},
	    {"Loot", LuaData_Loot},
	    {"MonsterSpell", LuaData_MonsterSpell},
	    {"Spell", LuaData_Spell},
	    {"InstantSpell", LuaData_Spell},
	    {"Action", LuaData_Action},
	    {"TalkAction", LuaData_TalkAction},
	    {"CreatureEvent", LuaData_CreatureEvent},
	    {"MoveEvent", LuaData_MoveEvent},
	    {"GlobalEvent", LuaData_GlobalEvent},
	    {"Weapon", LuaData_Weapon},
	    {"WeaponDistance", LuaData_Weapon},
	    {"WeaponWand", LuaData_Weapon},
	    {"WeaponMelee", LuaData_Weapon},
	    {"XMLDocument", LuaData_XMLDocument},
	    {"XMLNode", LuaData_XMLNode},
	};

	// className.metatable['t'] = type
	auto luaDataType = LuaDataTypeByClassName.find(className);
	if (luaDataType == LuaDataTypeByClassName.end()) {
		lua_pushinteger(luaState, LuaData_Unknown);
	} else {
		lua_pushinteger(luaState, luaDataType->second);
	}
	lua_rawseti(luaState, metatable, 't');

	// pop className, className.metatable
	lua_pop(luaState, 2);
}

void LuaScriptInterface::registerTable(std::string_view tableName)
{
	// _G[tableName] = {}
	lua_newtable(luaState);
	lua_setglobal(luaState, tableName.data());
}

void LuaScriptInterface::registerMethod(std::string_view globalName, std::string_view methodName, lua_CFunction func)
{
	// globalName.methodName = func
	lua_getglobal(luaState, globalName.data());
	lua_pushcfunction(luaState, func);
	lua_setfield(luaState, -2, methodName.data());

	// pop globalName
	lua_pop(luaState, 1);
}

void LuaScriptInterface::registerMetaMethod(std::string_view className, std::string_view methodName, lua_CFunction func)
{
	// className.metatable.methodName = func
	luaL_getmetatable(luaState, className.data());
	lua_pushcfunction(luaState, func);
	lua_setfield(luaState, -2, methodName.data());

	// pop className.metatable
	lua_pop(luaState, 1);
}

void LuaScriptInterface::registerGlobalMethod(std::string_view functionName, lua_CFunction func)
{
	// _G[functionName] = func
	lua_pushcfunction(luaState, func);
	lua_setglobal(luaState, functionName.data());
}

void LuaScriptInterface::registerVariable(std::string_view tableName, std::string_view name, lua_Integer value)
{
	// tableName.name = value
	lua_getglobal(luaState, tableName.data());
	Lua::setField(luaState, name.data(), value);

	// pop tableName
	lua_pop(luaState, 1);
}

void LuaScriptInterface::registerGlobalVariable(std::string_view name, lua_Integer value)
{
	// _G[name] = value
	lua_pushinteger(luaState, value);
	lua_setglobal(luaState, name.data());
}

void LuaScriptInterface::registerGlobalBoolean(std::string_view name, bool value)
{
	// _G[name] = value
	Lua::pushBoolean(luaState, value);
	lua_setglobal(luaState, name.data());
}

int LuaScriptInterface::luaDoPlayerAddItem(lua_State* L)
{
	// doPlayerAddItem(cid, itemid, <optional: default: 1> count/subtype, <optional: default: 1> canDropOnMap)
	// doPlayerAddItem(cid, itemid, <optional: default: 1> count, <optional: default: 1> canDropOnMap, <optional:
	// default: 1>subtype)
	Player* player = Lua::getPlayer(L, 1);
	if (!player) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::PLAYER_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	uint16_t itemId = Lua::getInteger<uint16_t>(L, 2);
	int32_t count = Lua::getInteger<int32_t>(L, 3, 1);
	bool canDropOnMap = Lua::getBoolean(L, 4, true);
	uint16_t subType = Lua::getInteger<uint16_t>(L, 5, 1);

	const ItemType& it = Item::items[itemId];
	int32_t itemCount;

	auto parameters = lua_gettop(L);
	if (parameters > 4) {
		// subtype already supplied, count then is the amount
		itemCount = std::max<int32_t>(1, count);
	} else if (it.hasSubType()) {
		if (it.stackable) {
			itemCount = static_cast<int32_t>(std::ceil(static_cast<float>(count) / static_cast<float>(it.stackSize)));
		} else {
			itemCount = 1;
		}
		subType = static_cast<uint16_t>(count);
	} else {
		itemCount = std::max<int32_t>(1, count);
	}

	while (itemCount > 0) {
		uint16_t stackCount = subType;
		if (it.stackable && stackCount > it.stackSize) {
			stackCount = it.stackSize;
		}

		Item* newItem = Item::CreateItem(itemId, stackCount);
		if (!newItem) {
			reportErrorFunc(L, getErrorDesc(LuaErrorCode::ITEM_NOT_FOUND));
			Lua::pushBoolean(L, false);
			return 1;
		}

		if (it.stackable) {
			subType -= stackCount;
		}

		ReturnValue ret = g_game.internalPlayerAddItem(player, newItem, canDropOnMap);
		if (ret != RETURNVALUE_NOERROR) {
			delete newItem;
			Lua::pushBoolean(L, false);
			return 1;
		}

		if (--itemCount == 0) {
			if (newItem->getParent()) {
				uint32_t uid = getScriptEnv()->addThing(newItem);
				lua_pushinteger(L, uid);
				return 1;
			} else {
				// stackable item stacked with existing object, newItem will be released
				Lua::pushBoolean(L, false);
				return 1;
			}
		}
	}

	Lua::pushBoolean(L, false);
	return 1;
}

int LuaScriptInterface::luaTransformToSHA1(lua_State* L)
{
	// transformToSHA1(text)
	Lua::pushString(L, transformToSHA1(Lua::getString(L, 1)));
	return 1;
}

int LuaScriptInterface::luaDebugPrint(lua_State* L)
{
	// debugPrint(text)
	reportErrorFunc(L, Lua::getString(L, 1));
	return 0;
}

int LuaScriptInterface::luaLogInfo(lua_State* L)
{
	g_logger().info(Lua::getString(L, 1));
	return 0;
}

int LuaScriptInterface::luaLogMigration(lua_State* L)
{
	g_logger().migration(Lua::getString(L, 1));
	return 0;
}

int LuaScriptInterface::luaLogWarning(lua_State* L)
{
	g_logger().warn(Lua::getString(L, 1));
	return 0;
}

int LuaScriptInterface::luaLogError(lua_State* L)
{
	g_logger().error(Lua::getString(L, 1));
	return 0;
}

int LuaScriptInterface::luaGetWorldTime(lua_State* L)
{
	// getWorldTime()
	int16_t time = g_game.getWorldTime();
	lua_pushinteger(L, time);
	return 1;
}

int LuaScriptInterface::luaGetWorldLight(lua_State* L)
{
	// getWorldLight()
	LightInfo lightInfo = g_game.getWorldLightInfo();
	lua_pushinteger(L, lightInfo.level);
	lua_pushinteger(L, lightInfo.color);
	return 2;
}

int LuaScriptInterface::luaSetWorldLight(lua_State* L)
{
	// setWorldLight(level, color)
	if (ConfigManager::getBoolean(ConfigManager::DEFAULT_WORLD_LIGHT)) {
		Lua::pushBoolean(L, false);
		return 1;
	}

	LightInfo lightInfo;
	lightInfo.level = Lua::getInteger<uint8_t>(L, 1);
	lightInfo.color = Lua::getInteger<uint8_t>(L, 2);
	g_game.setWorldLightInfo(lightInfo);
	Lua::pushBoolean(L, true);
	return 1;
}

int LuaScriptInterface::luaGetWorldUpTime(lua_State* L)
{
	// getWorldUpTime()
	uint64_t uptime = (OTSYS_TIME() - ProtocolStatus::start) / 1000;
	lua_pushinteger(L, uptime);
	return 1;
}

int LuaScriptInterface::luaGetSubTypeName(lua_State* L)
{
	// getSubTypeName(subType)
	int32_t subType = Lua::getInteger<int32_t>(L, 1);
	if (subType > 0) {
		Lua::pushString(L, Item::items[subType].name);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

bool LuaScriptInterface::getArea(lua_State* L, std::vector<uint32_t>& vec, uint32_t& rows)
{
	lua_pushnil(L);
	for (rows = 0; lua_next(L, -2) != 0; ++rows) {
		if (!Lua::isTable(L, -1)) {
			return false;
		}

		lua_pushnil(L);
		while (lua_next(L, -2) != 0) {
			if (!Lua::isInteger(L, -1)) {
				return false;
			}
			vec.push_back(Lua::getInteger<uint32_t>(L, -1));
			lua_pop(L, 1);
		}

		lua_pop(L, 1);
	}

	lua_pop(L, 1);
	return (rows != 0);
}

int LuaScriptInterface::luaCreateCombatArea(lua_State* L)
{
	// createCombatArea( {area}, <optional> {extArea} )
	ScriptEnvironment* env = getScriptEnv();
	if (env->getScriptId() != EVENT_ID_LOADING) {
		reportErrorFunc(L, "This function can only be used while loading the script.");
		Lua::pushBoolean(L, false);
		return 1;
	}

	uint32_t areaId = g_luaEnvironment.createAreaObject(env->getScriptInterface());
	AreaCombat* area = g_luaEnvironment.getAreaObject(areaId);

	int parameters = lua_gettop(L);
	if (parameters >= 2) {
		uint32_t rowsExtArea;
		std::vector<uint32_t> vecExtArea;
		if (!Lua::isTable(L, 2) || !getArea(L, vecExtArea, rowsExtArea)) {
			reportErrorFunc(L, "Invalid extended area table.");
			Lua::pushBoolean(L, false);
			return 1;
		}
		area->setupExtArea(vecExtArea, rowsExtArea);
	}

	uint32_t rowsArea = 0;
	std::vector<uint32_t> vecArea;
	if (!Lua::isTable(L, 1) || !getArea(L, vecArea, rowsArea)) {
		reportErrorFunc(L, "Invalid area table.");
		Lua::pushBoolean(L, false);
		return 1;
	}

	area->setupArea(vecArea, rowsArea);
	lua_pushinteger(L, areaId);
	return 1;
}

int LuaScriptInterface::luaDoAreaCombat(lua_State* L)
{
	// doAreaCombat(cid, type, pos, area, min, max, effect[, origin = ORIGIN_SPELL[, blockArmor = false[, blockShield =
	// false[, ignoreResistances = false]]]])
	Creature* creature = Lua::getCreature(L, 1);
	if (!creature && (!Lua::isInteger(L, 1) || Lua::getInteger<uint32_t>(L, 1) != 0)) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	uint32_t areaId = Lua::getInteger<uint32_t>(L, 4);
	const AreaCombat* area = g_luaEnvironment.getAreaObject(areaId);
	if (area || areaId == 0) {
		CombatType_t combatType = Lua::getInteger<CombatType_t>(L, 2);

		CombatParams params;
		params.combatType = combatType;
		params.impactEffect = Lua::getInteger<uint16_t>(L, 7);
		params.blockedByArmor = Lua::getBoolean(L, 8, false);
		params.blockedByShield = Lua::getBoolean(L, 9, false);
		params.ignoreResistances = Lua::getBoolean(L, 10, false);

		CombatDamage damage;
		damage.origin = Lua::getInteger<CombatOrigin>(L, 8, ORIGIN_SPELL);
		damage.primary.type = combatType;
		damage.primary.value = normal_random(Lua::getNumber<int32_t>(L, 6), Lua::getNumber<int32_t>(L, 5));

		Combat::doAreaCombat(creature, Lua::getPosition(L, 3), area, damage, params);
		Lua::pushBoolean(L, true);
	} else {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::AREA_NOT_FOUND));
		Lua::pushBoolean(L, false);
	}
	return 1;
}

int LuaScriptInterface::luaDoTargetCombat(lua_State* L)
{
	// doTargetCombat(cid, target, type, min, max, effect[, origin = ORIGIN_SPELL[, blockArmor = false[, blockShield =
	// false[, ignoreResistances = false]]]])
	Creature* creature = Lua::getCreature(L, 1);
	if (!creature && (!Lua::isInteger(L, 1) || Lua::getInteger<uint32_t>(L, 1) != 0)) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	Creature* target = Lua::getCreature(L, 2);
	if (!target) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	CombatType_t combatType = Lua::getInteger<CombatType_t>(L, 3);

	CombatParams params;
	params.combatType = combatType;
	params.impactEffect = Lua::getInteger<uint16_t>(L, 6);
	params.blockedByArmor = Lua::getBoolean(L, 8, false);
	params.blockedByShield = Lua::getBoolean(L, 9, false);
	params.ignoreResistances = Lua::getBoolean(L, 10, false);

	CombatDamage damage;
	damage.origin = Lua::getInteger<CombatOrigin>(L, 7, ORIGIN_SPELL);
	damage.primary.type = combatType;
	damage.primary.value = normal_random(Lua::getNumber<int32_t>(L, 4), Lua::getNumber<int32_t>(L, 5));

	Combat::doTargetCombat(creature, target, damage, params);
	Lua::pushBoolean(L, true);
	return 1;
}

int LuaScriptInterface::luaDoChallengeCreature(lua_State* L)
{
	// doChallengeCreature(cid, target[, force = false])
	Creature* creature = Lua::getCreature(L, 1);
	if (!creature) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	Creature* target = Lua::getCreature(L, 2);
	if (!target) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	target->challengeCreature(creature, Lua::getBoolean(L, 3, false));
	Lua::pushBoolean(L, true);
	return 1;
}

int LuaScriptInterface::luaIsValidUID(lua_State* L)
{
	// isValidUID(uid)
	Lua::pushBoolean(L, getScriptEnv()->getThingByUID(Lua::getInteger<uint32_t>(L, 1)) != nullptr);
	return 1;
}

int LuaScriptInterface::luaIsDepot(lua_State* L)
{
	// isDepot(uid)
	Container* container = getScriptEnv()->getContainerByUID(Lua::getInteger<uint32_t>(L, 1));
	Lua::pushBoolean(L, container && container->getDepotLocker());
	return 1;
}

int LuaScriptInterface::luaIsMoveable(lua_State* L)
{
	// isMoveable(uid)
	// isMovable(uid)
	Thing* thing = getScriptEnv()->getThingByUID(Lua::getInteger<uint32_t>(L, 1));
	Lua::pushBoolean(L, thing && thing->isPushable());
	return 1;
}

int LuaScriptInterface::luaDoAddContainerItem(lua_State* L)
{
	// doAddContainerItem(uid, itemid, <optional> count/subtype)
	uint32_t uid = Lua::getInteger<uint32_t>(L, 1);

	ScriptEnvironment* env = getScriptEnv();
	Container* container = env->getContainerByUID(uid);
	if (!container) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::CONTAINER_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	uint16_t itemId = Lua::getInteger<uint16_t>(L, 2);
	const ItemType& it = Item::items[itemId];

	int32_t itemCount = 1;
	int32_t subType = 1;
	uint32_t count = Lua::getInteger<uint32_t>(L, 3, 1);

	if (it.hasSubType()) {
		if (it.stackable) {
			itemCount = static_cast<int32_t>(std::ceil(static_cast<float>(count) / static_cast<float>(it.stackSize)));
		}

		subType = count;
	} else {
		itemCount = std::max<int32_t>(1, count);
	}

	while (itemCount > 0) {
		int32_t stackCount = std::min<int32_t>(subType, it.stackSize);
		Item* newItem = Item::CreateItem(itemId, static_cast<uint16_t>(stackCount));
		if (!newItem) {
			reportErrorFunc(L, getErrorDesc(LuaErrorCode::ITEM_NOT_FOUND));
			Lua::pushBoolean(L, false);
			return 1;
		}

		if (it.stackable) {
			subType -= stackCount;
		}

		ReturnValue ret = g_game.internalAddItem(container, newItem);
		if (ret != RETURNVALUE_NOERROR) {
			delete newItem;
			Lua::pushBoolean(L, false);
			return 1;
		}

		if (--itemCount == 0) {
			if (newItem->getParent()) {
				lua_pushinteger(L, env->addThing(newItem));
			} else {
				// stackable item stacked with existing object, newItem will be released
				Lua::pushBoolean(L, false);
			}
			return 1;
		}
	}

	Lua::pushBoolean(L, false);
	return 1;
}

int LuaScriptInterface::luaGetDepotId(lua_State* L)
{
	// getDepotId(uid)
	uint32_t uid = Lua::getInteger<uint32_t>(L, 1);

	Container* container = getScriptEnv()->getContainerByUID(uid);
	if (!container) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::CONTAINER_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	DepotLocker* depotLocker = container->getDepotLocker();
	if (!depotLocker) {
		reportErrorFunc(L, "Depot not found");
		Lua::pushBoolean(L, false);
		return 1;
	}

	lua_pushinteger(L, depotLocker->getDepotId());
	return 1;
}

int LuaScriptInterface::luaAddEvent(lua_State* L)
{
	// addEvent(callback, delay, ...)
	int parameters = lua_gettop(L);
	if (parameters < 2) {
		reportErrorFunc(L, fmt::format("Not enough parameters: {:d}.", parameters));
		Lua::pushBoolean(L, false);
		return 1;
	}

	if (!Lua::isFunction(L, 1)) {
		reportErrorFunc(L, "callback parameter should be a function.");
		Lua::pushBoolean(L, false);
		return 1;
	}

	// if (!Lua::isInteger(L, 2)) {
	// 	reportErrorFunc(L, "delay parameter should be a integer.");
	// 	Lua::pushBoolean(L, false);
	// 	return 1;
	// }

	if (getBoolean(ConfigManager::WARN_UNSAFE_SCRIPTS) || getBoolean(ConfigManager::CONVERT_UNSAFE_SCRIPTS)) {
		std::vector<std::pair<int32_t, LuaDataType>> indexes;
		for (int i = 3; i <= parameters; ++i) {
			if (lua_getmetatable(L, i) == 0) {
				continue;
			}

			lua_rawgeti(L, -1, 't');
			LuaDataType type = Lua::getInteger<LuaDataType>(L, -1);
			lua_pop(L, 2);

			switch (type) {
				case LuaData_Unknown:
				case LuaData_Tile: {
					break;
				}

				case LuaData_Player:
				case LuaData_Monster:
				case LuaData_Npc: {
					if (auto creature = Lua::getCreature(L, i)) {
						lua_pushinteger(L, creature->getID());
						lua_setiuservalue(L, i, 1);
					}

					break;
				}

				case LuaData_Item:
				case LuaData_Container:
				case LuaData_Teleport: {
					indexes.push_back({i, type});
					break;
				}

				default: {
					break;
				}
			}
		}

		if (!indexes.empty()) {
			if (getBoolean(ConfigManager::WARN_UNSAFE_SCRIPTS)) {
				bool plural = indexes.size() > 1;

				std::string warningString = "Argument";
				if (plural) {
					warningString += 's';
				}

				for (const auto& entry : indexes) {
					if (entry == indexes.front()) {
						warningString += ' ';
					} else if (entry == indexes.back()) {
						warningString += " and ";
					} else {
						warningString += ", ";
					}
					warningString += '#';
					warningString += std::to_string(entry.first);
				}

				if (plural) {
					warningString += " are unsafe";
				} else {
					warningString += " is unsafe";
				}

				reportErrorFunc(L, warningString);
			}

			if (getBoolean(ConfigManager::CONVERT_UNSAFE_SCRIPTS)) {
				for (const auto& entry : indexes) {
					switch (entry.second) {
						case LuaData_Item:
						case LuaData_Container:
						case LuaData_Teleport: {
							lua_getglobal(L, "Item");
							lua_getfield(L, -1, "getUniqueId");
							break;
						}
						default:
							break;
					}
					lua_replace(L, -2);
					lua_pushvalue(L, entry.first);
					lua_call(L, 1, 1);
					lua_replace(L, entry.first);
				}
			}
		}
	}

	LuaTimerEventDesc eventDesc;
	eventDesc.parameters.reserve(parameters -
	                             2); // safe to use -2 since we garanteed that there is at least two parameters
	for (int i = 0; i < parameters - 2; ++i) {
		eventDesc.parameters.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
	}

	uint32_t delay = std::max<uint32_t>(100, Lua::getInteger<uint32_t>(L, 2));
	lua_pop(L, 1);

	eventDesc.function = luaL_ref(L, LUA_REGISTRYINDEX);
	eventDesc.scriptId = getScriptEnv()->getScriptId();

	auto& lastTimerEventId = g_luaEnvironment.lastEventTimerId;
	eventDesc.eventId = g_scheduler.addEvent(
	    createSchedulerTask(delay, [=]() { g_luaEnvironment.executeTimerEvent(lastTimerEventId); }));

	g_luaEnvironment.timerEvents.emplace(lastTimerEventId, std::move(eventDesc));
	lua_pushinteger(L, lastTimerEventId++);
	return 1;
}

int LuaScriptInterface::luaStopEvent(lua_State* L)
{
	// stopEvent(eventId)
	uint32_t eventId = Lua::getInteger<uint32_t>(L, 1);

	auto& timerEvents = g_luaEnvironment.timerEvents;
	auto it = timerEvents.find(eventId);
	if (it == timerEvents.end()) {
		Lua::pushBoolean(L, false);
		return 1;
	}

	LuaTimerEventDesc timerEventDesc = std::move(it->second);
	timerEvents.erase(it);

	g_scheduler.stopEvent(timerEventDesc.eventId);
	luaL_unref(L, LUA_REGISTRYINDEX, timerEventDesc.function);

	for (auto parameter : timerEventDesc.parameters) {
		luaL_unref(L, LUA_REGISTRYINDEX, parameter);
	}

	Lua::pushBoolean(L, true);
	return 1;
}

int LuaScriptInterface::luaSaveServer(lua_State* L)
{
	// saveServer()
	g_globalEvents->save();
	g_game.saveGameState();
	Lua::pushBoolean(L, true);
	return 1;
}

int LuaScriptInterface::luaCleanMap(lua_State* L)
{
	// cleanMap()
	lua_pushinteger(L, g_game.map.clean());
	return 1;
}

int LuaScriptInterface::luaIsInWar(lua_State* L)
{
	// isInWar(cid, target)
	Player* player = Lua::getPlayer(L, 1);
	if (!player) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::PLAYER_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	Player* targetPlayer = Lua::getPlayer(L, 2);
	if (!targetPlayer) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::PLAYER_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	Lua::pushBoolean(L, player->isInWar(targetPlayer));
	return 1;
}

int LuaScriptInterface::luaGetWaypointPositionByName(lua_State* L)
{
	// getWaypointPositionByName(name)
	auto& waypoints = g_game.map.waypoints;

	auto it = waypoints.find(Lua::getString(L, -1));
	if (it != waypoints.end()) {
		Lua::pushPosition(L, it->second);
	} else {
		Lua::pushBoolean(L, false);
	}
	return 1;
}

int LuaScriptInterface::luaSendChannelMessage(lua_State* L)
{
	// sendChannelMessage(channelId, type, message)
	uint16_t channelId = Lua::getInteger<uint16_t>(L, 1);
	ChatChannel* channel = g_chat->getChannelById(channelId);
	if (!channel) {
		Lua::pushBoolean(L, false);
		return 1;
	}

	SpeakClasses type = Lua::getInteger<SpeakClasses>(L, 2);
	const std::string& message = Lua::getString(L, 3);
	channel->sendToAll(message, type);
	Lua::pushBoolean(L, true);
	return 1;
}

int LuaScriptInterface::luaSendGuildChannelMessage(lua_State* L)
{
	// sendGuildChannelMessage(guildId, type, message)
	uint32_t guildId = Lua::getInteger<uint32_t>(L, 1);
	ChatChannel* channel = g_chat->getGuildChannelById(guildId);
	if (!channel) {
		Lua::pushBoolean(L, false);
		return 1;
	}

	SpeakClasses type = Lua::getInteger<SpeakClasses>(L, 2);
	const std::string& message = Lua::getString(L, 3);
	channel->sendToAll(message, type);
	Lua::pushBoolean(L, true);
	return 1;
}

int LuaScriptInterface::luaIsScriptsInterface(lua_State* L)
{
	// isScriptsInterface()
	if (getScriptEnv()->getScriptInterface() == &g_scripts->getScriptInterface()) {
		Lua::pushBoolean(L, true);
	} else {
		reportErrorFunc(L, "Event: can only be called inside (data/scripts/)");
		Lua::pushBoolean(L, false);
	}
	return 1;
}

std::string LuaScriptInterface::escapeString(std::string s)
{
	boost::algorithm::replace_all(s, "\\", "\\\\");
	boost::algorithm::replace_all(s, "\"", "\\\"");
	boost::algorithm::replace_all(s, "'", "\\'");
	boost::algorithm::replace_all(s, "[[", "\\[[");
	return s;
}

const luaL_Reg LuaScriptInterface::luaConfigManagerTable[] = {
    {"getString", LuaScriptInterface::luaConfigManagerGetString},
    {"getNumber", LuaScriptInterface::luaConfigManagerGetNumber},
    {"getBoolean", LuaScriptInterface::luaConfigManagerGetBoolean},
    {nullptr, nullptr}};

int LuaScriptInterface::luaConfigManagerGetString(lua_State* L)
{
	Lua::pushString(L, getString(Lua::getInteger<ConfigManager::String>(L, -1)));
	return 1;
}

int LuaScriptInterface::luaConfigManagerGetNumber(lua_State* L)
{
	lua_pushinteger(L, getInteger(Lua::getInteger<ConfigManager::Integer>(L, -1)));
	return 1;
}

int LuaScriptInterface::luaConfigManagerGetBoolean(lua_State* L)
{
	Lua::pushBoolean(L, getBoolean(Lua::getInteger<ConfigManager::Boolean>(L, -1)));
	return 1;
}

const luaL_Reg LuaScriptInterface::luaDatabaseTable[] = {
    {"query", LuaScriptInterface::luaDatabaseExecute},
    {"asyncQuery", LuaScriptInterface::luaDatabaseAsyncExecute},
    {"storeQuery", LuaScriptInterface::luaDatabaseStoreQuery},
    {"asyncStoreQuery", LuaScriptInterface::luaDatabaseAsyncStoreQuery},
    {"escapeString", LuaScriptInterface::luaDatabaseEscapeString},
    {"escapeBlob", LuaScriptInterface::luaDatabaseEscapeBlob},
    {"lastInsertId", LuaScriptInterface::luaDatabaseLastInsertId},
    {"tableExists", LuaScriptInterface::luaDatabaseTableExists},
    {nullptr, nullptr}};

int LuaScriptInterface::luaDatabaseExecute(lua_State* L)
{
	Lua::pushBoolean(L, Database::getInstance().executeQuery(Lua::getString(L, -1)));
	return 1;
}

int LuaScriptInterface::luaDatabaseAsyncExecute(lua_State* L)
{
	std::function<void(DBResult_ptr, bool)> callback;
	if (lua_gettop(L) > 1) {
		int32_t ref = luaL_ref(L, LUA_REGISTRYINDEX);
		auto scriptId = getScriptEnv()->getScriptId();
		callback = [ref, scriptId](DBResult_ptr, bool success) {
			lua_State* luaState = g_luaEnvironment.getLuaState();
			if (!luaState) {
				return;
			}

			if (!LuaScriptInterface::reserveScriptEnv()) {
				luaL_unref(luaState, LUA_REGISTRYINDEX, ref);
				return;
			}

			lua_rawgeti(luaState, LUA_REGISTRYINDEX, ref);
			Lua::pushBoolean(luaState, success);
			auto env = getScriptEnv();
			env->setScriptId(scriptId, &g_luaEnvironment);
			g_luaEnvironment.callFunction(1);

			luaL_unref(luaState, LUA_REGISTRYINDEX, ref);
		};
	}
	g_databaseTasks.addTask(Lua::getString(L, -1), callback);
	return 0;
}

int LuaScriptInterface::luaDatabaseStoreQuery(lua_State* L)
{
	if (DBResult_ptr res = Database::getInstance().storeQuery(Lua::getString(L, -1))) {
		lua_pushinteger(L, ScriptEnvironment::addResult(res));
	} else {
		Lua::pushBoolean(L, false);
	}
	return 1;
}

int LuaScriptInterface::luaDatabaseAsyncStoreQuery(lua_State* L)
{
	std::function<void(DBResult_ptr, bool)> callback;
	if (lua_gettop(L) > 1) {
		int32_t ref = luaL_ref(L, LUA_REGISTRYINDEX);
		auto scriptId = getScriptEnv()->getScriptId();
		callback = [ref, scriptId](DBResult_ptr result, bool) {
			lua_State* luaState = g_luaEnvironment.getLuaState();
			if (!luaState) {
				return;
			}

			if (!LuaScriptInterface::reserveScriptEnv()) {
				luaL_unref(luaState, LUA_REGISTRYINDEX, ref);
				return;
			}

			lua_rawgeti(luaState, LUA_REGISTRYINDEX, ref);
			if (result) {
				lua_pushinteger(luaState, ScriptEnvironment::addResult(result));
			} else {
				Lua::pushBoolean(luaState, false);
			}
			auto env = getScriptEnv();
			env->setScriptId(scriptId, &g_luaEnvironment);
			g_luaEnvironment.callFunction(1);

			luaL_unref(luaState, LUA_REGISTRYINDEX, ref);
		};
	}
	g_databaseTasks.addTask(Lua::getString(L, -1), callback, true);
	return 0;
}

int LuaScriptInterface::luaDatabaseEscapeString(lua_State* L)
{
	Lua::pushString(L, Database::getInstance().escapeString(Lua::getString(L, -1)));
	return 1;
}

int LuaScriptInterface::luaDatabaseEscapeBlob(lua_State* L)
{
	uint32_t length = Lua::getInteger<uint32_t>(L, 2);
	Lua::pushString(L, Database::getInstance().escapeBlob(Lua::getString(L, 1).c_str(), length));
	return 1;
}

int LuaScriptInterface::luaDatabaseLastInsertId(lua_State* L)
{
	lua_pushinteger(L, Database::getInstance().getLastInsertId());
	return 1;
}

int LuaScriptInterface::luaDatabaseTableExists(lua_State* L)
{
	Lua::pushBoolean(L, DatabaseManager::tableExists(Lua::getString(L, -1)));
	return 1;
}

const luaL_Reg LuaScriptInterface::luaResultTable[] = {
    {"getNumber", LuaScriptInterface::luaResultGetNumber}, {"getString", LuaScriptInterface::luaResultGetString},
    {"getStream", LuaScriptInterface::luaResultGetStream}, {"next", LuaScriptInterface::luaResultNext},
    {"free", LuaScriptInterface::luaResultFree},           {nullptr, nullptr}};

int LuaScriptInterface::luaResultGetNumber(lua_State* L)
{
	DBResult_ptr res = ScriptEnvironment::getResultByID(Lua::getInteger<uint32_t>(L, 1));
	if (!res) {
		Lua::pushBoolean(L, false);
		return 1;
	}

	const std::string& s = Lua::getString(L, 2);
	lua_pushinteger(L, res->getNumber<int64_t>(s));
	return 1;
}

int LuaScriptInterface::luaResultGetString(lua_State* L)
{
	DBResult_ptr res = ScriptEnvironment::getResultByID(Lua::getInteger<uint32_t>(L, 1));
	if (!res) {
		Lua::pushBoolean(L, false);
		return 1;
	}

	const std::string& s = Lua::getString(L, 2);
	Lua::pushString(L, res->getString(s));
	return 1;
}

int LuaScriptInterface::luaResultGetStream(lua_State* L)
{
	DBResult_ptr res = ScriptEnvironment::getResultByID(Lua::getInteger<uint32_t>(L, 1));
	if (!res) {
		Lua::pushBoolean(L, false);
		return 1;
	}

	auto stream = res->getString(Lua::getString(L, 2));
	lua_pushlstring(L, stream.data(), stream.size());
	lua_pushinteger(L, stream.size());
	return 2;
}

int LuaScriptInterface::luaResultNext(lua_State* L)
{
	DBResult_ptr res = ScriptEnvironment::getResultByID(Lua::getInteger<uint32_t>(L, -1));
	if (!res) {
		Lua::pushBoolean(L, false);
		return 1;
	}

	Lua::pushBoolean(L, res->next());
	return 1;
}

int LuaScriptInterface::luaResultFree(lua_State* L)
{
	Lua::pushBoolean(L, ScriptEnvironment::removeResult(Lua::getInteger<uint32_t>(L, -1)));
	return 1;
}

// Userdata
int LuaScriptInterface::luaUserdataCompare(lua_State* L)
{
	// userdataA == userdataB
	Lua::pushBoolean(L, Lua::getUserdata<void>(L, 1, false) == Lua::getUserdata<void>(L, 2, false));
	return 1;
}

// _G
int LuaScriptInterface::luaIsType(lua_State* L)
{
	// isType(derived, base)
	lua_getmetatable(L, -2);
	lua_getmetatable(L, -2);

	lua_rawgeti(L, -2, 'p');
	uint_fast8_t parentsB = Lua::getInteger<uint_fast8_t>(L, 1);

	lua_rawgeti(L, -3, 'h');
	size_t hashB = Lua::getInteger<size_t>(L, 1);

	lua_rawgeti(L, -3, 'p');
	uint_fast8_t parentsA = Lua::getInteger<uint_fast8_t>(L, 1);
	for (uint_fast8_t i = parentsA; i < parentsB; ++i) {
		lua_getfield(L, -3, "__index");
		lua_replace(L, -4);
	}

	lua_rawgeti(L, -4, 'h');
	size_t hashA = Lua::getInteger<size_t>(L, 1);

	Lua::pushBoolean(L, hashA == hashB);
	return 1;
}

int LuaScriptInterface::luaRawGetMetatable(lua_State* L)
{
	// rawgetmetatable(metatableName)
	luaL_getmetatable(L, Lua::getString(L, 1).c_str());
	return 1;
}

// os
int LuaScriptInterface::luaSystemTime(lua_State* L)
{
	// os.mtime()
	lua_pushinteger(L, OTSYS_TIME());
	return 1;
}

int LuaScriptInterface::luaSystemNanoTime(lua_State* L)
{
	// os.ntime()
	lua_pushinteger(L, OTSYS_NANOTIME());
	return 1;
}

// table
int LuaScriptInterface::luaTableCreate(lua_State* L)
{
	// table.create(arrayLength, keyLength)
	lua_createtable(L, Lua::getInteger<int32_t>(L, 1), Lua::getInteger<int32_t>(L, 2));
	return 1;
}

//
LuaEnvironment::LuaEnvironment() : LuaScriptInterface("Main Interface") {}

LuaEnvironment::~LuaEnvironment()
{
	delete testInterface;
	closeState();
}

bool LuaEnvironment::initState()
{
	luaState = luaL_newstate();
	if (!luaState) {
		return false;
	}

	luaL_openlibs(luaState);
	registerFunctions();

	runningEventId = EVENT_ID_USER;
	return true;
}

bool LuaEnvironment::reInitState()
{
	// TODO: get children, reload children
	closeState();
	return initState();
}

bool LuaEnvironment::closeState()
{
	if (!luaState) {
		return false;
	}

	// Force full garbage collection before cleanup to release Lua-managed memory
	lua_gc(luaState, LUA_GCCOLLECT, 0);
	lua_gc(luaState, LUA_GCCOLLECT, 0);

	for (const auto& combatEntry : combatIdMap) {
		clearCombatObjects(combatEntry.first);
	}

	for (const auto& areaEntry : areaIdMap) {
		clearAreaObjects(areaEntry.first);
	}

	for (auto& timerEntry : timerEvents) {
		LuaTimerEventDesc timerEventDesc = std::move(timerEntry.second);
		for (int32_t parameter : timerEventDesc.parameters) {
			luaL_unref(luaState, LUA_REGISTRYINDEX, parameter);
		}
		luaL_unref(luaState, LUA_REGISTRYINDEX, timerEventDesc.function);
	}

	combatIdMap.clear();
	areaIdMap.clear();
	timerEvents.clear();
	cacheFiles.clear();

	// Release event table reference
	if (eventTableRef != -1) {
		luaL_unref(luaState, LUA_REGISTRYINDEX, eventTableRef);
		eventTableRef = -1;
	}

	lua_close(luaState);
	luaState = nullptr;
	return true;
}

LuaScriptInterface* LuaEnvironment::getTestInterface()
{
	if (!testInterface) {
		testInterface = new LuaScriptInterface("Test Interface");
		testInterface->initState();
	}
	return testInterface;
}

Combat_ptr LuaEnvironment::getCombatObject(uint32_t id) const
{
	auto it = combatMap.find(id);
	if (it == combatMap.end()) {
		return nullptr;
	}
	return it->second;
}

Combat_ptr LuaEnvironment::createCombatObject(LuaScriptInterface* interface)
{
	Combat_ptr combat = std::make_shared<Combat>();
	combatMap[++lastCombatId] = combat;
	combatIdMap[interface].push_back(lastCombatId);
	return combat;
}

void LuaEnvironment::clearCombatObjects(LuaScriptInterface* interface)
{
	auto it = combatIdMap.find(interface);
	if (it == combatIdMap.end()) {
		return;
	}

	for (uint32_t id : it->second) {
		auto itt = combatMap.find(id);
		if (itt != combatMap.end()) {
			combatMap.erase(itt);
		}
	}
	it->second.clear();
}

AreaCombat* LuaEnvironment::getAreaObject(uint32_t id) const
{
	auto it = areaMap.find(id);
	if (it == areaMap.end()) {
		return nullptr;
	}
	return it->second;
}

uint32_t LuaEnvironment::createAreaObject(LuaScriptInterface* interface)
{
	areaMap[++lastAreaId] = new AreaCombat;
	areaIdMap[interface].push_back(lastAreaId);
	return lastAreaId;
}

void LuaEnvironment::clearAreaObjects(LuaScriptInterface* interface)
{
	auto it = areaIdMap.find(interface);
	if (it == areaIdMap.end()) {
		return;
	}

	for (uint32_t id : it->second) {
		auto itt = areaMap.find(id);
		if (itt != areaMap.end()) {
			delete itt->second;
			areaMap.erase(itt);
		}
	}
	it->second.clear();
}

void LuaEnvironment::executeTimerEvent(uint32_t eventIndex)
{
	if (!timerEvents.contains(eventIndex)) {
		return;
	}

	LuaTimerEventDesc timerEventDesc = std::move(timerEvents[eventIndex]);
	timerEvents.erase(eventIndex);

	// push function
	lua_rawgeti(luaState, LUA_REGISTRYINDEX, timerEventDesc.function);

	// push parameters
	for (auto parameter : boost::adaptors::reverse(timerEventDesc.parameters)) {
		lua_rawgeti(luaState, LUA_REGISTRYINDEX, parameter);
		if (lua_getmetatable(luaState, -1) == 0) {
			continue;
		}

		lua_rawgeti(luaState, -1, 't');
		auto type = Lua::getInteger<LuaDataType>(luaState, -1);
		lua_pop(luaState, 2);

		switch (type) {
			case LuaData_Player:
			case LuaData_Monster:
			case LuaData_Npc: {
				if (lua_getiuservalue(luaState, -1, 1)) {
					auto creatureId = Lua::getInteger<uint32_t>(luaState, -1);
					if (auto creature = g_game.getCreatureByID(creatureId)) {
						Lua::pushUserdata<Creature>(luaState, creature);
						Lua::setCreatureMetatable(luaState, -1, creature);
					} else {
						lua_pushnil(luaState);
					}

					lua_replace(luaState, -3);
				}

				lua_pop(luaState, 1);
				break;
			}

			default: {
				break;
			}
		}
	}

	// call the function
	if (reserveScriptEnv()) {
		ScriptEnvironment* env = getScriptEnv();
		env->setTimerEvent();
		env->setScriptId(timerEventDesc.scriptId, this);
		callFunction(timerEventDesc.parameters.size());
	} else {
		LOG_ERROR("[Error - LuaScriptInterface::executeTimerEvent] Call stack overflow");
	}

	// free resources
	luaL_unref(luaState, LUA_REGISTRYINDEX, timerEventDesc.function);
	for (auto parameter : timerEventDesc.parameters) {
		luaL_unref(luaState, LUA_REGISTRYINDEX, parameter);
	}
}

int LuaScriptInterface::luaPlayerGetReset(lua_State* L) // reset system
{
	// player:getReset()
	Player* player = Lua::getUserdata<Player>(L, 1);
	if (player) {
		lua_pushnumber(L, player->getReset());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaPlayerDoReset(lua_State* L) // reset system
{
	// player:doReset()
	Player* player = Lua::getUserdata<Player>(L, 1);
	if (player) {
		player->doReset();
		lua_pushboolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaPlayerSetReset(lua_State* L) // reset system
{
	// player:setReset(reset)
	Player* player = Lua::getUserdata<Player>(L, 1);
	if (player) {
		uint32_t reset = Lua::getNumber<uint32_t>(L, 2);
		player->setReset(reset);
		lua_pushboolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaPlayerReloadWarList(lua_State* L)
{
	// player:reloadWarList()
	Player* player = Lua::getUserdata<Player>(L, 1);
	if (player) {
		player->reloadWarList();
		lua_pushboolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}