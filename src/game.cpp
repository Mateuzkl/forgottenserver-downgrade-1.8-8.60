// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "game.h"

#include "actions.h"
#include "bed.h"
#include "configmanager.h"
#include "creature.h"
#include "creatureevent.h"
#include "databasetasks.h"
#include "events.h"
#include "globalevent.h"
#include "instance_utils.h"
#include "iologindata.h"
#include "items.h"
#include "monster.h"
#include "movement.h"
#include "outputmessage.h"
#include "pugicast.h"
#include "decay.h"
#include "scheduler.h"
#include "script.h"
#include "server.h"
#include "stats.h"
#include "spells.h"
#include "spy.h"
#include "storeinbox.h"
#include "talkaction.h"
#include "scriptmanager.h"
#include "tools.h"
#include "weapons.h"
#include "logger.h"
#include <fmt/format.h>
#include "luascript.h"
#include "save_manager.h"

extern Vocations g_vocations;
extern Monsters g_monsters;
extern LuaEnvironment g_luaEnvironment;

namespace {

bool areDifferentNonZeroInstances(const Creature* first, const Creature* second)
{
	if (!first || !second) {
		return false;
	}

	const uint32_t firstInstanceId = first->getInstanceID();
	const uint32_t secondInstanceId = second->getInstanceID();
	return firstInstanceId != 0 && secondInstanceId != 0 && firstInstanceId != secondInstanceId;
}

bool isLocalPositionTalk(SpeakClasses type)
{
	return type == TALKTYPE_SAY || type == TALKTYPE_WHISPER || type == TALKTYPE_YELL ||
	       type == TALKTYPE_MONSTER_SAY || type == TALKTYPE_MONSTER_YELL ||
	       type == TALKTYPE_PRIVATE_NP || type == TALKTYPE_PRIVATE_PN;
}

bool isInsideStoreInbox(const Cylinder* cylinder)
{
	while (cylinder) {
		if (dynamic_cast<const StoreInbox*>(cylinder)) {
			return true;
		}
		cylinder = cylinder->getParent();
	}
	return false;
}

bool isCarriedByCreature(const Cylinder* cylinder)
{
	if (!cylinder) {
		return false;
	}

	if (cylinder->getCreature()) {
		return true;
	}

	const Item* item = cylinder->getItem();
	if (!item) {
		return false;
	}

	const Cylinder* topParent = item->getTopParent();
	return topParent && topParent->getCreature();
}

uint32_t getDestinationInstanceId(const Player* actor, const Cylinder* destination, uint32_t sourceInstanceId)
{
	if (!destination || isCarriedByCreature(destination) || !destination->getTile()) {
		return 0;
	}
	return actor ? actor->getInstanceID() : sourceInstanceId;
}

std::string getDamageAnimatedText(int32_t value)
{
	if (getBoolean(ConfigManager::MODIFY_DAMAGE_IN_K)) {
		return formatValueK(value);
	}
	return fmt::format("{:d}", value);
}

std::string getDamageStatusValue(int32_t value)
{
	if (getBoolean(ConfigManager::MODIFY_DAMAGE_IN_K)) {
		return formatValueK(value);
	}
	return std::to_string(value);
}

std::string getHitpointStatusString(int32_t value)
{
	return fmt::format("{:s} hitpoint{:s}", getDamageStatusValue(value), value != 1 ? "s" : "");
}

ReturnValue getStoreInboxLockedItemMoveReturn(const Item* item)
{
	if (!item || !isInsideStoreInbox(item->getParent())) {
		return RETURNVALUE_NOERROR;
	}

	if (item->isExerciseWeapon()) {
		return RETURNVALUE_CANNOTMOVEEXERCISEWEAPON;
	}

	const auto container = item->getContainer();
	if (!container) {
		return RETURNVALUE_NOERROR;
	}

	for (const auto& child : container->getItemList()) {
		const ReturnValue ret = getStoreInboxLockedItemMoveReturn(child.get());
		if (ret != RETURNVALUE_NOERROR) {
			return ret;
		}
	}
	return RETURNVALUE_NOERROR;
}

int64_t getMoveItemExhaustionDelay(const Position& toPos)
{
	if (ConfigManager::getBoolean(ConfigManager::SEPARATE_RING_NECKLACE_EXHAUSTION) && toPos.x == 0xFFFF &&
	    (toPos.y & 0x40) == 0) {
		if (toPos.y == CONST_SLOT_NECKLACE) {
			return getInteger(ConfigManager::NECKLACE_DELAY_INTERVAL);
		}
		if (toPos.y == CONST_SLOT_RING) {
			return getInteger(ConfigManager::RING_DELAY_INTERVAL);
		}
	}
	return getInteger(ConfigManager::ACTIONS_DELAY_INTERVAL);
}

bool hasNotMoveableActionId(const Item& item)
{
	return item.getActionId() == ACTIONID_NOT_MOVEABLE;
}

void closeContainersFromOtherInstances(Player* player)
{
	if (!player) {
		return;
	}

	std::vector<uint8_t> closeList;
	for (const auto& it : player->getOpenContainers()) {
		auto container = it.second.container.lock();
		if (container && container->getInstanceID() != 0 &&
		    container->getInstanceID() != player->getInstanceID()) {
			closeList.push_back(it.first);
		}
	}

	for (uint8_t cid : closeList) {
		player->closeContainer(cid);
		player->sendCloseContainer(cid);
	}
}

struct QuickLootResult
{
	uint32_t movedItems = 0;
	bool hadLoot = false;
	ReturnValue failure = RETURNVALUE_NOERROR;
};

bool isQuickLootCorpseType(const Container* container)
{
	if (!container) {
		return false;
	}

	if (container->isRewardCorpse()) {
		return true;
	}

	const ItemType& type = Item::items[container->getID()];
	return type.corpseType != RACE_NONE || container->getCorpseOwner() != 0;
}

const Container* getQuickLootCorpseContainer(const Container* container)
{
	for (auto* current = container; current; current = dynamic_cast<const Container*>(current->getParent())) {
		if (isQuickLootCorpseType(current)) {
			return current;
		}
	}
	return nullptr;
}

ReturnValue getQuickLootContainerReturn(const Player* player, const Container* container)
{
	const Container* corpseContainer = getQuickLootCorpseContainer(container);
	if (!player || !corpseContainer) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (corpseContainer->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID) || corpseContainer->hasAttribute(ITEM_ATTRIBUTE_ACTIONID)) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (corpseContainer->isRewardCorpse()) {
		return RETURNVALUE_NOERROR;
	}

	const uint32_t owner = corpseContainer->getCorpseOwner();
	if (owner != 0 && !player->canOpenCorpse(owner) && !player->hasFlag(PlayerFlag_CanEditHouses)) {
		return RETURNVALUE_YOUARENOTTHEOWNER;
	}
	return RETURNVALUE_NOERROR;
}

bool shouldQuickLootItem(const Player* player, const Item* item)
{
	if (!player || !item || !item->isPickupable()) {
		return false;
	}

	const bool listed = player->isQuickLootListedItem(item);
	if (player->getQuickLootFilter() == QUICKLOOTFILTER_ACCEPTEDLOOT) {
		return listed;
	}
	return !listed;
}

Container* getQuickLootDestination(Player* player)
{
	if (!player || !player->getQuickLootFallbackToMainContainer()) {
		return nullptr;
	}

	Item* backpackItem = player->getInventoryItem(CONST_SLOT_BACKPACK);
	return backpackItem ? backpackItem->getContainer() : nullptr;
}

QuickLootResult collectQuickLootContainer(Game& game, Player* player, Container* container)
{
	QuickLootResult result;
	const ReturnValue containerRet = getQuickLootContainerReturn(player, container);
	if (containerRet != RETURNVALUE_NOERROR) {
		result.failure = containerRet;
		return result;
	}

	Container* destination = getQuickLootDestination(player);
	if (!destination) {
		result.failure = RETURNVALUE_CONTAINERNOTENOUGHROOM;
		return result;
	}

	std::vector<Item*> lootItems;
	for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
		Item* item = *it;
		if (!item || item->isRemoved() || !shouldQuickLootItem(player, item)) {
			continue;
		}

		result.hadLoot = true;
		lootItems.push_back(item);
	}

	for (Item* item : lootItems) {
		if (!item || item->isRemoved()) {
			continue;
		}

		Cylinder* fromCylinder = item->getParent();
		if (!fromCylinder || fromCylinder == destination) {
			continue;
		}

		const uint16_t originalCount = item->getItemCount();
		ReturnValue ret = game.internalMoveItem(fromCylinder, destination, INDEX_WHEREEVER, item,
		                                        originalCount, nullptr, 0, player);
		if (ret == RETURNVALUE_NOERROR) {
			++result.movedItems;
			continue;
		}

		const uint16_t remainingCount = item->isRemoved() ? 0 : item->getItemCount();
		if (remainingCount < originalCount) {
			++result.movedItems;
		} else if (result.failure == RETURNVALUE_NOERROR) {
			result.failure = ret;
		}
	}

	return result;
}

uint32_t collectQuickLootTile(Game& game, Player* player, const Position& pos, uint32_t maxCorpses,
                              bool& foundCorpse, ReturnValue& firstFailure)
{
	Tile* tile = game.map.getTile(pos);
	if (!tile) {
		firstFailure = RETURNVALUE_NOTPOSSIBLE;
		return 0;
	}

	const TileItemVector* itemList = tile->getItemList();
	if (!itemList) {
		return 0;
	}

	uint32_t lootedCorpses = 0;
	for (const auto& itemRef : *itemList) {
		if (lootedCorpses >= maxCorpses) {
			break;
		}

		Container* container = itemRef ? itemRef->getContainer() : nullptr;
		if (!isQuickLootCorpseType(container)) {
			continue;
		}

		foundCorpse = true;
		QuickLootResult result = collectQuickLootContainer(game, player, container);
		if (result.movedItems > 0) {
			++lootedCorpses;
		} else if (result.failure != RETURNVALUE_NOERROR && firstFailure == RETURNVALUE_NOERROR) {
			firstFailure = result.failure;
		}
	}
	return lootedCorpses;
}

} // namespace

void Game::start(const std::shared_ptr<ServiceManager>& manager)
{
	serviceManager = manager;
	updateWorldTime();

	// Initialize offline training window
	offlineTrainingWindow.choices.emplace_back("Sword Fighting and Shielding", SKILL_SWORD);
	offlineTrainingWindow.choices.emplace_back("Axe Fighting and Shielding", SKILL_AXE);
	offlineTrainingWindow.choices.emplace_back("Club Fighting and Shielding", SKILL_CLUB);
	offlineTrainingWindow.choices.emplace_back("Distance Fighting and Shielding", SKILL_DISTANCE);
	offlineTrainingWindow.choices.emplace_back("Magic Level and Shielding", SKILL_MAGLEVEL);
	offlineTrainingWindow.buttons.emplace_back("Start", 1);
	offlineTrainingWindow.buttons.emplace_back("Cancel", 0);

	if (ConfigManager::getBoolean(ConfigManager::DEFAULT_WORLD_LIGHT)) {
		g_scheduler.addEvent(createSchedulerTask(EVENT_LIGHTINTERVAL, [this]() { checkLight(); }));
	}
	g_scheduler.addEvent(createSchedulerTask(EVENT_CREATURE_THINK_INTERVAL, [this]() { checkCreatures(0); }));
	g_scheduler.addEvent(createSchedulerTask(1000, [this]() { checkSereneStatus(); }));
}

GameState_t Game::getGameState() const { return gameState.load(std::memory_order_acquire); }

void Game::setWorldType(WorldType_t type) { worldType = type; }

void Game::registerInstanceArea(uint32_t instanceId, const Position& fromPos, const Position& toPos)
{
	if (instanceId == 0) {
		return;
	}

	instanceAreas[instanceId] = {fromPos, toPos};
}

void Game::unregisterInstanceArea(uint32_t instanceId)
{
	instanceAreas.erase(instanceId);
}

const Game::InstanceArea* Game::getInstanceArea(uint32_t instanceId) const
{
	const auto it = instanceAreas.find(instanceId);
	return it != instanceAreas.end() ? &it->second : nullptr;
}

bool Game::isPositionInArea(const Position& pos, const Position& fromPos, const Position& toPos)
{
	const int32_t minX = std::min<int32_t>(fromPos.x, toPos.x);
	const int32_t maxX = std::max<int32_t>(fromPos.x, toPos.x);
	const int32_t minY = std::min<int32_t>(fromPos.y, toPos.y);
	const int32_t maxY = std::max<int32_t>(fromPos.y, toPos.y);
	const int32_t minZ = std::min<int32_t>(fromPos.z, toPos.z);
	const int32_t maxZ = std::max<int32_t>(fromPos.z, toPos.z);
	return pos.x >= minX && pos.x <= maxX && pos.y >= minY && pos.y <= maxY && pos.z >= minZ && pos.z <= maxZ;
}

void Game::setGameState(GameState_t newState)
{
	if (gameState.load(std::memory_order_acquire) == GAME_STATE_SHUTDOWN) {
		return; // this cannot be stopped
	}

	if (gameState.load(std::memory_order_acquire) == newState) {
		return;
	}

	gameState.store(newState, std::memory_order_release);
	switch (newState) {
		case GAME_STATE_INIT: {
			groups.load();
			g_chat->load();

			map.spawns.startup();

			mounts.loadFromXml();

			raids.loadFromXml();
			raids.startup();

			loadMotdNum();
			loadPlayersRecord();
			loadGameStorageValues();
			loadAccountStorageValues();

			g_globalEvents->startup();
			break;
		}

		case GAME_STATE_SHUTDOWN: {
			LOG_INFO(">> Starting shutdown sequence...");

			g_globalEvents->save();
			g_globalEvents->shutdown();
			LOG_INFO(">> Global events saved and shutdown.");

			// kick all players that are still online
			while (true) {
				auto onlinePlayers = getPlayers();
				if (onlinePlayers.empty()) {
					break;
				}
				onlinePlayers.front()->kickPlayer(true);
			}
			LOG_INFO(">> All players kicked.");

			// Stop spawn scheduler events to prevent new spawns during shutdown.
			// Actual spawn cleanup (refcount decrements) is deferred to
			// Game::shutdown() where it runs AFTER all creatures are removed,
			// avoiding a race where scheduler-dispatched checkSpawn tasks
			// could access destroyed Spawn objects.
			for (const auto& spawn : map.spawns.getSpawnList()) {
				spawn->stopEvent();
			}

			saveMotdNum();
			saveGameState();

			g_scheduler.stop();
			g_databaseTasks.stop();
			g_dispatcher.stop();
#ifdef STATS_ENABLED
			g_stats.stop();
#endif
			shutdown();
			LOG_INFO(">> Shutdown complete.");
			break;
		}

		case GAME_STATE_CLOSED: {
			g_globalEvents->save();

			/* kick all players without the CanAlwaysLogin flag */
			for (const auto& player : getPlayers()) {
				if (!player->hasFlag(PlayerFlag_CanAlwaysLogin)) {
					player->kickPlayer(true);
				}
			}

			saveGameState();
			break;
		}

		default:
			break;
	}
}

void Game::saveGameState(bool crash /* = false */)
{
	AutoStat stat("Game::saveGameState", crash ? "crash" : "full");
	if (gameState == GAME_STATE_NORMAL) {
		setGameState(GAME_STATE_MAINTAIN);
	}

	if (crash) {
		LOG_WARN("[Anti-Rollback] Server crash detected — emergency save initiated.");
	}

	uint32_t savedCount = 0;
	for (const auto& player : getPlayers()) {
		if (crash) {
			const Town* town = player->getTown();
			if (town) {
				player->loginPosition = town->getTemplePosition();
			} else {
				player->loginPosition = player->getPosition();
			}
		} else {
			player->loginPosition = player->getPosition();
		}
		++savedCount;
	}

	g_saveManager.saveAll();
	g_databaseTasks.flush();

	if (crash && savedCount > 0) {
		LOG_WARN(fmt::format("[Anti-Rollback] Emergency save completed — {} player(s) saved at temple.", savedCount));
	}

	if (gameState == GAME_STATE_MAINTAIN) {
		setGameState(GAME_STATE_NORMAL);
	}
}

bool Game::loadMainMap(std::string_view filename)
{
	return map.loadMap(fmt::format("data/world/{}.otbm", filename), true);
}

void Game::loadMap(const std::string& path) { map.loadMap(path, false); }

Cylinder* Game::internalGetCylinder(Player* player, const Position& pos) const
{
	if (pos.x != 0xFFFF) {
		return map.getTile(pos);
	}

	// container
	if (pos.y & 0x40) {
		uint8_t from_cid = pos.y & 0x0F;
		return player->getContainerByID(from_cid);
	}

	// inventory
	return player;
}

Thing* Game::internalGetThing(Player* player, const Position& pos, int32_t index, uint32_t spriteId,
                              stackPosType_t type) const
{
	if (pos.x != 0xFFFF) {
		Tile* tile = map.getTile(pos);
		if (!tile) {
			return nullptr;
		}

		Thing* thing;
		switch (type) {
			case STACKPOS_LOOK: {
				return tile->getTopVisibleThing(player);
			}

			case STACKPOS_MOVE: {
				Item* item = tile->getTopDownItem(player->getInstanceID());
				if (item && item->isMoveable()) {
					thing = item;
				} else {
					thing = tile->getTopVisibleCreature(player);
				}
				break;
			}

			case STACKPOS_USEITEM: {
				thing = tile->getUseItem(index, player);
				break;
			}

			case STACKPOS_TOPDOWN_ITEM: {
				thing = tile->getTopDownItem(player->getInstanceID());
				break;
			}

			case STACKPOS_USETARGET: {
				thing = tile->getTopVisibleCreature(player);
				if (!thing) {
					thing = tile->getUseItem(index, player);
				}
				break;
			}

			default: {
				thing = nullptr;
				break;
			}
		}

		if (player && player->getPosition().z == tile->getPosition().z) {
			const Position& playerPos = player->getPosition();
			const Position& tilePos = tile->getPosition();

			if (playerPos.y < tilePos.y && tile->hasProperty(CONST_PROP_ISHORIZONTAL) && tile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
				thing = nullptr;
			} else if (playerPos.x < tilePos.x && tile->hasProperty(CONST_PROP_ISVERTICAL) && tile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
				thing = nullptr;
			} else {
				if (const Tile* playerTile = player->getTile()) {
					if (tilePos.y < playerPos.y && playerTile->hasProperty(CONST_PROP_ISHORIZONTAL) && playerTile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
						thing = nullptr;
					} else if (tilePos.x < playerPos.x && playerTile->hasProperty(CONST_PROP_ISVERTICAL) && playerTile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
						thing = nullptr;
					}
				}
			}
		}
		return thing;
	}

	// container
	if (pos.y & 0x40) {
		uint8_t fromCid = pos.y & 0x0F;

		Container* parentContainer = player->getContainerByID(fromCid);
		if (!parentContainer) {
			return nullptr;
		}

		uint8_t slot = pos.z;
		return parentContainer->getItemByIndex(player->getContainerIndex(fromCid) + slot);
	} else if (pos.y == 0 && pos.z == 0) {
		const ItemType& it = Item::items[static_cast<uint16_t>(spriteId)];
		if (it.id == 0) {
			return nullptr;
		}

		int32_t subType;
		if (it.isFluidContainer() && index < static_cast<int32_t>(sizeof(reverseFluidMap) / sizeof(uint8_t))) {
			subType = reverseFluidMap[index];
		} else {
			subType = -1;
		}

		return findItemOfType(player, it.id, true, subType);
	}

	// inventory
	slots_t slot = static_cast<slots_t>(pos.y);
	if (slot == CONST_SLOT_STORE_INBOX) {
		return player->getStoreInbox();
	}
	return player->getInventoryItem(slot);
}

void Game::internalGetPosition(Item* item, Position& pos, uint8_t& stackpos)
{
	pos.x = 0;
	pos.y = 0;
	pos.z = 0;
	stackpos = 0;

	Cylinder* topParent = item->getTopParent();
	if (topParent) {
		if (Player* player = dynamic_cast<Player*>(topParent)) {
			pos.x = 0xFFFF;

			Container* container = dynamic_cast<Container*>(item->getParent());
			if (container) {
				pos.y = static_cast<uint16_t>(0x40) | static_cast<uint16_t>(player->getContainerID(container));
				pos.z = static_cast<uint8_t>(container->getThingIndex(item));
				stackpos = pos.z;
			} else {
				pos.y = static_cast<uint16_t>(player->getThingIndex(item));
				stackpos = pos.y;
			}
		} else if (Tile* tile = topParent->getTile()) {
			pos = tile->getPosition();
			stackpos = static_cast<uint16_t>(tile->getThingIndex(item));
		}
	}
}

Creature* Game::getCreatureByID(uint32_t id)
{
	if (id <= Player::playerAutoID) {
		return getPlayerByID(id).get();
	} else if (id <= Monster::monsterAutoID) {
		return getMonsterByID(id);
	} else if (id <= Npc::npcAutoID) {
		return getNpcByID(id);
	}
	return nullptr;
}

Monster* Game::getMonsterByID(uint32_t id)
{
	if (id == 0) {
		return nullptr;
	}

	auto it = monsters.find(id);
	if (it == monsters.end()) {
		return nullptr;
	}
	auto monster = it->second.lock();
	return monster.get();
}

Npc* Game::getNpcByID(uint32_t id)
{
	if (id == 0) {
		return nullptr;
	}

	auto it = npcs.find(id);
	if (it == npcs.end()) {
		return nullptr;
	}
	auto npc = it->second.lock();
	return npc.get();
}

std::shared_ptr<Player> Game::getPlayerByID(uint32_t id)
{
	if (id == 0) {
		return nullptr;
	}

	std::shared_lock<std::shared_mutex> lock(playersMutex);
	auto it = players.find(id);
	if (it == players.end()) {
		return nullptr;
	}
	return it->second.lock();
}

Creature* Game::getCreatureByName(std::string_view s)
{
	if (s.empty()) {
		return nullptr;
	}

	const std::string lowerCaseName = asLowerCaseString(std::string{s});

	{
		std::shared_lock<std::shared_mutex> lock(playersMutex);
		auto it = mappedPlayerNames.find(lowerCaseName);
		if (it != mappedPlayerNames.end()) {
			return it->second.lock().get();
		}
	}

	auto equalCreatureName = [&](const auto& it) {
		auto creature = it.second.lock();
		if (!creature) {
			return false;
		}

		auto& name = creature->getName();
		return caseInsensitiveEqual(lowerCaseName, name);
	};

	{
		auto it = std::find_if(npcs.begin(), npcs.end(), equalCreatureName);
		if (it != npcs.end()) {
			auto npc = it->second.lock();
			return npc.get();
		}
	}

	{
		auto it = std::find_if(monsters.begin(), monsters.end(), equalCreatureName);
		if (it != monsters.end()) {
			auto monster = it->second.lock();
			return monster.get();
		}
	}

	return nullptr;
}

Npc* Game::getNpcByName(std::string_view npcName)
{
	if (npcName.empty()) {
		return nullptr;
	}

	for (const auto& it : npcs) {
		auto npc = it.second.lock();
		if (npc && caseInsensitiveEqual(npcName, npc->getName())) {
			return npc.get();
		}
	}
	return nullptr;
}

std::shared_ptr<Player> Game::getPlayerByName(std::string_view s)
{
	if (s.empty()) {
		return nullptr;
	}

	std::shared_lock<std::shared_mutex> lock(playersMutex);
	auto it = mappedPlayerNames.find(asLowerCaseString(std::string{s}));
	if (it == mappedPlayerNames.end()) {
		return nullptr;
	}
	return it->second.lock();
}

std::shared_ptr<Player> Game::getPlayerByGUID(const uint32_t& guid)
{
	if (guid == 0) {
		return nullptr;
	}

	std::shared_lock<std::shared_mutex> lock(playersMutex);
	auto it = mappedPlayerGuids.find(guid);
	if (it == mappedPlayerGuids.end()) {
		return nullptr;
	}
	return it->second.lock();
}

ReturnValue Game::getPlayerByNameWildcard(std::string_view s, std::shared_ptr<Player>& player)
{
	size_t strlen = s.length();
	if (strlen == 0 || strlen > PLAYER_NAME_LENGTH) {
		return RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE;
	}

	if (s.back() == '~') {
		auto query = asLowerCaseString(std::string{s.substr(0, strlen - 1)});
		std::string result;
		ReturnValue ret;
		
		{
			std::shared_lock<std::shared_mutex> lock(playersMutex);
			ret = wildcardTree.findOne(query, result);
		}
		
		if (ret != RETURNVALUE_NOERROR) {
			return ret;
		}

		player = getPlayerByName(result);
	} else {
		player = getPlayerByName(s);
	}

	if (!player) {
		return RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE;
	}

	return RETURNVALUE_NOERROR;
}

std::shared_ptr<Player> Game::getPlayerByAccount(uint32_t acc)
{
	std::shared_lock<std::shared_mutex> lock(playersMutex);
	for (const auto& it : players) {
		auto player = it.second.lock();
		if (player && player->getAccount() == acc) {
			return player;
		}
	}
	return nullptr;
}

bool Game::reserveLogin(uint32_t guid)
{
	std::unique_lock<std::shared_mutex> lock(pendingLoginsMutex);
	return pendingLogins.emplace(guid).second;
}

void Game::releaseLogin(uint32_t guid)
{
	std::unique_lock<std::shared_mutex> lock(pendingLoginsMutex);
	pendingLogins.erase(guid);
}

bool Game::isLoginPending(uint32_t guid) const
{
	std::shared_lock<std::shared_mutex> lock(pendingLoginsMutex);
	return pendingLogins.contains(guid);
}

std::vector<std::shared_ptr<Player>> Game::getPlayers() const
{
	std::vector<std::shared_ptr<Player>> onlinePlayers;
	std::shared_lock<std::shared_mutex> lock(playersMutex);
	onlinePlayers.reserve(players.size());
	for (const auto& [id, weakPlayer] : players) {
		if (auto player = weakPlayer.lock()) {
			onlinePlayers.emplace_back(std::move(player));
		}
	}
	return onlinePlayers;
}

bool Game::internalPlaceCreature(Creature* creature, const Position& pos, bool extendedPos /*=false*/,
                                 bool forced /*= false*/)
{
	if (creature->getParent() != nullptr) {
		return false;
	}

	creature->setID();
	std::shared_ptr<Creature> creatureRef = creature->weak_from_this().lock();
	if (!creatureRef) {
		// The caller must already hold a shared_ptr to this creature.
		// Creating a new shared_ptr here would produce a second control block
		// for the same raw pointer, leading to double-free / use-after-free.
		return false;
	}
	
	{
		std::unique_lock<std::shared_mutex> lock(creatureRefsMutex);
		creatureSharedRefs[creature->getID()] = creatureRef;
	}

	if (!map.placeCreature(pos, creature, extendedPos, forced)) {
		std::unique_lock<std::shared_mutex> lock(creatureRefsMutex);
		creatureSharedRefs.erase(creature->getID());
		return false;
	}

	creature->addList();

	return true;
}

bool Game::placeCreature(Creature* creature, const Position& pos, bool extendedPos /*=false*/, bool forced /*= false*/,
                         MagicEffectClasses magicEffect /*= CONST_ME_TELEPORT*/)
{
	if (!internalPlaceCreature(creature, pos, extendedPos, forced)) {
		return false;
	}

	auto creatureRef = getCreatureSharedRef(creature);

	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true);
	for (const auto& spectator : spectators.players()) {
		Player* tmpPlayer = static_cast<Player*>(spectator.get());
		if (!tmpPlayer->compareInstance(creature->getInstanceID())) {
			continue;
		}

		if (tmpPlayer->canSeeCreature(creature)) {
			tmpPlayer->sendCreatureAppear(creature, creature->getPosition(), magicEffect);
		}
	}

	for (const auto& spectator : spectators) {
		if (!spectator || !spectator->compareInstance(creature->getInstanceID())) {
			continue;
		}

		spectator->onCreatureAppear(creature, true);
	}

	creature->getParent()->postAddNotification(creature, nullptr, 0);

	addCreatureCheck(creature);
	creature->onPlacedCreature();
	return true;
}

bool Game::removeCreature(Creature* creature, bool isLogout /* = true*/)
{
	if (creature->isRemoved()) {
		return false;
	}

	auto creatureRef = getCreatureSharedRef(creature);
	if (!creatureRef) {
		return false;
	}

	Tile* tile = creature->getTile();

	SpectatorVec spectators;
	map.getSpectators(spectators, tile->getPosition(), true);

	std::vector<int32_t> oldStackPosVector;
	oldStackPosVector.reserve(spectators.size());
	for (const auto& spectator : spectators.players()) {
		Player* player = static_cast<Player*>(spectator.get());
		oldStackPosVector.push_back(
		    player->canSeeCreature(creature) ? tile->getClientIndexOfCreature(player, creature) : -1);
	}

	tile->removeCreature(creature);

	const Position& tilePosition = tile->getPosition();

	// send to client
	size_t i = 0;
	for (const auto& spectator : spectators.players()) {
		Player* player = static_cast<Player*>(spectator.get());
		if (player->canSeeCreature(creature)) {
			player->sendRemoveTileThing(tilePosition, oldStackPosVector[i]);
		}
		++i;
	}

	// event method
	for (const auto& spectator : spectators) {
		if (!spectator || !spectator->compareInstance(creature->getInstanceID())) {
			continue;
		}

		spectator->onRemoveCreature(creature, isLogout);
	}

	auto master = creature->getMaster();
	if (master && !master->isRemoved()) {
		creature->setMaster(nullptr);
	}

	creature->getParent()->postRemoveNotification(creature, nullptr, 0);

	creature->removeList();
	creature->setRemoved();

	// Eagerly release internal structures while the creature still exists.
	// These would be cleaned by the destructor eventually, but releasing now
	// prevents the allocations from being reported as leaked if the
	// destructor runs late due to refcount chain dependencies.
	creature->damageMap.clear();

	ReleaseCreature(creatureRef);

	removeCreatureCheck(creature);

	// Explicitly clear master ref on each summon BEFORE recursive removal.
	// removeCreature(summon) would skip setMaster(nullptr) because 'creature'
	// is already marked removed. Clearing here avoids relying on destructor
	// ordering for the master ref decrement.
	std::vector<std::shared_ptr<Creature>> summonRefs;
	summonRefs.reserve(creature->summons.size());
	for (const auto& summonRef : creature->summons) {
		if (auto summon = summonRef.lock()) {
			summonRefs.push_back(std::move(summon));
		}
	}
	creature->summons.clear();

	for (const auto& summon : summonRefs) {
		summon->setSkillLoss(false);
		if (summon->getMaster().get() == creature) {
			summon->removeMaster();
		}
		removeCreature(summon.get());
	}

	// Drop the shared_ptr anchor from the global creature registry.
	{
		std::unique_lock<std::shared_mutex> lock(creatureRefsMutex);
		creatureSharedRefs.erase(creature->getID());
	}

	return true;
}

