// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "configmanager.h"
#include "events.h"
#include "game.h"
#include "logger.h"
#include "luascript.h"
#include "monster.h"
#include "monsters.h"
#include "script.h"
#include "spells.h"
#include "talkaction.h"

#include <fmt/format.h>

extern Events* g_events;
extern Vocations g_vocations;
extern Game g_game;
extern Monsters g_monsters;
extern Scripts* g_scripts;
extern Spells* g_spells;
extern TalkActions* g_talkActions;

extern LuaEnvironment g_luaEnvironment;

namespace {
using namespace Lua;

// Game
int luaGameGetSpectators(lua_State* L)
{
	// Game.getSpectators(position[, multifloor = false[, onlyPlayer = false[, minRangeX = 0[, maxRangeX = 0[,
	// minRangeY = 0[, maxRangeY = 0]]]]]])
	const Position& position = getPosition(L, 1);
	bool multifloor = getBoolean(L, 2, false);
	bool onlyPlayers = getBoolean(L, 3, false);
	int32_t minRangeX = getInteger<int32_t>(L, 4, 0);
	int32_t maxRangeX = getInteger<int32_t>(L, 5, 0);
	int32_t minRangeY = getInteger<int32_t>(L, 6, 0);
	int32_t maxRangeY = getInteger<int32_t>(L, 7, 0);

	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, position, multifloor, onlyPlayers, minRangeX, maxRangeX, minRangeY, maxRangeY);

	lua_createtable(L, spectators.size(), 0);