void Game::executeDeath(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (creature && !creature->isRemoved()) {
		creature->onDeath();
	}
}

void Game::playerMoveThing(uint32_t playerId, const Position& fromPos, uint16_t spriteId, uint8_t fromStackPos,
                           const Position& toPos, uint8_t count)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	uint8_t fromIndex = 0;
	if (fromPos.x == 0xFFFF) {
		if (fromPos.y & 0x40) {
			fromIndex = fromPos.z;
		} else {
			fromIndex = static_cast<uint8_t>(fromPos.y);
		}
	} else {
		fromIndex = fromStackPos;
	}

	Thing* thing = internalGetThing(player, fromPos, fromIndex, 0, STACKPOS_MOVE);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (Creature* movingCreature = thing->getCreature()) {
		Tile* tile = map.getTile(toPos);
		if (!tile) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		if (movingCreature->getPosition().isInRange(player->getPosition(), 1, 1, 0)) {
			auto task =
			    createSchedulerTask(static_cast<uint32_t>(getInteger(ConfigManager::RANGE_MOVE_CREATURE_INTERVAL)),
			                        ([=, this, playerID = player->getID(), creatureID = movingCreature->getID()]() {
				                        playerMoveCreatureByID(playerID, creatureID, fromPos, toPos);
			                        }));
			player->setNextActionTask(std::move(task));
		} else {
			playerMoveCreature(player, movingCreature, movingCreature->getPosition(), tile);
		}
	} else if (thing->getItem()) {
		Cylinder* toCylinder = internalGetCylinder(player, toPos);
		if (!toCylinder) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		playerMoveItem(player, fromPos, spriteId, fromStackPos, toPos, count, thing->getItem(), toCylinder);
	}
}

void Game::playerMoveCreatureByID(uint32_t playerId, uint32_t movingCreatureId, const Position& movingCreatureOrigPos,
                                  const Position& toPos)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Creature* movingCreature = getCreatureByID(movingCreatureId);
	if (!movingCreature) {
		return;
	}

	Tile* toTile = map.getTile(toPos);
	if (!toTile) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	playerMoveCreature(player, movingCreature, movingCreatureOrigPos, toTile);
}

void Game::playerMoveCreature(Player* player, Creature* movingCreature, const Position& movingCreatureOrigPos,
                              Tile* toTile)
{
	if (player->hasFlag(PlayerFlag_CanThrowFar)) {
		if (g_events->eventPlayerOnMoveCreature(player, movingCreature, movingCreature->getPosition(),
		                                        toTile->getPosition())) {
			ReturnValue ret = internalMoveCreature(*movingCreature, *toTile, FLAG_NOLIMIT);
			if (ret != RETURNVALUE_NOERROR) {
				player->sendCancelMessage(ret);
			}
		}

		return;
	}

	if (!player->canDoAction()) {
		uint32_t delay = player->getNextActionTime();
		auto task =
		    createSchedulerTask(delay, ([=, this, playerID = player->getID(), movingCreatureID = movingCreature->getID(),
		                                toPos = toTile->getPosition()]() {
			    playerMoveCreatureByID(playerID, movingCreatureID, movingCreatureOrigPos, toPos);
		    }));
		player->setNextActionTask(std::move(task));
		return;
	}

	if (movingCreature->isMovementBlocked()) {
		player->sendCancelMessage(RETURNVALUE_CREATURENOTMOVEABLE);
		return;
	}

	player->setNextActionTask(nullptr);

	if (!movingCreatureOrigPos.isInRange(player->getPosition(), 1, 1, 0)) {
		// need to walk to the creature first before moving it
		std::vector<Direction> listDir;
		if (player->getPathTo(movingCreatureOrigPos, listDir, 0, 1, true, true)) {
			g_dispatcher.addTask([=, this, playerID = player->getID(), listDir = std::move(listDir)]() {
				playerAutoWalk(playerID, listDir);
			});
			auto task = createSchedulerTask(
			    static_cast<uint32_t>(getInteger(ConfigManager::RANGE_MOVE_CREATURE_INTERVAL)),
			    ([=, this, playerID = player->getID(), movingCreatureID = movingCreature->getID(),
			     toPos = toTile->getPosition()] {
				    playerMoveCreatureByID(playerID, movingCreatureID, movingCreatureOrigPos, toPos);
			    }));
			player->setNextWalkActionTask(std::move(task));
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	if ((!movingCreature->isPushable() && !player->hasFlag(PlayerFlag_CanPushAllCreatures)) ||
	    (movingCreature->isInGhostMode() && !player->canSeeGhostMode(movingCreature))) {
		player->sendCancelMessage(RETURNVALUE_NOTMOVEABLE);
		return;
	}

	// check throw distance
	const Position& movingCreaturePos = movingCreature->getPosition();
	const Position& toPos = toTile->getPosition();
	if ((movingCreaturePos.getDistanceX(toPos) > movingCreature->getThrowRange()) ||
	    (movingCreaturePos.getDistanceY(toPos) > movingCreature->getThrowRange()) ||
	    (movingCreaturePos.getDistanceZ(toPos) * 4 > movingCreature->getThrowRange())) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		return;
	}

	if (!movingCreaturePos.isInRange(player->getPosition(), 1, 1, 0)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (player != movingCreature) {
		if (getBoolean(ConfigManager::PUSH_CREATURE_ZONE)) {
			if (player->getZone() == ZONE_PROTECTION && movingCreature->isPlayer()) {
				player->sendCancelMessage("You cannot move players who are in a Protection Zone.");
				return;
			}

			if (player->getZone() == ZONE_NOPVP && movingCreature->isPlayer()) {
				player->sendCancelMessage("You cannot move players who are in a Zone No-PvP.");
				return;
			}

			if (movingCreature->getZone() == ZONE_PROTECTION && movingCreature->isPlayer()) {
				player->sendCancelMessage("You cannot move players who are in a Protection Zone.");
				return;
			}

			if (movingCreature->getZone() == ZONE_NOPVP && movingCreature->isPlayer()) {
				player->sendCancelMessage("You cannot move players who are in a Zone No-PvP.");
				return;
			}
		}

		if (toTile->hasFlag(TILESTATE_BLOCKPATH)) {
			player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
			return;
		} else if ((movingCreature->getZone() == ZONE_PROTECTION && !toTile->hasFlag(TILESTATE_PROTECTIONZONE)) ||
		           (movingCreature->getZone() == ZONE_NOPVP && !toTile->hasFlag(TILESTATE_NOPVPZONE))) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		} else {
			if (CreatureVector* tileCreatures = toTile->getCreatures()) {
				for (const auto& tileCreature : *tileCreatures) {
					if (!tileCreature->isInGhostMode() && movingCreature->compareInstance(tileCreature->getInstanceID())) {
						player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
						return;
					}
				}
			}

			Npc* movingNpc = movingCreature->getNpc();
			if (movingNpc && !Spawns::isInZone(movingNpc->getMasterPos(), movingNpc->getMasterRadius(), toPos)) {
				player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
				return;
			}
		}
	}

	if (!g_events->eventPlayerOnMoveCreature(player, movingCreature, movingCreaturePos, toPos)) {
		return;
	}

	ReturnValue ret = internalMoveCreature(*movingCreature, *toTile, 0);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
	}
}

ReturnValue Game::internalMoveCreature(Creature* creature, Direction direction, uint32_t flags /*= 0*/)
{
	creature->setLastPosition(creature->getPosition());
	const Position& currentPos = creature->getPosition();
	Position destPos = getNextPosition(direction, currentPos);
	Player* player = creature->getPlayer();
	if (player && player->isTokenLocked()) {
		player->sendCancelMessage("You are locked by Token Protection.");
		return RETURNVALUE_NOTPOSSIBLE;
	}

	bool diagonalMovement = (direction & DIRECTION_DIAGONAL_MASK) != 0;
	if (player && !diagonalMovement) {
		// try to go up
		if (currentPos.z != 8 && creature->getTile()->hasHeight(3)) {
			Tile* tmpTile = map.getTile(currentPos.x, currentPos.y, currentPos.getZ() - 1);
			if (tmpTile == nullptr || (tmpTile->getGround() == nullptr && !tmpTile->hasFlag(TILESTATE_BLOCKSOLID))) {
				tmpTile = map.getTile(destPos.x, destPos.y, destPos.getZ() - 1);
				if (tmpTile && tmpTile->getGround() && !tmpTile->hasFlag(TILESTATE_IMMOVABLEBLOCKSOLID)) {
					flags |= FLAG_IGNOREBLOCKITEM | FLAG_IGNOREBLOCKCREATURE;

					if (!tmpTile->hasFlag(TILESTATE_FLOORCHANGE)) {
						player->setDirection(direction);
						destPos.z--;
					}
				}
			}
		}

		// try to go down
		if (currentPos.z != 7 && currentPos.z == destPos.z) {
			Tile* tmpTile = map.getTile(destPos.x, destPos.y, destPos.z);
			if (tmpTile == nullptr || (tmpTile->getGround() == nullptr && !tmpTile->hasFlag(TILESTATE_BLOCKSOLID))) {
				tmpTile = map.getTile(destPos.x, destPos.y, destPos.z + 1);
				if (tmpTile && tmpTile->hasHeight(3) && !tmpTile->hasFlag(TILESTATE_IMMOVABLEBLOCKSOLID)) {
					flags |= FLAG_IGNOREBLOCKITEM | FLAG_IGNOREBLOCKCREATURE;
					player->setDirection(direction);
					destPos.z++;
				}
			}
		}
	}

	Tile* toTile = map.getTile(destPos);
	if (!toTile) {
		return RETURNVALUE_NOTPOSSIBLE;
	}
	return internalMoveCreature(*creature, *toTile, flags);
}

ReturnValue Game::internalMoveCreature(Creature& creature, Tile& toTile, uint32_t flags /*= 0*/)
{
	if (creature.hasCondition(CONDITION_ROOTED)) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	// check if we can move the creature to the destination
	ReturnValue ret = toTile.queryAdd(0, creature, 1, flags);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	map.moveCreature(creature, toTile);
	if (creature.getParent() != &toTile) {
		return RETURNVALUE_NOERROR;
	}

	int32_t index = 0;
	Item* toItem = nullptr;
	Tile* subCylinder = nullptr;
	Tile* toCylinder = &toTile;
	Tile* fromCylinder = nullptr;
	uint32_t n = 0;

	while ((subCylinder = toCylinder->queryDestination(index, creature, &toItem, flags,
	                                                    creature.getInstanceID())) != toCylinder) {
		map.moveCreature(creature, *subCylinder);

		if (creature.getParent() != subCylinder) {
			// could happen if a script move the creature
			fromCylinder = nullptr;
			break;
		}

		fromCylinder = toCylinder;
		toCylinder = subCylinder;
		flags = 0;

		// to prevent infinite loop
		if (++n >= MAP_MAX_LAYERS) {
			break;
		}
	}

	if (fromCylinder) {
		const Position& fromPosition = fromCylinder->getPosition();
		const Position& toPosition = toCylinder->getPosition();
		if (fromPosition.z != toPosition.z && (fromPosition.x != toPosition.x || fromPosition.y != toPosition.y)) {
			Direction dir = getDirectionTo(fromPosition, toPosition);
			if ((dir & DIRECTION_DIAGONAL_MASK) == 0) {
				internalCreatureTurn(&creature, dir);
			}
		}
	}

	return RETURNVALUE_NOERROR;
}

void Game::playerMoveItemByPlayerID(uint32_t playerId, const Position& fromPos, uint16_t spriteId, uint8_t fromStackPos,
                                    const Position& toPos, uint8_t count)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}
	playerMoveItem(player, fromPos, spriteId, fromStackPos, toPos, count, nullptr, nullptr);
}

void Game::playerMoveItem(Player* player, const Position& fromPos, uint16_t spriteId, uint8_t fromStackPos,
                          const Position& toPos, uint8_t count, Item* item, Cylinder* toCylinder)
{
	if (player->hasCondition(CONDITION_EXHAUST_WEAPON, EXHAUST_MOVEITEM)) {
		uint32_t delay = MIN_TASK_INTERVAL;
		if (Condition* cond = player->getCondition(CONDITION_EXHAUST_WEAPON, CONDITIONID_DEFAULT, EXHAUST_MOVEITEM)) {
			int64_t remaining = cond->getEndTime() - OTSYS_TIME();
			if (remaining > 0) {
				delay = static_cast<uint32_t>(remaining);
			}
		}
		auto task = createSchedulerTask(delay, ([=, this, playerID = player->getID()]() {
			playerMoveItemByPlayerID(playerID, fromPos, spriteId, fromStackPos, toPos, count);
		}));
		player->setNextActionTask(std::move(task));
		return;
	}

	player->setNextActionTask(nullptr);

	if (item == nullptr) {
		uint8_t fromIndex = 0;
		if (fromPos.x == 0xFFFF) {
			if (fromPos.y & 0x40) {
				fromIndex = fromPos.z;
			} else {
				fromIndex = static_cast<uint8_t>(fromPos.y);
			}
		} else {
			fromIndex = fromStackPos;
		}

		Thing* thing = internalGetThing(player, fromPos, fromIndex, 0, STACKPOS_MOVE);
		if (!thing || !thing->getItem()) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		item = thing->getItem();
	}

	if (item->getClientID() != spriteId) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (item->getTopParent() == player) {
		item->setInstanceID(0);
	}
	if (!InstanceUtils::isPlayerInSameInstance(player, item->getInstanceID())) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (hasNotMoveableActionId(*item)) {
		player->sendCancelMessage(RETURNVALUE_NOTMOVEABLE);
		return;
	}

	Cylinder* fromCylinder = internalGetCylinder(player, fromPos);
	if (fromCylinder == nullptr) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (toCylinder == nullptr) {
		toCylinder = internalGetCylinder(player, toPos);
		if (toCylinder == nullptr) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}
	}

	// hangable item specific code
	const auto playerMoveHangableItem = [&](const Position& playerPos, const Position& mapFromPos,
	                                              const Tile* toCylinderTile, const Position& mapToPos) -> bool {
		if (!item->isHangable() || !toCylinderTile->hasFlag(TILESTATE_SUPPORTS_HANGABLE)) {
			return false;
		}

		// destination supports hangable objects so need to move there first
		bool vertical = toCylinderTile->hasProperty(CONST_PROP_ISVERTICAL);
		if (vertical) {
			if (playerPos.x + 1 == mapToPos.x) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return true;
			}
		} else { // horizontal
			if (playerPos.y + 1 == mapToPos.y) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return true;
			}
		}

		if (!playerPos.isInRange(mapToPos, 1, 1, 0)) {
			Position walkPos = mapToPos;
			if (vertical) {
				walkPos.x++;
			} else {
				walkPos.y++;
			}

			Position itemPos = fromPos;
			uint8_t itemStackPos = fromStackPos;

			if (fromPos.x != 0xFFFF && mapFromPos.isInRange(playerPos, 1, 1) &&
			    !mapFromPos.isInRange(walkPos, 1, 1, 0)) {
				// need to pickup the item first
				Item* moveItem = nullptr;

				ReturnValue ret = internalMoveItem(fromCylinder, player, INDEX_WHEREEVER, item, count, &moveItem, 0,
				                                   player, nullptr, &fromPos, &toPos);
				if (ret != RETURNVALUE_NOERROR) {
					player->sendCancelMessage(ret);
					return true;
				}

				// changing the position since its now in the inventory of the player
				internalGetPosition(moveItem, itemPos, itemStackPos);
			}

			std::vector<Direction> listDir;
			if (player->getPathTo(walkPos, listDir, 0, 0, true, true)) {
				g_dispatcher.addTask([=, this, playerID = player->getID(), listDir = std::move(listDir)]() {
					playerAutoWalk(playerID, listDir);
				});

				auto task = createSchedulerTask(
				    static_cast<uint32_t>(getInteger(ConfigManager::RANGE_MOVE_ITEM_INTERVAL)),
				    ([this, playerID = player->getID(), itemPos, spriteId, itemStackPos, toPos, count]() {
					    playerMoveItemByPlayerID(playerID, itemPos, spriteId, itemStackPos, toPos, count);
				    }));
				player->setNextWalkActionTask(std::move(task));
			} else {
				player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
			}

			return true;
		}

		return false;
	};

	if (player->isTokenProtected()) {
		bool fromPlayer = false;
		Cylinder* current = fromCylinder;
		while (current) {
			if (current == player) {
				fromPlayer = true;
				break;
			}
			current = current->getParent();
		}

		if (fromPlayer) {
			if (!player->canMoveOwnItems(item)) {
				player->sendTextMessage(MESSAGE_EVENT_ADVANCE, "[TOKEN]: To move your items out, disable token security!");
				player->sendCancelMessage(RETURNVALUE_ITEMSTOKENPROTECTED);
				return;
			}
		}
	}

	if (player->hasFlag(PlayerFlag_CanThrowFar)) {
		const Tile* toCylinderTile = toCylinder->getTile();
		if (playerMoveHangableItem(player->getPosition(), fromCylinder->getTile()->getPosition(), toCylinderTile,
		                           toCylinderTile->getPosition())) {
			return;
		}

		uint8_t toIndex = 0;
		if (toPos.x == 0xFFFF) {
			if (toPos.y & 0x40) {
				toIndex = toPos.z;
			} else {
				toIndex = static_cast<uint8_t>(toPos.y);
			}
		}

		ReturnValue ret = internalMoveItem(fromCylinder, toCylinder, toIndex, item, count, nullptr, FLAG_NOLIMIT,
		                                   player, nullptr, &fromPos, &toPos);
		if (ret != RETURNVALUE_NOERROR) {
			player->sendCancelMessage(ret);
		} else {
			if (Container* srcContainer = dynamic_cast<Container*>(fromCylinder)) {
				for (const auto& [cid, openCont] : player->getOpenContainers()) {
					auto openContPtr = openCont.container.lock();
					if (openContPtr && openContPtr.get() == srcContainer) {
						player->sendContainer(cid, srcContainer, srcContainer->getParent() != nullptr, openCont.index);
						break;
					}
				}
			} else if (Tile* srcTile = fromCylinder->getTile()) {
				player->sendUpdateTile(srcTile, srcTile->getPosition());
			}

			if (Container* dstContainer = dynamic_cast<Container*>(toCylinder)) {
				for (const auto& [cid, openCont] : player->getOpenContainers()) {
					auto openContPtr = openCont.container.lock();
					if (openContPtr && openContPtr.get() == dstContainer) {
						player->sendContainer(cid, dstContainer, dstContainer->getParent() != nullptr, openCont.index);
						break;
					}
				}
			} else if (Tile* dstTile = toCylinder->getTile()) {
				player->sendUpdateTile(dstTile, dstTile->getPosition());
			}
		}

		return;
	}

	if (!item->isPushable() || item->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTMOVEABLE);
		return;
	}

	const Position& playerPos = player->getPosition();
	const Position& mapFromPos = fromCylinder->getTile()->getPosition();
	if (playerPos.z != mapFromPos.z) {
		player->sendCancelMessage(playerPos.z > mapFromPos.z ? RETURNVALUE_FIRSTGOUPSTAIRS
		                                                     : RETURNVALUE_FIRSTGODOWNSTAIRS);
		return;
	}

	if (!playerPos.isInRange(mapFromPos, 1, 1)) {
		// need to walk to the item first before using it
		std::vector<Direction> listDir;
		if (player->getPathTo(item->getPosition(), listDir, 0, 1, true, true)) {
			g_dispatcher.addTask([=, this, playerID = player->getID(), listDir = std::move(listDir)]() {
				playerAutoWalk(playerID, listDir);
			});

			auto task = createSchedulerTask(
			    static_cast<uint32_t>(getInteger(ConfigManager::RANGE_MOVE_ITEM_INTERVAL)),
			    ([=, this, playerID = player->getID()]() {
				    playerMoveItemByPlayerID(playerID, fromPos, spriteId, fromStackPos, toPos, count);
			    }));
			player->setNextWalkActionTask(std::move(task));
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	const Tile* toCylinderTile = toCylinder->getTile();
	const Position& mapToPos = toCylinderTile->getPosition();
	if (playerMoveHangableItem(playerPos, mapFromPos, toCylinderTile, mapToPos)) {
		return;
	}

	if (!item->isPickupable() && playerPos.z != mapToPos.z) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		return;
	}

	int32_t throwRange = item->getThrowRange();
	if ((playerPos.getDistanceX(mapToPos) > throwRange) || (playerPos.getDistanceY(mapToPos) > throwRange)) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		return;
	}

	if (!canThrowObjectTo(mapFromPos, mapToPos, true, false, throwRange, throwRange)) {
		player->sendCancelMessage(RETURNVALUE_CANNOTTHROW);
		return;
	}

	uint8_t toIndex = 0;
	if (toPos.x == 0xFFFF) {
		if (toPos.y & 0x40) {
			toIndex = toPos.z;
		} else {
			toIndex = static_cast<uint8_t>(toPos.y);
		}
	}

	ReturnValue ret =
	    internalMoveItem(fromCylinder, toCylinder, toIndex, item, count, nullptr, 0, player, nullptr, &fromPos, &toPos);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
	} else {
		if (auto c = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_EXHAUST_WEAPON,
		            getMoveItemExhaustionDelay(toPos), 0, false, EXHAUST_MOVEITEM)) {
			player->addCondition(std::move(c));
		}
	}
}

ReturnValue Game::internalMoveItem(Cylinder* fromCylinder, Cylinder* toCylinder, int32_t index, Item* item,
                                   uint32_t count, Item** _moveItem, uint32_t flags /*= 0*/,
                                   Creature* actor /* = nullptr*/, Item* tradeItem /* = nullptr*/,
                                   const Position* fromPos /*= nullptr*/, const Position* toPos /*= nullptr*/)
{
	Player* actorPlayer = actor ? actor->getPlayer() : nullptr;
	const uint32_t sourceInstanceId = isCarriedByCreature(item->getParent()) ? 0 : item->getInstanceID();
	if (item->getInstanceID() != sourceInstanceId) {
		item->setInstanceID(0);
	}
	if (actorPlayer && !InstanceUtils::isPlayerInSameInstance(actorPlayer, sourceInstanceId)) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (actorPlayer && fromPos && toPos) {
		const ReturnValue ret = g_events->eventPlayerOnMoveItem(actorPlayer, item, static_cast<uint16_t>(count),
		                                                        *fromPos, *toPos, fromCylinder, toCylinder);
		if (ret != RETURNVALUE_NOERROR) {
			return ret;
		}

		if (!actorPlayer->hasFlag(PlayerFlag_CanEditHouses)) {
			if (Tile* fromTile = fromCylinder->getTile()) {
				if (HouseTile* fromHouseTile = dynamic_cast<HouseTile*>(fromTile)) {
					House* fromHouse = fromHouseTile->getHouse();
					if (fromHouse && !fromHouse->canModifyItems(actorPlayer)) {
						return RETURNVALUE_CANNOTMOVEITEMISPROTECTED;
					}
				}
			}
			if (dynamic_cast<const Tile*>(toCylinder)) {
				if (const Tile *toTile = toCylinder->getTile()) {
					if (const TileItemVector *items = toTile->getItemList()) {
						if (items->size() >= TILE_MAX_ITEMS) {
							return RETURNVALUE_CANNOTADDMOREITEMSONTILE;
						}
					}
				}
			}

			if (Tile* toTile = toCylinder->getTile()) {
				if (HouseTile* toHouseTile = dynamic_cast<HouseTile*>(toTile)) {
					House* toHouse = toHouseTile->getHouse();
					if (toHouse && !toHouse->canModifyItems(actorPlayer)) {
						return RETURNVALUE_CANNOTMOVEITEMISPROTECTED;
					}
				}
			}

		}
	}

	Item* toItem = nullptr;

	Cylinder* subCylinder;
	int floorN = 0;
	uint32_t destinationInstanceId = getDestinationInstanceId(actorPlayer, toCylinder, sourceInstanceId);

	while ((subCylinder = toCylinder->queryDestination(index, *item, &toItem, flags,
	                                                    destinationInstanceId)) != toCylinder) {
		toCylinder = subCylinder;
		flags = 0;
		destinationInstanceId = getDestinationInstanceId(actorPlayer, toCylinder, sourceInstanceId);

		// to prevent infinite loop
		if (++floorN >= MAP_MAX_LAYERS) {
			break;
		}
	}

	if (actorPlayer) {
		const ReturnValue storeInboxLockRet = getStoreInboxLockedItemMoveReturn(item);
		if (storeInboxLockRet != RETURNVALUE_NOERROR) {
			return storeInboxLockRet;
		}

		if (isInsideStoreInbox(toCylinder)) {
			return RETURNVALUE_NOTPOSSIBLE;
		}
	}

	// destination is the same as the source?
	if (item == toItem) {
		return RETURNVALUE_NOERROR; // silently ignore move
	}

	// Check if destination has teleport items (blocked teleport IDs)
	if (actorPlayer && toPos) {
		const Tile* toTile = toCylinder->getTile();
		if (toTile) {
			const auto& blockedIds = ConfigManager::getBlockedTeleportIds();

			// Check ground item for teleport
			const Item* ground = toTile->getGround();
			if (ground) {
				const uint16_t groundId = ground->getID();
				if (std::find(blockedIds.begin(), blockedIds.end(), groundId) != blockedIds.end()) {
					actorPlayer->sendCancelMessage(RETURNVALUE_CANNOTTHROWONTELEPORT);
					InstanceUtils::sendMagicEffectToInstance(*toPos, actorPlayer->getInstanceID(), CONST_ME_POFF);
					return RETURNVALUE_CANNOTTHROWONTELEPORT;
				}
			}

			// Check all items on the tile for teleports
			const TileItemVector* items = toTile->getItemList();
			if (items) {
				for (const auto& tileItem : *items) {
					if (tileItem) {
						const uint16_t itemId = tileItem->getID();
						if (std::find(blockedIds.begin(), blockedIds.end(), itemId) != blockedIds.end()) {
							actorPlayer->sendCancelMessage(RETURNVALUE_CANNOTTHROWONTELEPORT);
							InstanceUtils::sendMagicEffectToInstance(*toPos, actorPlayer->getInstanceID(), CONST_ME_POFF);
							return RETURNVALUE_CANNOTTHROWONTELEPORT;
						}
					}
				}
			}
		}
	}

	// Check for reward containers
	if (Container* toContainer = dynamic_cast<Container*>(toCylinder)) {
		if (toContainer->isRewardCorpse() || toContainer->getID() == ITEM_REWARD_CONTAINER) {
			return RETURNVALUE_NOTPOSSIBLE;
		}

		if (actorPlayer && toContainer->getID() == ITEM_GOLD_POUCH) {
			if (isInsideStoreInbox(toContainer)) {
				return RETURNVALUE_NOTPOSSIBLE;
			}
		}
	}

	if (Container* itemContainer = dynamic_cast<Container*>(item)) {
		if (itemContainer->isRewardCorpse() || item->getID() == ITEM_REWARD_CONTAINER) {
			return RETURNVALUE_NOERROR;
		}
	}

	// check if we can add this item
	ReturnValue ret = toCylinder->queryAdd(index, *item, count, flags, actor);
	if (ret == RETURNVALUE_NEEDEXCHANGE) {
		// check if we can add it to source cylinder
		ret = fromCylinder->queryAdd(fromCylinder->getThingIndex(item), *toItem, toItem->getItemCount(), 0);
		if (ret == RETURNVALUE_NOERROR) {
			if (actorPlayer && fromPos && toPos) {
				const ReturnValue eventRet = g_events->eventPlayerOnMoveItem(
				    actorPlayer, toItem, toItem->getItemCount(), *toPos, *fromPos, toCylinder, fromCylinder);
				if (eventRet != RETURNVALUE_NOERROR) {
					return eventRet;
				}
			}

			// check how much we can move
			uint32_t maxExchangeQueryCount = 0;
			ReturnValue retExchangeMaxCount =
			    fromCylinder->queryMaxCount(INDEX_WHEREEVER, *toItem, toItem->getItemCount(), maxExchangeQueryCount, 0);

			if (retExchangeMaxCount != RETURNVALUE_NOERROR && maxExchangeQueryCount == 0) {
				return retExchangeMaxCount;
			}

			if (toCylinder->queryRemove(*toItem, toItem->getItemCount(), flags, actor) == RETURNVALUE_NOERROR) {
				int32_t oldToItemIndex = toCylinder->getThingIndex(toItem);
				auto toItemRef = getItemSharedRef(toItem); // keep alive during exchange
				if (!toItemRef) {
					return RETURNVALUE_NOTPOSSIBLE;
				}
				toCylinder->removeThing(toItem, toItem->getItemCount());
				fromCylinder->addThing(toItem);

				if (oldToItemIndex != -1) {
					toCylinder->postRemoveNotification(toItem, fromCylinder, oldToItemIndex);
				}

				int32_t newToItemIndex = fromCylinder->getThingIndex(toItem);
				if (newToItemIndex != -1) {
					fromCylinder->postAddNotification(toItem, toCylinder, newToItemIndex);
				}

				ret = toCylinder->queryAdd(index, *item, count, flags, actor);

				if (actorPlayer && fromPos && toPos && !toItem->isRemoved()) {
					g_events->eventPlayerOnItemMoved(actorPlayer, toItem, static_cast<uint16_t>(count), *toPos,
					                                 *fromPos, toCylinder, fromCylinder);
				}

				toItem = nullptr;
			}
		}
	}

	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	// check how much we can move
	uint32_t maxQueryCount = 0;
	ReturnValue retMaxCount = toCylinder->queryMaxCount(index, *item, count, maxQueryCount, flags);
	if (retMaxCount != RETURNVALUE_NOERROR && maxQueryCount == 0) {
		return retMaxCount;
	}

	uint32_t m;
	if (item->isStackable()) {
		m = std::min<uint32_t>(count, maxQueryCount);
	} else {
		m = maxQueryCount;
	}

	Item* moveItem = item;

	// check if we can remove this item
	ret = fromCylinder->queryRemove(*item, m, flags, actor);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	if (tradeItem) {
		if (toCylinder->getItem() == tradeItem) {
			return RETURNVALUE_NOTENOUGHROOM;
		}

		Cylinder* tmpCylinder = toCylinder->getParent();
		while (tmpCylinder) {
			if (tmpCylinder->getItem() == tradeItem) {
				return RETURNVALUE_NOTENOUGHROOM;
			}

			tmpCylinder = tmpCylinder->getParent();
		}
	}

	// remove the item
	int32_t itemIndex = fromCylinder->getThingIndex(item);
	Item* updateItem = nullptr;
	std::shared_ptr<Item> clonedMoveItem;
	auto itemRef = getItemSharedRef(item);
	if (!itemRef) {
		return RETURNVALUE_NOTPOSSIBLE;
	}
	fromCylinder->removeThing(item, m);

	// update item(s)
	if (item->isStackable()) {
		uint32_t n;

		if (toItem && toItem->getInstanceID() == destinationInstanceId &&
		    item->equalsIgnoringInstance(toItem)) {
			n = std::min<uint32_t>(toItem->getStackSize() - toItem->getItemCount(), m);
			toCylinder->updateThing(toItem, toItem->getID(), toItem->getItemCount() + n);
			updateItem = toItem;
		} else {
			n = 0;
		}

		int32_t newCount = m - n;
		if (newCount > 0) {
			clonedMoveItem = item->clone();
			moveItem = clonedMoveItem.get();
			moveItem->setItemCount(static_cast<uint8_t>(newCount));
		} else {
			moveItem = nullptr;
		}

		if (item->isRemoved()) {
			item->stopDecaying();
			ReleaseItem(item);
		}
	}

	// add item
	if (moveItem /*m - n > 0*/) {
		moveItem->setInstanceID(destinationInstanceId);
		toCylinder->addThing(index, moveItem);
	}

	if (itemIndex != -1) {
		if (moveItem == item) {
			item->setInstanceID(sourceInstanceId);
			fromCylinder->postRemoveNotification(item, toCylinder, itemIndex);
			item->setInstanceID(destinationInstanceId);
		} else {
			fromCylinder->postRemoveNotification(item, toCylinder, itemIndex);
		}
	}

	if (moveItem) {
		int32_t moveItemIndex = toCylinder->getThingIndex(moveItem);
		if (moveItemIndex != -1) {
			toCylinder->postAddNotification(moveItem, fromCylinder, moveItemIndex);
		}
		moveItem->startDecaying();
	}

	if (updateItem) {
		int32_t updateItemIndex = toCylinder->getThingIndex(updateItem);
		if (updateItemIndex != -1) {
			toCylinder->postAddNotification(updateItem, fromCylinder, updateItemIndex);
		}
		updateItem->startDecaying();
	}

	if (_moveItem) {
		if (moveItem) {
			*_moveItem = moveItem;
		} else {
			*_moveItem = item;
		}
	}

	// we could not move all, inform the player
	if (item->isStackable() && maxQueryCount < count) {
		return retMaxCount;
	}

	if (actorPlayer && fromPos && toPos) {
		if (updateItem && !updateItem->isRemoved()) {
			g_events->eventPlayerOnItemMoved(actorPlayer, updateItem, static_cast<uint16_t>(count), *fromPos, *toPos,
			                                 fromCylinder, toCylinder);
		} else if (moveItem && !moveItem->isRemoved()) {
			g_events->eventPlayerOnItemMoved(actorPlayer, moveItem, static_cast<uint16_t>(count), *fromPos, *toPos,
			                                 fromCylinder, toCylinder);
		} else if (item && !item->isRemoved()) {
			g_events->eventPlayerOnItemMoved(actorPlayer, item, static_cast<uint16_t>(count), *fromPos, *toPos,
			                                 fromCylinder, toCylinder);
		}
	}

	return ret;
}

ReturnValue Game::internalAddItem(Cylinder* toCylinder, Item* item, int32_t index /*= INDEX_WHEREEVER*/,
                                  uint32_t flags /* = 0*/, bool test /* = false*/)
{
	uint32_t remainderCount = 0;
	return internalAddItem(toCylinder, item, index, flags, test, remainderCount);
}

ReturnValue Game::internalAddItem(Cylinder* toCylinder, Item* item, int32_t index, uint32_t flags, bool test,
                                  uint32_t& remainderCount)
{
	if (toCylinder == nullptr || item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	Cylinder* destCylinder = toCylinder;
	Item* toItem = nullptr;
	const uint32_t destinationInstanceId = getDestinationInstanceId(nullptr, toCylinder, item->getInstanceID());
	toCylinder = toCylinder->queryDestination(index, *item, &toItem, flags, destinationInstanceId);

	// check if we can add this item
	ReturnValue ret = toCylinder->queryAdd(index, *item, item->getItemCount(), flags);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	/*
	Check if we can move add the whole amount, we do this by checking against the original cylinder,
	since the queryDestination can return a cylinder that might only hold a part of the full amount.
	*/
	uint32_t maxQueryCount = 0;
	ret = destCylinder->queryMaxCount(INDEX_WHEREEVER, *item, item->getItemCount(), maxQueryCount, flags);

	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	if (test) {
		return RETURNVALUE_NOERROR;
	}

	if (item->isStackable() && item->equals(toItem)) {
		uint32_t m = std::min<uint32_t>(item->getItemCount(), maxQueryCount);
		uint32_t n = std::min<uint32_t>(toItem->getStackSize() - toItem->getItemCount(), m);

		toCylinder->updateThing(toItem, toItem->getID(), toItem->getItemCount() + n);

		int32_t count = m - n;
		std::shared_ptr<Item> clonedRemainderItem;
		if (count > 0) {
			if (item->getItemCount() != count) {
				clonedRemainderItem = item->clone();
				Item* remainderItem = clonedRemainderItem.get();
				remainderItem->setItemCount(static_cast<uint8_t>(count));
				if (internalAddItem(destCylinder, remainderItem, INDEX_WHEREEVER, flags, false) !=
				    RETURNVALUE_NOERROR) {
					ReleaseItem(remainderItem);
					remainderCount = count;
				}

				// The original stackable item only served as the merge source.
				// Once any remainder is handled, the core must retire it so callers
				// do not leak or keep using a consumed object.
				item->onRemoved();
				ReleaseItem(item);
			} else {
				toCylinder->addThing(index, item);

				int32_t itemIndex = toCylinder->getThingIndex(item);
				if (itemIndex != -1) {
					toCylinder->postAddNotification(item, nullptr, itemIndex);
				}
			}
		} else {
			// fully merged with toItem, item will be destroyed
			item->onRemoved();
			ReleaseItem(item);

			int32_t itemIndex = toCylinder->getThingIndex(toItem);
			if (itemIndex != -1) {
				toCylinder->postAddNotification(toItem, nullptr, itemIndex);
			}
		}
	} else {
		toCylinder->addThing(index, item);

		int32_t itemIndex = toCylinder->getThingIndex(item);
		if (itemIndex != -1) {
			toCylinder->postAddNotification(item, nullptr, itemIndex);
		}
	}

	return RETURNVALUE_NOERROR;
}

ReturnValue Game::internalRemoveItem(Item* item, int32_t count /*= -1*/, bool test /*= false*/, uint32_t flags /*= 0*/)
{
	extern bool isValidItemPointer(Item*);
	if (!isValidItemPointer(item)) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	Cylinder* cylinder = item->getParent();
	if (cylinder == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (count == -1) {
		count = item->getItemCount();
	}

	// check if we can remove this item
	ReturnValue ret = cylinder->queryRemove(*item, count, flags | FLAG_IGNORENOTMOVEABLE);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	if (!item->canRemove()) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!test) {
		int32_t index = cylinder->getThingIndex(item);

		auto itemRef = getItemSharedRef(item);
		if (!itemRef) {
			return RETURNVALUE_NOTPOSSIBLE;
		}

		// remove the item
		cylinder->removeThing(item, count);

		if (item->isRemoved()) {
			item->onRemoved();
			item->stopDecaying();
			ReleaseItem(item);
		}

		cylinder->postRemoveNotification(item, nullptr, index);
	}

	return RETURNVALUE_NOERROR;
}

ReturnValue Game::internalPlayerAddItem(Player* player, Item* item, bool dropOnMap /*= true*/,
                                        slots_t slot /*= CONST_SLOT_WHEREEVER*/)
{
	uint32_t remainderCount = 0;
	ReturnValue ret = internalAddItem(player, item, static_cast<int32_t>(slot), 0, false, remainderCount);
	if (remainderCount != 0) {
		auto remainderItem = Item::CreateItem(item->getID(), static_cast<uint16_t>(remainderCount));
		internalAddItem(player->getTile(), remainderItem.get(), INDEX_WHEREEVER, FLAG_NOLIMIT);
	}

	if (ret != RETURNVALUE_NOERROR && dropOnMap) {
		ret = internalAddItem(player->getTile(), item, INDEX_WHEREEVER, FLAG_NOLIMIT);
	}

	return ret;
}

Item* Game::findItemOfType(Cylinder* cylinder, uint16_t itemId, bool depthSearch /*= true*/,
                           int32_t subType /*= -1*/) const
{
	if (cylinder == nullptr) {
		return nullptr;
	}

	std::vector<Container*> containers;
	for (size_t i = cylinder->getFirstIndex(), j = cylinder->getLastIndex(); i < j; ++i) {
		Thing* thing = cylinder->getThing(i);
		if (!thing) {
			continue;
		}

		Item* item = thing->getItem();
		if (!item) {
			continue;
		}

		if (item->getID() == itemId && (subType == -1 || subType == item->getSubType())) {
			return item;
		}

		if (depthSearch) {
			Container* container = item->getContainer();
			if (container) {
				containers.push_back(container);
			}
		}
	}

	size_t i = 0;
	while (i < containers.size()) {
		Container* container = containers[i++];
		for (const auto& item : container->getItemList()) {
			if (item->getID() == itemId && (subType == -1 || subType == item->getSubType())) {
				return item.get();
			}

			Container* subContainer = item->getContainer();
			if (subContainer) {
				containers.push_back(subContainer);
			}
		}
	}
	return nullptr;
}

bool Game::removeMoney(Cylinder* cylinder, uint64_t money, uint32_t flags /*= 0*/)
{
	if (cylinder == nullptr) {
		return false;
	}

	if (money == 0) {
		return true;
	}

	std::vector<Container*> containers;

	std::multimap<uint64_t, Item*> moneyMap;
	uint64_t moneyCount = 0;

	for (size_t i = cylinder->getFirstIndex(), j = cylinder->getLastIndex(); i < j; ++i) {
		Thing* thing = cylinder->getThing(i);
		if (!thing) {
			continue;
		}

		Item* item = thing->getItem();
		if (!item) {
			continue;
		}

		Container* container = item->getContainer();
		if (container) {
			containers.push_back(container);
		} else {
			const uint32_t worth = item->getWorth();
			if (worth != 0) {
				moneyCount += worth;
				moneyMap.emplace(worth, item);
			}
		}
	}

	size_t i = 0;
	while (i < containers.size()) {
		Container* container = containers[i++];
		for (const auto& item : container->getItemList()) {
			Container* tmpContainer = item->getContainer();
			if (tmpContainer) {
				containers.push_back(tmpContainer);
			} else {
				const uint32_t worth = item->getWorth();
				if (worth != 0) {
					moneyCount += worth;
					moneyMap.emplace(worth, item.get());
				}
			}
		}
	}

	if (moneyCount < money) {
		return false;
	}

	for (const auto& moneyEntry : moneyMap) {
		Item* item = moneyEntry.second;
		if (moneyEntry.first < money) {
			internalRemoveItem(item);
			money -= moneyEntry.first;
		} else if (moneyEntry.first > money) {
			const uint32_t worth = moneyEntry.first / item->getItemCount();
			const uint32_t removeCount = std::ceil(money / static_cast<double>(worth));

			addMoney(cylinder, static_cast<uint64_t>(worth * removeCount) - money, flags);
			internalRemoveItem(item, removeCount);
			break;
		} else {
			internalRemoveItem(item);
			break;
		}
	}
	return true;
}

void Game::addMoney(Cylinder* cylinder, uint64_t money, uint32_t flags /*= 0*/)
{
	if (money == 0) {
		return;
	}

	for (const auto& it : Item::items.currencyItems) {
		const uint64_t worth = it.first;

		uint32_t currencyCoins = money / worth;
		if (currencyCoins <= 0) {
			continue;
		}

		money -= currencyCoins * worth;
		while (currencyCoins > 0) {
			const uint16_t count = std::min<uint16_t>(100, static_cast<uint16_t>(currencyCoins));

			auto remaindItem = Item::CreateItem(it.second, count);

			ReturnValue ret = internalAddItem(cylinder, remaindItem.get(), INDEX_WHEREEVER, flags);
			if (ret != RETURNVALUE_NOERROR) {
				internalAddItem(cylinder->getTile(), remaindItem.get(), INDEX_WHEREEVER, FLAG_NOLIMIT);
			}

			currencyCoins -= count;
		}
	}
}

Item* Game::transformItem(Item* item, uint16_t newId, int32_t newCount /*= -1*/)
{
	if (item->getID() == newId && (newCount == -1 || (newCount == item->getSubType() &&
	                                                  newCount != 0))) { // chargeless item placed on map = infinite
		return item;
	}

	Cylinder* cylinder = item->getParent();
	if (cylinder == nullptr) {
		return nullptr;
	}

	int32_t itemIndex = cylinder->getThingIndex(item);
	if (itemIndex == -1) {
		return item;
	}

	if (!item->canTransform()) {
		return item;
	}

	const ItemType& newType = Item::items[newId];
	if (newType.id == 0) {
		return item;
	}

	auto itemRef = getItemSharedRef(item);
	if (!itemRef) {
		return item;
	}

	const ItemType& curType = Item::items[item->getID()];
	if (curType.alwaysOnTop != newType.alwaysOnTop) {
		// This only occurs when you transform items on tiles from a downItem to a topItem (or vice versa)
		// Remove the old, and add the new
		cylinder->removeThing(item, item->getItemCount());
		cylinder->postRemoveNotification(item, cylinder, itemIndex);

		item->setID(newId);
		if (newCount != -1) {
			item->setSubType(static_cast<uint16_t>(newCount));
		}
		cylinder->addThing(item);

		Cylinder* newParent = item->getParent();
		if (newParent == nullptr) {
			item->stopDecaying();
			ReleaseItem(item);
			return nullptr;
		}

		newParent->postAddNotification(item, cylinder, newParent->getThingIndex(item));
		if (!item->isRemoved()) {
			// Same Container/itemlist safety as the updateThing branch below.
			if (item->getContainer()) {
				g_game.startDecay(item);
			} else {
				item->startDecaying();
			}
		}
		return item;
	}

	if (curType.type == newType.type) {
		// Both items has the same type so we can safely change id/subtype
		if (newCount == 0 && (item->isStackable() || item->hasAttribute(ITEM_ATTRIBUTE_CHARGES))) {
			if (item->isStackable()) {
				internalRemoveItem(item);
				return nullptr;
			} else {
				int32_t newItemId = newId;
				if (curType.id == newType.id) {
					newItemId = curType.decayTo;
				}

				if (newItemId <= 0) {
					internalRemoveItem(item);
					return nullptr;
				} else if (newItemId != newId) {
					// Replacing the the old item with the new while maintaining the old position
					auto newItem = Item::CreateItem(static_cast<uint16_t>(newItemId), 1);
					if (!newItem) {
						return nullptr;
					}

					cylinder->replaceThing(itemIndex, newItem.get());
					cylinder->postAddNotification(newItem.get(), cylinder, itemIndex);

					item->setParent(nullptr);
					cylinder->postRemoveNotification(item, cylinder, itemIndex);
					item->stopDecaying();
					ReleaseItem(item);
					// replaceThing may not accept newItem — guard before startDecaying
					// and release the orphaned allocation if it was not added.
					if (cylinder->getThingIndex(newItem.get()) != -1) {
						if (!newItem->isRemoved()) {
							if (newItem->getContainer()) {
								g_game.startDecay(newItem);
							} else {
								newItem->startDecaying();
							}
						}
						return newItem.get();
					}
					ReleaseItem(newItem.get());
					return nullptr;
				}
				return transformItem(item, static_cast<uint16_t>(newItemId));
			}
		} else {
			cylinder->postRemoveNotification(item, cylinder, itemIndex);
			uint16_t itemId = item->getID();
			int32_t count = item->getSubType();

			if (curType.id != newType.id) {
				if (newType.group != curType.group) {
					item->setDefaultSubtype();
				}

				itemId = newId;
			}

			if (newCount != -1 && newType.hasSubType()) {
				count = newCount;
			}

			cylinder->updateThing(item, itemId, count);
			cylinder->postAddNotification(item, cylinder, itemIndex);
			if (!item->isRemoved()) {
				if (item->getContainer()) {
					g_game.startDecay(item);
				} else {
					item->startDecaying();
				}
			}
			return item;
		}
	}

	// Replacing the old item with the new while maintaining the old position
	std::shared_ptr<Item> newItem;
	if (newCount == -1) {
		newItem = Item::CreateItem(newId);
	} else {
		newItem = Item::CreateItem(newId, static_cast<uint16_t>(newCount));
	}

	if (!newItem) {
		return nullptr;
	}

	cylinder->replaceThing(itemIndex, newItem.get());
	cylinder->postAddNotification(newItem.get(), cylinder, itemIndex);

	item->setParent(nullptr);
	cylinder->postRemoveNotification(item, cylinder, itemIndex);
	item->stopDecaying();
	ReleaseItem(item);

	// replaceThing() may reject newItem (e.g. the cylinder is full or the
	// tile rejects the item type).  When that happens getThingIndex() returns
	// -1, meaning newItem was never adopted by the cylinder and has no owner.
	// Without this guard the allocation leaks — Valgrid loss record 2,117.
	if (cylinder->getThingIndex(newItem.get()) != -1) {
		if (!newItem->isRemoved()) {
			if (newItem->getContainer()) {
				g_game.startDecay(newItem);
			} else {
				newItem->startDecaying();
			}
		}
		return newItem.get();
	}

	// newItem was not accepted — release it to avoid a definite memory leak.
	ReleaseItem(newItem.get());
	return nullptr;
}

ReturnValue Game::internalTeleport(Thing* thing, const Position& newPos, bool pushMove /* = true*/,
                                   uint32_t flags /*= 0*/,
                                   MagicEffectClasses magicEffect /*= CONST_ME_TELEPORT*/)
{
	if (newPos == thing->getPosition()) {
		return RETURNVALUE_NOERROR;
	} else if (thing->isRemoved()) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	Tile* toTile = map.getTile(newPos);
	if (!toTile) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (Creature* creature = thing->getCreature()) {
		ReturnValue ret = toTile->queryAdd(0, *creature, 1, FLAG_NOLIMIT);
		if (ret != RETURNVALUE_NOERROR) {
			return ret;
		}

		if (Player* player = creature->getPlayer()) {
			closeContainersFromOtherInstances(player);
		}

		Position origPos = creature->getPosition();
		uint32_t instanceId = creature->getInstanceID();

		if (magicEffect != CONST_ME_NONE) {
			InstanceUtils::sendMagicEffectToInstance(origPos, instanceId, magicEffect);
		}

		map.moveCreature(*creature, *toTile, !pushMove);

		if (magicEffect != CONST_ME_NONE) {
			InstanceUtils::sendMagicEffectToInstance(newPos, instanceId, magicEffect);
		}

		return RETURNVALUE_NOERROR;
	} else if (Item* item = thing->getItem()) {
		return internalMoveItem(item->getParent(), toTile, INDEX_WHEREEVER, item, item->getItemCount(), nullptr, flags);
	}
	return RETURNVALUE_NOTPOSSIBLE;
}

Item* searchForItem(Container* container, uint16_t itemId)
{
	for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
		if ((*it)->getID() == itemId) {
			return *it;
		}
	}

	return nullptr;
}

slots_t getSlotType(const ItemType& it)
{
	slots_t slot = CONST_SLOT_RIGHT;
	if (it.weaponType != WeaponType_t::WEAPON_SHIELD) {
		int32_t slotPosition = it.slotPosition;

		if (slotPosition & SLOTP_HEAD) {
			slot = CONST_SLOT_HEAD;
		} else if (slotPosition & SLOTP_NECKLACE) {
			slot = CONST_SLOT_NECKLACE;
		} else if (slotPosition & SLOTP_ARMOR) {
			slot = CONST_SLOT_ARMOR;
		} else if (slotPosition & SLOTP_LEGS) {
			slot = CONST_SLOT_LEGS;
		} else if (slotPosition & SLOTP_FEET) {
			slot = CONST_SLOT_FEET;
		} else if (slotPosition & SLOTP_RING) {
			slot = CONST_SLOT_RING;
		} else if (slotPosition & SLOTP_AMMO) {
			slot = CONST_SLOT_AMMO;
		} else if (slotPosition & SLOTP_TWO_HAND || slotPosition & SLOTP_LEFT) {
			slot = CONST_SLOT_LEFT;
		}
	}

	return slot;
}

// Implementation of player invoked events
void Game::playerMove(uint32_t playerId, Direction direction)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	if (player->isMovementBlocked()) {
		player->sendCancelWalk();
		return;
	}

	player->resetIdleTime();
	player->setNextWalkActionTask(nullptr);

	player->startAutoWalk(direction);
}

bool Game::playerBroadcastMessage(Player* player, std::string_view text) const
{
	if (!player->hasFlag(PlayerFlag_CanBroadcast)) {
		return false;
	}

	LOG_INFO(fmt::format("> {} broadcasted: \"{}\".", player->getName(), text));

	for (const auto& onlinePlayer : getPlayers()) {
		onlinePlayer->sendPrivateMessage(player, TALKTYPE_BROADCAST, text);
	}

	return true;
}

void Game::playerCreatePrivateChannel(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player || !player->isPremium()) {
		return;
	}

	if (ChatChannel* channel = g_chat->createChannel(*player, CHANNEL_PRIVATE)) {
		if (!channel->addUser(g_game.getCreatureSharedRef<Player>(player))) {
			return;
		}

		player->sendCreatePrivateChannel(channel->getId(), channel->getName());
		channel->executeOnJoinEvent(*player);
	}
}

void Game::playerChannelInvite(uint32_t playerId, std::string_view name)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	PrivateChatChannel* channel = g_chat->getPrivateChannel(*player);
	if (!channel) {
		return;
	}

	auto invitePlayerRef = getPlayerByName(name);

	Player* invitePlayer = invitePlayerRef.get();
	if (!invitePlayer) {
		return;
	}

	if (player == invitePlayer) {
		return;
	}

	channel->invitePlayer(*player, *invitePlayer);
}

void Game::playerChannelExclude(uint32_t playerId, std::string_view name)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	PrivateChatChannel* channel = g_chat->getPrivateChannel(*player);
	if (!channel) {
		return;
	}

	auto excludePlayerRef = getPlayerByName(name);

	Player* excludePlayer = excludePlayerRef.get();
	if (!excludePlayer) {
		return;
	}

	if (player == excludePlayer) {
		return;
	}

	channel->excludePlayer(*player, *excludePlayer);
}

void Game::playerRequestChannels(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	player->sendChannelsDialog();
}

void Game::playerOpenChannel(uint32_t playerId, uint16_t channelId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	if (channelId == CHANNEL_CAST && player->client->isBroadcasting()) {
		player->client->sendCastChannel();
		return;
	}

	if (ChatChannel* channel = g_chat->addUserToChannel(*player, channelId)) {
		player->sendChannel(channel->getId(), channel->getName());
		channel->executeOnJoinEvent(*player);
	}
}

void Game::playerCloseChannel(uint32_t playerId, uint16_t channelId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	g_chat->removeUserFromChannel(*player, channelId);
}

void Game::playerOpenPrivateChannel(uint32_t playerId, std::string receiver)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	if (!IOLoginData::formatPlayerName(receiver)) {
		player->sendCancelMessage("A player with this name does not exist.");
		return;
	}

	if (player->getName() == receiver) {
		player->sendCancelMessage("You cannot set up a private message channel with yourself.");
		return;
	}

	player->sendOpenPrivateChannel(receiver);
}

void Game::playerCloseNpcChannel(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	SpectatorVec spectators;
	map.getSpectators(spectators, player->getPosition());
	for (const auto& spectator : spectators.npcs()) {
		static_cast<Npc*>(spectator.get())->onPlayerCloseChannel(player);
	}
}

void Game::playerReceivePing(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	player->receivePing();
}

void Game::playerAutoWalk(uint32_t playerId, const std::vector<Direction>& listDir)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	player->resetIdleTime();

	if (player->getCondition(CONDITION_CLIPORT, CONDITIONID_DEFAULT)) {
		const Position& playerPos = player->getPosition();
		Position nextPos = Position(playerPos.x, playerPos.y, playerPos.z);
		for (const auto dir : listDir) {
			nextPos = getNextPosition(dir, nextPos);
		}

		nextPos = getClosestFreeTile(player, nextPos, true);
		if (nextPos.x == 0 || nextPos.y == 0) {
			return player->sendCancelWalk();
		}

		internalCreatureTurn(player, getDirectionTo(playerPos, nextPos, false));
		internalTeleport(player, nextPos, true);
		return;
	}

	player->setNextWalkTask(nullptr);
	player->startAutoWalk(listDir);
}

Position Game::getClosestFreeTile(Creature* creature, const Position& nextPos, bool extended /* = false*/)
{
	std::vector<std::pair<int8_t, int8_t>> relList{{0, 0}, {-1, -1}, {-1, 0}, {-1, 1}, {0, -1},
	                                               {0, 1}, {1, -1},  {1, 0},  {1, 1}};

	if (extended) {
		relList.push_back(std::pair<int8_t, int8_t>(-2, 0));
		relList.push_back(std::pair<int8_t, int8_t>(0, -2));
		relList.push_back(std::pair<int8_t, int8_t>(0, 2));
		relList.push_back(std::pair<int8_t, int8_t>(2, 0));
	}

	for (const auto& [x, y] : relList) {
		if (const Tile* tile = map.getTile(nextPos.x + x, nextPos.y + y, nextPos.z)) {
			if (tile->getGround() &&
			    tile->queryAdd(0, *creature, 1, FLAG_IGNOREBLOCKITEM, creature) == RETURNVALUE_NOERROR) {
				return tile->getPosition();
			}
		}
	}

	return Position(0, 0, 0);
}

void Game::playerStopAutoWalk(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	player->stopWalk();
}

void Game::playerUseItemEx(uint32_t playerId, const Position& fromPos, uint8_t fromStackPos, uint16_t fromSpriteId,
                           const Position& toPos, uint8_t toStackPos, uint16_t toSpriteId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	bool isHotkey = (fromPos.x == 0xFFFF && fromPos.y == 0 && fromPos.z == 0);
	if (isHotkey && !getBoolean(ConfigManager::AIMBOT_HOTKEY_ENABLED)) {
		return;
	}

	Thing* thing = internalGetThing(player, fromPos, fromStackPos, fromSpriteId, STACKPOS_USEITEM);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}
	Item* item = thing->getItem();
	if (!item || !item->isUseable()) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	if (!InstanceUtils::isPlayerInSameInstance(player, item->getInstanceID())) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (Thing* targetThing = internalGetThing(player, toPos, toStackPos, 0, STACKPOS_USETARGET)) {
		if (Item* targetItem = targetThing->getItem()) {
			if (!InstanceUtils::isPlayerInSameInstance(player, targetItem->getInstanceID())) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return;
			}
		} else if (Creature* targetCreature = targetThing->getCreature()) {
			if (!InstanceUtils::isPlayerInSameInstance(player, targetCreature->getInstanceID())) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return;
			}
		}
	}

	if (player->hasFlag(PlayerFlag_CanThrowFar)) {
		player->resetIdleTime();
		player->setNextActionTask(nullptr);

		auto itemRef = getItemSharedRef(item);
		if (!itemRef) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}
		g_actions->useItemEx(player, fromPos, toPos, toStackPos, itemRef, isHotkey);
		player->maintainAttackFlow();
		return;
	}

	Position walkToPos = fromPos;
	ReturnValue ret = g_actions->canUse(player, fromPos);
	if (ret == RETURNVALUE_NOERROR) {
		ret = g_actions->canUse(player, toPos, item);
		if (ret == RETURNVALUE_TOOFARAWAY) {
			walkToPos = toPos;
		}
	}

	if (ret != RETURNVALUE_NOERROR) {
		if (ret == RETURNVALUE_TOOFARAWAY) {
			Position itemPos = fromPos;
			uint8_t itemStackPos = fromStackPos;

			if (fromPos.x != 0xFFFF && toPos.x != 0xFFFF && fromPos.isInRange(player->getPosition(), 1, 1, 0) &&
			    !fromPos.isInRange(toPos, 1, 1, 0)) {
				Item* moveItem = nullptr;

				ret = internalMoveItem(item->getParent(), player, INDEX_WHEREEVER, item, item->getItemCount(),
				                       &moveItem, 0, player, nullptr, &fromPos, &toPos);
				if (ret != RETURNVALUE_NOERROR) {
					player->sendCancelMessage(ret);
					return;
				}

				// changing the position since its now in the inventory of the player
				internalGetPosition(moveItem, itemPos, itemStackPos);
			}

			std::vector<Direction> listDir;
			if (player->getPathTo(walkToPos, listDir, 0, 1, true, true)) {
				g_dispatcher.addTask([=, this, playerID = player->getID(), listDir = std::move(listDir)]() {
					playerAutoWalk(playerID, listDir);
				});

				auto task = createSchedulerTask(
				    static_cast<uint32_t>(getInteger(ConfigManager::RANGE_USE_ITEM_EX_INTERVAL)),
						([this, playerId, itemPos, itemStackPos, fromSpriteId, toPos, toStackPos, toSpriteId]()
						{ playerUseItemEx(playerId, itemPos, itemStackPos, fromSpriteId, toPos, toStackPos, toSpriteId); }));
				player->setNextWalkActionTask(std::move(task));
			} else {
				player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
			}
			return;
		}

		player->sendCancelMessage(ret);
		return;
	}

	if (!player->canDoAction()) {
		uint32_t delay = player->getNextActionTime();
		auto task = createSchedulerTask(
			delay, ([this, playerId, fromPos, fromStackPos, fromSpriteId, toPos, toStackPos, toSpriteId]() {
			playerUseItemEx(playerId, fromPos, fromStackPos, fromSpriteId, toPos, toStackPos, toSpriteId);
		}));
		player->setNextActionTask(std::move(task));
		return;
	}

	player->resetIdleTime();
	player->setNextActionTask(nullptr);

	auto itemRef = getItemSharedRef(item);
	if (!itemRef) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	g_actions->useItemEx(player, fromPos, toPos, toStackPos, itemRef, isHotkey);
	player->maintainAttackFlow();
}