	int index = 0;
	for (Creature* creature : spectators) {
		// Avoid crashes by ignoring invalid creatures
		if (!creature || creature->isRemoved()) {
			continue;
		}
		pushUserdata<Creature>(L, creature);
		setCreatureMetatable(L, -1, creature);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaGameGetPlayers(lua_State* L)
{
	// Game.getPlayers()
	lua_createtable(L, g_game.getPlayersOnline(), 0);

	int index = 0;
	for (const auto& playerEntry : g_game.getPlayers()) {
		pushUserdata<Player>(L, playerEntry.second);
		setMetatable(L, -1, "Player");
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaGameGetNpcs(lua_State* L)
{
	// Game.getNpcs()
	lua_createtable(L, g_game.getNpcsOnline(), 0);

	int index = 0;
	for (const auto& npcEntry : g_game.getNpcs()) {
		pushUserdata<Npc>(L, npcEntry.second);
		setMetatable(L, -1, "Npc");
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaGameGetMonsters(lua_State* L)
{
	// Game.getMonsters()
	lua_createtable(L, g_game.getMonstersOnline(), 0);

	int index = 0;
	for (const auto& monsterEntry : g_game.getMonsters()) {
		pushUserdata<Monster>(L, monsterEntry.second);
		setMetatable(L, -1, "Monster");
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaGameLoadMap(lua_State* L)
{
	// Game.loadMap(path)
	const std::string& path = getString(L, 1);
	g_dispatcher.addTask([path]() {
		try {
			g_game.loadMap(path);
		} catch (const std::exception& e) {
			LOG_ERROR(fmt::format("[Error - luaGameLoadMap] Failed to load map '{}': {}", path, e.what()));
			LOG_ERROR("Please verify if the file exists and is in the correct location.");
		}
	});
	return 0;
}

int luaGameGetExperienceStage(lua_State* L)
{
	// Game.getExperienceStage(level)
	uint32_t level = getInteger<uint32_t>(L, 1);
	lua_pushnumber(L, ConfigManager::getExperienceStage(level));
	return 1;
}

int luaGameGetSkillStage(lua_State* L)
{
	// Game.getSkillStage(level)
	uint32_t level = getInteger<uint32_t>(L, 1);
	lua_pushnumber(L, ConfigManager::getSkillStage(level));
	return 1;
}

int luaGameGetMagicLevelStage(lua_State* L)
{
	// Game.getMagicLevelStage(level)
	uint32_t level = getInteger<uint32_t>(L, 1);
	lua_pushnumber(L, ConfigManager::getMagicLevelStage(level));
	return 1;
}

int luaGameGetExperienceForLevel(lua_State* L)
{
	// Game.getExperienceForLevel(level)
	const uint32_t level = getInteger<uint32_t>(L, 1);
	if (level == 0) {
		lua_pushinteger(L, 0);
	} else {
		lua_pushinteger(L, Player::getExpForLevel(level));
	}
	return 1;
}

int luaGameGetMonsterCount(lua_State* L)
{
	// Game.getMonsterCount()
	lua_pushinteger(L, g_game.getMonstersOnline());
	return 1;
}

int luaGameGetPlayerCount(lua_State* L)
{
	// Game.getPlayerCount()
	lua_pushinteger(L, g_game.getPlayersOnline());
	return 1;
}

int luaGameGetNpcCount(lua_State* L)
{
	// Game.getNpcCount()
	lua_pushinteger(L, g_game.getNpcsOnline());
	return 1;
}

int luaGameGetMonsterTypes(lua_State* L)
{
	// Game.getMonsterTypes()
	auto& type = g_monsters.monsters;
	lua_createtable(L, type.size(), 0);

	for (auto& mType : type) {
		pushUserdata<MonsterType>(L, &mType.second);
		setMetatable(L, -1, "MonsterType");
		lua_setfield(L, -2, mType.first.c_str());
	}
	return 1;
}

int luaGameGetCurrencyItems(lua_State* L)
{
	// Game.getCurrencyItems()
	const auto& currencyItems = Item::items.currencyItems;
	size_t size = currencyItems.size();
	lua_createtable(L, size, 0);

	for (const auto& it : currencyItems) {
		const ItemType& itemType = Item::items[it.second];
		pushUserdata<const ItemType>(L, &itemType);
		setMetatable(L, -1, "ItemType");
		lua_rawseti(L, -2, size--);
	}
	return 1;
}

int luaGameGetItemTypeByClientId(lua_State* L)
{
	// Game.getItemTypeByClientId(clientId)
	uint16_t spriteId = getInteger<uint16_t>(L, 1);
	const ItemType& itemType = Item::items.getItemIdByClientId(spriteId);
	if (itemType.id != 0) {
		pushUserdata<const ItemType>(L, &itemType);
		setMetatable(L, -1, "ItemType");
	} else {
		lua_pushnil(L);
	}

	return 1;
}

int luaGameGetTalkActions(lua_State* L)
{
	// Game.getTalkActions()
	const auto& talkactions = g_talkActions->getTalkactions();
	lua_createtable(L, talkactions.size(), 0);

	int index = 0;
	for (const auto& talkEntry : talkactions) {
		pushUserdata<const TalkAction>(L, &talkEntry.second);
		setMetatable(L, -1, "TalkAction");
		lua_rawseti(L, -2, ++index);
	}

	return 1;
}

int luaGameGetTowns(lua_State* L)
{
	// Game.getTowns()
	const auto& towns = g_game.map.towns.getTowns();
	lua_createtable(L, towns.size(), 0);

	int index = 0;
	for (auto& townEntry : towns) {
		pushUserdata<Town>(L, townEntry.second);
		setMetatable(L, -1, "Town");
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaGameGetHouses(lua_State* L)
{
	// Game.getHouses()
	const auto& houses = g_game.map.houses.getHouses();
	lua_createtable(L, houses.size(), 0);

	int index = 0;
	for (auto& houseEntry : houses) {
		pushUserdata<House>(L, houseEntry.second);
		setMetatable(L, -1, "House");
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaGameGetOutfits(lua_State* L)
{
	// Game.getOutfits(playerSex)
	if (!isInteger(L, 1)) {
		lua_pushnil(L);
		return 1;
	}

	PlayerSex_t playerSex = getInteger<PlayerSex_t>(L, 1);
	if (playerSex > PLAYERSEX_LAST) {
		lua_pushnil(L);
		return 1;
	}

	const auto& outfits = Outfits::getInstance().getOutfits(playerSex);
	lua_createtable(L, outfits.size(), 0);

	int index = 0;
	for (auto outfit : outfits) {
		pushOutfit(L, outfit);
		lua_rawseti(L, -2, ++index);
	}

	return 1;
}

int luaGameGetMounts(lua_State* L)
{
	// Game.getMounts()
	const auto& mounts = g_game.mounts.getMounts();
	lua_createtable(L, mounts.size(), 0);

	int index = 0;
	for (const auto& mount : mounts) {
		pushMount(L, &mount);
		lua_rawseti(L, -2, ++index);
	}

	return 1;
}

int luaGameGetVocations(lua_State* L)
{
	// Game.getVocations()
	const auto& vocations = g_vocations.getVocations();
	lua_createtable(L, vocations.size(), 0);

	int index = 0;
	for (const auto& [id, vocation] : vocations) {
		pushUserdata<const Vocation>(L, &vocation);
		setMetatable(L, -1, "Vocation");
		lua_rawseti(L, -2, ++index);
	}

	return 1;
}

int luaGameGetRuneSpells(lua_State* L)
{
	// Game.getRuneSpells()
	const auto& runeSpells = g_spells->getRuneSpells();

	lua_createtable(L, runeSpells.size(), 0);

	int index = 0;
	for (const auto& spell : runeSpells | std::views::values) {
		pushUserdata<const Spell>(L, &spell);
		setMetatable(L, -1, "Spell");
		lua_rawseti(L, -2, ++index);
	}

	return 1;
}

int luaGameGetInstantSpells(lua_State* L)
{
	// Game.getInstantSpells()
	const auto& instantSpells = g_spells->getInstantSpells();

	lua_createtable(L, instantSpells.size(), 0);

	int index = 0;
	for (const auto& spell : instantSpells | std::views::values) {
		pushUserdata<const Spell>(L, &spell);
		setMetatable(L, -1, "Spell");
		lua_rawseti(L, -2, ++index);
	}

	return 1;
}

int luaGameGetGameState(lua_State* L)
{
	// Game.getGameState()
	lua_pushinteger(L, g_game.getGameState());
	return 1;
}

int luaGameSetGameState(lua_State* L)
{
	// Game.setGameState(state)
	GameState_t state = getInteger<GameState_t>(L, 1);
	g_game.setGameState(state);
	pushBoolean(L, true);
	return 1;
}

int luaGameGetWorldType(lua_State* L)
{
	// Game.getWorldType()
	lua_pushinteger(L, g_game.getWorldType());
	return 1;
}

int luaGameSetWorldType(lua_State* L)
{
	// Game.setWorldType(type)
	WorldType_t type = getInteger<WorldType_t>(L, 1);
	g_game.setWorldType(type);
	pushBoolean(L, true);
	return 1;
}

int luaGameGetReturnMessage(lua_State* L)
{
	// Game.getReturnMessage(value)
	ReturnValue value = getInteger<ReturnValue>(L, 1);
	pushString(L, getReturnMessage(value));
	return 1;
}

int luaGameGetItemAttributeByName(lua_State* L)
{
	// Game.getItemAttributeByName(name)
	lua_pushinteger(L, stringToItemAttribute(getString(L, 1)));
	return 1;
}

int luaGameCreateItem(lua_State* L)
{
	// Game.createItem(itemId[, count[, position]])
	uint16_t count = getInteger<uint16_t>(L, 2, 1);
	uint16_t id;
	if (isInteger(L, 1)) {
		id = getInteger<uint16_t>(L, 1);
	} else {
		id = Item::items.getItemIdByName(getString(L, 1));
		if (id == 0) {
			lua_pushnil(L);
			return 1;
		}
	}

	const ItemType& it = Item::items[id];
	if (it.stackable) {
		count = std::min<uint16_t>(count, it.stackSize);
	}

	Item* item = Item::CreateItem(id, count);
	if (!item) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) >= 3) {
		const Position& position = getPosition(L, 3);
		Tile* tile = g_game.map.getTile(position);
		if (!tile) {
			delete item;
			lua_pushnil(L);
			return 1;
		}

		g_game.internalAddItem(tile, item, INDEX_WHEREEVER, FLAG_NOLIMIT);
	} else {
		LuaScriptInterface::getScriptEnv()->addTempItem(item);
		item->setParent(VirtualCylinder::virtualCylinder);
	}

	pushUserdata<Item>(L, item);
	setItemMetatable(L, -1, item);
	return 1;
}

int luaGameCreateContainer(lua_State* L)
{
	// Game.createContainer(itemId, size[, position])
	uint16_t size = getInteger<uint16_t>(L, 2);
	uint16_t id;
	if (isInteger(L, 1)) {
		id = getInteger<uint16_t>(L, 1);
	} else {
		id = Item::items.getItemIdByName(getString(L, 1));
		if (id == 0) {
			lua_pushnil(L);
			return 1;
		}
	}

	Container* container = Item::CreateItemAsContainer(id, size);
	if (!container) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) >= 3) {
		const Position& position = getPosition(L, 3);
		Tile* tile = g_game.map.getTile(position);
		if (!tile) {
			delete container;
			lua_pushnil(L);
			return 1;
		}

		g_game.internalAddItem(tile, container, INDEX_WHEREEVER, FLAG_NOLIMIT);
	} else {
		LuaScriptInterface::getScriptEnv()->addTempItem(container);
		container->setParent(VirtualCylinder::virtualCylinder);
	}

	pushUserdata<Container>(L, container);
	setMetatable(L, -1, "Container");
	return 1;
}

int luaGameCreateMonster(lua_State* L)
{
	// Game.createMonster(monsterName, position[, extended = false[, force = false[, magicEffect =
	// CONST_ME_TELEPORT]]])
	Monster* monster = Monster::createMonster(getString(L, 1));
	if (!monster) {
		lua_pushnil(L);
		return 1;
	}

	const Position& position = getPosition(L, 2);
	bool extended = getBoolean(L, 3, false);
	bool force = getBoolean(L, 4, false);
	MagicEffectClasses magicEffect = getInteger<MagicEffectClasses>(L, 5, CONST_ME_TELEPORT);
	if (g_events->eventMonsterOnSpawn(monster, position, false, true) || force) {
		if (g_game.placeCreature(monster, position, extended, force, magicEffect)) {
			pushUserdata<Monster>(L, monster);
			setMetatable(L, -1, "Monster");
		} else {
			delete monster;
			lua_pushnil(L);
		}
	} else {
		delete monster;
		lua_pushnil(L);
	}
	return 1;
}

int luaGameCreateNpc(lua_State* L)
{
	// Game.createNpc(npcName, position[, extended = false[, force = false[, magicEffect = CONST_ME_TELEPORT]]])
	Npc* npc = Npc::createNpc(getString(L, 1));
	if (!npc) {
		lua_pushnil(L);
		return 1;
	}

	const Position& position = getPosition(L, 2);
	bool extended = getBoolean(L, 3, false);
	bool force = getBoolean(L, 4, false);
	MagicEffectClasses magicEffect = getInteger<MagicEffectClasses>(L, 5, CONST_ME_TELEPORT);
	if (g_game.placeCreature(npc, position, extended, force, magicEffect)) {
		pushUserdata<Npc>(L, npc);
		setMetatable(L, -1, "Npc");
	} else {
		delete npc;
		lua_pushnil(L);
	}
	return 1;
}

int luaGameCreateTile(lua_State* L)
{
	// Game.createTile(x, y, z[, isDynamic = false])
	// Game.createTile(position[, isDynamic = false])
	Position position;
	bool isDynamic;
	if (isTable(L, 1)) {
		position = getPosition(L, 1);
		isDynamic = getBoolean(L, 2, false);
	} else {
		position.x = getInteger<uint16_t>(L, 1);
		position.y = getInteger<uint16_t>(L, 2);
		position.z = getInteger<uint16_t>(L, 3);
		isDynamic = getBoolean(L, 4, false);
	}

	Tile* tile = g_game.map.getTile(position);
	if (!tile) {
		std::unique_ptr<Tile> newTile;
		if (isDynamic) {
			newTile = std::make_unique<DynamicTile>(position.x, position.y, position.z);
		} else {
			newTile = std::make_unique<StaticTile>(position.x, position.y, position.z);
		}
		tile = newTile.get();
		g_game.map.setTile(position, std::move(newTile));
	}

	pushUserdata(L, tile);
	setMetatable(L, -1, "Tile");
	return 1;
}

int luaGameCreateMonsterType(lua_State* L)
{
	// Game.createMonsterType(name)
	if (LuaScriptInterface::getScriptEnv()->getScriptInterface() != &g_scripts->getScriptInterface()) {
		reportErrorFunc(L, "MonsterTypes can only be registered in the Scripts interface.");
		lua_pushnil(L);
		return 1;
	}

	const std::string& name = getString(L, 1);
	if (name.length() == 0) {
		lua_pushnil(L);
		return 1;
	}

	MonsterType* monsterType = g_monsters.getMonsterType(name, false);
	if (!monsterType) {
		monsterType = &g_monsters.monsters[boost::algorithm::to_lower_copy<std::string>(name)];
		monsterType->name = name;
		monsterType->nameDescription = "a " + name;
	} else {
		monsterType->info.lootItems.clear();
		monsterType->info.attackSpells.clear();
		monsterType->info.defenseSpells.clear();
		monsterType->info.scripts.clear();
		monsterType->info.thinkEvent = -1;
		monsterType->info.creatureAppearEvent = -1;
		monsterType->info.creatureDisappearEvent = -1;
		monsterType->info.creatureMoveEvent = -1;
		monsterType->info.creatureSayEvent = -1;
	}

	pushUserdata<MonsterType>(L, monsterType);
	setMetatable(L, -1, "MonsterType");
	return 1;
}

int luaGameStartRaid(lua_State* L)
{
	// Game.startRaid(raidName)
	const std::string& raidName = getString(L, 1);

	Raid* raid = g_game.raids.getRaidByName(raidName);
	if (!raid || !raid->isLoaded()) {
		lua_pushinteger(L, RETURNVALUE_NOSUCHRAIDEXISTS);
		return 1;
	}

	if (g_game.raids.getRunning()) {
		lua_pushinteger(L, RETURNVALUE_ANOTHERRAIDISALREADYEXECUTING);
		return 1;
	}

	g_game.raids.setRunning(raid);
	raid->startRaid();
	lua_pushinteger(L, RETURNVALUE_NOERROR);
	return 1;
}

int luaGameSendAnimatedText(lua_State* L)
{
	// Game.sendAnimatedText(message, position, color[, players])
	int parameters = lua_gettop(L);
	if (parameters < 3) {
		pushBoolean(L, false);
		return 1;
	}

	TextColor_t color = getInteger<TextColor_t>(L, 3);
	const Position& position = getPosition(L, 2);
	const std::string& message = getString(L, 1);

	if (!position.x || !position.y) {
		pushBoolean(L, false);
		return 1;
	}

	SpectatorVec spectators;
	if (parameters >= 4) {
		getSpectators<Player>(L, 4, spectators);
	}

	if (spectators.empty()) {
		g_game.addAnimatedText(message, position, color);
	} else {
		g_game.addAnimatedText(spectators, message, position, color);
	}

	pushBoolean(L, true);
	return 1;
}

int luaGameGetClientVersion(lua_State* L)
{
	// Game.getClientVersion()
	lua_createtable(L, 0, 3);
	setField(L, "min", CLIENT_VERSION_MIN);
	setField(L, "max", CLIENT_VERSION_MAX);
	setField(L, "string", CLIENT_VERSION_STR);
	return 1;
}

int luaGameReload(lua_State* L)
{
	// Game.reload(reloadType)
	ReloadTypes_t reloadType = getInteger<ReloadTypes_t>(L, 1);
	if (reloadType == RELOAD_TYPE_GLOBAL) {
		pushBoolean(L, g_luaEnvironment.loadFile("data/global.lua") == 0);
		pushBoolean(L, g_scripts->loadScripts("scripts/lib", true, true));
		lua_gc(g_luaEnvironment.getLuaState(), LUA_GCCOLLECT, 0);
		return 2;
	}

	pushBoolean(L, g_game.reload(reloadType));
	lua_gc(g_luaEnvironment.getLuaState(), LUA_GCCOLLECT, 0);
	return 1;
}

int luaGameGetAccountStorageValue(lua_State* L)
{
	// Game.getAccountStorageValue(accountId, key)
	uint32_t accountId = getInteger<uint32_t>(L, 1);
	uint32_t key = getInteger<uint32_t>(L, 2);

	lua_pushinteger(L, g_game.getAccountStorageValue(accountId, key));

	return 1;
}

int luaGameSetAccountStorageValue(lua_State* L)
{
	// Game.setAccountStorageValue(accountId, key, value)
	uint32_t accountId = getInteger<uint32_t>(L, 1);
	uint32_t key = getInteger<uint32_t>(L, 2);
	int32_t value = getInteger<int32_t>(L, 3);

	g_game.setAccountStorageValue(accountId, key, value);
	pushBoolean(L, true);

	return 1;
}

int luaGameSaveAccountStorageValues(lua_State* L)
{
	// Game.saveAccountStorageValues()
	pushBoolean(L, g_game.saveAccountStorageValues());

	return 1;
}

int luaGameGetWaypoints(lua_State* L)
{
	// Game.getWaypoints()
	lua_createtable(L, g_game.map.waypoints.size(), 0);

	for (const auto& [name, position] : g_game.map.waypoints) {
		pushPosition(L, position);
		setMetatable(L, -1, "Position");
		lua_setfield(L, -2, name.c_str());
	}
	return 1;
}

int luaGameGetThingFromClientPos(lua_State* L)
{
	// Game.getThingFromClientPos(player, position, stackPos)
	const auto player = getPlayer(L, 1);
	const Position& position = getPosition(L, 2);
	const auto stackPos = getInteger<uint8_t>(L, 3);
	auto thing = g_game.internalGetThing(player, position, stackPos, 0, STACKPOS_LOOK);
	pushThing(L, thing);
	return 1;
}

int luaGameGetGameStorageValue(lua_State* L)
{
	// Game.getStorageValue(key)
	uint32_t key = getInteger<uint32_t>(L, 1);

	const auto& value = g_game.getStorageValue(key);
	if (value) {
		lua_pushinteger(L, value.value());
	} else if (isInteger(L, 3)) {
		lua_pushinteger(L, getInteger<int64_t>(L, 3));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaGameSetGameStorageValue(lua_State* L)
{
	// Game.setGameStorageValue(key, value)
	if (!isInteger(L, 1)) {
		reportErrorFunc(L, "Invalid storage key.");
		lua_pushnil(L);
		return 1;
	}

	uint32_t key = getInteger<uint32_t>(L, 1);
	if (isInteger(L, 2)) {
		int64_t value = getInteger<int64_t>(L, 2);
		g_game.setStorageValue(key, value);
	} else {
		g_game.setStorageValue(key, std::nullopt);
	}

	pushBoolean(L, true);
	return 1;
}

int luaGameSaveGameStorageValues(lua_State* L)
{
	// Game.saveStorageValues()
	pushBoolean(L, g_game.saveGameStorageValues());

	return 1;
}
} // namespace

void LuaScriptInterface::registerGame()
{
	// Game
	registerTable("Game");

	registerMethod("Game", "getSpectators", luaGameGetSpectators);
	registerMethod("Game", "getPlayers", luaGameGetPlayers);
	registerMethod("Game", "getNpcs", luaGameGetNpcs);
	registerMethod("Game", "getMonsters", luaGameGetMonsters);
	registerMethod("Game", "loadMap", luaGameLoadMap);

	registerMethod("Game", "getExperienceStage", luaGameGetExperienceStage);
	registerMethod("Game", "getSkillStage", luaGameGetSkillStage);
	registerMethod("Game", "getMagicLevelStage", luaGameGetMagicLevelStage);
	registerMethod("Game", "getExperienceForLevel", luaGameGetExperienceForLevel);
	registerMethod("Game", "getMonsterCount", luaGameGetMonsterCount);
	registerMethod("Game", "getPlayerCount", luaGameGetPlayerCount);
	registerMethod("Game", "getNpcCount", luaGameGetNpcCount);
	registerMethod("Game", "getMonsterTypes", luaGameGetMonsterTypes);
	registerMethod("Game", "getCurrencyItems", luaGameGetCurrencyItems);
	registerMethod("Game", "getItemTypeByClientId", luaGameGetItemTypeByClientId);
	registerMethod("Game", "getTalkActions", luaGameGetTalkActions);

	registerMethod("Game", "getTowns", luaGameGetTowns);
	registerMethod("Game", "getHouses", luaGameGetHouses);
	registerMethod("Game", "getOutfits", luaGameGetOutfits);
	registerMethod("Game", "getMounts", luaGameGetMounts);
	registerMethod("Game", "getVocations", luaGameGetVocations);
	registerMethod("Game", "getRuneSpells", luaGameGetRuneSpells);
	registerMethod("Game", "getInstantSpells", luaGameGetInstantSpells);

	registerMethod("Game", "getGameState", luaGameGetGameState);
	registerMethod("Game", "setGameState", luaGameSetGameState);

	registerMethod("Game", "getWorldType", luaGameGetWorldType);
	registerMethod("Game", "setWorldType", luaGameSetWorldType);

	registerMethod("Game", "getItemAttributeByName", luaGameGetItemAttributeByName);
	registerMethod("Game", "getReturnMessage", luaGameGetReturnMessage);

	registerMethod("Game", "createItem", luaGameCreateItem);
	registerMethod("Game", "createContainer", luaGameCreateContainer);
	registerMethod("Game", "createMonster", luaGameCreateMonster);
	registerMethod("Game", "createNpc", luaGameCreateNpc);
	registerMethod("Game", "createTile", luaGameCreateTile);
	registerMethod("Game", "createMonsterType", luaGameCreateMonsterType);

	registerMethod("Game", "startRaid", luaGameStartRaid);

	registerMethod("Game", "sendAnimatedText", luaGameSendAnimatedText);

	registerMethod("Game", "getClientVersion", luaGameGetClientVersion);

	registerMethod("Game", "reload", luaGameReload);

	registerMethod("Game", "getAccountStorageValue", luaGameGetAccountStorageValue);
	registerMethod("Game", "setAccountStorageValue", luaGameSetAccountStorageValue);
	registerMethod("Game", "saveAccountStorageValues", luaGameSaveAccountStorageValues);

	registerMethod("Game", "getWaypoints", luaGameGetWaypoints);
	registerMethod("Game", "getThingFromClientPos", luaGameGetThingFromClientPos);

	registerMethod("Game", "getStorageValue", luaGameGetGameStorageValue);
	registerMethod("Game", "setStorageValue", luaGameSetGameStorageValue);
	registerMethod("Game", "saveStorageValues", luaGameSaveGameStorageValues);
}