void Game::playerUseItem(uint32_t playerId, const Position& pos, uint8_t stackPos, uint8_t index, uint16_t spriteId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	bool isHotkey = (pos.x == 0xFFFF && pos.y == 0 && pos.z == 0);
	if (isHotkey && !getBoolean(ConfigManager::AIMBOT_HOTKEY_ENABLED)) {
		return;
	}

	Thing* thing = internalGetThing(player, pos, stackPos, spriteId, STACKPOS_USEITEM);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}
	Item* item = thing->getItem();
	if (!item || item->isUseable()) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	if (player->hasFlag(PlayerFlag_CanThrowFar)) {
		player->resetIdleTime();
		player->setNextActionTask(nullptr);

		auto itemRef = getItemSharedRef(item);
		if (!itemRef) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}
		g_actions->useItem(player, pos, index, itemRef, isHotkey);
		player->maintainAttackFlow();

		for (const auto& [cid, openCont] : player->getOpenContainers()) {
			auto openContPtr = openCont.container.lock();
			if (openContPtr) {
				player->sendContainer(cid, openContPtr.get(),
				                      openContPtr->getParent() != nullptr, openCont.index);
			}
		}

		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			player->sendInventoryItem(static_cast<slots_t>(slot),
			                          player->getInventoryItem(static_cast<slots_t>(slot)));
		}

		return;
	}

	if (!InstanceUtils::isPlayerInSameInstance(player, item->getInstanceID())) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	ReturnValue ret = g_actions->canUse(player, pos);
	if (ret != RETURNVALUE_NOERROR) {
		if (ret == RETURNVALUE_TOOFARAWAY) {
			std::vector<Direction> listDir;
			if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
				g_dispatcher.addTask([=, this, playerID = player->getID(), listDir = std::move(listDir)]() {
					playerAutoWalk(playerID, listDir);
				});

				auto task =
				    createSchedulerTask(static_cast<uint32_t>(getInteger(ConfigManager::RANGE_USE_ITEM_INTERVAL)),
				                        ([this, playerId, pos, stackPos, index, spriteId]()
				                        { playerUseItem(playerId, pos, stackPos, index, spriteId); }));
				player->setNextWalkActionTask(std::move(task));
				return;
			}

			ret = RETURNVALUE_THEREISNOWAY;
		}

		player->sendCancelMessage(ret);
		return;
	}

	if (!player->canDoAction()) {
		uint32_t delay = player->getNextActionTime();
		auto task =
		    createSchedulerTask(delay, ([this, playerId, pos, stackPos, index, spriteId]()
		    { playerUseItem(playerId, pos, stackPos, index, spriteId); }));
		player->setNextActionTask(std::move(task));
		return;
	}

	player->resetIdleTime();
	player->setNextActionTask(nullptr);

	auto itemRef = getItemSharedRef(item);
	if (!itemRef) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}
	g_actions->useItem(player, pos, index, itemRef, isHotkey);

	if (!itemRef->isRemoved() && itemRef->getCorpseOwner() != 0) {
		player->lootCorpse(itemRef->getContainer());
	}
	player->maintainAttackFlow();
}

void Game::playerQuickLoot(uint32_t playerId, const Position& pos, uint16_t itemId, uint8_t stackPos,
                           bool lootAllCorpses)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player || !getBoolean(ConfigManager::QUICK_LOOT_ENABLED)) {
		return;
	}

	if (!player->canDoAction()) {
		const uint32_t delay = player->getNextActionTime();
		auto task = createSchedulerTask(delay, ([this, playerId, pos, itemId, stackPos, lootAllCorpses]() {
			playerQuickLoot(playerId, pos, itemId, stackPos, lootAllCorpses);
		}));
		player->setNextActionTask(std::move(task));
		return;
	}

	if (!player->hasFlag(PlayerFlag_CanThrowFar) && pos.x != 0xFFFF &&
	    !pos.isInRange(player->getPosition(), 1, 1, 0)) {
		std::vector<Direction> listDir;
		if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
			g_dispatcher.addTask([=, this, playerID = player->getID(), listDir = std::move(listDir)]() {
				playerAutoWalk(playerID, listDir);
			});

			auto task = createSchedulerTask(
			    static_cast<uint32_t>(getInteger(ConfigManager::RANGE_USE_ITEM_INTERVAL)),
			    ([this, playerId, pos, itemId, stackPos, lootAllCorpses]()
			     { playerQuickLoot(playerId, pos, itemId, stackPos, lootAllCorpses); }));
			player->setNextWalkActionTask(std::move(task));
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	player->resetIdleTime();
	player->setNextActionTask(nullptr);

	const uint32_t maxQuickLootCorpses = static_cast<uint32_t>(
	    std::max<int64_t>(1, ConfigManager::getInteger(ConfigManager::QUICK_LOOT_MAX_CORPSES)));
	if (lootAllCorpses && pos.x != 0xFFFF) {
		bool foundCorpse = false;
		ReturnValue firstFailure = RETURNVALUE_NOERROR;
		const uint32_t lootedCorpses = collectQuickLootTile(*this, player, pos, maxQuickLootCorpses, foundCorpse,
		                                                    firstFailure);
		if (foundCorpse) {
			if (lootedCorpses == 0 && firstFailure != RETURNVALUE_NOERROR) {
				player->sendCancelMessage(firstFailure);
			} else if (lootedCorpses > 1) {
				player->sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("You looted {:d} corpses.", lootedCorpses));
			}
			player->maintainAttackFlow();
			return;
		}
	}

	Thing* thing = internalGetThing(player, pos, stackPos, itemId, STACKPOS_USEITEM);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Item* item = thing->getItem();
	if (!item) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (!InstanceUtils::isPlayerInSameInstance(player, item->getInstanceID())) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (Container* container = item->getContainer()) {
		QuickLootResult result = collectQuickLootContainer(*this, player, container);
		if (result.movedItems == 0 && result.failure != RETURNVALUE_NOERROR) {
			player->sendCancelMessage(result.failure);
		}
		player->maintainAttackFlow();
		return;
	}

	auto* sourceContainer = dynamic_cast<Container*>(item->getParent());
	if (!sourceContainer) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	ReturnValue containerRet = getQuickLootContainerReturn(player, sourceContainer);
	if (containerRet != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(containerRet);
		return;
	}

	if (!shouldQuickLootItem(player, item)) {
		return;
	}

	Container* destination = getQuickLootDestination(player);
	if (!destination) {
		player->sendCancelMessage(RETURNVALUE_CONTAINERNOTENOUGHROOM);
		return;
	}

	ReturnValue ret = internalMoveItem(sourceContainer, destination, INDEX_WHEREEVER, item, item->getItemCount(),
	                                   nullptr, 0, player);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
	}
	player->maintainAttackFlow();
}

void Game::playerLootNearby(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player || !getBoolean(ConfigManager::QUICK_LOOT_ENABLED)) {
		return;
	}

	if (!player->canDoAction()) {
		const uint32_t delay = player->getNextActionTime();
		auto task = createSchedulerTask(delay, ([this, playerId]() { playerLootNearby(playerId); }));
		player->setNextActionTask(std::move(task));
		return;
	}

	player->resetIdleTime();
	player->setNextActionTask(nullptr);

	const uint32_t maxQuickLootCorpses = static_cast<uint32_t>(
	    std::max<int64_t>(1, ConfigManager::getInteger(ConfigManager::QUICK_LOOT_MAX_CORPSES)));
	const Position& playerPos = player->getPosition();
	uint32_t lootedCorpses = 0;
	bool foundCorpse = false;
	ReturnValue firstFailure = RETURNVALUE_NOERROR;

	for (int32_t x = -1; x <= 1 && lootedCorpses < maxQuickLootCorpses; ++x) {
		for (int32_t y = -1; y <= 1 && lootedCorpses < maxQuickLootCorpses; ++y) {
			Position tilePos(
			    static_cast<uint16_t>(static_cast<int32_t>(playerPos.x) + x),
			    static_cast<uint16_t>(static_cast<int32_t>(playerPos.y) + y),
			    playerPos.z);
			lootedCorpses += collectQuickLootTile(*this, player, tilePos,
			                                      maxQuickLootCorpses - lootedCorpses, foundCorpse, firstFailure);
		}
	}

	if (!foundCorpse) {
		player->sendCancelMessage("No lootable corpses nearby.");
	} else if (lootedCorpses == 0 && firstFailure != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(firstFailure);
	} else if (lootedCorpses > 1) {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("You looted {:d} corpses.", lootedCorpses));
	}
	player->maintainAttackFlow();
}

void Game::playerSetQuickLootFallback(uint32_t playerId, bool fallback)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player || !getBoolean(ConfigManager::QUICK_LOOT_ENABLED)) {
		return;
	}

	player->setQuickLootFallbackToMainContainer(fallback);
	player->sendLootContainers();
}

void Game::playerQuickLootBlackWhitelist(uint32_t playerId, QuickLootFilter_t filter, std::vector<uint16_t> itemIds)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player || !getBoolean(ConfigManager::QUICK_LOOT_ENABLED)) {
		return;
	}

	player->setQuickLootBlackWhitelist(filter, itemIds);
}

void Game::playerUseWithCreature(uint32_t playerId, const Position& fromPos, uint8_t fromStackPos, uint32_t creatureId,
                                 uint16_t spriteId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Creature* creature = getCreatureByID(creatureId);
	if (!creature) {
		return;
	}
	
	if (player->hasFlag(PlayerFlag_CanThrowFar)) {
		if (Thing* thing = internalGetThing(player, fromPos, fromStackPos, spriteId, STACKPOS_USEITEM)) {
			Item* item = thing->getItem();
			if (!item || !item->isUseable()) {
				player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
				return;
			}

			Cylinder* creatureParent = creature->getParent();
			if (!creatureParent) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return;
			}

			player->resetIdleTime();
			player->setNextActionTask(nullptr);
			bool isHotkey = (fromPos.x == 0xFFFF && fromPos.y == 0 && fromPos.z == 0);
			auto itemRef = getItemSharedRef(item);
			if (!itemRef) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return;
			}
			g_actions->useItemEx(player, fromPos, creature->getPosition(),
			                     static_cast<uint8_t>(creatureParent->getThingIndex(creature)),
			                     itemRef, isHotkey, creature);
			player->maintainAttackFlow();
		} else {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		}
		return;
	}

	if (!InstanceUtils::canInteract(player, creature)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (!creature->getPosition().isInRange(player->getPosition(), Map::maxClientViewportX - 1,
	                                       Map::maxClientViewportY - 1, 0)) {
		return;
	}

	bool isHotkey = (fromPos.x == 0xFFFF && fromPos.y == 0 && fromPos.z == 0);
	if (!getBoolean(ConfigManager::AIMBOT_HOTKEY_ENABLED)) {
		if (creature->isPlayer() || isHotkey) {
			player->sendCancelMessage(RETURNVALUE_DIRECTPLAYERSHOOT);
			return;
		}
	}

	Thing* thing = internalGetThing(player, fromPos, fromStackPos, spriteId, STACKPOS_USEITEM);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Item* item = thing->getItem();
	if (!item || !item->isUseable()) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	if (!InstanceUtils::isPlayerInSameInstance(player, item->getInstanceID())) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Position toPos = creature->getPosition();
	Position walkToPos = fromPos;
	ReturnValue ret = g_actions->canUse(player, fromPos);
	if (ret == RETURNVALUE_NOERROR) {
		ret = g_actions->canUse(player, toPos, item);
		if (ret == RETURNVALUE_TOOFARAWAY) {
			walkToPos = toPos;
		}
	}

	if (ret != RETURNVALUE_NOERROR) {
		if (ret == RETURNVALUE_TOOFARAWAY) {
			Position itemPos = fromPos;
			uint8_t itemStackPos = fromStackPos;

			if (fromPos.x != 0xFFFF && fromPos.isInRange(player->getPosition(), 1, 1, 0) &&
			    !fromPos.isInRange(toPos, 1, 1, 0)) {
				Item* moveItem = nullptr;
				ret = internalMoveItem(item->getParent(), player, INDEX_WHEREEVER, item, item->getItemCount(),
				                       &moveItem, 0, player, nullptr, &fromPos, &toPos);
				if (ret != RETURNVALUE_NOERROR) {
					player->sendCancelMessage(ret);
					return;
				}

				// changing the position since its now in the inventory of the player
				internalGetPosition(moveItem, itemPos, itemStackPos);
			}

			std::vector<Direction> listDir;
			if (player->getPathTo(walkToPos, listDir, 0, 1, true, true)) {
				g_dispatcher.addTask([=, this, playerID = player->getID(), listDir = std::move(listDir)]() {
					playerAutoWalk(playerID, listDir);
				});

				auto task = createSchedulerTask(
				    static_cast<uint32_t>(getInteger(ConfigManager::RANGE_USE_WITH_CREATURE_INTERVAL)),
				    ([this, playerId, itemPos, itemStackPos, creatureId, spriteId]()
				    { playerUseWithCreature(playerId, itemPos, itemStackPos, creatureId, spriteId); }));
				player->setNextWalkActionTask(std::move(task));
			} else {
				player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
			}
			return;
		}

		player->sendCancelMessage(ret);
		return;
	}

	if (!player->canDoAction()) {
		uint32_t delay = player->getNextActionTime();
		auto task = createSchedulerTask(
		    delay, ([this, playerId, fromPos, fromStackPos, creatureId, spriteId]()
			{ playerUseWithCreature(playerId, fromPos, fromStackPos, creatureId, spriteId); }));
		player->setNextActionTask(std::move(task));
		return;
	}

	player->resetIdleTime();
	player->setNextActionTask(nullptr);

	auto itemRef = getItemSharedRef(item);
	if (!itemRef) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	g_actions->useItemEx(player, fromPos, creature->getPosition(),
	                     static_cast<uint8_t>(creature->getParent()->getThingIndex(creature)),
	                     itemRef, isHotkey, creature);
	player->maintainAttackFlow();
}

void Game::playerCloseContainer(uint32_t playerId, uint8_t cid)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	player->closeContainer(cid);
	player->sendCloseContainer(cid);
}

void Game::playerMoveUpContainer(uint32_t playerId, uint8_t cid)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Container* container = player->getContainerByID(cid);
	if (!container) {
		return;
	}

	Container* parentContainer = dynamic_cast<Container*>(container->getRealParent());
	if (!parentContainer) {
		return;
	}

	if (!InstanceUtils::isPlayerInSameInstance(player, parentContainer->getInstanceID())) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	int8_t test_cid = player->getContainerID(parentContainer);
	if (test_cid != -1) {
		player->closeContainer(test_cid);
		player->sendCloseContainer(test_cid);
		return;
	}

	bool hasParent = (dynamic_cast<const Container*>(parentContainer->getParent()) != nullptr);
	player->addContainer(cid, parentContainer);
	player->sendContainer(cid, parentContainer, hasParent, player->getContainerIndex(cid));
}

void Game::playerUpdateContainer(uint32_t playerId, uint8_t cid)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Container* container = player->getContainerByID(cid);
	if (!container) {
		return;
	}

	bool hasParent = (dynamic_cast<const Container*>(container->getParent()) != nullptr);
	player->sendContainer(cid, container, hasParent, player->getContainerIndex(cid));
}

void Game::playerRotateItem(uint32_t playerId, const Position& pos, uint8_t stackPos, const uint16_t spriteId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Thing* thing = internalGetThing(player, pos, stackPos, 0, STACKPOS_TOPDOWN_ITEM);
	if (!thing) {
		return;
	}

	Item* item = thing->getItem();
	if (!item || item->getClientID() != spriteId || !item->isRotatable() ||
	    item->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (player->hasFlag(PlayerFlag_CanThrowFar)) {
		g_events->eventPlayerOnRotateItem(player, item);
		return;
	}

	if (pos.x != 0xFFFF && !pos.isInRange(player->getPosition(), 1, 1, 0)) {
		std::vector<Direction> listDir;
		if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
			g_dispatcher.addTask([=, this, playerID = player->getID(), listDir = std::move(listDir)]() {
				playerAutoWalk(playerID, listDir);
			});

			auto task =
			    createSchedulerTask(static_cast<uint32_t>(getInteger(ConfigManager::RANGE_ROTATE_ITEM_INTERVAL)),
			                        ([this, playerId, pos, stackPos, spriteId]()
			                        { playerRotateItem(playerId, pos, stackPos, spriteId); }));
			player->setNextWalkActionTask(std::move(task));
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	g_events->eventPlayerOnRotateItem(player, item);
}

void Game::playerWriteItem(uint32_t playerId, uint32_t windowTextId, std::string_view text)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	if (windowTextId == std::numeric_limits<uint32_t>().max()) {
		player->parseAutoLootWindow(std::string(text));
		return;
	}

	uint16_t maxTextLength = 0;
	uint32_t internalWindowTextId = 0;

	Item* writeItem = player->getWriteItem(internalWindowTextId, maxTextLength);
	if (text.length() > maxTextLength || windowTextId != internalWindowTextId) {
		return;
	}

	if (!writeItem || writeItem->isRemoved()) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Cylinder* topParent = writeItem->getTopParent();

	Player* owner = dynamic_cast<Player*>(topParent);
	if (owner && owner != player) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (!writeItem->getPosition().isInRange(player->getPosition(), 1, 1, 0)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	for (auto creatureEvent : player->getCreatureEvents(CREATURE_EVENT_TEXTEDIT)) {
		if (!creatureEvent->executeTextEdit(player, writeItem, text, windowTextId)) {
			player->setWriteItem(nullptr);
			return;
		}
	}

	if (!text.empty()) {
		if (writeItem->getText() != text) {
			writeItem->setText(text);
			writeItem->setWriter(player->getName());
			writeItem->setDate(time(nullptr));
		}
	} else {
		writeItem->resetText();
		writeItem->resetWriter();
		writeItem->resetDate();
	}

	uint16_t newId = Item::items[writeItem->getID()].writeOnceItemId;
	if (newId != 0) {
		transformItem(writeItem, newId);
	}

	player->setWriteItem(nullptr);
}

void Game::playerUpdateHouseWindow(uint32_t playerId, uint8_t listId, uint32_t windowTextId, std::string_view text)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	if (windowTextId == std::numeric_limits<uint32_t>().max()) {
		player->parseAutoLootWindow(std::string(text));
		return;
	}

	uint32_t internalWindowTextId;
	uint32_t internalListId;

	House* house = player->getEditHouse(internalWindowTextId, internalListId);
	if (house && house->canEditAccessList(internalListId, player) && internalWindowTextId == windowTextId &&
	    listId == 0) {
		house->setAccessList(internalListId, text);
	}

	player->setEditHouse(nullptr);
}

void Game::playerRequestTrade(uint32_t playerId, const Position& pos, uint8_t stackPos, uint32_t tradePlayerId,
                              uint16_t spriteId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	auto tradePartnerRef = getPlayerByID(tradePlayerId);

	Player* tradePartner = tradePartnerRef.get();
	if (!tradePartner || tradePartner == player) {
		player->sendCancelMessage("Select a player to trade with.");
		return;
	}

	if (areDifferentNonZeroInstances(player, tradePartner)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (!tradePartner->getPosition().isInRange(player->getPosition(), 2, 2, 0)) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		return;
	}

	if (!canThrowObjectTo(tradePartner->getPosition(), player->getPosition(), true, true)) {
		player->sendCancelMessage(RETURNVALUE_CANNOTTHROW);
		return;
	}

	Thing* tradeThing = internalGetThing(player, pos, stackPos, 0, STACKPOS_TOPDOWN_ITEM);
	if (!tradeThing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Item* tradeItem = tradeThing->getItem();
	if (tradeItem->getClientID() != spriteId || !tradeItem->isPickupable() ||
	    tradeItem->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (getBoolean(ConfigManager::ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS)) {
		if (const auto tile = tradeItem->getTile()) {
			if (const auto houseTile = tile->getHouseTile()) {
			if (!tradeItem->getTopParent()->getCreature() && !houseTile->getHouse()->isInvited(player)) {
				player->sendCancelMessage(RETURNVALUE_PLAYERISNOTINVITED);
				return;
				}
			}
		}
	}

	const Position& playerPosition = player->getPosition();
	const Position& tradeItemPosition = tradeItem->getPosition();
	if (playerPosition.z != tradeItemPosition.z) {
		player->sendCancelMessage(playerPosition.z > tradeItemPosition.z ? RETURNVALUE_FIRSTGOUPSTAIRS
		                                                                 : RETURNVALUE_FIRSTGODOWNSTAIRS);
		return;
	}

	if (!tradeItemPosition.isInRange(playerPosition, 1, 1)) {
		std::vector<Direction> listDir;
		if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
			g_dispatcher.addTask([=, this, playerID = player->getID(), listDir = std::move(listDir)]() {
				playerAutoWalk(playerID, listDir);
			});

			auto task = createSchedulerTask(RANGE_REQUEST_TRADE_INTERVAL,
				([this, playerId, pos, stackPos, tradePlayerId, spriteId]()
				{ playerRequestTrade(playerId, pos, stackPos, tradePlayerId, spriteId); }));
			player->setNextWalkActionTask(std::move(task));
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	cleanupExpiredTradeItems();

	Container* tradeItemContainer = tradeItem->getContainer();
	if (tradeItemContainer) {
		for (const auto& [weakItem, _] : tradeItems) {
			auto itemRef = weakItem.lock();
			if (!itemRef) {
				continue;
			}

			Item* item = itemRef.get();
			if (tradeItem == item) {
				player->sendCancelMessage("This item is already being traded.");
				return;
			}

			if (tradeItemContainer->isHoldingItem(item)) {
				player->sendCancelMessage("This item is already being traded.");
				return;
			}

			Container* container = item->getContainer();
			if (container && container->isHoldingItem(tradeItem)) {
				player->sendCancelMessage("This item is already being traded.");
				return;
			}
		}
	} else {
		for (const auto& [weakItem, _] : tradeItems) {
			auto itemRef = weakItem.lock();
			if (!itemRef) {
				continue;
			}

			Item* item = itemRef.get();
			if (tradeItem == item) {
				player->sendCancelMessage("This item is already being traded.");
				return;
			}

			Container* container = item->getContainer();
			if (container && container->isHoldingItem(tradeItem)) {
				player->sendCancelMessage("This item is already being traded.");
				return;
			}
		}
	}

	Container* tradeContainer = tradeItem->getContainer();
	if (tradeContainer && tradeContainer->getItemHoldingCount() + 1 > 100) {
		player->sendCancelMessage("You can only trade up to 100 objects at once.");
		return;
	}

	if (!g_events->eventPlayerOnTradeRequest(player, tradePartner, tradeItem)) {
		return;
	}

	internalStartTrade(player, tradePartner, tradeItem);
}

bool Game::internalStartTrade(Player* player, Player* tradePartner, Item* tradeItem)
{
	cleanupExpiredTradeItems();

	if (player->tradeState != TRADE_NONE &&
	    !(player->tradeState == TRADE_ACKNOWLEDGE && player->getTradePartner().get() == tradePartner)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREALREADYTRADING);
		return false;
	} else if (tradePartner->tradeState != TRADE_NONE && tradePartner->getTradePartner().get() != player) {
		player->sendCancelMessage(RETURNVALUE_THISPLAYERISALREADYTRADING);
		return false;
	}

	auto tradeItemRef = getItemSharedRef(tradeItem);
	if (!tradeItemRef) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return false;
	}
	player->tradeItem = tradeItemRef;
	player->setTradePartner(std::static_pointer_cast<Player>(getCreatureSharedRef(tradePartner)));
	player->tradeState = TRADE_INITIATED;
	tradeItems[tradeItem->weak_from_this()] = player->getID();

	player->sendTradeItemRequest(player->getName(), tradeItem, true);

	if (tradePartner->tradeState == TRADE_NONE) {
		tradePartner->sendTextMessage(MESSAGE_EVENT_ADVANCE,
		                              fmt::format("{:s} wants to trade with you.", player->getName()));
		tradePartner->tradeState = TRADE_ACKNOWLEDGE;
		tradePartner->setTradePartner(std::static_pointer_cast<Player>(getCreatureSharedRef(player)));
	} else {
		auto counterOfferItemRef = tradePartner->getTradeItemRef();
		Item* counterOfferItem = counterOfferItemRef.get();
		player->sendTradeItemRequest(tradePartner->getName(), counterOfferItem, false);
		tradePartner->sendTradeItemRequest(player->getName(), tradeItem, false);
	}

	return true;
}

void Game::playerAcceptTrade(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	if (!(player->getTradeState() == TRADE_ACKNOWLEDGE || player->getTradeState() == TRADE_INITIATED)) {
		return;
	}

	auto tradePartnerLock = player->getTradePartner();
	if (!tradePartnerLock) {
		return;
	}
	Player* tradePartner = tradePartnerLock.get();

	if (areDifferentNonZeroInstances(player, tradePartner)) {
		internalCloseTrade(player, false);
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		tradePartner->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	player->setTradeState(TRADE_ACCEPT);

	if (tradePartner->getTradeState() == TRADE_ACCEPT) {
		if (!canThrowObjectTo(tradePartner->getPosition(), player->getPosition(), true, true)) {
			internalCloseTrade(player, false);
			player->sendCancelMessage(RETURNVALUE_CANNOTTHROW);
			tradePartner->sendCancelMessage(RETURNVALUE_CANNOTTHROW);
			return;
		}

		auto playerTradeItemRef = player->getTradeItemRef();
		auto partnerTradeItemRef = tradePartner->getTradeItemRef();
		Item* playerTradeItem = playerTradeItemRef.get();
		Item* partnerTradeItem = partnerTradeItemRef.get();
		if (!playerTradeItem || !partnerTradeItem) {
			internalCloseTrade(player, false);
			return;
		}

		if (!g_events->eventPlayerOnTradeAccept(player, tradePartner, playerTradeItem, partnerTradeItem)) {
			internalCloseTrade(player, false);
			return;
		}

		player->setTradeState(TRADE_TRANSFER);
		tradePartner->setTradeState(TRADE_TRANSFER);

		eraseTradeItem(playerTradeItem);
		eraseTradeItem(partnerTradeItem);

		bool isSuccess = false;

		ReturnValue tradePartnerRet = RETURNVALUE_NOERROR;
		ReturnValue playerRet = RETURNVALUE_NOERROR;

		// if player is trying to trade its own backpack
		if (tradePartner->getInventoryItem(CONST_SLOT_BACKPACK) == partnerTradeItem) {
			tradePartnerRet = (tradePartner->getInventoryItem(getSlotType(Item::items[playerTradeItem->getID()]))
			                       ? RETURNVALUE_NOTENOUGHROOM
			                       : RETURNVALUE_NOERROR);
		}

		if (player->getInventoryItem(CONST_SLOT_BACKPACK) == playerTradeItem) {
			playerRet = (player->getInventoryItem(getSlotType(Item::items[partnerTradeItem->getID()]))
			                 ? RETURNVALUE_NOTENOUGHROOM
			                 : RETURNVALUE_NOERROR);
		}

		// both players try to trade equipped backpacks
		if (player->getInventoryItem(CONST_SLOT_BACKPACK) == playerTradeItem &&
		    tradePartner->getInventoryItem(CONST_SLOT_BACKPACK) == partnerTradeItem) {
			playerRet = RETURNVALUE_NOTENOUGHROOM;
		}

		if (tradePartnerRet == RETURNVALUE_NOERROR && playerRet == RETURNVALUE_NOERROR) {
			tradePartnerRet = internalAddItem(tradePartner, playerTradeItem, INDEX_WHEREEVER, 0, true);
			playerRet = internalAddItem(player, partnerTradeItem, INDEX_WHEREEVER, 0, true);
			if (tradePartnerRet == RETURNVALUE_NOERROR && playerRet == RETURNVALUE_NOERROR) {
				playerRet = internalRemoveItem(playerTradeItem, playerTradeItem->getItemCount(), true);
				tradePartnerRet = internalRemoveItem(partnerTradeItem, partnerTradeItem->getItemCount(), true);
				if (tradePartnerRet == RETURNVALUE_NOERROR && playerRet == RETURNVALUE_NOERROR) {
					tradePartnerRet = internalMoveItem(playerTradeItem->getParent(), tradePartner, INDEX_WHEREEVER,
					                                   playerTradeItem, playerTradeItem->getItemCount(), nullptr,
					                                   FLAG_IGNOREAUTOSTACK, nullptr, partnerTradeItem);
					if (tradePartnerRet == RETURNVALUE_NOERROR) {
						internalMoveItem(partnerTradeItem->getParent(), player, INDEX_WHEREEVER, partnerTradeItem,
						                 partnerTradeItem->getItemCount(), nullptr, FLAG_IGNOREAUTOSTACK);
						playerTradeItem->onTradeEvent(ON_TRADE_TRANSFER, tradePartner);
						partnerTradeItem->onTradeEvent(ON_TRADE_TRANSFER, player);
						isSuccess = true;
					}
				}
			}
		}

		if (!isSuccess) {
			std::string errorDescription;

			if (partnerTradeItem) {
				errorDescription = getTradeErrorDescription(tradePartnerRet, playerTradeItem);
				tradePartner->sendTextMessage(MESSAGE_EVENT_ADVANCE, errorDescription);
				partnerTradeItem->onTradeEvent(ON_TRADE_CANCEL, tradePartner);
			}

			if (playerTradeItem) {
				errorDescription = getTradeErrorDescription(playerRet, partnerTradeItem);
				player->sendTextMessage(MESSAGE_EVENT_ADVANCE, errorDescription);
				playerTradeItem->onTradeEvent(ON_TRADE_CANCEL, player);
			}
		}

		g_events->eventPlayerOnTradeCompleted(player, tradePartner, playerTradeItem, partnerTradeItem, isSuccess);

		player->setTradeState(TRADE_NONE);
		player->tradeItem.reset();
		player->setTradePartner(nullptr);
		player->sendTradeClose();

		tradePartner->setTradeState(TRADE_NONE);
		tradePartner->tradeItem.reset();
		tradePartner->setTradePartner(nullptr);
		tradePartner->sendTradeClose();
	}
}

std::string Game::getTradeErrorDescription(ReturnValue ret, Item* item)
{
	if (item) {
		if (ret == RETURNVALUE_NOTENOUGHCAPACITY) {
			return fmt::format("You do not have enough capacity to carry {:s}.\n {:s}",
			                   item->isStackable() && item->getItemCount() > 1 ? "these objects" : "this object",
			                   item->getWeightDescription());
		} else if (ret == RETURNVALUE_NOTENOUGHROOM || ret == RETURNVALUE_CONTAINERNOTENOUGHROOM) {
			return fmt::format("You do not have enough room to carry {:s}.",
			                   item->isStackable() && item->getItemCount() > 1 ? "these objects" : "this object");
		}
	}
	return "Trade could not be completed.";
}

void Game::playerLookInTrade(uint32_t playerId, bool lookAtCounterOffer, uint8_t index)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	auto tradePartnerLock = player->getTradePartner();
	if (!tradePartnerLock) {
		return;
	}
	Player* tradePartner = tradePartnerLock.get();

	std::shared_ptr<Item> tradeItemRef;
	if (lookAtCounterOffer) {
		tradeItemRef = tradePartner->getTradeItemRef();
	} else {
		tradeItemRef = player->getTradeItemRef();
	}

	Item* tradeItem = tradeItemRef.get();
	if (!tradeItem) {
		return;
	}

	const Position& playerPosition = player->getPosition();
	const Position& tradeItemPosition = tradeItem->getPosition();

	int32_t lookDistance =
	    std::max(playerPosition.getDistanceX(tradeItemPosition), playerPosition.getDistanceY(tradeItemPosition));
	if (index == 0) {
		g_events->eventPlayerOnLookInTrade(player, tradePartner, tradeItem, lookDistance);
		return;
	}

	Container* tradeContainer = tradeItem->getContainer();
	if (!tradeContainer) {
		return;
	}

	std::vector<const Container*> containers{tradeContainer};
	size_t i = 0;
	while (i < containers.size()) {
		const Container* container = containers[i++];
		for (const auto& item : container->getItemList()) {
			Container* tmpContainer = item->getContainer();
			if (tmpContainer) {
				containers.push_back(tmpContainer);
			}

			if (--index == 0) {
				g_events->eventPlayerOnLookInTrade(player, tradePartner, item.get(), lookDistance);
				return;
			}
		}
	}
}

void Game::playerCloseTrade(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	internalCloseTrade(player);
}

void Game::internalCloseTrade(Player* player, bool sendCancel /* = true*/)
{
	auto tradePartnerLock = player->getTradePartner();
	Player* tradePartner = tradePartnerLock.get();
	if ((tradePartner && tradePartner->getTradeState() == TRADE_TRANSFER) ||
	    player->getTradeState() == TRADE_TRANSFER) {
		return;
	}

	// Cache and clear player's trade item before calling Lua callbacks
	// to prevent reentrancy issues if onTradeEvent modifies trade state.
	auto playerTradeItemRef = player->getTradeItemRef();
	Item* playerTradeItem = playerTradeItemRef.get();
	if (playerTradeItem) {
		player->tradeItem.reset();

		eraseTradeItem(playerTradeItem);

		playerTradeItem->onTradeEvent(ON_TRADE_CANCEL, player);
	}

	player->setTradeState(TRADE_NONE);
	player->setTradePartner(nullptr);

	if (sendCancel) {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, "Trade cancelled.");
	}
	player->sendTradeClose();

	if (tradePartner) {
		auto partnerTradeItemRef = tradePartner->getTradeItemRef();
		Item* partnerTradeItem = partnerTradeItemRef.get();
		if (partnerTradeItem) {
			tradePartner->tradeItem.reset();

			eraseTradeItem(partnerTradeItem);

			partnerTradeItem->onTradeEvent(ON_TRADE_CANCEL, tradePartner);
		}

		tradePartner->setTradeState(TRADE_NONE);
		tradePartner->setTradePartner(nullptr);

		if (sendCancel) {
			tradePartner->sendTextMessage(MESSAGE_STATUS_SMALL, "Trade cancelled.");
		}
		tradePartner->sendTradeClose();
	}
}

void Game::playerPurchaseItem(uint32_t playerId, uint16_t spriteId, uint8_t count, uint8_t amount,
                              bool ignoreCap /* = false*/, bool inBackpacks /* = false*/)
{
	if (amount == 0 || amount > 100) {
		return;
	}

	auto playerRef = getPlayerByID(playerId);

	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	int32_t onBuy, onSell;

	Npc* merchant = player->getShopOwner(onBuy, onSell);
	if (!merchant) {
		return;
	}

	const ItemType& it = Item::items[spriteId];
	if (it.id == 0) {
		return;
	}

	uint8_t subType;
	if (it.isSplash() || it.isFluidContainer()) {
		subType = clientFluidToServer(count);
	} else {
		subType = count;
	}

	if (!player->hasShopItemForSale(it.id, subType)) {
		return;
	}

	merchant->onPlayerTrade(player, onBuy, it.id, subType, amount, ignoreCap, inBackpacks);
}

void Game::playerSellItem(uint32_t playerId, uint16_t spriteId, uint8_t count, uint8_t amount, bool ignoreEquipped)
{
	if (amount == 0 || amount > 100) {
		return;
	}

	auto playerRef = getPlayerByID(playerId);

	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	int32_t onBuy, onSell;

	Npc* merchant = player->getShopOwner(onBuy, onSell);
	if (!merchant) {
		return;
	}

	const ItemType& it = Item::items[spriteId];
	if (it.id == 0) {
		return;
	}

	uint8_t subType;
	if (it.isSplash() || it.isFluidContainer()) {
		subType = clientFluidToServer(count);
	} else {
		subType = count;
	}

	merchant->onPlayerTrade(player, onSell, it.id, subType, amount, ignoreEquipped);
}

void Game::playerCloseShop(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	player->closeShopWindow();
}

void Game::playerLookInShop(uint32_t playerId, uint16_t spriteId, uint8_t count)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	int32_t onBuy, onSell;

	Npc* merchant = player->getShopOwner(onBuy, onSell);
	if (!merchant) {
		return;
	}

	const ItemType& it = Item::items[spriteId];
	if (it.id == 0) {
		return;
	}

	int32_t subType;
	if (it.isFluidContainer() || it.isSplash()) {
		subType = clientFluidToServer(count);
	} else {
		subType = count;
	}

	if (!player->hasShopItemForSale(it.id, static_cast<uint8_t>(subType))) {
		return;
	}

	g_events->eventPlayerOnLookInShop(player, &it, static_cast<uint8_t>(subType));
}

void Game::playerLookAt(uint32_t playerId, const Position& pos, uint8_t stackPos)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Thing* thing = internalGetThing(player, pos, stackPos, 0, STACKPOS_LOOK);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (const Item* item = thing->getItem();
	    item && !InstanceUtils::canSeeItemInInstance(player->getInstanceID(), item)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Position thingPos = thing->getPosition();
	if (!player->canSee(thingPos)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Position playerPos = player->getPosition();

	int32_t lookDistance = -1;
	if (thing != player) {
		lookDistance = std::max(playerPos.getDistanceX(thingPos), playerPos.getDistanceY(thingPos));
		if (playerPos.z != thingPos.z) {
			lookDistance += 15;
		}
	}

	g_events->eventPlayerOnLook(player, pos, thing, stackPos, lookDistance);
}

void Game::playerLookInBattleList(uint32_t playerId, uint32_t creatureId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Creature* creature = getCreatureByID(creatureId);
	if (!creature) {
		return;
	}

	if (!player->canSeeCreature(creature)) {
		return;
	}

	const Position& creaturePos = creature->getPosition();
	if (!player->canSee(creaturePos)) {
		return;
	}

	int32_t lookDistance = -1;
	if (creature != player) {
		const Position& playerPos = player->getPosition();
		lookDistance = std::max(playerPos.getDistanceX(creaturePos), playerPos.getDistanceY(creaturePos));
		if (playerPos.z != creaturePos.z) {
			lookDistance += 15;
		}
	}

	g_events->eventPlayerOnLookInBattleList(player, creature, lookDistance);
}

void Game::playerCancelAttackAndFollow(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	playerSetAttackedCreature(playerId, 0);
	playerFollowCreature(playerId, 0);
	player->stopWalk();
}

void Game::playerSetAttackedCreature(uint32_t playerId, uint32_t creatureId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	if (player->getAttackedCreatureShared() && creatureId == 0) {
		player->setAttackedCreature(nullptr);
		player->sendCancelTarget();
		return;
	}

	Creature* attackCreature = getCreatureByID(creatureId);
	if (!attackCreature) {
		player->setAttackedCreature(nullptr);
		player->sendCancelTarget();
		return;
	}

	if (!InstanceUtils::canInteract(player, attackCreature)) {
		player->sendCancelMessage(RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE);
		player->sendCancelTarget();
		player->setAttackedCreature(nullptr);
		return;
	}

	ReturnValue ret = Combat::canTargetCreature(player, attackCreature);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
		player->sendCancelTarget();
		player->setAttackedCreature(nullptr);
		return;
	}

	player->setAttackedCreature(attackCreature);
}

void Game::playerFollowCreature(uint32_t playerId, uint32_t creatureId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Creature *followCreature = getCreatureByID(creatureId);
	if (followCreature && !InstanceUtils::canInteract(player, followCreature)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		player->sendCancelTarget();
		return;
	}

	player->setAttackedCreature(nullptr);
	player->setFollowCreature(followCreature);
}

void Game::playerRequestAddVip(uint32_t playerId, std::string_view name)
{
	if (name.length() > PLAYER_NAME_LENGTH) {
		return;
	}

	auto playerRef = getPlayerByID(playerId);

	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	auto vipPlayerRef = getPlayerByName(name);

	Player* vipPlayer = vipPlayerRef.get();
	if (!vipPlayer) {
		uint32_t guid;
		bool specialVip;
		std::string formattedName{name};
		if (!IOLoginData::getGuidByNameEx(guid, specialVip, formattedName)) {
			player->sendTextMessage(MESSAGE_STATUS_SMALL, "A player with this name does not exist.");
			return;
		}

		if (specialVip && !player->hasFlag(PlayerFlag_SpecialVIP)) {
			player->sendTextMessage(MESSAGE_STATUS_SMALL, "You can not add this player.");
			return;
		}

		player->addVIP(guid, formattedName, VIPSTATUS_OFFLINE);
	} else {
		if (vipPlayer->hasFlag(PlayerFlag_SpecialVIP) && !player->hasFlag(PlayerFlag_SpecialVIP)) {
			player->sendTextMessage(MESSAGE_STATUS_SMALL, "You can not add this player.");
			return;
		}

		if (!vipPlayer->isInGhostMode() || player->canSeeGhostMode(vipPlayer)) {
			player->addVIP(vipPlayer->getGUID(), vipPlayer->getName(), VIPSTATUS_ONLINE);
		} else {
			player->addVIP(vipPlayer->getGUID(), vipPlayer->getName(), VIPSTATUS_OFFLINE);
		}
	}
}

void Game::playerRequestRemoveVip(uint32_t playerId, uint32_t guid)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	player->removeVIP(guid);
}

void Game::playerTurn(uint32_t playerId, Direction dir)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	if (!g_events->eventPlayerOnTurn(player, dir)) {
		return;
	}

	player->resetIdleTime();
	internalCreatureTurn(player, dir);
}

void Game::playerRequestOutfit(uint32_t playerId)
{
	if (!getBoolean(ConfigManager::ALLOW_CHANGEOUTFIT)) {
		return;
	}

	auto playerRef = getPlayerByID(playerId);

	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	player->sendOutfitWindow();
}

void Game::playerChangeOutfit(uint32_t playerId, Outfit_t outfit, bool randomizeMount /* = false*/)
{
	if (!getBoolean(ConfigManager::ALLOW_CHANGEOUTFIT)) {
		return;
	}

	auto playerRef = getPlayerByID(playerId);

	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	player->randomizeMount = randomizeMount;

	const Outfit* playerOutfit = Outfits::getInstance().getOutfitByLookType(outfit.lookType);
	if (!playerOutfit) {
		outfit.lookMount = 0;
	}

	if (outfit.lookMount != 0) {
		Mount* mount = mounts.getMountByClientID(outfit.lookMount);
		if (!mount) {
			return;
		}

		if (!player->hasMount(mount)) {
			return;
		}

		int32_t speedChange = mount->speed;
		if (player->isMounted()) {
			Mount* prevMount = mounts.getMountByID(player->getCurrentMount());
			if (prevMount) {
				speedChange -= prevMount->speed;
			}
		}

		changeSpeed(player, speedChange);
		player->changeMount(mount->id, true);
		player->setCurrentMount(mount->id);
	} else {
		if (player->isMounted()) {
			player->dismount();
		}

		player->wasMounted = false;
	}

	if (player->canWear(outfit.lookType, outfit.lookAddons)) {
		player->changeOutfit(outfit, false);

		if (player->hasCondition(CONDITION_OUTFIT)) {
			return;
		}

		if (player->randomizeMount && player->hasMounts()) {
			const Mount* mount = mounts.getMountByID(player->getRandomMount());
			outfit.lookMount = mount->clientId;
		}

		internalCreatureChangeOutfit(player, outfit);
	}

	if (player->isMounted()) {
		player->onChangeZone(player->getZone());
	}
}

void Game::playerSay(uint32_t playerId, uint16_t channelId, SpeakClasses type, std::string_view receiver,
                     std::string_view text, bool forceCastOnFoot /* = false */)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	player->resetIdleTime();

	if (playerSaySpell(player, type, text, forceCastOnFoot)) {
		return;
	}

	if (type == TALKTYPE_PRIVATE_PN) {
		playerSpeakToNpc(player, text);
		return;
	}

	uint32_t muteTime = player->isMuted();
	if (muteTime > 0) {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("You are still muted for {:d} seconds.", muteTime));
		return;
	}

	if (!player->isAccessPlayer()) {
		lua_State* L = g_luaEnvironment.getLuaState();
		if (L) {
			if (g_luaEnvironment.loadFile("data/anti-divulgacao.lua") == 0) {
				lua_getglobal(L, "checkMessage");
				if (lua_isfunction(L, -1)) {
					lua_pushlstring(L, text.data(), text.length());
					lua_pushboolean(L, player->isAccessPlayer());
					
					if (lua_pcall(L, 2, 2, 0) == 0) {
						bool isBlocked = lua_toboolean(L, -2);
						if (isBlocked) {
							std::string replacement = lua_tostring(L, -1);
							internalCreatureSay(player, TALKTYPE_SAY, replacement, false);
							return;
						}
						lua_pop(L, 2);
					} else {
						lua_pop(L, 1);
					}
				} else {
					lua_pop(L, 1);
				}
			}
		}
	}

	if (channelId == CHANNEL_CAST) {
		player->client->sendCastMessage(player->getName(), std::string{text}, TALKTYPE_CHANNEL_Y);
		return;
	}

	if (!text.empty() && text.front() == '/' && player->isAccessPlayer()) {
		return;
	}

	player->removeMessageBuffer();

	switch (type) {
		case TALKTYPE_PRIVATE:
		case TALKTYPE_PRIVATE_RED:
			playerSpeakTo(player, type, receiver, text);
			break;

		case TALKTYPE_SAY:
			internalCreatureSay(player, TALKTYPE_SAY, text, false);
			break;

		case TALKTYPE_WHISPER:
			playerWhisper(player, text);
			break;

		case TALKTYPE_YELL:
			playerYell(player, text);
			break;

		case TALKTYPE_CHANNEL_O:
		case TALKTYPE_CHANNEL_Y:
		case TALKTYPE_CHANNEL_R1:
			g_chat->talkToChannel(*player, type, text, channelId);
			break;

		case TALKTYPE_BROADCAST:
			playerBroadcastMessage(player, text);
			break;

		default:
			break;
	}
}

bool Game::playerSaySpell(Player* player, SpeakClasses type, std::string_view text, bool forceCastOnFoot /* = false */)
{
	TalkActionResult result = g_talkActions->playerSaySpell(player, type, text);
	if (result == TalkActionResult::BREAK) {
		return true;
	}

	std::string words{text};

	result = g_spells->playerSaySpell(player, words, forceCastOnFoot);
	if (result == TalkActionResult::BREAK) {
		if (!getBoolean(ConfigManager::EMOTE_SPELLS)) {
			return internalCreatureSay(player, TALKTYPE_SAY, words, false);
		} else {
			return internalCreatureSay(player, TALKTYPE_MONSTER_SAY, words, false);
		}

	} else if (result == TalkActionResult::FAILED) {
		return true;
	}

	return false;
}

void Game::playerWhisper(Player* player, std::string_view text)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, player->getPosition(), false, false, Map::maxClientViewportX, Map::maxClientViewportX,
	                  Map::maxClientViewportY, Map::maxClientViewportY);

	// send to client
	for (const auto& spectator : spectators.players()) {
		Player* spectatorPlayer = static_cast<Player*>(spectator.get());
		if (!spectatorPlayer->compareInstance(player->getInstanceID())) {
			continue;
		}
		if (!player->getPosition().isInRange(spectatorPlayer->getPosition(), 1, 1)) {
			spectatorPlayer->sendCreatureSay(player, TALKTYPE_WHISPER, "pspsps");
		} else {
			spectatorPlayer->sendCreatureSay(player, TALKTYPE_WHISPER, text);
		}
	}

	// event method
	for (const auto& spectator : spectators) {
		if (!spectator->compareInstance(player->getInstanceID())) {
			continue;
		}
		spectator->onCreatureSay(player, TALKTYPE_WHISPER, text);
	}
}

bool Game::playerYell(Player* player, std::string_view text)
{
	if (player->hasCondition(CONDITION_YELLTICKS)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return false;
	}

	if (!player->isAccessPlayer() && !player->hasFlag(PlayerFlag_IgnoreYellCheck)) {
		const int64_t minimumLevel = getInteger(ConfigManager::YELL_MINIMUM_LEVEL);
		if (player->getLevel() < minimumLevel) {
			if (getBoolean(ConfigManager::YELL_ALLOW_PREMIUM)) {
				if (!player->isPremium()) {
					player->sendTextMessage(
					    MESSAGE_STATUS_SMALL,
					    fmt::format("You may not yell unless you have reached level {:d} or have a premium account.",
					                minimumLevel));
					return false;
				}
			} else {
				player->sendTextMessage(
				    MESSAGE_STATUS_SMALL,
				    fmt::format("You may not yell unless you have reached level {:d}.", minimumLevel));
				return false;
			}
		}

		auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_YELLTICKS, 30000, 0);
		player->addCondition(std::move(condition));
	}

	internalCreatureSay(player, TALKTYPE_YELL, asUpperCaseString(std::string{text}), false);
	return true;
}

bool Game::playerSpeakTo(Player* player, SpeakClasses type, std::string_view receiver, std::string_view text)
{
	auto toPlayerRef = getPlayerByName(receiver);
	Player* toPlayer = toPlayerRef.get();
	if (!toPlayer) {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, "A player with this name is not online.");
		return false;
	}

	if (type == TALKTYPE_PRIVATE_RED &&
	    (player->hasFlag(PlayerFlag_CanTalkRedPrivate) || player->getAccountType() >= ACCOUNT_TYPE_GAMEMASTER)) {
		type = TALKTYPE_PRIVATE_RED;
	} else {
		type = TALKTYPE_PRIVATE;
	}

	if (!player->isAccessPlayer() && !player->hasFlag(PlayerFlag_IgnoreSendPrivateCheck)) {
		const int64_t minimumLevel = getInteger(ConfigManager::MINIMUM_LEVEL_TO_SEND_PRIVATE);
		if (player->getLevel() < minimumLevel) {
			if (getBoolean(ConfigManager::PREMIUM_TO_SEND_PRIVATE)) {
				if (!player->isPremium()) {
					player->sendTextMessage(
					    MESSAGE_STATUS_SMALL,
					    fmt::format(
					        "You may not send private messages unless you have reached level {:d} or have a premium account.",
					        minimumLevel));
					return false;
				}
			} else {
				player->sendTextMessage(
				    MESSAGE_STATUS_SMALL,
				    fmt::format("You may not send private messages unless you have reached level {:d}.", minimumLevel));
				return false;
			}
		}
	}

	toPlayer->sendPrivateMessage(player, type, text);
	toPlayer->onCreatureSay(player, type, text);

	// Spy: Lua-based PM keyword logger
	{
		lua_State* L = g_luaEnvironment.getLuaState();
		if (L) {
			if (g_luaEnvironment.loadFile("data/scripts/spy/spy_pm_logger.lua") == 0) {
				lua_getglobal(L, "checkPrivateMessage");
				if (lua_isfunction(L, -1)) {
					lua_pushlstring(L, player->getName().data(), player->getName().size());
					lua_pushlstring(L, toPlayer->getName().data(), toPlayer->getName().size());
					lua_pushlstring(L, text.data(), text.size());
					if (lua_pcall(L, 3, 0, 0) != 0) {
						lua_pop(L, 1);
					}
				} else {
					lua_pop(L, 1);
				}
			}
		}
	}

	if (toPlayer->isInGhostMode() && !player->canSeeGhostMode(toPlayer)) {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, "A player with this name is not online.");
	} else {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("Message sent to {:s}.", toPlayer->getName()));
	}
	return true;
}

void Game::playerSpeakToNpc(Player* player, std::string_view text)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, player->getPosition());
	for (const auto& spectator : spectators.npcs()) {
		if (InstanceUtils::isPlayerInSameInstance(player, spectator->getInstanceID())) {
			spectator->onCreatureSay(player, TALKTYPE_PRIVATE_PN, text);
		}
	}
}

//--
bool Game::canThrowObjectTo(const Position& fromPos, const Position& toPos, bool checkLineOfSight /*= true*/,
                            bool sameFloor /*= false*/, int32_t rangex /*= Map::maxClientViewportX*/,
                            int32_t rangey /*= Map::maxClientViewportY*/) const
{
	return map.canThrowObjectTo(fromPos, toPos, checkLineOfSight, sameFloor, rangex, rangey);
}

bool Game::isSightClear(const Position& fromPos, const Position& toPos, bool sameFloor /*= false*/) const
{
	return map.isSightClear(fromPos, toPos, sameFloor);
}

bool Game::internalCreatureTurn(Creature* creature, Direction dir)
{
	if (creature->getDirection() == dir) {
		return false;
	}

	creature->setDirection(dir);

	// send to client
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (const auto& spectator : spectators.players()) {
		Player* tmpPlayer = static_cast<Player*>(spectator.get());
		if (tmpPlayer->canSeeCreature(creature)) {
			tmpPlayer->sendCreatureTurn(creature);
		}
	}
	return true;
}

bool Game::internalCreatureSay(Creature* creature, SpeakClasses type, std::string_view text, bool ghostMode,
                               SpectatorVec* spectatorsPtr /* = nullptr*/, const Position* pos /* = nullptr*/,
                               bool echo /* = false*/)
{
	if (text.empty()) {
		return false;
	}

	if (!pos) {
		pos = &creature->getPosition();
	}

	SpectatorVec spectators;

	if (!spectatorsPtr || spectatorsPtr->empty()) {
		// This somewhat complex construct ensures that the cached SpectatorVec
		// is used if available and if it can be used, else a local vector is
		// used (hopefully the compiler will optimize away the construction of
		// the temporary when it's not used).
		if (type != TALKTYPE_YELL && type != TALKTYPE_MONSTER_YELL) {
			map.getSpectators(spectators, *pos, false, false, Map::maxClientViewportX, Map::maxClientViewportX,
			                  Map::maxClientViewportY, Map::maxClientViewportY);
		} else {
			map.getSpectators(spectators, *pos, true, false, (Map::maxClientViewportX * 2) + 2,
			                  (Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2,
			                  (Map::maxClientViewportY * 2) + 2);
		}
	} else {
		spectators = (*spectatorsPtr);
	}

	// send to client
	const bool localPositionTalk = isLocalPositionTalk(type);
	for (const auto& spectator : spectators) {
		Player* tmpPlayer = spectator ? spectator->getPlayer() : nullptr;
		if (!tmpPlayer) {
			continue;
		}
		if (localPositionTalk && areDifferentNonZeroInstances(tmpPlayer, creature)) {
			continue;
		}
		if (!ghostMode || tmpPlayer->canSeeCreature(creature)) {
			tmpPlayer->sendCreatureSay(creature, type, text, pos);
		}
	}

	// event method
	if (!echo) {
		for (const auto& spectator : spectators) {
			if (!spectator) {
				continue;
			}
			if (localPositionTalk && areDifferentNonZeroInstances(spectator.get(), creature)) {
				continue;
			}
			spectator->onCreatureSay(creature, type, text);
			if (creature != spectator.get()) {
				g_events->eventCreatureOnHear(spectator.get(), creature, text, type);
			}
		}
	}
	return true;
}

void Game::checkCreatureWalk(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (creature && !creature->isRemoved() && !creature->isDead()) {
		creature->onWalk();
	}
}

void Game::updateCreatureWalk(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (creature && !creature->isRemoved() && !creature->isDead()) {
		creature->isUpdatingPath = false;
		creature->goToFollowCreature();
	}
}

void Game::checkCreatureAttack(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (creature && !creature->isRemoved() && !creature->isDead()) {
		creature->onAttacking(0);
	}
}

void Game::addCreatureCheck(Creature* creature)
{
	if (!creature || creature->isRemoved()) {
		return;
	}

	if (gameState == GAME_STATE_SHUTDOWN) {
		return;
	}

	creature->creatureCheck = true;

	if (creature->inCheckCreaturesVector) {
		// already in a vector
		return;
	}

	creature->inCheckCreaturesVector = true;
	checkCreatureLists[uniform_random(0, EVENT_CREATURECOUNT - 1)].push_back(getCreatureSharedRef(creature));
}

void Game::removeCreatureCheck(Creature* creature)
{
	if (!creature) {
		return;
	}

	if (creature->inCheckCreaturesVector) {
		creature->creatureCheck = false;
	}
}

void Game::checkCreatures(size_t index)
{
	auto& checkCreatureList = checkCreatureLists[index];
	size_t i = 0;

	while (i < checkCreatureList.size()) {
		Creature* creature = checkCreatureList[i].get();

		if (!creature) {
			checkCreatureList[i] = checkCreatureList.back();
			checkCreatureList.pop_back();
			continue;
		}

		if (creature->creatureCheck) {
			if (!creature->isDead() && !creature->isRemoved()) {
				creature->onThink(EVENT_CREATURE_THINK_INTERVAL);
				creature->onAttacking(EVENT_CREATURE_THINK_INTERVAL);
				creature->executeConditions(EVENT_CREATURE_THINK_INTERVAL);
			} else {
				// Dead/removed creatures sitting idle — mark for removal next cycle
				creature->creatureCheck = false;
			}
			++i;
		} else {
			creature->inCheckCreaturesVector = false;
			checkCreatureList[i] = checkCreatureList.back();
			checkCreatureList.pop_back();
		}
	}

	if (!ToReleaseCreatures.empty() || !ToReleaseItems.empty()) {
		cleanup();
	}

#ifdef STATS_ENABLED
	g_stats.playersOnline = getPlayersOnline();
#endif

	g_scheduler.addEvent(createSchedulerTask(EVENT_CHECK_CREATURE_INTERVAL,
	                                         [index]() { g_game.checkCreatures((index + 1) % EVENT_CREATURECOUNT); }));
}

void Game::checkSereneStatus()
{
	// OPTIMIZATION: Increased interval from 1s to 5s - serene status
	// does not need sub-second precision, 5 seconds is more than enough.
	g_scheduler.addEvent(createSchedulerTask(5000, [this]() { checkSereneStatus(); }));

	for (const auto& player : getPlayers()) {
		if (!player || !player->isMonk()) {
			continue;
		}

		// Forced serene via cooldown (Focus Serenity spell)
		if (player->getSereneCooldown() > 0) {
			player->setSerene(true);
			continue;
		}

		// Check natural serene conditions:
		// 1) No nearby party members, OR
		// 2) Fewer than 6 non-summoned monsters around
		const Position &pos = player->getPosition();
		const Party* party = player->getParty();
		bool hasNearbyPartyMembers = false;

		if (party) {
			auto leader = party->getLeader();
			if (leader && leader.get() != player.get()) {
				const Position& lpos = leader->getPosition();
				if (pos.z == lpos.z && std::max(
					std::abs(pos.x - lpos.x), std::abs(pos.y - lpos.y)) <= 10) {
					hasNearbyPartyMembers = true;
				}
			}
			if (!hasNearbyPartyMembers) {
				for (auto& weakMember : const_cast<Party*>(party)->getMembers()) {
					if (auto memberRef = weakMember.lock()) {
						Player* member = memberRef.get();
						if (member == player.get()) continue;
						const Position& mpos = member->getPosition();
						if (pos.z == mpos.z && std::max(
							std::abs(pos.x - mpos.x), std::abs(pos.y - mpos.y)) <= 10) {
							hasNearbyPartyMembers = true;
							break;
						}
					}
				}
			}
		}

		bool notBoxed = true;
		SpectatorVec spectators;
		// OPTIMIZATION: Reduced scan area from 7x7x5x5 to 5x5x5x5
		map.getSpectators(spectators, pos, false, false, 5, 5, 5, 5);
		int monsterCount = 0;
		for (const auto& spec : spectators.monsters()) {
			if (!spec->getMaster()) {
				if (++monsterCount >= 6) {
					notBoxed = false;
					break;
				}
			}
		}

		bool condition1 = !party || !hasNearbyPartyMembers;
		bool condition2 = notBoxed;
		player->setSerene(condition1 && condition2);
	}
}

void Game::changeSpeed(Creature* creature, int32_t varSpeedDelta)
{
	int32_t varSpeed = creature->getSpeed() - creature->getBaseSpeed();
	varSpeed += varSpeedDelta;

	creature->setSpeed(varSpeed);

	// send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), false, true);
	const uint32_t creatureInstance = creature->getInstanceID();
	for (const auto& spectator : spectators.players()) {
		Player* p = static_cast<Player*>(spectator.get());
		if (!p->compareInstance(creatureInstance)) {
			continue;
		}
		p->sendChangeSpeed(creature, creature->getStepSpeed());
	}
}

void Game::setCreatureSpeed(Creature* creature, int32_t speed)
{
	creature->setBaseSpeed(speed);

	//send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), false, true);
	const uint32_t creatureInstance = creature->getInstanceID();
	for (const auto& spectator : spectators.players()) {
		Player* p = static_cast<Player*>(spectator.get());
		if (!p->compareInstance(creatureInstance)) {
			continue;
		}
		p->sendChangeSpeed(creature, creature->getStepSpeed());
	}
}

void Game::internalCreatureChangeOutfit(Creature* creature, const Outfit_t& outfit)
{
	if (!g_events->eventCreatureOnChangeOutfit(creature, outfit)) {
		return;
	}

	creature->setCurrentOutfit(outfit);

	if (creature->isInvisible()) {
		return;
	}

	// send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	const uint32_t creatureInstance = creature->getInstanceID();
	for (const auto& spectator : spectators.players()) {
		Player* p = static_cast<Player*>(spectator.get());
		if (!p->compareInstance(creatureInstance)) {
			continue;
		}
		p->sendCreatureChangeOutfit(creature, outfit);
	}
}

void Game::internalCreatureChangeVisible(Creature* creature, bool visible)
{
	// send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	const uint32_t creatureInstance = creature->getInstanceID();
	for (const auto& spectator : spectators.players()) {
		Player* p = static_cast<Player*>(spectator.get());
		if (!p->compareInstance(creatureInstance)) {
			continue;
		}
		p->sendCreatureChangeVisible(creature, visible);
	}
}

void Game::changeLight(const Creature* creature)
{
	// send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	const uint32_t creatureInstance = creature->getInstanceID();
	for (const auto& spectator : spectators.players()) {
		Player* p = static_cast<Player*>(spectator.get());
		if (!p->compareInstance(creatureInstance)) {
			continue;
		}
		p->sendCreatureLight(creature);
	}
}

bool Game::combatBlockHit(CombatDamage& damage, Creature* attacker, Creature* target, bool checkDefense,
                          bool checkArmor, bool field, bool ignoreResistances /*= false */)
{
	if (damage.primary.type == COMBAT_NONE && damage.secondary.type == COMBAT_NONE) {
		return true;
	}

	if (target->isPlayer() && target->isInGhostMode()) {
		return true;
	}

	if (attacker && !attacker->compareInstance(target->getInstanceID())) {
		return true;
	}

	std::shared_ptr<Creature> attackerRef;
	if (attacker) {
		attackerRef = attacker->weak_from_this().lock();
		if (!attackerRef) {
			LOG_ERROR(fmt::format("[Game::combatBlockHit] Failed to lock attacker shared reference: {}",
			                      static_cast<const void*>(attacker)));
			return true;
		}
	}

	uint32_t targetInstanceId = target->getInstanceID();
	const auto sendBlockEffect = [targetInstanceId](BlockType_t blockType, CombatType_t combatType,
	                                                const Position& targetPos) {
		if (blockType == BLOCK_DEFENSE) {
			InstanceUtils::sendMagicEffectToInstance(targetPos, targetInstanceId, CONST_ME_POFF);
		} else if (blockType == BLOCK_ARMOR) {
			InstanceUtils::sendMagicEffectToInstance(targetPos, targetInstanceId, CONST_ME_BLOCKHIT);
		} else if (blockType == BLOCK_IMMUNITY) {
			uint8_t hitEffect = 0;
			switch (combatType) {
				case COMBAT_UNDEFINEDDAMAGE: {
					return;
				}
				case COMBAT_ENERGYDAMAGE:
				case COMBAT_FIREDAMAGE:
				case COMBAT_PHYSICALDAMAGE:
				case COMBAT_ICEDAMAGE:
				case COMBAT_DEATHDAMAGE: {
					hitEffect = CONST_ME_BLOCKHIT;
					break;
				}
				case COMBAT_AGONYDAMAGE: {
					hitEffect = CONST_ME_AGONY;
					break;
				}
				case COMBAT_EARTHDAMAGE: {
					hitEffect = CONST_ME_GREEN_RINGS;
					break;
				}
				case COMBAT_HOLYDAMAGE: {
					hitEffect = CONST_ME_HOLYDAMAGE;
					break;
				}
				default: {
					hitEffect = CONST_ME_POFF;
					break;
				}
			}
			InstanceUtils::sendMagicEffectToInstance(targetPos, targetInstanceId, hitEffect);
		}
	};

	BlockType_t primaryBlockType, secondaryBlockType;
	if (damage.primary.type != COMBAT_NONE) {
		damage.primary.value = std::abs(damage.primary.value);
		primaryBlockType = target->blockHit(attackerRef, damage.primary.type, damage.primary.value, checkDefense,
		                                    checkArmor, field, ignoreResistances, damage.origin);

		if (damage.primary.type != COMBAT_HEALING) {
			damage.primary.value = -damage.primary.value;
			sendBlockEffect(primaryBlockType, damage.primary.type, target->getPosition());
		}
	} else {
		primaryBlockType = BLOCK_NONE;
	}

	if (damage.secondary.type != COMBAT_NONE) {
		damage.secondary.value = std::abs(damage.secondary.value);
		secondaryBlockType = target->blockHit(attackerRef, damage.secondary.type, damage.secondary.value, false, false,
		                                      field, ignoreResistances, damage.origin);
		if (damage.secondary.type != COMBAT_HEALING) {
			damage.secondary.value = -damage.secondary.value;
			sendBlockEffect(secondaryBlockType, damage.secondary.type, target->getPosition());
		}
	} else {
		secondaryBlockType = BLOCK_NONE;
	}

	damage.blockType = primaryBlockType;

	return (primaryBlockType != BLOCK_NONE) && (secondaryBlockType != BLOCK_NONE);
}

void Game::combatGetTypeInfo(CombatType_t combatType, Creature* target, TextColor_t& color, uint8_t& effect)
{
	switch (combatType) {
		case COMBAT_PHYSICALDAMAGE: {
			std::shared_ptr<Item> splash;
			// Capture tile once — target may be removed between two getTile()
			// calls, leaving a created splash with nowhere to go (definite leak).
			Tile* targetTile = target->getTile();
			switch (target->getRace()) {
				case RACE_VENOM:
					color = TEXTCOLOR_LIGHTGREEN;
					effect = CONST_ME_HITBYPOISON;
					if (targetTile) {
						splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_SLIME);
					}
					break;
				case RACE_BLOOD:
					color = TEXTCOLOR_RED;
					effect = CONST_ME_DRAWBLOOD;
					if (targetTile && !targetTile->hasFlag(TILESTATE_PROTECTIONZONE)) {
						splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_BLOOD);
					}
					break;
				case RACE_UNDEAD:
					color = TEXTCOLOR_GREY;
					effect = CONST_ME_HITAREA;
					break;
				case RACE_FIRE:
					color = TEXTCOLOR_ORANGE;
					effect = CONST_ME_DRAWBLOOD;
					break;
				case RACE_ENERGY:
					color = TEXTCOLOR_PURPLE;
					effect = CONST_ME_ENERGYHIT;
					break;
				case RACE_INK:
					color = TEXTCOLOR_DARKGREY;
					effect = CONST_ME_BLACK_BLOOD;
					if (const Tile* tile = target->getTile()) {
						if (tile && !tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
							splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_INK);
						}
					}
					break;
				default:
					color = TEXTCOLOR_NONE;
					effect = CONST_ME_NONE;
					break;
			}

			if (splash) {
				splash->setInstanceID(target->getInstanceID());
				// targetTile is captured once and reused — same tile where the
				// PZ check was made; splash is only created when tile != nullptr.
				if (internalAddItem(targetTile, splash.get(), INDEX_WHEREEVER, FLAG_NOLIMIT) == RETURNVALUE_NOERROR) {
					splash->startDecaying();
				} else {
					ReleaseItem(splash.get());
				}
			}

			break;
		}

		case COMBAT_ENERGYDAMAGE: {
			color = TEXTCOLOR_PURPLE;
			effect = CONST_ME_ENERGYHIT;
			break;
		}

		case COMBAT_EARTHDAMAGE: {
			color = TEXTCOLOR_LIGHTGREEN;
			effect = CONST_ME_GREEN_RINGS;
			break;
		}

		case COMBAT_DROWNDAMAGE: {
			color = TEXTCOLOR_LIGHTBLUE;
			effect = CONST_ME_LOSEENERGY;
			break;
		}
		case COMBAT_FIREDAMAGE: {
			color = TEXTCOLOR_ORANGE;
			effect = CONST_ME_HITBYFIRE;
			break;
		}
		case COMBAT_ICEDAMAGE: {
			color = TEXTCOLOR_TEAL;
			effect = CONST_ME_ICEATTACK;
			break;
		}
		case COMBAT_HOLYDAMAGE: {
			color = TEXTCOLOR_YELLOW;
			effect = CONST_ME_HOLYDAMAGE;
			break;
		}
		case COMBAT_DEATHDAMAGE: {
			color = TEXTCOLOR_DARKRED;
			effect = CONST_ME_SMALLCLOUDS;
			break;
		}
		case COMBAT_AGONYDAMAGE: {
			color = TEXTCOLOR_DARKRED;
			effect = CONST_ME_AGONY;
			break;
		}
		case COMBAT_LIFEDRAIN: {
			color = TEXTCOLOR_RED;
			effect = CONST_ME_MAGIC_RED;
			break;
		}
		default: {
			color = TEXTCOLOR_NONE;
			effect = CONST_ME_NONE;
			break;
		}
	}
}

static int32_t increaseDamageByPercent(int32_t value, uint16_t percent)
{
	if (value <= 0 || percent == 0) {
		return value;
	}

	const int64_t result = static_cast<int64_t>(value) + (static_cast<int64_t>(value) * percent / 100);
	return static_cast<int32_t>(std::min<int64_t>(result, std::numeric_limits<int32_t>::max()));
}

static int32_t reduceDamageByPercent(int32_t value, uint16_t percent)
{
	if (value <= 0 || percent == 0) {
		return value;
	}

	if (percent >= 100) {
		return 0;
	}

	return value - static_cast<int32_t>(static_cast<int64_t>(value) * percent / 100);
}

static uint16_t getPreyDamageBoostPercent(const std::shared_ptr<Player>& player, const std::shared_ptr<Creature>& target)
{
	if (!player || !target) {
		return 0;
	}

	auto targetMonster = std::dynamic_pointer_cast<Monster>(target);
	if (!targetMonster || targetMonster->getMaster()) {
		return 0;
	}

	return player->getPreyDamageBoost(targetMonster->getName());
}

static uint16_t getPreyDamageReductionPercent(const std::shared_ptr<Player>& player, const std::shared_ptr<Creature>& attacker)
{
	if (!player || !attacker) {
		return 0;
	}

	auto attackerMonster = std::dynamic_pointer_cast<Monster>(attacker);
	if (!attackerMonster || attackerMonster->getMaster()) {
		return 0;
	}

	return player->getPreyDamageReduction(attackerMonster->getName());
}

static void applyPreyCombatBonuses(CombatDamage& damage, const std::shared_ptr<Creature>& attacker, const std::shared_ptr<Creature>& target)
{
	if (damage.preyApplied) {
		return;
	}
	damage.preyApplied = true;

	auto attackerPlayer = std::dynamic_pointer_cast<Player>(attacker);
	if (attackerPlayer) {
		const uint16_t boost = getPreyDamageBoostPercent(attackerPlayer, target);
		damage.primary.value = increaseDamageByPercent(damage.primary.value, boost);
		damage.secondary.value = increaseDamageByPercent(damage.secondary.value, boost);
		return;
	}

	auto targetPlayer = std::dynamic_pointer_cast<Player>(target);
	if (targetPlayer) {
		const uint16_t reduction = getPreyDamageReductionPercent(targetPlayer, attacker);
		damage.primary.value = reduceDamageByPercent(damage.primary.value, reduction);
		damage.secondary.value = reduceDamageByPercent(damage.secondary.value, reduction);
	}
}

void Game::applyResetSystemBonuses(CombatDamage& damage, Player* attackerPlayer, Player* targetPlayer)
{
	if (!ConfigManager::getBoolean(ConfigManager::RESET_SYSTEM_ENABLED)) {
		return;
	}

	const bool isHeal = (damage.primary.type == COMBAT_HEALING);

	// Damage bonus: Attacker is player, target is NOT a player (PvE only)
	if (!isHeal && attackerPlayer && !targetPlayer) {
		float dmgPct = attackerPlayer->getResetDamageBonus();
		if (damage.spellResetMultiplier >= 0.0f) {
			dmgPct = damage.spellResetMultiplier;
		}
		if (dmgPct > 0.0f) {
			damage.primary.value += static_cast<int32_t>(damage.primary.value * dmgPct / 100.0f);
			if (damage.secondary.value != 0) {
				damage.secondary.value += static_cast<int32_t>(damage.secondary.value * dmgPct / 100.0f);
			}
		}
	}

	// Defense bonus: Target is player, attacker is NOT a player (PvE only)
	if (!isHeal && targetPlayer && !attackerPlayer) {
		float defPct = targetPlayer->getResetDefenseBonus();
		if (defPct > 0.0f) {
			defPct = std::min(defPct, 90.0f);
			damage.primary.value -= static_cast<int32_t>(damage.primary.value * defPct / 100.0f);
			if (damage.secondary.value != 0) {
				damage.secondary.value -= static_cast<int32_t>(damage.secondary.value * defPct / 100.0f);
			}
		}
	}

	// Healing bonus: Target is player, any source
	if (isHeal && targetPlayer) {
		float healPct = targetPlayer->getResetHealingBonus();
		if (healPct > 0.0f) {
			damage.primary.value += static_cast<int32_t>(damage.primary.value * healPct / 100.0f);
		}
	}
}

bool Game::combatChangeHealth(Creature* attacker, Creature* target, CombatDamage& damage)
{
	if (!target || target->isDead() || target->isRemoved()) {
		return false;
	}

	if (attacker && !attacker->compareInstance(target->getInstanceID())) {
		return false;
	}

	auto targetRef = target->weak_from_this().lock();
	if (!targetRef) {
		return false;
	}

	std::shared_ptr<Creature> attackerRef;
	if (attacker) {
		attackerRef = attacker->weak_from_this().lock();
		if (!attackerRef) {
			return false;
		}
	}

	const Position& targetPos = target->getPosition();
	if (damage.primary.type == COMBAT_HEALING) {
		applyResetSystemBonuses(damage, attacker ? attacker->getPlayer() : nullptr, target->getPlayer());
		int32_t healAmount = damage.primary.value + damage.secondary.value;
		if (healAmount > 0) {
			int32_t realHeal = target->getHealth();
			target->gainHealth(attackerRef, healAmount);
			realHeal = target->getHealth() - realHeal;

			if (realHeal > 0) {
				SpectatorVec spectators;
				map.getSpectators(spectators, targetPos, false, true);
				InstanceUtils::filterByInstanceInPlace(spectators, target->getInstanceID());

				addAnimatedText(spectators, fmt::format("{:d}", realHeal), targetPos,
				                static_cast<TextColor_t>(getInteger(ConfigManager::HEALTH_GAIN_COLOUR)));

				Player* attackerPlayer = attacker ? attacker->getPlayer() : nullptr;
				Player* targetPlayer = target->getPlayer();
				for (const auto& spectator : spectators) {
					Player* tmpPlayer = static_cast<Player*>(spectator.get());
					if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
						tmpPlayer->sendTextMessage(MESSAGE_STATUS_DEFAULT, fmt::format("You heal {:s} for {:d} hitpoints.", target->getNameDescription(), realHeal));
					} else if (tmpPlayer == targetPlayer) {
						if (!attacker) {
							tmpPlayer->sendTextMessage(MESSAGE_STATUS_DEFAULT, fmt::format("You gained {:d} hitpoints.", realHeal));
						} else if (targetPlayer == attackerPlayer) {
							tmpPlayer->sendTextMessage(MESSAGE_STATUS_DEFAULT, fmt::format("You healed yourself for {:d} hitpoints.", realHeal));
						} else {
							tmpPlayer->sendTextMessage(MESSAGE_STATUS_DEFAULT, fmt::format("You were healed by {:s} for {:d} hitpoints.", attacker->getNameDescription(), realHeal));
						}
					}
				}
			}
		}

		// Fire onHealthChange creature events for healing
		const auto& healEvents = target->getCreatureEvents(CREATURE_EVENT_HEALTHCHANGE);
		if (!healEvents.empty()) {
			for (CreatureEvent* creatureEvent : healEvents) {
				creatureEvent->executeHealthChange(target, attacker, damage);
			}
		}

		return true;
	}

	if (damage.primary.value > 0) {

		Player* attackerPlayer;
		if (attacker) {
			attackerPlayer = attacker->getPlayer();
		} else {
			attackerPlayer = nullptr;
		}

		Player* targetPlayer = target->getPlayer();
		if (attackerPlayer && targetPlayer && attackerPlayer->getSkull() == SKULL_BLACK &&
		    attackerPlayer->getSkullClient(targetPlayer) == SKULL_NONE) {
			return false;
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto& events = target->getCreatureEvents(CREATURE_EVENT_HEALTHCHANGE);
			if (!events.empty()) {
				for (CreatureEvent* creatureEvent : events) {
					creatureEvent->executeHealthChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeHealth(attacker, target, damage);
			}
		}

		int32_t realHealthChange = target->getHealth();
		target->gainHealth(attackerRef, damage.primary.value);
		realHealthChange = target->getHealth() - realHealthChange;

		// rewardboss healing contribution
		if (target && target->getPlayer()) {
			for (const auto& [monsterId, rewardInfo] : g_game.rewardBossTracking) {
				Monster* monster = getMonsterByID(monsterId);
				if (monster && monster->isRewardBoss()) {
					const Position& playerPos = target->getPosition();
					const Position& monsterPos = monster->getPosition();
					double distBetweenTargetAndBoss = std::sqrt(std::pow(playerPos.x - monsterPos.x, 2) + std::pow(playerPos.y - monsterPos.y, 2));
					if (distBetweenTargetAndBoss < 7) {
						uint32_t playerGuid = target->getPlayer()->getGUID();
						rewardBossTracking[monsterId].playerScoreTable[playerGuid].damageTaken += realHealthChange * ConfigManager::getFloat(ConfigManager::REWARD_RATE_HEALING_DONE);
					}
				}
			}
		}

		if (realHealthChange > 0 && !target->isInGhostMode()) {
			auto damageString = getHitpointStatusString(realHealthChange);

			std::string spectatorMessage;

			TextMessage message;

			SpectatorVec spectators;
			map.getSpectators(spectators, targetPos, false, true);
			InstanceUtils::filterByInstanceInPlace(spectators, target->getInstanceID());

			addAnimatedText(spectators, fmt::format("{:d}", realHealthChange), targetPos,
                      		static_cast<TextColor_t>(getInteger(ConfigManager::HEALTH_GAIN_COLOUR)));
			for (const auto& spectator : spectators) {
				Player* tmpPlayer = static_cast<Player*>(spectator.get());
				if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
					message.type = MESSAGE_STATUS_DEFAULT;
					message.text = fmt::format("You heal {:s} for {:s}.", target->getNameDescription(), damageString);
				} else if (tmpPlayer == targetPlayer) {
					message.type = MESSAGE_STATUS_DEFAULT;
					if (!attacker) {
						message.text = fmt::format("You were healed for {:s}.", damageString);
					} else if (targetPlayer == attackerPlayer) {
						message.text = fmt::format("You healed yourself for {:s}.", damageString);
					} else {
						message.text = fmt::format("You were healed by {:s} for {:s}.", attacker->getNameDescription(),
						                           damageString);
					}
				} else {
					message.type = MESSAGE_STATUS_DEFAULT;
					if (spectatorMessage.empty()) {
						if (!attacker) {
							spectatorMessage =
							    fmt::format("{:s} was healed for {:s}.", target->getNameDescription(), damageString);
						} else if (attacker == target) {
							spectatorMessage = fmt::format(
							    "{:s} healed {:s}self for {:s}.", attacker->getNameDescription(),
							    targetPlayer ? (targetPlayer->getSex() == PLAYERSEX_FEMALE ? "her" : "him") : "it",
							    damageString);
						} else {
							spectatorMessage = fmt::format("{:s} healed {:s} for {:s}.", attacker->getNameDescription(),
							                               target->getNameDescription(), damageString);
						}
						spectatorMessage[0] = static_cast<char>(std::toupper(spectatorMessage[0]));
					}
					message.type = MESSAGE_STATUS_DEFAULT;
				}
				tmpPlayer->sendTextMessage(message);
			}
		}
	} else if (damage.primary.type != COMBAT_HEALING) {
		if (!target->isAttackable()) {
			if (!target->isInGhostMode()) {
				InstanceUtils::sendMagicEffectToInstance(targetPos, target->getInstanceID(), CONST_ME_POFF);
			}
			return true;
		}

		Player* attackerPlayer;
		if (attacker) {
			attackerPlayer = attacker->getPlayer();
		} else {
			attackerPlayer = nullptr;
		}

		Player* targetPlayer = target->getPlayer();
		if (attackerPlayer && targetPlayer && attackerPlayer->getSkull() == SKULL_BLACK &&
		    attackerPlayer->getSkullClient(targetPlayer) == SKULL_NONE) {
			return false;
		}

		if (ConfigManager::getBoolean(ConfigManager::MONSTER_LEVEL_ENABLED)) {
			Monster* monster = attacker ? attacker->getMonster() : nullptr;
			if (monster && monster->getLevel() > 0) {
				float bonusDmg = monster_level::getBonusDamage() * monster->getLevel();
				if (bonusDmg != 0.0f) {
					damage.primary.value += static_cast<int32_t>(std::round(damage.primary.value * bonusDmg));
					damage.secondary.value += static_cast<int32_t>(std::round(damage.secondary.value * bonusDmg));
				}
			}
		}

		damage.primary.value = std::abs(damage.primary.value);
		damage.secondary.value = std::abs(damage.secondary.value);

		// Reset system: apply damage/defense bonuses (PvE only)
		applyResetSystemBonuses(damage, attackerPlayer, targetPlayer);

		if (targetPlayer && targetPlayer->isAvatarActive()) {
			damage.primary.value -= static_cast<int32_t>(std::ceil(damage.primary.value * AVATAR_DAMAGE_REDUCTION_PERCENT / 100.0));
			damage.secondary.value -= static_cast<int32_t>(std::ceil(damage.secondary.value * AVATAR_DAMAGE_REDUCTION_PERCENT / 100.0));
		}

		applyPreyCombatBonuses(damage, attackerRef, targetRef);

		int32_t healthChange = damage.primary.value + damage.secondary.value;
		if (healthChange == 0) {
			return true;
		}

		TextMessage message;

		SpectatorVec spectators;
		if (targetPlayer && target->hasCondition(CONDITION_MANASHIELD) &&
		    damage.primary.type != COMBAT_UNDEFINEDDAMAGE) {
			int32_t manaDamage = std::min<int32_t>(targetPlayer->getMana(), healthChange);
			if (manaDamage != 0) {
				if (damage.origin != ORIGIN_NONE) {
					const auto& events = target->getCreatureEvents(CREATURE_EVENT_MANACHANGE);
					if (!events.empty()) {
						for (CreatureEvent* creatureEvent : events) {
							creatureEvent->executeManaChange(target, attacker, damage);
						}
						healthChange = damage.primary.value + damage.secondary.value;
						if (healthChange == 0) {
							return true;
						}
						manaDamage = std::min<int32_t>(targetPlayer->getMana(), healthChange);
					}
				}

				targetPlayer->drainMana(attackerRef, manaDamage);

				map.getSpectators(spectators, targetPos, true, true);
				InstanceUtils::filterByInstanceInPlace(spectators, target->getInstanceID());
				addMagicEffect(spectators, targetPos, CONST_ME_LOSEENERGY);

				std::string spectatorMessage;

				addAnimatedText(spectators, fmt::format("{:+d}", -manaDamage), targetPos,
				                static_cast<TextColor_t>(getInteger(ConfigManager::MANA_LOSS_COLOUR)));

				const uint16_t preyBoost = getPreyDamageBoostPercent(std::dynamic_pointer_cast<Player>(attackerRef), targetRef);
				const uint16_t preyReduction = getPreyDamageReductionPercent(std::dynamic_pointer_cast<Player>(targetRef), attackerRef);

				for (const auto& spectator : spectators) {
					Player* tmpPlayer = static_cast<Player*>(spectator.get());
					if (tmpPlayer->getPosition().z != targetPos.z) {
						continue;
					}

					if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
						message.type = MESSAGE_STATUS_DEFAULT;
						message.text = fmt::format("{:s} loses {:d} mana due to your attack.",
						                           target->getNameDescription(), manaDamage);
						message.text[0] = static_cast<char>(std::toupper(message.text[0]));
						if (preyBoost > 0) {
							message.text += fmt::format(" (Prey Damage Boost +{:d}%)", preyBoost);
						}
					} else if (tmpPlayer == targetPlayer) {
						message.type = MESSAGE_STATUS_DEFAULT;
						if (!attacker) {
							message.text = fmt::format("You lose {:d} mana.", manaDamage);
						} else if (targetPlayer == attackerPlayer) {
							message.text = fmt::format("You lose {:d} mana due to your own attack.", manaDamage);
						} else {
							message.text = fmt::format("You lose {:d} mana due to an attack by {:s}.", manaDamage,
							                           attacker->getNameDescription());
							if (preyReduction > 0) {
								message.text += fmt::format(" (Prey Damage Reduction -{:d}%)", preyReduction);
							}
						}
					} else {
						message.type = MESSAGE_STATUS_DEFAULT;
						if (spectatorMessage.empty()) {
							if (!attacker) {
								spectatorMessage =
								    fmt::format("{:s} loses {:d} mana.", target->getNameDescription(), manaDamage);
							} else if (attacker == target) {
								spectatorMessage = fmt::format(
								    "{:s} loses {:d} mana due to {:s} own attack.", target->getNameDescription(),
								    manaDamage, targetPlayer->getSex() == PLAYERSEX_FEMALE ? "her" : "his");
							} else {
								spectatorMessage = fmt::format("{:s} loses {:d} mana due to an attack by {:s}.",
								                               target->getNameDescription(), manaDamage,
								                               attacker->getNameDescription());
							}
							spectatorMessage[0] = static_cast<char>(std::toupper(spectatorMessage[0]));
						}
					}
					tmpPlayer->sendTextMessage(message);
				}

				damage.primary.value -= manaDamage;
				if (damage.primary.value < 0) {
					damage.secondary.value = std::max<int32_t>(0, damage.secondary.value + damage.primary.value);
					damage.primary.value = 0;
				}
			}
		}

		int32_t realDamage = damage.primary.value + damage.secondary.value;
		if (realDamage == 0) {
			return true;
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto& events = target->getCreatureEvents(CREATURE_EVENT_HEALTHCHANGE);
			if (!events.empty()) {
				for (CreatureEvent* creatureEvent : events) {
					creatureEvent->executeHealthChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeHealth(attacker, target, damage);
			}
		}

		int32_t targetHealth = target->getHealth();
		if (damage.primary.value >= targetHealth) {
			damage.primary.value = targetHealth;
			damage.secondary.value = 0;
		} else if (damage.secondary.value) {
			damage.secondary.value = std::min<int32_t>(damage.secondary.value, targetHealth - damage.primary.value);
		}

		realDamage = damage.primary.value + damage.secondary.value;
		if (realDamage == 0) {
			return true;
		}

		if (spectators.empty()) {
			map.getSpectators(spectators, targetPos, true, true);
			InstanceUtils::filterByInstanceInPlace(spectators, target->getInstanceID());
		}

		message.primary.value = damage.primary.value;
		message.secondary.value = damage.secondary.value;
		uint8_t hitEffect;
		if (message.primary.value) {
			combatGetTypeInfo(damage.primary.type, target, message.primary.color, hitEffect);
			if (hitEffect != CONST_ME_NONE) {
				addMagicEffect(spectators, targetPos, hitEffect);
			}

			if (message.primary.color != TEXTCOLOR_NONE) {
				addAnimatedText(spectators, getDamageAnimatedText(message.primary.value), targetPos, message.primary.color);
			}
		}

		if (message.secondary.value) {
			combatGetTypeInfo(damage.secondary.type, target, message.secondary.color, hitEffect);
			if (hitEffect != CONST_ME_NONE) {
				addMagicEffect(spectators, targetPos, hitEffect);
			}

			if (message.secondary.color != TEXTCOLOR_NONE) {
				addAnimatedText(spectators, getDamageAnimatedText(message.secondary.value), targetPos,
				                message.secondary.color);
			}
		}

		if (message.primary.color != TEXTCOLOR_NONE || message.secondary.color != TEXTCOLOR_NONE) {
			auto damageString = getHitpointStatusString(realDamage);

			std::string spectatorMessage;
			const uint16_t preyBoost = getPreyDamageBoostPercent(std::dynamic_pointer_cast<Player>(attackerRef), targetRef);
			const uint16_t preyReduction = getPreyDamageReductionPercent(std::dynamic_pointer_cast<Player>(targetRef), attackerRef);

			for (const auto& spectator : spectators) {
				Player* tmpPlayer = static_cast<Player*>(spectator.get());
				if (tmpPlayer->getPosition().z != targetPos.z) {
					continue;
				}

				if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
					message.type = MESSAGE_STATUS_DEFAULT;
					message.text =
					    fmt::format("{:s} loses {:s} due to your attack.", target->getNameDescription(), damageString);
					message.text[0] = static_cast<char>(std::toupper(message.text[0]));
					if (preyBoost > 0) {
						message.text += fmt::format(" (Prey Damage Boost +{:d}%)", preyBoost);
					}
				} else if (tmpPlayer == targetPlayer) {
					message.type = MESSAGE_STATUS_DEFAULT;
					if (!attacker) {
						message.text = fmt::format("You lose {:s}.", damageString);
					} else if (targetPlayer == attackerPlayer) {
						message.text = fmt::format("You lose {:s} due to your own attack.", damageString);
					} else {
						message.text = fmt::format("You lose {:s} due to an attack by {:s}.", damageString,
						                           attacker->getNameDescription());
						if (preyReduction > 0) {
							message.text += fmt::format(" (Prey Damage Reduction -{:d}%)", preyReduction);
						}
					}
				} else {
					message.type = MESSAGE_STATUS_DEFAULT;
					if (spectatorMessage.empty()) {
						if (!attacker) {
							spectatorMessage =
							    fmt::format("{:s} loses {:s}.", target->getNameDescription(), damageString);
						} else if (attacker == target) {
							spectatorMessage = fmt::format(
							    "{:s} loses {:s} due to {:s} own attack.", target->getNameDescription(), damageString,
							    targetPlayer ? (targetPlayer->getSex() == PLAYERSEX_FEMALE ? "her" : "his") : "its");
						} else {
							spectatorMessage =
							    fmt::format("{:s} loses {:s} due to an attack by {:s}.", target->getNameDescription(),
							                damageString, attacker->getNameDescription());
						}
						spectatorMessage[0] = static_cast<char>(std::toupper(spectatorMessage[0]));
					}
					message.text = spectatorMessage;
				}
				tmpPlayer->sendTextMessage(message);
			}
		}

			// rewardboss player attacking boss
			if (target && target->getMonster() && target->getMonster()->isRewardBoss()) {
				uint32_t monsterId = target->getMonster()->getID();
				if (!rewardBossTracking.contains(monsterId)) {
					rewardBossTracking[monsterId] = RewardBossContributionInfo();
				}
				if (attacker && attacker->getPlayer()) {
					uint32_t playerGuid = attacker->getPlayer()->getGUID();
					rewardBossTracking[monsterId].playerScoreTable[playerGuid].damageDone += realDamage * ConfigManager::getFloat(ConfigManager::REWARD_RATE_DAMAGE_DONE);
				}
			}
			// rewardboss boss attacking player
			if (attacker && attacker->getMonster() && attacker->getMonster()->isRewardBoss()) {
				uint32_t monsterId = attacker->getMonster()->getID();
				if (!rewardBossTracking.contains(monsterId)) {
					rewardBossTracking[monsterId] = RewardBossContributionInfo();
				}
				if (target->getPlayer()) {
					uint32_t playerGuid = target->getPlayer()->getGUID();
					rewardBossTracking[monsterId].playerScoreTable[playerGuid].damageTaken += realDamage * ConfigManager::getFloat(ConfigManager::REWARD_RATE_DAMAGE_TAKEN);
				}
			}


		if (realDamage >= targetHealth) {
			for (CreatureEvent* creatureEvent : target->getCreatureEvents(CREATURE_EVENT_PREPAREDEATH)) {
				if (!creatureEvent->executeOnPrepareDeath(target, attacker)) {
					return false;
				}
			}
		}

		target->drainHealth(attackerRef, realDamage);
		addCreatureHealth(spectators, target);

		// onPlayerAttack callback
		if (attackerPlayer) {
			if (Monster* targetMonster = target->getMonster()) {
				targetMonster->callPlayerAttackEvent(attackerPlayer);
			}
		}
	}

	return true;
}

bool Game::combatChangeMana(Creature* attacker, Creature* target, CombatDamage& damage)
{
	Player* targetPlayer = target->getPlayer();
	if (!targetPlayer) {
		return true;
	}

	if (attacker && !attacker->compareInstance(target->getInstanceID())) {
		return false;
	}

	std::shared_ptr<Creature> attackerRef;
	if (attacker) {
		attackerRef = attacker->weak_from_this().lock();
		if (!attackerRef) {
			LOG_ERROR(fmt::format("[Game::combatChangeMana] Failed to lock attacker shared reference: {}",
			                      static_cast<const void*>(attacker)));
			return false;
		}
	}

	if (ConfigManager::getBoolean(ConfigManager::MONSTER_LEVEL_ENABLED)) {
		Monster* monster = attacker ? attacker->getMonster() : nullptr;
		if (monster && monster->getLevel() > 0) {
			float bonusDmg = monster_level::getBonusDamage() * monster->getLevel();
			if (bonusDmg != 0.0f) {
				if (damage.primary.value < 0) {
					damage.primary.value += static_cast<int32_t>(std::round(damage.primary.value * bonusDmg));
				}
				if (damage.secondary.value < 0) {
					damage.secondary.value += static_cast<int32_t>(std::round(damage.secondary.value * bonusDmg));
				}
			}
		}
	}

	int32_t manaChange = damage.primary.value + damage.secondary.value;
	if (manaChange > 0) {
		if (attacker) {
			const Player* attackerPlayer = attacker->getPlayer();
			if (attackerPlayer && attackerPlayer->getSkull() == SKULL_BLACK &&
			    attackerPlayer->getSkullClient(target) == SKULL_NONE) {
				return false;
			}
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto& events = target->getCreatureEvents(CREATURE_EVENT_MANACHANGE);
			if (!events.empty()) {
				for (CreatureEvent* creatureEvent : events) {
					creatureEvent->executeManaChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeMana(attacker, target, damage);
			}
		}

		// Reset system: mana bonuses (healing bonus intentionally excluded from mana)
		if (targetPlayer && ConfigManager::getBoolean(ConfigManager::RESET_SYSTEM_ENABLED)) {
			if (attacker) {
				float spellPct = targetPlayer->getResetManaSpellBonus();
				if (spellPct > 0.0f) {
					manaChange += static_cast<int32_t>(manaChange * spellPct / 100.0f);
				}
			} else {
				float potionPct = targetPlayer->getResetManaPotionBonus();
				if (potionPct > 0.0f) {
					manaChange += static_cast<int32_t>(manaChange * potionPct / 100.0f);
				}
			}
		}

		int32_t realManaChange = targetPlayer->getMana();
		targetPlayer->changeMana(manaChange);
		realManaChange = targetPlayer->getMana() - realManaChange;

		if (realManaChange > 0) {
			SpectatorVec spectators;
			map.getSpectators(spectators, target->getPosition(), false, true);
			InstanceUtils::filterByInstanceInPlace(spectators, target->getInstanceID());

			addAnimatedText(spectators, fmt::format("{:d}", realManaChange), target->getPosition(),
			                static_cast<TextColor_t>(getInteger(ConfigManager::MANA_GAIN_COLOUR)));

			Player* attackerPlayer = attacker ? attacker->getPlayer() : nullptr;
			for (const auto& spectator : spectators) {
				Player* tmpPlayer = static_cast<Player*>(spectator.get());
				if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
					tmpPlayer->sendTextMessage(MESSAGE_STATUS_DEFAULT, fmt::format("You heal {:s} for {:d} mana.", target->getNameDescription(), realManaChange));
				} else if (tmpPlayer == targetPlayer) {
					if (!attacker) {
						tmpPlayer->sendTextMessage(MESSAGE_STATUS_DEFAULT, fmt::format("You gained {:d} mana.", realManaChange));
					} else if (targetPlayer == attackerPlayer) {
						tmpPlayer->sendTextMessage(MESSAGE_STATUS_DEFAULT, fmt::format("You healed yourself for {:d} mana.", realManaChange));
					} else {
						tmpPlayer->sendTextMessage(MESSAGE_STATUS_DEFAULT, fmt::format("You were healed by {:s} for {:d} mana.", attacker->getNameDescription(), realManaChange));
					}
				}
			}
		}
	} else {
		const Position& targetPos = target->getPosition();
		if (!target->isAttackable()) {
			if (!target->isInGhostMode()) {
				InstanceUtils::sendMagicEffectToInstance(targetPos, target->getInstanceID(), CONST_ME_POFF);
			}
			return false;
		}

		Player* attackerPlayer;
		if (attacker) {
			attackerPlayer = attacker->getPlayer();
		} else {
			attackerPlayer = nullptr;
		}

		if (attackerPlayer && attackerPlayer->getSkull() == SKULL_BLACK &&
		    attackerPlayer->getSkullClient(targetPlayer) == SKULL_NONE) {
			return false;
		}

		int32_t manaLoss = std::min<int32_t>(targetPlayer->getMana(), -manaChange);
		BlockType_t blockType = target->blockHit(attackerRef, COMBAT_MANADRAIN, manaLoss);
		if (blockType != BLOCK_NONE) {
			InstanceUtils::sendMagicEffectToInstance(targetPos, target->getInstanceID(), CONST_ME_POFF);
			return false;
		}

		if (manaLoss <= 0) {
			return true;
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto& events = target->getCreatureEvents(CREATURE_EVENT_MANACHANGE);
			if (!events.empty()) {
				for (CreatureEvent* creatureEvent : events) {
					creatureEvent->executeManaChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeMana(attacker, target, damage);
			}
		}

		targetPlayer->drainMana(attackerRef, manaLoss);

		std::string spectatorMessage;

		TextMessage message;

		SpectatorVec spectators;
		map.getSpectators(spectators, targetPos, false, true);
		InstanceUtils::filterByInstanceInPlace(spectators, target->getInstanceID());

		addAnimatedText(spectators, fmt::format("{:+d}", manaLoss), targetPos,
				static_cast<TextColor_t>(getInteger(ConfigManager::MANA_LOSS_COLOUR)));

		for (const auto& spectator : spectators) {
			Player* tmpPlayer = static_cast<Player*>(spectator.get());
			if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
				message.type = MESSAGE_STATUS_DEFAULT;
				message.text =
				    fmt::format("{:s} loses {:d} mana due to your attack.", target->getNameDescription(), manaLoss);
				message.text[0] = static_cast<char>(std::toupper(message.text[0]));
			} else if (tmpPlayer == targetPlayer) {
				message.type = MESSAGE_STATUS_DEFAULT;
				if (!attacker) {
					message.text = fmt::format("You lose {:d} mana.", manaLoss);
				} else if (targetPlayer == attackerPlayer) {
					message.text = fmt::format("You lose {:d} mana due to your own attack.", manaLoss);
				} else {
					message.text = fmt::format("You lose {:d} mana due to an attack by {:s}.", manaLoss,
					                           attacker->getNameDescription());
				}
			} else {
				message.type = MESSAGE_STATUS_DEFAULT;
				if (spectatorMessage.empty()) {
					if (!attacker) {
						spectatorMessage = fmt::format("{:s} loses {:d} mana.", target->getNameDescription(), manaLoss);
					} else if (attacker == target) {
						spectatorMessage =
						    fmt::format("{:s} loses {:d} mana due to {:s} own attack.", target->getNameDescription(),
						                manaLoss, targetPlayer->getSex() == PLAYERSEX_FEMALE ? "her" : "his");
					} else {
						spectatorMessage =
						    fmt::format("{:s} loses {:d} mana due to an attack by {:s}.", target->getNameDescription(),
						                manaLoss, attacker->getNameDescription());
					}
					spectatorMessage[0] = static_cast<char>(std::toupper(spectatorMessage[0]));
				}
			}
			tmpPlayer->sendTextMessage(message);
		}
	}

	return true;
}

void Game::addCreatureHealth(const Creature* target)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, target->getPosition(), true, true);
	InstanceUtils::filterByInstanceInPlace(spectators, target->getInstanceID());
	addCreatureHealth(spectators, target);
}

void Game::addCreatureHealth(const SpectatorVec& spectators, const Creature* target)
{
	for (const auto& spectator : spectators) {
		Player* player = spectator ? spectator->getPlayer() : nullptr;
		if (!player) {
			continue;
		}
		player->sendCreatureHealth(target);
	}
}

void Game::addAnimatedText(std::string_view message, const Position& pos, TextColor_t color, uint32_t instanceId)
{
	if (message.empty()) {
		return;
	}

	SpectatorVec spectators;
	map.getSpectators(spectators, pos, true, true);
	InstanceUtils::filterByInstanceInPlace(spectators, instanceId);
	addAnimatedText(spectators, message, pos, color);
}

void Game::addAnimatedText(const SpectatorVec& spectators, std::string_view message, const Position& pos,
                           TextColor_t color)
{
	for (const auto& spectator : spectators) {
		Player* player = spectator ? spectator->getPlayer() : nullptr;
		if (!player) {
			continue;
		}
		player->sendAnimatedText(message, pos, color);
	}
}

void Game::addMagicEffect(const Position& pos, uint16_t effect, uint32_t instanceId)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, pos, true, true);
	InstanceUtils::filterByInstanceInPlace(spectators, instanceId);
	addMagicEffect(spectators, pos, effect);
}

void Game::addMagicEffect(const SpectatorVec& spectators, const Position& pos, uint16_t effect)
{
	for (const auto& spectator : spectators) {
		Player* player = spectator ? spectator->getPlayer() : nullptr;
		if (!player) {
			continue;
		}
		player->sendMagicEffect(pos, effect);
	}
}

void InstanceUtils::sendMagicEffectToInstance(const Position &pos, uint32_t instanceId, uint8_t effect)
{
	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, pos, true, true);
	sendMagicEffectToInstance(spectators, pos, effect, instanceId);
}

void Game::addDistanceEffect(const Position& fromPos, const Position& toPos, uint16_t effect, uint32_t instanceId)
{
	SpectatorVec spectators, toPosSpectators;
	map.getSpectators(spectators, fromPos, true, true);
	map.getSpectators(toPosSpectators, toPos, true, true);
	spectators.addSpectators(toPosSpectators);

	InstanceUtils::filterByInstanceInPlace(spectators, instanceId);
	addDistanceEffect(spectators, fromPos, toPos, effect);
}

void Game::addDistanceEffect(const SpectatorVec& spectators, const Position& fromPos, const Position& toPos,
                             uint16_t effect)
{
	for (const auto& spectator : spectators) {
		Player* player = spectator ? spectator->getPlayer() : nullptr;
		if (!player) {
			continue;
		}
		player->sendDistanceShoot(fromPos, toPos, effect);
	}
}

void Game::setAccountStorageValue(const uint32_t accountId, const uint32_t key, const int32_t value)
{
	if (value == -1) {
		accountStorageMap[accountId].erase(key);
		return;
	}

	accountStorageMap[accountId][key] = value;
}

int32_t Game::getAccountStorageValue(const uint32_t accountId, const uint32_t key) const
{
	const auto& accountMapIt = accountStorageMap.find(accountId);
	if (accountMapIt != accountStorageMap.end()) {
		const auto& storageMapIt = accountMapIt->second.find(key);
		if (storageMapIt != accountMapIt->second.end()) {
			return storageMapIt->second;
		}
	}
	return -1;
}

void Game::loadAccountStorageValues()
{
	Database& db = Database::getInstance();

	DBResult_ptr result;
	if ((result = db.storeQuery("SELECT `account_id`, `key`, `value` FROM `account_storage`"))) {
		do {
			g_game.setAccountStorageValue(result->getNumber<uint32_t>("account_id"), result->getNumber<uint32_t>("key"),
			                              result->getNumber<int32_t>("value"));
		} while (result->next());
	}
}

bool Game::saveAccountStorageValues() const
{
	DBTransaction transaction;
	Database& db = Database::getInstance();

	if (!transaction.begin()) {
		return false;
	}

	if (!db.executeQuery("DELETE FROM `account_storage`")) {
		return false;
	}

	for (const auto& accountIt : g_game.accountStorageMap) {
		if (accountIt.second.empty()) {
			break;
		}

		DBInsert accountStorageQuery("INSERT INTO `account_storage` (`account_id`, `key`, `value`) VALUES");
		for (const auto& storageIt : accountIt.second) {
			if (!accountStorageQuery.addRow(
			        fmt::format("{:d}, {:d}, {:d}", accountIt.first, storageIt.first, storageIt.second))) {
				return false;
			}
		}

		if (!accountStorageQuery.execute()) {
			return false;
		}
	}

	return transaction.commit();
}

void Game::startDecay(Item* item)
{
	if (!item) [[unlikely]] {
		return;
	}

	ItemDecayState_t decayState = item->getDecaying();
	if (decayState == DECAYING_STOPPING || (!item->canDecay() && decayState == DECAYING_TRUE)) {
		stopDecay(item);
		return;
	}

	if (!item->canDecay() || decayState == DECAYING_TRUE) {
		return;
	}

	int32_t duration = item->getIntAttr(ITEM_ATTRIBUTE_DURATION);
	auto itemRef = getItemSharedRef(item);
	if (!itemRef) {
		return;
	}

	if (duration > 0) {
		g_decay.startDecay(std::move(itemRef), duration);
	} else {
		internalDecayItem(std::move(itemRef));
	}
}

void Game::startDecay(std::shared_ptr<Item> item)
{
	if (!item) [[unlikely]] {
		return;
	}

	ItemDecayState_t decayState = item->getDecaying();
	if (decayState == DECAYING_STOPPING || (!item->canDecay() && decayState == DECAYING_TRUE)) {
		stopDecay(item.get());
		return;
	}

	if (!item->canDecay() || decayState == DECAYING_TRUE) {
		return;
	}

	int32_t duration = item->getIntAttr(ITEM_ATTRIBUTE_DURATION);
	if (duration > 0) {
		g_decay.startDecay(std::move(item), duration);
	} else {
		internalDecayItem(std::move(item));
	}
}

void Game::stopDecay(Item* item)
{
	if (!item) [[unlikely]] {
		return;
	}

	if (item->hasAttribute(ITEM_ATTRIBUTE_DECAYSTATE)) {
		if (item->hasAttribute(ITEM_ATTRIBUTE_DURATION_TIMESTAMP)) {
			g_decay.stopDecay(item->weak_from_this(), item->getIntAttr(ITEM_ATTRIBUTE_DURATION_TIMESTAMP));
			item->removeAttribute(ITEM_ATTRIBUTE_DURATION_TIMESTAMP);
		} else {
			item->removeAttribute(ITEM_ATTRIBUTE_DECAYSTATE);
		}
	}
}

void Game::stopDecay(const std::shared_ptr<Item>& item)
{
	stopDecay(item.get());
}

void Game::internalDecayItem(std::shared_ptr<Item> item)
{
	if (!item) [[unlikely]] {
		return;
	}

	Item* itemPtr = item.get();
	const ItemType& it = Item::items[itemPtr->getID()];
	const int32_t decayTo = it.decayTo;
	if (decayTo > 0) {
		transformItem(itemPtr, decayTo);
	} else {
		ReturnValue ret = internalRemoveItem(itemPtr);
		if (ret != RETURNVALUE_NOERROR) {
			LOG_ERROR(fmt::format(
				"[Debug - Game::internalDecayItem] internalDecayItem failed, error code: {}, item id: {}",
				static_cast<uint32_t>(ret), itemPtr->getID()
			));
		}
	}
}

// ============================================================
// Loot Highlight System
// ============================================================

// Called once after loot is dropped into a corpse container.
// ownerPlayerId: the player who has exclusive rights to open the corpse.
// The highlight pulses every 2 seconds.
// Phase 1 (0-10s): effect visible only to owner.
// Phase 2 (10s+):  effect visible to everyone until corpse is opened/decayed.
void Game::startLootHighlight(Container* corpse, uint32_t ownerPlayerId)
{
	if (!corpse || corpse->empty()) {
		return;
	}

	// Send the first effect immediately to owner and party
	auto ownerRef = getPlayerByID(ownerPlayerId);
	Player* owner = ownerRef.get();
	if (owner && InstanceUtils::isPlayerInSameInstance(owner, corpse->getInstanceID())) {
		owner->sendMagicEffect(corpse->getPosition(), CONST_ME_LOOT_HIGHLIGHT);
		if (Party* party = owner->getParty()) {
			if (auto leader = party->getLeader()) {
				if (leader.get() != owner && InstanceUtils::isPlayerInSameInstance(leader.get(), corpse->getInstanceID())) {
					leader->sendMagicEffect(corpse->getPosition(), CONST_ME_LOOT_HIGHLIGHT);
				}
			}
			for (const auto& memberRef : party->getMembers()) {
				if (auto member = memberRef.lock()) {
					if (member.get() != owner && InstanceUtils::isPlayerInSameInstance(member.get(), corpse->getInstanceID())) {
						member->sendMagicEffect(corpse->getPosition(), CONST_ME_LOOT_HIGHLIGHT);
					}
				}
			}
		}
	}

	auto corpseItem = corpse->weak_from_this().lock();
	if (!corpseItem) {
		return;
	}

	std::weak_ptr<Item> weakCorpse = corpseItem;
	auto scheduledEventId = std::make_shared<uint32_t>(0);
	cleanupExpiredLootHighlightEvents();

	// Schedule the first repeating tick
	uint32_t eventId = g_scheduler.addEvent(createSchedulerTask(
	    LOOT_HIGHLIGHT_PULSE_MS,
	    ([this, weakCorpse, scheduledEventId, ownerPlayerId,
	      ownerTicksLeft = LOOT_HIGHLIGHT_OWNER_MS - static_cast<int32_t>(LOOT_HIGHLIGHT_PULSE_MS),
	      totalTicksLeft = LOOT_HIGHLIGHT_MAX_DURATION_MS - static_cast<int32_t>(LOOT_HIGHLIGHT_PULSE_MS)]() {
		    auto corpseItem = weakCorpse.lock();
		    if (!corpseItem) {
			    eraseLootHighlightEvent(weakCorpse, *scheduledEventId);
			    return;
		    }

		    checkLootHighlight(corpseItem, ownerPlayerId, ownerTicksLeft, totalTicksLeft, *scheduledEventId);
	    })));

	*scheduledEventId = eventId;
	lootHighlightEvents[weakCorpse] = eventId;
}

void Game::checkLootHighlight(std::shared_ptr<Item> corpseItem, uint32_t ownerPlayerId, int32_t ownerTicksLeft,
                              int32_t totalTicksLeft, uint32_t eventId)
{
	if (!corpseItem) {
		return;
	}

	Container* corpse = corpseItem->getContainer();
	if (!corpse) {
		return;
	}

	std::weak_ptr<Item> weakCorpse = corpseItem;

	// Remove entry first
	auto it = lootHighlightEvents.find(weakCorpse);
	if (it == lootHighlightEvents.end() || it->second != eventId) {
		return;
	}
	lootHighlightEvents.erase(it);

	// Validate stop conditions
	Tile* tile = corpse->getTile();
	if (!tile || corpse->isRemoved() || corpse->empty() || totalTicksLeft < 0) {
		return; // Stop permanently
	}

	const Position& pos = corpse->getPosition();

	if (ownerTicksLeft > 0) {
		// Phase 1 — Owner and Party
		auto ownerRef = getPlayerByID(ownerPlayerId);
		Player* owner = ownerRef.get();
		if (owner && InstanceUtils::isPlayerInSameInstance(owner, corpse->getInstanceID())) {
			owner->sendMagicEffect(pos, CONST_ME_LOOT_HIGHLIGHT);
			if (Party* party = owner->getParty()) {
				if (auto leader = party->getLeader()) {
					if (leader.get() != owner && InstanceUtils::isPlayerInSameInstance(leader.get(), corpse->getInstanceID())) {
						leader->sendMagicEffect(pos, CONST_ME_LOOT_HIGHLIGHT);
					}
				}
				for (const auto& memberRef : party->getMembers()) {
					if (auto member = memberRef.lock()) {
						if (member.get() != owner && InstanceUtils::isPlayerInSameInstance(member.get(), corpse->getInstanceID())) {
							member->sendMagicEffect(pos, CONST_ME_LOOT_HIGHLIGHT);
						}
					}
				}
			}
		}
	} else {
		// Phase 2 — Public
		SpectatorVec spectators;
		map.getSpectators(spectators, pos, false, true);
		for (const auto& spec : spectators) {
			if (Player* p = spec->getPlayer()) {
				if (!InstanceUtils::isPlayerInSameInstance(p, corpse->getInstanceID())) {
					continue;
				}

				p->sendMagicEffect(pos, CONST_ME_LOOT_HIGHLIGHT);
			}
		}
	}

	// Reschedule with decreased timers
	auto scheduledEventId = std::make_shared<uint32_t>(0);
	uint32_t newEventId = g_scheduler.addEvent(createSchedulerTask(
	    LOOT_HIGHLIGHT_PULSE_MS,
	    ([this, weakCorpse, scheduledEventId, ownerPlayerId,
	      nextOwnerTicks = ownerTicksLeft - static_cast<int32_t>(LOOT_HIGHLIGHT_PULSE_MS),
	      nextTotalTicks = totalTicksLeft - static_cast<int32_t>(LOOT_HIGHLIGHT_PULSE_MS)]() {
		    auto corpseItem = weakCorpse.lock();
		    if (!corpseItem) {
			    eraseLootHighlightEvent(weakCorpse, *scheduledEventId);
			    return;
		    }

		    checkLootHighlight(corpseItem, ownerPlayerId, nextOwnerTicks, nextTotalTicks, *scheduledEventId);
	    })));

	*scheduledEventId = newEventId;
	lootHighlightEvents[weakCorpse] = newEventId;
}

void Game::stopLootHighlight(Container* corpse)
{
	if (!corpse) {
		return;
	}

	auto corpseItem = corpse->weak_from_this().lock();
	if (!corpseItem) {
		return;
	}

	std::weak_ptr<Item> weakCorpse = corpseItem;
	auto it = lootHighlightEvents.find(weakCorpse);
	if (it == lootHighlightEvents.end()) {
		return; // No highlight active for this corpse
	}

	g_scheduler.stopEvent(it->second);
	lootHighlightEvents.erase(it);
}

// ============================================================

void Game::checkLight()
{
	g_scheduler.addEvent(createSchedulerTask(EVENT_LIGHTINTERVAL, [this]() { checkLight(); }));
	uint8_t previousLightLevel = lightLevel;
	updateWorldLightLevel();

	if (previousLightLevel != lightLevel) {
		LightInfo lightInfo = getWorldLightInfo();

		for (const auto& player : getPlayers()) {
			player->sendWorldLight(lightInfo);
		}
	}
}

void Game::updateWorldLightLevel()
{
	if (getWorldTime() >= GAME_SUNRISE && getWorldTime() <= GAME_DAYTIME) {
		lightLevel = ((GAME_DAYTIME - GAME_SUNRISE) - (GAME_DAYTIME - getWorldTime())) * float(LIGHT_CHANGE_SUNRISE) +
		             LIGHT_NIGHT;
	} else if (getWorldTime() >= GAME_SUNSET && getWorldTime() <= GAME_NIGHTTIME) {
		lightLevel = LIGHT_DAY - ((getWorldTime() - GAME_SUNSET) * float(LIGHT_CHANGE_SUNSET));
	} else if (getWorldTime() >= GAME_NIGHTTIME || getWorldTime() < GAME_SUNRISE) {
		lightLevel = LIGHT_NIGHT;
	} else {
		lightLevel = LIGHT_DAY;
	}
}

LightState_t Game::getLightState() const
{
	if (worldTime >= GAME_SUNRISE && worldTime < GAME_DAYTIME) {
		return LIGHT_STATE_SUNRISE;
	} else if (worldTime >= GAME_DAYTIME && worldTime < GAME_SUNSET) {
		return LIGHT_STATE_DAY;
	} else if (worldTime >= GAME_SUNSET && worldTime < GAME_NIGHTTIME) {
		return LIGHT_STATE_SUNSET;
	}
	return LIGHT_STATE_NIGHT;
}

void Game::updateWorldTime()
{
	g_scheduler.addEvent(createSchedulerTask(EVENT_WORLDTIMEINTERVAL, [this]() { updateWorldTime(); }));
	time_t osTime = time(nullptr);
	struct tm timeInfo;
#if defined(_WIN32)
	localtime_s(&timeInfo, &osTime);
#else
	localtime_r(&osTime, &timeInfo);
#endif
	int16_t baseTime = static_cast<int16_t>((timeInfo.tm_sec + (timeInfo.tm_min * 60)) / 2.5f);
	worldTime = (baseTime + worldTimeOffset) % 1440;
	if (worldTime < 0) {
		worldTime += 1440;
	}

	LightState_t currentLightState = getLightState();
	if (currentLightState != lastLightState) {
		lastLightState = currentLightState;
		g_globalEvents->periodChange(currentLightState);
	}
}

void Game::setWorldTime(int16_t time)
{
	time_t osTime = std::time(nullptr);
	struct tm timeInfo;
#if defined(_WIN32)
	localtime_s(&timeInfo, &osTime);
#else
	localtime_r(&osTime, &timeInfo);
#endif
	int16_t baseTime = static_cast<int16_t>((timeInfo.tm_sec + (timeInfo.tm_min * 60)) / 2.5f);
	worldTimeOffset = (time - baseTime) % 1440;
	updateWorldTime();
}

void Game::shutdown()
{
	LOG_INFO(">> Shutting down...");

	{
		auto toRemove = getPlayers();
		for (const auto& player : toRemove) {
			if (!player->isRemoved()) {
				removeCreature(player.get(), true);
			}
		}
	}

	{
		std::vector<Monster*> toRemove;
		toRemove.reserve(monsters.size());
		for (auto& [id, monster] : monsters) {
			if (auto monsterRef = monster.lock()) {
				toRemove.push_back(monsterRef.get());
			}
		}
		for (Monster* monster : toRemove) {
			if (!monster->isRemoved()) {
				removeCreature(monster, false);
			}
		}
	}

	{
		std::vector<Npc*> toRemove;
		toRemove.reserve(npcs.size());
		for (auto& [id, npc] : npcs) {
			if (auto npcRef = npc.lock()) {
				toRemove.push_back(npcRef.get());
			}
		}
		for (Npc* npc : toRemove) {
			if (!npc->isRemoved()) {
				removeCreature(npc, false);
			}
		}
	}

	ScriptEnvironment::clearTempItems();
	cleanup();

	for (auto& checkCreatureList : checkCreatureLists) {
		for (const auto& creatureRef : checkCreatureList) {
			Creature* creature = creatureRef.get();
			if (Creature::isAlive(creature)) {
				creature->attackedCreature.reset();
				creature->followCreature.reset();
				creature->master.reset();
				creature->summons.clear();
			}
		}
	}

	for (auto& checkCreatureList : checkCreatureLists) {
		for (const auto& creatureRef : checkCreatureList) {
			Creature* creature = creatureRef.get();
			if (Creature::isAlive(creature)) {
				creature->inCheckCreaturesVector = false;
			}
		}
		checkCreatureList.clear();
	}

	map.spawns.clear();
	raids.clear();
	guilds.clear();

	cleanup();

	g_decay.clear();
	cleanup();

	g_scheduler.shutdown();
	g_databaseTasks.shutdown();
	g_dispatcher.shutdown();

#ifdef STATS_ENABLED
	g_stats.shutdown();
#endif

	if (auto manager = serviceManager.lock()) {
		manager->stop();
	}

	{
		std::unique_lock<std::shared_mutex> lock(creatureRefsMutex);
		creatureSharedRefs.clear();
	}

	Item::clearGlobalRegistry();

	OutputMessagePool::drainPool();

	LOG_INFO(">> Shutdown complete.");
}

void Game::cleanup()
{
	// free memory
	ToReleaseCreatures.clear();

	ToReleaseItems.clear(); // shared_ptrs destroyed, items freed
}

void Game::ReleaseCreature(Creature* creature)
{
	if (auto creatureRef = getCreatureSharedRef(creature)) {
		ToReleaseCreatures.push_back(std::move(creatureRef));
	}
}

void Game::ReleaseCreature(std::shared_ptr<Creature> creature) { ToReleaseCreatures.push_back(std::move(creature)); }

void Game::ReleaseItem(Item* item)
{
	if (auto itemRef = getItemSharedRef(item)) {
		ToReleaseItems.push_back(std::move(itemRef));
	}
}

void Game::ReleaseItem(std::shared_ptr<Item> item) { ToReleaseItems.push_back(std::move(item)); }

void Game::cleanupExpiredTradeItems()
{
	std::erase_if(tradeItems, [](const auto& entry) { return entry.first.expired(); });
}

void Game::eraseTradeItem(Item* item)
{
	if (!item) {
		return;
	}

	auto it = tradeItems.find(item->weak_from_this());
	if (it != tradeItems.end()) {
		tradeItems.erase(it);
	}
}

void Game::cleanupExpiredLootHighlightEvents()
{
	std::erase_if(lootHighlightEvents, [](const auto& entry) { return entry.first.expired(); });
}

bool Game::eraseLootHighlightEvent(const std::weak_ptr<Item>& corpse, uint32_t eventId)
{
	auto it = lootHighlightEvents.find(corpse);
	if (it == lootHighlightEvents.end() || it->second != eventId) {
		return false;
	}

	lootHighlightEvents.erase(it);
	return true;
}

void Game::broadcastMessage(std::string_view text, MessageClasses type) const
{
	LOG_INFO(fmt::format("> Broadcasted message: \"{}\".", text));
	for (const auto& player : getPlayers()) {
		player->sendTextMessage(type, text);
	}
}

void Game::updateCreatureWalkthrough(const Creature* creature)
{
	// send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	const uint32_t creatureInstance = creature->getInstanceID();
	for (const auto& spectator : spectators.players()) {
		auto tmpPlayer = static_cast<Player*>(spectator.get());
		if (!tmpPlayer->compareInstance(creatureInstance)) {
			continue;
		}
		tmpPlayer->sendCreatureWalkthrough(creature, tmpPlayer->canWalkthroughEx(creature));
	}
}

void Game::updateKnownCreature(const Creature* creature)
{
	// send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	const uint32_t creatureInstance = creature->getInstanceID();
	for (const auto& spectator : spectators.players()) {
		Player* p = static_cast<Player*>(spectator.get());
		if (!p->compareInstance(creatureInstance)) {
			continue;
		}
		p->sendUpdateTileCreature(creature);
	}
}

void Game::updateCreatureEmblem(Creature* creature)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	const uint32_t creatureInstance = creature->getInstanceID();
	for (const auto& spectator : spectators.players()) {
		Player* p = static_cast<Player*>(spectator.get());
		if (!p->compareInstance(creatureInstance)) {
			continue;
		}
		p->sendCreatureEmblem(creature);
	}
}

void Game::updateCreatureIcon(const Player* spectator, const Creature* creature)
{
	if (!spectator || !creature) {
		return;
	}
	spectator->sendCreatureIcon(creature);
}

void Game::updateCreatureIcon(const Creature* creature)
{
	if (!creature) {
		return;
	}

	const Tile* tile = creature->getTile();
	if (!tile) {
		return;
	}

	SpectatorVec spectators;
	map.getSpectators(spectators, tile->getPosition(), true, true);
	const uint32_t creatureInstance = creature->getInstanceID();
	for (const auto& spectator : spectators.players()) {
		Player* p = static_cast<Player*>(spectator.get());
		if (!p->compareInstance(creatureInstance)) {
			continue;
		}
		p->sendCreatureIcon(creature);
	}
}

void Game::updateCreatureSkull(const Creature* creature)
{
	// Allow influenced/fiendish monsters to show skull in any world type
	bool isForgeMonster = false;
	if (const Monster* monster = creature->getMonster()) {
		isForgeMonster = monster->isInfluenced() || monster->isFiendish();
	}

	if (!isForgeMonster && getWorldType() != WORLD_TYPE_PVP) {
		return;
	}

	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	const uint32_t creatureInstance = creature->getInstanceID();
	for (const auto& spectator : spectators.players()) {
		Player* p = static_cast<Player*>(spectator.get());
		if (!p->compareInstance(creatureInstance)) {
			continue;
		}
		p->sendCreatureSkull(creature);
	}
}

void Game::updatePlayerShield(Player* player)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, player->getPosition(), true, true);
	const uint32_t creatureInstance = player->getInstanceID();
	for (const auto& spectator : spectators.players()) {
		Player* p = static_cast<Player*>(spectator.get());
		if (!p->compareInstance(creatureInstance)) {
			continue;
		}
		p->sendCreatureShield(player);
	}
}

void Game::loadMotdNum()
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery("SELECT `value` FROM `server_config` WHERE `config` = 'motd_num'");
	if (result) {
		motdNum = result->getNumber<uint32_t>("value");
	} else {
		db.executeQuery("INSERT INTO `server_config` (`config`, `value`) VALUES ('motd_num', '0')");
	}

	result = db.storeQuery("SELECT UNHEX(`value`) AS `value` FROM `server_config` WHERE `config` = 'motd_hash'");
	if (result) {
		motdHash = result->getString("value");
		if (motdHash != transformToSHA1(getString(ConfigManager::MOTD))) {
			++motdNum;
		}
	} else {
		db.executeQuery("INSERT INTO `server_config` (`config`, `value`) VALUES ('motd_hash', '')");
	}
}

void Game::saveMotdNum() const
{
	Database& db = Database::getInstance();
	db.executeQuery(fmt::format("UPDATE `server_config` SET `value` = '{:d}' WHERE `config` = 'motd_num'", motdNum));
	db.executeQuery(fmt::format("UPDATE `server_config` SET `value` = HEX('{:s}') WHERE `config` = 'motd_hash'",
	                            transformToSHA1(getString(ConfigManager::MOTD))));
}

void Game::checkPlayersRecord()
{
	const size_t playersOnline = getPlayersOnline();
	if (playersOnline > playersRecord) {
		uint32_t previousRecord = playersRecord;
		playersRecord = playersOnline;

		for (auto& it : g_globalEvents->getEventMap(GLOBALEVENT_RECORD)) {
			it.second.executeRecord(playersRecord, previousRecord);
		}
		updatePlayersRecord();
	}
}

void Game::updatePlayersRecord() const
{
	Database& db = Database::getInstance();
	db.executeQuery(
	    fmt::format("UPDATE `server_config` SET `value` = '{:d}' WHERE `config` = 'players_record'", playersRecord));
}

void Game::loadPlayersRecord()
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery("SELECT `value` FROM `server_config` WHERE `config` = 'players_record'");
	if (result) {
		playersRecord = result->getNumber<uint32_t>("value");
	} else {
		db.executeQuery("INSERT INTO `server_config` (`config`, `value`) VALUES ('players_record', '0')");
	}
}

void Game::playerInviteToParty(uint32_t playerId, uint32_t invitedId)
{
	if (playerId == invitedId) {
		return;
	}

	auto playerRef = getPlayerByID(playerId);

	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	auto invitedPlayerRef = getPlayerByID(invitedId);

	Player* invitedPlayer = invitedPlayerRef.get();
	if (!invitedPlayer || invitedPlayer->isInviting(player)) {
		return;
	}

	if (invitedPlayer->getParty()) {
		player->sendTextMessage(MESSAGE_INFO_DESCR,
		                        fmt::format("{:s} is already in a party.", invitedPlayer->getName()));
		return;
	}

	Party* party = player->getParty();
	if (!party) {
		Party::create(player);
		party = player->getParty();
	} else if (party->getLeader().get() != player) {
		return;
	}

	if (!g_events->eventPartyOnInvite(party, invitedPlayer)) {
		if (party->empty()) {
			party->disband();
		}
		return;
	}

	party->invitePlayer(*invitedPlayer);
}

void Game::playerJoinParty(uint32_t playerId, uint32_t leaderId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	auto leaderRef = getPlayerByID(leaderId);

	Player* leader = leaderRef.get();
	if (!leader || !leader->isInviting(player)) {
		return;
	}

	Party* party = leader->getParty();
	if (!party || party->getLeader().get() != leader) {
		return;
	}

	if (player->getParty()) {
		player->sendTextMessage(MESSAGE_INFO_DESCR, "You are already in a party.");
		return;
	}

	party->joinParty(*player);
}

void Game::playerRevokePartyInvitation(uint32_t playerId, uint32_t invitedId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Party* party = player->getParty();
	if (!party || party->getLeader().get() != player) {
		return;
	}

	auto invitedPlayerRef = getPlayerByID(invitedId);

	Player* invitedPlayer = invitedPlayerRef.get();
	if (!invitedPlayer || !player->isInviting(invitedPlayer)) {
		return;
	}

	party->revokeInvitation(*invitedPlayer);
}

void Game::playerPassPartyLeadership(uint32_t playerId, uint32_t newLeaderId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Party* party = player->getParty();
	if (!party || party->getLeader().get() != player) {
		return;
	}

	auto newLeaderRef = getPlayerByID(newLeaderId);

	Player* newLeader = newLeaderRef.get();
	if (!newLeader || !player->isPartner(newLeader)) {
		return;
	}

	party->passPartyLeadership(newLeader);
}

void Game::playerLeaveParty(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Party* party = player->getParty();
	if (!party || player->hasCondition(CONDITION_INFIGHT)) {
		return;
	}

	party->leaveParty(player);
}

void Game::playerEnableSharedPartyExperience(uint32_t playerId, bool sharedExpActive)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	Party* party = player->getParty();
	if (!party) {
		return;
	}

	if (player->hasCondition(CONDITION_INFIGHT) && player->getZone() != ZONE_PROTECTION) {
		player->sendCancelMessage("You cannot activate shared experience while in a fight.");
		return;
	}

	party->setSharedExperience(player, sharedExpActive);
}

void Game::sendGuildMotd(uint32_t playerId)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	if (const auto& guild = player->getGuild()) {
		player->sendChannelMessage("Message of the Day", guild->getMotd(), TALKTYPE_CHANNEL_R1, CHANNEL_GUILD);
	}
}

void Game::kickPlayer(uint32_t playerId, bool displayEffect)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	player->kickPlayer(displayEffect);
}

void Game::playerReportRuleViolation(uint32_t playerId, std::string_view targetName, uint8_t reportType,
                                     uint8_t reportReason, std::string_view comment, std::string_view translation)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	g_events->eventPlayerOnReportRuleViolation(player, targetName, reportType, reportReason, comment, translation);
}

void Game::playerReportBug(uint32_t playerId, std::string_view message)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	g_events->eventPlayerOnReportBug(player, message);
}

void Game::parsePlayerNetworkMessage(uint32_t playerId, uint8_t recvByte, NetworkMessage_ptr msg)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	g_events->eventPlayerOnNetworkMessage(player, recvByte, msg);
}

void Game::parsePlayerExtendedOpcode(uint32_t playerId, uint8_t opcode, std::string_view buffer)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	for (CreatureEvent* creatureEvent : player->getCreatureEvents(CREATURE_EVENT_EXTENDED_OPCODE)) {
		creatureEvent->executeExtendedOpcode(player, opcode, buffer);
	}
}

bool Game::playerStartSpy(uint32_t godPlayerId, const std::string& targetName)
{
	auto godRef = getPlayerByID(godPlayerId);
	Player* god = godRef.get();
	if (!god || god->getAccountType() < ACCOUNT_TYPE_GOD) {
		return false;
	}

	auto targetRef = getPlayerByName(targetName);

	Player* target = targetRef.get();
	if (!target || target == god) {
		return false;
	}

	return g_spy.startSpy(god, target);
}

bool Game::playerStopSpy(uint32_t godPlayerId)
{
	auto godRef = getPlayerByID(godPlayerId);
	Player* god = godRef.get();
	if (!god) {
		return false;
	}

	return g_spy.stopSpy(god);
}

bool Game::playerSpyInventory(uint32_t godPlayerId, const std::string& targetName)
{
	auto godRef = getPlayerByID(godPlayerId);
	Player* god = godRef.get();
	if (!god || god->getAccountType() < ACCOUNT_TYPE_GOD) {
		return false;
	}

	auto targetRef = getPlayerByName(targetName);

	Player* target = targetRef.get();
	if (!target) {
		return false;
	}

	return g_spy.spyInventory(god, target);
}

bool Game::playerStopSpyInventory(uint32_t godPlayerId)
{
	auto godRef = getPlayerByID(godPlayerId);
	Player* god = godRef.get();
	if (!god) {
		return false;
	}

	return g_spy.stopSpyInventory(god);
}

void Game::forceAddCondition(uint32_t creatureId, Condition* condition)
{
	Condition_ptr condPtr(condition);
	Creature* creature = getCreatureByID(creatureId);
	if (!creature) {
		return;
	}

	creature->addCondition(std::move(condPtr), true);
}

void Game::forceRemoveCondition(uint32_t creatureId, ConditionType_t type)
{
	Creature* creature = getCreatureByID(creatureId);
	if (!creature) {
		return;
	}

	creature->removeCondition(type, true);
}

void Game::sendOfflineTrainingDialog(Player* player)
{
	if (!player) {
		return;
	}

	if (!player->hasModalWindowOpen(offlineTrainingWindow.id)) {
		player->sendModalWindow(offlineTrainingWindow);
	}
}

void Game::playerAnswerModalWindow(uint32_t playerId, uint32_t modalWindowId, uint8_t button, uint8_t choice)
{
	auto playerRef = getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!player) {
		return;
	}

	if (!player->hasModalWindowOpen(modalWindowId)) {
		return;
	}

	player->onModalWindowHandled(modalWindowId);

	// offline training, hard-coded
	if (modalWindowId == std::numeric_limits<uint32_t>::max()) {
		if (button != 1) {
			player->sendTextMessage(MESSAGE_EVENT_ADVANCE, "Offline training aborted.\nCome back when you feel tired.");
		}
	} else {
		for (auto creatureEvent : player->getCreatureEvents(CREATURE_EVENT_MODALWINDOW)) {
			creatureEvent->executeModalWindow(player, modalWindowId, button, choice);
		}
	}
}

void Game::addPlayer(Player* player)
{
	auto playerRef = getPlayerSharedByID(player->getID());
	if (!playerRef) {
		return;
	}

	const std::string& lowercase_name = asLowerCaseString(player->getName());
	
	{
		std::unique_lock<std::shared_mutex> lock(playersMutex);
		mappedPlayerNames[lowercase_name] = playerRef;
		mappedPlayerGuids[player->getGUID()] = playerRef;
		wildcardTree.insert(lowercase_name);
		players[player->getID()] = std::move(playerRef);
		playersOnline.store(players.size(), std::memory_order_relaxed);
	}
	
	checkPlayersRecord();
}

void Game::removePlayer(Player* player)
{
	// Spy cleanup: stop any spy sessions involving this player
	g_spy.onPlayerDisconnect(player->getID());

	const std::string& lowercase_name = asLowerCaseString(player->getName());
	
	{
		std::unique_lock<std::shared_mutex> lock(playersMutex);
		mappedPlayerNames.erase(lowercase_name);
		mappedPlayerGuids.erase(player->getGUID());
		wildcardTree.remove(lowercase_name);
		players.erase(player->getID());
		playersOnline.store(players.size(), std::memory_order_relaxed);
	}
}

void Game::addNpc(Npc* npc)
{
	npcs[npc->getID()] = std::static_pointer_cast<Npc>(getCreatureSharedRef(npc));
	npcsOnline.store(npcs.size(), std::memory_order_relaxed);
}

void Game::removeNpc(Npc* npc)
{
	npcs.erase(npc->getID());
	npcsOnline.store(npcs.size(), std::memory_order_relaxed);
}

void Game::addMonster(Monster* monster)
{
	monsters[monster->getID()] = std::static_pointer_cast<Monster>(getCreatureSharedRef(monster));
	monstersOnline.store(monsters.size(), std::memory_order_relaxed);
}

void Game::removeMonster(Monster* monster)
{
	monsters.erase(monster->getID());
	monstersOnline.store(monsters.size(), std::memory_order_relaxed);
}

Guild_ptr Game::getGuild(uint32_t id) const
{
	auto it = guilds.find(id);
	if (it == guilds.end()) {
		return nullptr;
	}
	return it->second;
}

void Game::addGuild(Guild_ptr guild) 
{
  if (!guild) {
     return;
   }
   
	guilds[guild->getId()] = guild;
}

void Game::removeGuild(uint32_t guildId) { guilds.erase(guildId); }

void Game::internalRemoveItems(std::vector<ObserverPtr<Item>> itemList, uint32_t amount, bool stackable)
{
	if (stackable) {
		for (Item* item : itemList) {
			if (item->getItemCount() > amount) {
				internalRemoveItem(item, amount);
				break;
			} else {
				amount -= item->getItemCount();
				internalRemoveItem(item);
			}
		}
	} else {
		for (Item* item : itemList) {
			internalRemoveItem(item);
		}
	}
}

BedItem* Game::getBedBySleeper(uint32_t guid)
{
	auto it = bedSleepersMap.find(guid);
	if (it == bedSleepersMap.end()) {
		return nullptr;
	}

	auto bed = it->second.lock();
	if (!bed) {
		bedSleepersMap.erase(it);
		return nullptr;
	}
	return bed.get();
}

void Game::setBedSleeper(BedItem* bed, uint32_t guid)
{
	if (!bed) {
		return;
	}

	auto bedItem = bed->weak_from_this().lock();
	if (!bedItem) {
		return;
	}

	bedSleepersMap[guid] = std::static_pointer_cast<BedItem>(bedItem);
}

void Game::removeBedSleeper(uint32_t guid)
{
	auto it = bedSleepersMap.find(guid);
	if (it != bedSleepersMap.end()) {
		bedSleepersMap.erase(it);
	}
}

Item* Game::getUniqueItem(uint16_t uniqueId)
{
	auto it = uniqueItems.find(uniqueId);
	if (it == uniqueItems.end()) {
		return nullptr;
	}

	auto item = it->second.lock();
	if (!item) {
		uniqueItems.erase(it);
		return nullptr;
	}
	return item.get();
}

bool Game::addUniqueItem(uint16_t uniqueId, Item* item)
{
	if (!item) {
		return false;
	}

	auto itemRef = item->weak_from_this();
	if (itemRef.expired()) {
		LOG_WARN(fmt::format("Unique id {} was not registered because the item is not shared-owned", uniqueId));
		return false;
	}

	auto result = uniqueItems.emplace(uniqueId, std::move(itemRef));
	if (!result.second) {
		LOG_WARN(fmt::format("Duplicate unique id: {}", uniqueId));
	}
	return result.second;
}

void Game::removeUniqueItem(uint16_t uniqueId)
{
	auto it = uniqueItems.find(uniqueId);
	if (it != uniqueItems.end()) {
		uniqueItems.erase(it);
	}
}

void Game::resetDamageTracking(uint32_t monsterId)
{
	rewardBossTracking.erase(monsterId);
}

bool Game::reload(ReloadTypes_t reloadType)
{
	switch (reloadType) {
		case RELOAD_TYPE_ACTIONS: {
			g_actions->clear(true);
			g_scripts->clearLoadedFiles();
			g_scripts->loadScripts("scripts/actions", false, true);
			LOG_INFO("Actions reloaded successfully.");
			return true;
		}
		case RELOAD_TYPE_CHAT: {
			bool result = g_chat->load();
			if (result) LOG_INFO("Chat reloaded successfully.");
			return result;
		}
		case RELOAD_TYPE_CONFIG: {
			bool result = ConfigManager::load();
			if (result) LOG_INFO("Config reloaded successfully.");
			return result;
		}
		case RELOAD_TYPE_CREATURESCRIPTS: {
			g_creatureEvents->clear(true);
			g_scripts->clearLoadedFiles();
			g_scripts->loadScripts("scripts/creaturescripts", false, true);
			g_creatureEvents->removeInvalidEvents();
			LOG_INFO("CreatureScripts reloaded successfully.");
			return true;
		}
		case RELOAD_TYPE_EVENTS: {
			bool result = g_events->load();
			if (result) LOG_INFO("Events reloaded successfully.");
			return result;
		}
		case RELOAD_TYPE_GLOBALEVENTS: {
			g_globalEvents->clear(true);
			g_scripts->clearLoadedFiles();
			g_scripts->loadScripts("scripts/globalevents", false, true);
			LOG_INFO("GlobalEvents reloaded successfully.");
			return true;
		}
		case RELOAD_TYPE_ITEMS: {
			for (const auto& player : getPlayers()) {
				player->reloadEquipmentStats();
			}
			bool result = Item::items.reload();
			if (result) LOG_INFO("Items reloaded successfully.");
			for (const auto& player : getPlayers()) {
				player->applyEquipmentStats();
			}
			return result;
		}
		case RELOAD_TYPE_MONSTERS: {
			g_monsters.reload();
			g_scripts->clearLoadedFiles();
			g_scripts->loadScripts("monsters", false, true);
			LOG_INFO("Monsters reloaded successfully.");
			return true;
		}
		case RELOAD_TYPE_OUTFITS: {
			// First, remove outfit attributes from all online players
			for (const auto& player : getPlayers()) {
				if (player->outfitAttributes) {
					const Outfit_t defaultOutfit = player->getDefaultOutfit();
					PlayerSex_t sex = player->getSex();
					uint32_t outfitId = Outfits::getInstance().getOutfitId(sex, defaultOutfit.lookType);
					if (outfitId != 0) {
						Outfits::getInstance().removeAttributes(player->getID(), outfitId, sex);
					}
					player->outfitAttributes = false;
				}
			}
			
			// Reload the outfit catalog
			bool result = Outfits::getInstance().reload();
			
			// Reapply outfit attributes to online players based on new catalog
			if (result) {
				for (const auto& player : getPlayers()) {
					const Outfit_t defaultOutfit = player->getDefaultOutfit();
					PlayerSex_t sex = player->getSex();
					if (defaultOutfit.lookAddons >= getInteger(ConfigManager::MAX_ADDON_ATTRIBUTES)) {
						const Outfit* outfit = Outfits::getInstance().getOutfitByLookType(defaultOutfit.lookType, sex);
						if (outfit) {
							uint32_t outfitId = Outfits::getInstance().getOutfitId(sex, defaultOutfit.lookType);
							player->outfitAttributes = Outfits::getInstance().addAttributes(player->getID(), outfitId, sex);
						}
					}
				}
				LOG_INFO("Outfits reloaded successfully.");
			}
			
			return result;
		}
		case RELOAD_TYPE_MOUNTS: {
			bool result = mounts.reload();
			if (result) LOG_INFO("Mounts reloaded successfully.");
			return result;
		}
		case RELOAD_TYPE_MOVEMENTS: {
			g_moveEvents->clear(true);
			g_scripts->clearLoadedFiles();
			g_scripts->loadScripts("scripts/movements", false, true);
			LOG_INFO("Movements reloaded successfully.");
			return true;
		}
		case RELOAD_TYPE_NPCS: {
			Npcs::reload();
			LOG_INFO("NPCs reloaded successfully.");
			return true;
		}

		case RELOAD_TYPE_RAIDS: {
			bool result = raids.reload() && raids.startup();
			if (result) LOG_INFO("Raids reloaded successfully.");
			return result;
		}

		case RELOAD_TYPE_SPELLS: {
			g_spells->clear(true);
			g_scripts->clearLoadedFiles();
			g_scripts->loadScripts("scripts/spells", false, true);
			g_monsters.reload();
			g_scripts->loadScripts("monsters", false, true);
			LOG_INFO("Spells reloaded successfully.");
			return true;
		}

		case RELOAD_TYPE_TALKACTIONS: {
			g_talkActions->clear(true);
			g_scripts->clearLoadedFiles();
			g_scripts->loadScripts("scripts/talkactions", false, true);
			LOG_INFO("TalkActions reloaded successfully.");
			return true;
		}

		case RELOAD_TYPE_WEAPONS: {
			g_weapons->clear(true);
			g_weapons->loadDefaults();
			g_scripts->clearLoadedFiles();
			g_scripts->loadScripts("scripts/weapons", false, true);
			LOG_INFO("Weapons reloaded successfully.");
			return true;
		}

		case RELOAD_TYPE_SCRIPTS: {
			g_actions->clear(true);
			g_creatureEvents->clear(true);
			g_moveEvents->clear(true);
			g_talkActions->clear(true);
			g_globalEvents->clear(true);
			g_weapons->clear(true);
			g_weapons->loadDefaults();
			g_spells->clear(true);
			g_scripts->clearLoadedFiles();
			g_scripts->loadScripts("scripts", false, true);
			g_monsters.reload();
			g_scripts->loadScripts("monsters", false, true);
			g_creatureEvents->removeInvalidEvents();
			LOG_INFO("Scripts reloaded successfully.");
			return true;
		}

		default: {
			g_actions->clear(true);
			g_creatureEvents->clear(true);
			g_moveEvents->clear(true);
			g_talkActions->clear(true);
			g_globalEvents->clear(true);
			g_spells->clear(true);
			g_weapons->clear(true);
			
			g_spells->reload();
			g_monsters.reload();
			g_actions->reload();
			ConfigManager::load();
			g_creatureEvents->reload();
			g_moveEvents->reload();
			Npcs::reload();
			raids.reload() && raids.startup();
			g_talkActions->reload();
			Item::items.reload();
			g_weapons->reload();
			g_weapons->loadDefaults();
			mounts.reload();
			g_globalEvents->reload();
			g_events->load();
			g_chat->load();
			g_scripts->clearLoadedFiles();
			g_scripts->loadScripts("scripts", false, true);
			g_monsters.reload();
			g_scripts->loadScripts("monsters", false, true);
			g_creatureEvents->removeInvalidEvents();
			LOG_INFO("All reloaded successfully.");
			return true;
		}
	}
	return true;
}

void Game::loadGameStorageValues()
{
	Database& db = Database::getInstance();

	DBResult_ptr result;
	if ((result = db.storeQuery("SELECT `key`, `value` FROM `game_storage`"))) {
		do {
			g_game.setStorageValue(result->getNumber<uint32_t>("key"), result->getNumber<int32_t>("value"));
		} while (result->next());
	}
}

bool Game::saveGameStorageValues() const
{
	DBTransaction transaction;
	Database& db = Database::getInstance();

	if (!transaction.begin()) {
		return false;
	}

	if (!db.executeQuery("DELETE FROM `game_storage`")) {
		return false;
	}

	for (const auto& [key, value] : g_game.storageMap) {
		DBInsert gameStorageQuery("INSERT INTO `game_storage` (`key`, `value`) VALUES");
		if (!gameStorageQuery.addRow(fmt::format("{:d}, {:d}", key, value))) {
			return false;
		}

		if (!gameStorageQuery.execute()) {
			return false;
		}
	}

	return transaction.commit();
}

void Game::setStorageValue(uint32_t key, std::optional<int64_t> value)
{
	if (value && value.value() != -1) {
		storageMap.insert_or_assign(key, value.value());
	} else {
		storageMap.erase(key);
	}
}

std::optional<int64_t> Game::getStorageValue(uint32_t key) const
{
	auto it = storageMap.find(key);
	if (it == storageMap.end()) {
		return std::nullopt;
	}
	return std::make_optional(it->second);
}

std::vector<ObserverPtr<Player>> Game::getLiveCasters(std::string_view name) const
{
	std::vector<ObserverPtr<Player>> casters;
	for (const auto& player : getPlayers()) {
		if (player && player->client && player->client->isBroadcasting()) {
			if (name.empty() || caseInsensitiveContains(player->getName(), name)) {
				casters.push_back(player.get());
			}
		}
	}
	return casters;
}
