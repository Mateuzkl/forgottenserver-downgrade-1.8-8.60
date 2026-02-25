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
#include "iologindata.h"
#include "items.h"
#include "monster.h"
#include "movement.h"
#include "protocolgame.h"
#include "protocolspectator.h"
#include "pugicast.h"
#include "decay.h"
#include "scheduler.h"
#include "script.h"
#include "server.h"
#include "stats.h"
#include "spells.h"
#include "talkaction.h"
#include "weapons.h"
#include "logger.h"
#include <fmt/format.h>
#include <limits>
#include "luascript.h"

extern Actions* g_actions;
extern Chat* g_chat;
extern TalkActions* g_talkActions;
extern Spells* g_spells;
extern Vocations g_vocations;
extern GlobalEvents* g_globalEvents;
extern CreatureEvents* g_creatureEvents;
extern Events* g_events;
extern Monsters g_monsters;
extern MoveEvents* g_moveEvents;
extern Weapons* g_weapons;
extern Scripts* g_scripts;
extern LuaEnvironment g_luaEnvironment;

void Game::start(ServiceManager* manager)
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
}

GameState_t Game::getGameState() const { return gameState; }

std::vector<Player*> Game::getLiveCasters(const std::string& password)
{
	std::vector<Player*> casters;

	for(Player* caster : liveCasters) {
		if(caster->isLiveCasting() && caster->castPassword == password) {
			casters.push_back(caster);
		}
	}

	return casters;
}

void Game::setWorldType(WorldType_t type) { worldType = type; }

void Game::setGameState(GameState_t newState)
{
	if (gameState == GAME_STATE_SHUTDOWN) {
		return; // this cannot be stopped
	}

	if (gameState == newState) {
		return;
	}

	gameState = newState;
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
			auto it = players.begin();
			while (it != players.end()) {
				it->second->kickPlayer(true);
				it = players.begin();
			}
			LOG_INFO(">> All players kicked.");

			// Clear all spawns and release monster memory
			map.spawns.clear();

			saveMotdNum();
			LOG_INFO(">> Saving game state...");
			saveGameState();
			LOG_INFO(">> Game state saved successfully.");

			g_dispatcher.addTask([this]() { shutdown(); });

			g_scheduler.stop();
			g_databaseTasks.stop();
			g_dispatcher.stop();
#ifdef STATS_ENABLED
			g_stats.stop();
#endif
			LOG_INFO(">> Shutdown complete.");
			break;
		}

		case GAME_STATE_CLOSED: {
			g_globalEvents->save();

			/* kick all players without the CanAlwaysLogin flag */
			auto it = players.begin();
			while (it != players.end()) {
				if (!it->second->hasFlag(PlayerFlag_CanAlwaysLogin)) {
					it->second->kickPlayer(true);
					it = players.begin();
				} else {
					++it;
				}
			}

			saveGameState();
			break;
		}

		default:
			break;
	}
}

void Game::saveGameState()
{
	AutoStat stat("Game::saveGameState", "full");
	if (gameState == GAME_STATE_NORMAL) {
		setGameState(GAME_STATE_MAINTAIN);
	}

	LOG_INFO(">> Saving server...");

	if (!saveGameStorageValues()) {
		LOG_ERROR("[Error - Game::saveGameState] Failed to save game storage values.");
	}

	if (!saveAccountStorageValues()) {
		LOG_ERROR("[Error - Game::saveGameState] Failed to save account-level storage values.");
	}

	for (const auto& it : players) {
		it.second->loginPosition = it.second->getPosition();
		IOLoginData::savePlayer(it.second);
	}

	Map::save();

	g_databaseTasks.flush();

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
				Item* item = tile->getTopDownItem();
				if (item && item->isMoveable()) {
					thing = item;
				} else {
					thing = tile->getTopVisibleCreature(player);
				}
				break;
			}

			case STACKPOS_USEITEM: {
				thing = tile->getUseItem(index);
				break;
			}

			case STACKPOS_TOPDOWN_ITEM: {
				thing = tile->getTopDownItem();
				break;
			}

			case STACKPOS_USETARGET: {
				thing = tile->getTopVisibleCreature(player);
				if (!thing) {
					thing = tile->getUseItem(index);
				}
				break;
			}

			default: {
				thing = nullptr;
				break;
			}
		}

		if (player && tile->hasFlag(TILESTATE_SUPPORTS_HANGABLE)) {
			// do extra checks here if the thing is accessible
			if (thing && thing->getItem()) {
				if (tile->hasProperty(CONST_PROP_ISVERTICAL)) {
					if (player->getPosition().x + 1 == tile->getPosition().x) {
						thing = nullptr;
					}
				} else { // horizontal
					if (player->getPosition().y + 1 == tile->getPosition().y) {
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
		const ItemType& it = Item::items.getItemIdByClientId(static_cast<uint16_t>(spriteId));
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
	return player->getInventoryItem(static_cast<slots_t>(pos.y));
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
		return getPlayerByID(id);
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
	return it->second;
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
	return it->second;
}

Player* Game::getPlayerByID(uint32_t id)
{
	if (id == 0) {
		return nullptr;
	}

	auto it = players.find(id);
	if (it == players.end()) {
		return nullptr;
	}
	return it->second;
}

Creature* Game::getCreatureByName(const std::string& s)
{
	if (s.empty()) {
		return nullptr;
	}

	const std::string& lowerCaseName = boost::algorithm::to_lower_copy<std::string>(s);

	{
		auto it = mappedPlayerNames.find(lowerCaseName);
		if (it != mappedPlayerNames.end()) {
			return it->second;
		}
	}

	auto equalCreatureName = [&](const std::pair<uint32_t, Creature*>& it) {
		auto& name = it.second->getName();
		return lowerCaseName.size() == name.size() &&
		       std::equal(lowerCaseName.begin(), lowerCaseName.end(), name.begin(),
		                  [](char a, char b) { return a == std::tolower(b); });
	};

	{
		auto it = std::find_if(npcs.begin(), npcs.end(), equalCreatureName);
		if (it != npcs.end()) {
			return it->second;
		}
	}

	{
		auto it = std::find_if(monsters.begin(), monsters.end(), equalCreatureName);
		if (it != monsters.end()) {
			return it->second;
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
		if (caseInsensitiveEqual(npcName, it.second->getName())) {
			return it.second;
		}
	}
	return nullptr;
}

Player* Game::getPlayerByName(std::string_view s)
{
	if (s.empty()) {
		return nullptr;
	}

	auto it = mappedPlayerNames.find(boost::algorithm::to_lower_copy<std::string>(std::string{s}));
	if (it == mappedPlayerNames.end()) {
		return nullptr;
	}
	return it->second;
}

Player* Game::getPlayerByGUID(const uint32_t& guid)
{
	if (guid == 0) {
		return nullptr;
	}

	auto it = mappedPlayerGuids.find(guid);
	if (it == mappedPlayerGuids.end()) {
		return nullptr;
	}
	return it->second;
}

ReturnValue Game::getPlayerByNameWildcard(std::string_view s, Player*& player)
{
	size_t strlen = s.length();
	if (strlen == 0 || strlen > PLAYER_NAME_LENGTH) {
		return RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE;
	}

	if (s.back() == '~') {
		auto query = boost::algorithm::to_lower_copy<std::string>(std::string{s.substr(0, strlen - 1)});
		std::string result;
		ReturnValue ret = wildcardTree.findOne(query, result);
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

Player* Game::getPlayerByAccount(uint32_t acc)
{
	for (const auto& it : players) {
		if (it.second->getAccount() == acc) {
			return it.second;
		}
	}
	return nullptr;
}

bool Game::internalPlaceCreature(Creature* creature, const Position& pos, bool extendedPos /*=false*/,
                                 bool forced /*= false*/)
{
	if (creature->getParent() != nullptr) {
		return false;
	}

	if (!map.placeCreature(pos, creature, extendedPos, forced)) {
		return false;
	}

	creature->incrementReferenceCounter();
	creature->setID();
	creature->addList();
	return true;
}

bool Game::placeCreature(Creature* creature, const Position& pos, bool extendedPos /*=false*/, bool forced /*= false*/,
                         MagicEffectClasses magicEffect /*= CONST_ME_TELEPORT*/)
{
	if (!internalPlaceCreature(creature, pos, extendedPos, forced)) {
		return false;
	}

	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true);
	for (Creature* spectator : spectators) {
		if (Player* tmpPlayer = spectator->getPlayer()) {
			tmpPlayer->sendCreatureAppear(creature, creature->getPosition(), magicEffect);
		}
	}

	for (Creature* spectator : spectators) {
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

	Tile* tile = creature->getTile();

	std::vector<int32_t> oldStackPosVector;

	SpectatorVec spectators;
	map.getSpectators(spectators, tile->getPosition(), true);
	for (Creature* spectator : spectators) {
		if (Player* player = spectator->getPlayer()) {
			oldStackPosVector.push_back(
			    player->canSeeCreature(creature) ? tile->getClientIndexOfCreature(player, creature) : -1);
		}
	}

	tile->removeCreature(creature);

	const Position& tilePosition = tile->getPosition();

	// send to client
	size_t i = 0;
	uint32_t creatureId = creature->getID();
	for (Creature* spectator : spectators) {
		if (Player* player = spectator->getPlayer()) {
			if (player->canSeeCreature(creature)) {
				player->sendRemoveTileThing(tilePosition, oldStackPosVector[i++]);
				if (player->client) {
					player->client->removeKnownCreature(creatureId);
					if (player->isLiveCasting()) {
						for (auto& castSpectator : player->spectators) {
							if (castSpectator && castSpectator->isAcceptingPackets()) {
								castSpectator->removeKnownCreature(creatureId);
							}
						}
					}
				}
			}
		}
	}

	// event method
	for (Creature* spectator : spectators) {
		spectator->onRemoveCreature(creature, isLogout);
	}

	Creature* master = creature->getMaster();
	if (master && !master->isRemoved()) {
		creature->setMaster(nullptr);
	}

	creature->getParent()->postRemoveNotification(creature, nullptr, 0);

	creature->removeList();
	creature->setRemoved();
	ReleaseCreature(creature);

	removeCreatureCheck(creature);

	for (Creature* summon : creature->summons) {
		summon->setSkillLoss(false);
		removeCreature(summon);
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
	Player* player = getPlayerByID(playerId);
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
			SchedulerTask* task =
			    createSchedulerTask(static_cast<uint32_t>(getInteger(ConfigManager::RANGE_MOVE_CREATURE_INTERVAL)),
			                        ([=, this, playerID = player->getID(), creatureID = movingCreature->getID()]() {
				                        playerMoveCreatureByID(playerID, creatureID, fromPos, toPos);
			                        }));
			player->setNextActionTask(task);
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
	Player* player = getPlayerByID(playerId);
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
		SchedulerTask* task =
		    createSchedulerTask(delay, ([=, this, playerID = player->getID(), movingCreatureID = movingCreature->getID(),
		                                toPos = toTile->getPosition()]() {
			    playerMoveCreatureByID(playerID, movingCreatureID, movingCreatureOrigPos, toPos);
		    }));
		player->setNextActionTask(task);
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
			SchedulerTask* task = createSchedulerTask(
			    static_cast<uint32_t>(getInteger(ConfigManager::RANGE_MOVE_CREATURE_INTERVAL)),
			    ([=, this, playerID = player->getID(), movingCreatureID = movingCreature->getID(),
			     toPos = toTile->getPosition()] {
				    playerMoveCreatureByID(playerID, movingCreatureID, movingCreatureOrigPos, toPos);
			    }));
			player->setNextWalkActionTask(task);
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
		if (toTile->hasFlag(TILESTATE_BLOCKPATH)) {
			player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
			return;
		} else if ((movingCreature->getZone() == ZONE_PROTECTION && !toTile->hasFlag(TILESTATE_PROTECTIONZONE)) ||
		           (movingCreature->getZone() == ZONE_NOPVP && !toTile->hasFlag(TILESTATE_NOPVPZONE))) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		} else {
			if (CreatureVector* tileCreatures = toTile->getCreatures()) {
				for (Creature* tileCreature : *tileCreatures) {
					if (!tileCreature->isInGhostMode()) {
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

	while ((subCylinder = toCylinder->queryDestination(index, creature, &toItem, flags)) != toCylinder) {
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	playerMoveItem(player, fromPos, spriteId, fromStackPos, toPos, count, nullptr, nullptr);
}

void Game::playerMoveItem(Player* player, const Position& fromPos, uint16_t spriteId, uint8_t fromStackPos,
                          const Position& toPos, uint8_t count, Item* item, Cylinder* toCylinder)
{
	if (!player->canDoAction()) {
		uint32_t delay = player->getNextActionTime();
		SchedulerTask* task = createSchedulerTask(delay, ([=, this, playerID = player->getID()]() {
			playerMoveItemByPlayerID(playerID, fromPos, spriteId, fromStackPos, toPos, count);
		}));
		player->setNextActionTask(task);
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
	const auto playerMoveHangableItem = [=, this](const Position& playerPos, const Position& mapFromPos,
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

				SchedulerTask* task = createSchedulerTask(
				    static_cast<uint32_t>(getInteger(ConfigManager::RANGE_MOVE_ITEM_INTERVAL)),
				    ([this, playerID = player->getID(), itemPos, spriteId, itemStackPos, toPos, count]() {
					    playerMoveItemByPlayerID(playerID, itemPos, spriteId, itemStackPos, toPos, count);
				    }));
				player->setNextWalkActionTask(task);
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

			SchedulerTask* task = createSchedulerTask(
			    static_cast<uint32_t>(getInteger(ConfigManager::RANGE_MOVE_ITEM_INTERVAL)),
			    ([=, this, playerID = player->getID()]() {
				    playerMoveItemByPlayerID(playerID, fromPos, spriteId, fromStackPos, toPos, count);
			    }));
			player->setNextWalkActionTask(task);
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
	}
}

ReturnValue Game::internalMoveItem(Cylinder* fromCylinder, Cylinder* toCylinder, int32_t index, Item* item,
                                   uint32_t count, Item** _moveItem, uint32_t flags /*= 0*/,
                                   Creature* actor /* = nullptr*/, Item* tradeItem /* = nullptr*/,
                                   const Position* fromPos /*= nullptr*/, const Position* toPos /*= nullptr*/)
{
	Player* actorPlayer = actor ? actor->getPlayer() : nullptr;
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
			if (const Tile* toTile = toCylinder->getTile()) {
				if (const TileItemVector* items = toTile->getItemList()) {
					if (items->size() >= TILE_MAX_ITEMS) {
						return RETURNVALUE_CANNOTADDMOREITEMSONTILE;
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

	while ((subCylinder = toCylinder->queryDestination(index, *item, &toItem, flags)) != toCylinder) {
		toCylinder = subCylinder;
		flags = 0;

		// to prevent infinite loop
		if (++floorN >= MAP_MAX_LAYERS) {
			break;
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
					addMagicEffect(*toPos, CONST_ME_POFF);
					return RETURNVALUE_CANNOTTHROWONTELEPORT;
				}
			}

			// Check all items on the tile for teleports
			const TileItemVector* items = toTile->getItemList();
			if (items) {
				for (const Item* tileItem : *items) {
					if (tileItem) {
						const uint16_t itemId = tileItem->getID();
						if (std::find(blockedIds.begin(), blockedIds.end(), itemId) != blockedIds.end()) {
							actorPlayer->sendCancelMessage(RETURNVALUE_CANNOTTHROWONTELEPORT);
							addMagicEffect(*toPos, CONST_ME_POFF);
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
	}

	if (Container* itemContainer = dynamic_cast<Container*>(item)) {
		                		if (itemContainer->isRewardCorpse() || item->getID() == ITEM_REWARD_CONTAINER) {   
			return RETURNVALUE_NOERROR; // silently ignore move
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
	fromCylinder->removeThing(item, m);

	// update item(s)
	if (item->isStackable()) {
		uint32_t n;

		if (item->equals(toItem)) {
			n = std::min<uint32_t>(toItem->getStackSize() - toItem->getItemCount(), m);
			toCylinder->updateThing(toItem, toItem->getID(), toItem->getItemCount() + n);
			updateItem = toItem;
		} else {
			n = 0;
		}

		int32_t newCount = m - n;
		if (newCount > 0) {
			moveItem = item->clone();
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
		toCylinder->addThing(index, moveItem);
	}

	if (itemIndex != -1) {
		fromCylinder->postRemoveNotification(item, toCylinder, itemIndex);
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
	toCylinder = toCylinder->queryDestination(index, *item, &toItem, flags);

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
		if (count > 0) {
			if (item->getItemCount() != count) {
				Item* remainderItem = item->clone();
				remainderItem->setItemCount(static_cast<uint8_t>(count));
				if (internalAddItem(destCylinder, remainderItem, INDEX_WHEREEVER, flags, false) !=
				    RETURNVALUE_NOERROR) {
					ReleaseItem(remainderItem);
					remainderCount = count;
				}
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

		// remove the item
		cylinder->removeThing(item, count);

		if (item->isRemoved()) {
			item->onRemoved();
			item->stopDecaying();
			if (item->canDecay()) {
				toDecayItems.remove(item);
			}
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
		Item* remainderItem = Item::CreateItem(item->getID(), static_cast<uint16_t>(remainderCount));
		ReturnValue remaindRet = internalAddItem(player->getTile(), remainderItem, INDEX_WHEREEVER, FLAG_NOLIMIT);
		if (remaindRet != RETURNVALUE_NOERROR) {
			ReleaseItem(remainderItem);
		}
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
		for (Item* item : container->getItemList()) {
			if (item->getID() == itemId && (subType == -1 || subType == item->getSubType())) {
				return item;
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
		for (Item* item : container->getItemList()) {
			Container* tmpContainer = item->getContainer();
			if (tmpContainer) {
				containers.push_back(tmpContainer);
			} else {
				const uint32_t worth = item->getWorth();
				if (worth != 0) {
					moneyCount += worth;
					moneyMap.emplace(worth, item);
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

			Item* remaindItem = Item::CreateItem(it.second, count);

			ReturnValue ret = internalAddItem(cylinder, remaindItem, INDEX_WHEREEVER, flags);
			if (ret != RETURNVALUE_NOERROR) {
				internalAddItem(cylinder->getTile(), remaindItem, INDEX_WHEREEVER, FLAG_NOLIMIT);
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
		item->startDecaying();
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
					Item* newItem = Item::CreateItem(static_cast<uint16_t>(newItemId), 1);
					if (newItem == nullptr) {
						return nullptr;
					}

					cylinder->replaceThing(itemIndex, newItem);
					cylinder->postAddNotification(newItem, cylinder, itemIndex);

					item->setParent(nullptr);
					cylinder->postRemoveNotification(item, cylinder, itemIndex);
					item->stopDecaying();
					ReleaseItem(item);
					newItem->startDecaying();
					return newItem;
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
			item->startDecaying();
			return item;
		}
	}

	// Replacing the old item with the new while maintaining the old position
	Item* newItem;
	if (newCount == -1) {
		newItem = Item::CreateItem(newId);
	} else {
		newItem = Item::CreateItem(newId, static_cast<uint16_t>(newCount));
	}

	if (newItem == nullptr) {
		return nullptr;
	}

	cylinder->replaceThing(itemIndex, newItem);
	cylinder->postAddNotification(newItem, cylinder, itemIndex);

	item->setParent(nullptr);
	cylinder->postRemoveNotification(item, cylinder, itemIndex);
	item->stopDecaying();
	ReleaseItem(item);

	newItem->startDecaying();

	return newItem;
}

ReturnValue Game::internalTeleport(Thing* thing, const Position& newPos, bool pushMove /* = true*/,
                                   uint32_t flags /*= 0*/)
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

		map.moveCreature(*creature, *toTile, !pushMove);
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
	Player* player = getPlayerByID(playerId);
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

	for (const auto& it : players) {
		it.second->sendPrivateMessage(player, TALKTYPE_BROADCAST, text);
	}

	return true;
}

void Game::playerCreatePrivateChannel(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player || !player->isPremium()) {
		return;
	}

	if (ChatChannel* channel = g_chat->createChannel(*player, CHANNEL_PRIVATE)) {
		if (!channel->addUser(*player)) {
			return;
		}

		player->sendCreatePrivateChannel(channel->getId(), channel->getName());
		channel->executeOnJoinEvent(*player);
	}
}

void Game::playerChannelInvite(uint32_t playerId, std::string_view name)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	PrivateChatChannel* channel = g_chat->getPrivateChannel(*player);
	if (!channel) {
		return;
	}

	Player* invitePlayer = getPlayerByName(name);
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	PrivateChatChannel* channel = g_chat->getPrivateChannel(*player);
	if (!channel) {
		return;
	}

	Player* excludePlayer = getPlayerByName(name);
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->sendChannelsDialog();
}

void Game::playerOpenChannel(uint32_t playerId, uint16_t channelId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (ChatChannel* channel = g_chat->addUserToChannel(*player, channelId)) {
		player->sendChannel(channel->getId(), channel->getName());
		channel->executeOnJoinEvent(*player);
	}
}

void Game::playerCloseChannel(uint32_t playerId, uint16_t channelId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if(channelId == CHANNEL_CAST && (player->isLiveCasting() || player->isSpectating())) {
		player->sendChannel(CHANNEL_CAST, "Live Cast");
		return;
	}

	g_chat->removeUserFromChannel(*player, channelId);
}

void Game::playerOpenPrivateChannel(uint32_t playerId, std::string receiver)
{
	Player* player = getPlayerByID(playerId);
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	SpectatorVec spectators;
	map.getSpectators(spectators, player->getPosition());
	for (Creature* spectator : spectators) {
		if (Npc* npc = spectator->getNpc()) {
			npc->onPlayerCloseChannel(player);
		}
	}
}

void Game::playerReceivePing(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->receivePing();
}

void Game::playerAutoWalk(uint32_t playerId, const std::vector<Direction>& listDir)
{
	Player* player = getPlayerByID(playerId);
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->stopWalk();
}

void Game::playerUseItemEx(uint32_t playerId, const Position& fromPos, uint8_t fromStackPos, uint16_t fromSpriteId,
                           const Position& toPos, uint8_t toStackPos, uint16_t toSpriteId)
{
	Player* player = getPlayerByID(playerId);
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
	if (!item || !item->isUseable() || item->getClientID() != fromSpriteId) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
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

				SchedulerTask* task = createSchedulerTask(
				    static_cast<uint32_t>(getInteger(ConfigManager::RANGE_USE_ITEM_EX_INTERVAL)), ([=, this]() {
					    playerUseItemEx(playerId, itemPos, itemStackPos, fromSpriteId, toPos, toStackPos, toSpriteId);
				    }));
				player->setNextWalkActionTask(task);
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
		SchedulerTask* task = createSchedulerTask(delay, ([=, this]() {
			playerUseItemEx(playerId, fromPos, fromStackPos, fromSpriteId, toPos, toStackPos, toSpriteId);
		}));
		player->setNextActionTask(task);
		return;
	}

	player->resetIdleTime();
	player->setNextActionTask(nullptr);

	g_actions->useItemEx(player, fromPos, toPos, toStackPos, item, isHotkey);
	player->maintainAttackFlow();
}

void Game::playerUseItem(uint32_t playerId, const Position& pos, uint8_t stackPos, uint8_t index, uint16_t spriteId)
{
	Player* player = getPlayerByID(playerId);
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
	if (!item || item->isUseable() || item->getClientID() != spriteId) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
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

				SchedulerTask* task =
				    createSchedulerTask(static_cast<uint32_t>(getInteger(ConfigManager::RANGE_USE_ITEM_INTERVAL)),
				                        ([=, this]() { playerUseItem(playerId, pos, stackPos, index, spriteId); }));
				player->setNextWalkActionTask(task);
				return;
			}

			ret = RETURNVALUE_THEREISNOWAY;
		}

		player->sendCancelMessage(ret);
		return;
	}

	if (!player->canDoAction()) {
		uint32_t delay = player->getNextActionTime();
		SchedulerTask* task =
		    createSchedulerTask(delay, ([=, this]() { playerUseItem(playerId, pos, stackPos, index, spriteId); }));
		player->setNextActionTask(task);
		return;
	}

	player->resetIdleTime();
	player->setNextActionTask(nullptr);

	g_actions->useItem(player, pos, index, item, isHotkey);

	if (item->getCorpseOwner() != 0) {
		player->lootCorpse(item->getContainer());
	}
	player->maintainAttackFlow();
}

void Game::playerUseWithCreature(uint32_t playerId, const Position& fromPos, uint8_t fromStackPos, uint32_t creatureId,
                                 uint16_t spriteId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Creature* creature = getCreatureByID(creatureId);
	if (!creature) {
		return;
	}


	if (!creature->getPosition().isInRange(player->getPosition(), Map::maxClientViewportX - 1,
	                                       Map::maxClientViewportY - 1, 0)) {
		return;
	}

	bool isHotkey = (fromPos.x == 0xFFFF && fromPos.y == 0 && fromPos.z == 0);
	if (!getBoolean(ConfigManager::AIMBOT_HOTKEY_ENABLED)) {
		if (creature->getPlayer() || isHotkey) {
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
	if (!item || !item->isUseable() || item->getClientID() != spriteId) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
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

				SchedulerTask* task = createSchedulerTask(
				    static_cast<uint32_t>(getInteger(ConfigManager::RANGE_USE_WITH_CREATURE_INTERVAL)),
				    ([=, this]() { playerUseWithCreature(playerId, itemPos, itemStackPos, creatureId, spriteId); }));
				player->setNextWalkActionTask(task);
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
		SchedulerTask* task = createSchedulerTask(
		    delay, ([=, this]() { playerUseWithCreature(playerId, fromPos, fromStackPos, creatureId, spriteId); }));
		player->setNextActionTask(task);
		return;
	}

	player->resetIdleTime();
	player->setNextActionTask(nullptr);

	g_actions->useItemEx(player, fromPos, creature->getPosition(),
	                     static_cast<uint8_t>(creature->getParent()->getThingIndex(creature)), item, isHotkey,
	                     creature);
}

void Game::playerCloseContainer(uint32_t playerId, uint8_t cid)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->closeContainer(cid);
	player->sendCloseContainer(cid);
}

void Game::playerMoveUpContainer(uint32_t playerId, uint8_t cid)
{
	Player* player = getPlayerByID(playerId);
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
	Player* player = getPlayerByID(playerId);
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
	Player* player = getPlayerByID(playerId);
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

			SchedulerTask* task =
			    createSchedulerTask(static_cast<uint32_t>(getInteger(ConfigManager::RANGE_ROTATE_ITEM_INTERVAL)),
			                        ([=, this]() { playerRotateItem(playerId, pos, stackPos, spriteId); }));
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	g_events->eventPlayerOnRotateItem(player, item);
}

void Game::playerWriteItem(uint32_t playerId, uint32_t windowTextId, std::string_view text)
{
	Player* player = getPlayerByID(playerId);
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
	Player* player = getPlayerByID(playerId);
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Player* tradePartner = getPlayerByID(tradePlayerId);
	if (!tradePartner || tradePartner == player) {
		player->sendCancelMessage("Select a player to trade with.");
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

			SchedulerTask* task = createSchedulerTask(RANGE_REQUEST_TRADE_INTERVAL, ([=, this]() {
				playerRequestTrade(playerId, pos, stackPos, tradePlayerId, spriteId);
			}));
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	Container* tradeItemContainer = tradeItem->getContainer();
	if (tradeItemContainer) {
		for (const auto& it : tradeItems) {
			Item* item = it.first;
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
		for (const auto& it : tradeItems) {
			Item* item = it.first;
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
	if (player->tradeState != TRADE_NONE &&
	    !(player->tradeState == TRADE_ACKNOWLEDGE && player->tradePartner == tradePartner)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREALREADYTRADING);
		return false;
	} else if (tradePartner->tradeState != TRADE_NONE && tradePartner->tradePartner != player) {
		player->sendCancelMessage(RETURNVALUE_THISPLAYERISALREADYTRADING);
		return false;
	}

	player->tradePartner = tradePartner;
	player->tradeItem = tradeItem;
	player->tradeState = TRADE_INITIATED;
	tradeItem->incrementReferenceCounter();
	tradeItems[tradeItem] = player->getID();

	player->sendTradeItemRequest(player->getName(), tradeItem, true);

	if (tradePartner->tradeState == TRADE_NONE) {
		tradePartner->sendTextMessage(MESSAGE_EVENT_ADVANCE,
		                              fmt::format("{:s} wants to trade with you.", player->getName()));
		tradePartner->tradeState = TRADE_ACKNOWLEDGE;
		tradePartner->tradePartner = player;
	} else {
		Item* counterOfferItem = tradePartner->tradeItem;
		player->sendTradeItemRequest(tradePartner->getName(), counterOfferItem, false);
		tradePartner->sendTradeItemRequest(player->getName(), tradeItem, false);
	}

	return true;
}

void Game::playerAcceptTrade(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!(player->getTradeState() == TRADE_ACKNOWLEDGE || player->getTradeState() == TRADE_INITIATED)) {
		return;
	}

	Player* tradePartner = player->tradePartner;
	if (!tradePartner) {
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

		Item* playerTradeItem = player->tradeItem;
		Item* partnerTradeItem = tradePartner->tradeItem;

		if (!g_events->eventPlayerOnTradeAccept(player, tradePartner, playerTradeItem, partnerTradeItem)) {
			internalCloseTrade(player, false);
			return;
		}

		player->setTradeState(TRADE_TRANSFER);
		tradePartner->setTradeState(TRADE_TRANSFER);

		auto it = tradeItems.find(playerTradeItem);
		if (it != tradeItems.end()) {
			ReleaseItem(it->first);
			tradeItems.erase(it);
		}

		it = tradeItems.find(partnerTradeItem);
		if (it != tradeItems.end()) {
			ReleaseItem(it->first);
			tradeItems.erase(it);
		}

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

			if (tradePartner->tradeItem) {
				errorDescription = getTradeErrorDescription(tradePartnerRet, playerTradeItem);
				tradePartner->sendTextMessage(MESSAGE_EVENT_ADVANCE, errorDescription);
				tradePartner->tradeItem->onTradeEvent(ON_TRADE_CANCEL, tradePartner);
			}

			if (player->tradeItem) {
				errorDescription = getTradeErrorDescription(playerRet, partnerTradeItem);
				player->sendTextMessage(MESSAGE_EVENT_ADVANCE, errorDescription);
				player->tradeItem->onTradeEvent(ON_TRADE_CANCEL, player);
			}
		}

		g_events->eventPlayerOnTradeCompleted(player, tradePartner, playerTradeItem, partnerTradeItem, isSuccess);

		player->setTradeState(TRADE_NONE);
		player->tradeItem = nullptr;
		player->tradePartner = nullptr;
		player->sendTradeClose();

		tradePartner->setTradeState(TRADE_NONE);
		tradePartner->tradeItem = nullptr;
		tradePartner->tradePartner = nullptr;
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Player* tradePartner = player->tradePartner;
	if (!tradePartner) {
		return;
	}

	Item* tradeItem;
	if (lookAtCounterOffer) {
		tradeItem = tradePartner->getTradeItem();
	} else {
		tradeItem = player->getTradeItem();
	}

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
		for (Item* item : container->getItemList()) {
			Container* tmpContainer = item->getContainer();
			if (tmpContainer) {
				containers.push_back(tmpContainer);
			}

			if (--index == 0) {
				g_events->eventPlayerOnLookInTrade(player, tradePartner, item, lookDistance);
				return;
			}
		}
	}
}

void Game::playerCloseTrade(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	internalCloseTrade(player);
}

void Game::internalCloseTrade(Player* player, bool sendCancel /* = true*/)
{
	Player* tradePartner = player->tradePartner;
	if ((tradePartner && tradePartner->getTradeState() == TRADE_TRANSFER) ||
	    player->getTradeState() == TRADE_TRANSFER) {
		return;
	}

	if (player->getTradeItem()) {
		auto it = tradeItems.find(player->getTradeItem());
		if (it != tradeItems.end()) {
			ReleaseItem(it->first);
			tradeItems.erase(it);
		}

		player->tradeItem->onTradeEvent(ON_TRADE_CANCEL, player);
		player->tradeItem = nullptr;
	}

	player->setTradeState(TRADE_NONE);
	player->tradePartner = nullptr;

	if (sendCancel) {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, "Trade cancelled.");
	}
	player->sendTradeClose();

	if (tradePartner) {
		if (tradePartner->getTradeItem()) {
			auto it = tradeItems.find(tradePartner->getTradeItem());
			if (it != tradeItems.end()) {
				ReleaseItem(it->first);
				tradeItems.erase(it);
			}

			tradePartner->tradeItem->onTradeEvent(ON_TRADE_CANCEL, tradePartner);
			tradePartner->tradeItem = nullptr;
		}

		tradePartner->setTradeState(TRADE_NONE);
		tradePartner->tradePartner = nullptr;

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

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	int32_t onBuy, onSell;

	Npc* merchant = player->getShopOwner(onBuy, onSell);
	if (!merchant) {
		return;
	}

	const ItemType& it = Item::items.getItemIdByClientId(spriteId);
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

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	int32_t onBuy, onSell;

	Npc* merchant = player->getShopOwner(onBuy, onSell);
	if (!merchant) {
		return;
	}

	const ItemType& it = Item::items.getItemIdByClientId(spriteId);
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->closeShopWindow();
}

void Game::playerLookInShop(uint32_t playerId, uint16_t spriteId, uint8_t count)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	int32_t onBuy, onSell;

	Npc* merchant = player->getShopOwner(onBuy, onSell);
	if (!merchant) {
		return;
	}

	const ItemType& it = Item::items.getItemIdByClientId(spriteId);
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Thing* thing = internalGetThing(player, pos, stackPos, 0, STACKPOS_LOOK);
	if (!thing) {
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
	Player* player = getPlayerByID(playerId);
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	playerSetAttackedCreature(playerId, 0);
	playerFollowCreature(playerId, 0);
	player->stopWalk();
}

void Game::playerSetAttackedCreature(uint32_t playerId, uint32_t creatureId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (player->getAttackedCreature() && creatureId == 0) {
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

	ReturnValue ret = Combat::canTargetCreature(player, attackCreature);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
		player->sendCancelTarget();
		player->setAttackedCreature(nullptr);
		return;
	}

	player->setAttackedCreature(attackCreature);
	g_dispatcher.addTask([this, id = player->getID()]() { updateCreatureWalk(id); });
}

void Game::playerFollowCreature(uint32_t playerId, uint32_t creatureId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->setAttackedCreature(nullptr);
	g_dispatcher.addTask([this, id = player->getID()]() { updateCreatureWalk(id); });
	player->setFollowCreature(getCreatureByID(creatureId));
}

void Game::playerSetFightModes(uint32_t playerId, fightMode_t fightMode, bool chaseMode, bool secureMode)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->setFightMode(fightMode);
	player->setChaseMode(chaseMode);
	player->setSecureMode(secureMode);
}

void Game::playerRequestAddVip(uint32_t playerId, std::string_view name)
{
	if (name.length() > PLAYER_NAME_LENGTH) {
		return;
	}

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Player* vipPlayer = getPlayerByName(name);
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->removeVIP(guid);
}

void Game::playerTurn(uint32_t playerId, Direction dir)
{
	Player* player = getPlayerByID(playerId);
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

	Player* player = getPlayerByID(playerId);
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

	Player* player = getPlayerByID(playerId);
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
		player->setCurrentMount(mount->id);
	} else {
		if (player->isMounted()) {
			player->dismount();
		}

		player->wasMounted = false;
	}

	if (player->canWear(outfit.lookType, outfit.lookAddons)) {
		player->defaultOutfit = outfit;

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
                     std::string_view text)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->resetIdleTime();

	if (playerSaySpell(player, type, text)) {
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
			if (g_luaEnvironment.loadFile("data/anti_advertising.lua") == 0) {
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

bool Game::playerSaySpell(Player* player, SpeakClasses type, std::string_view text)
{
	TalkActionResult result = g_talkActions->playerSaySpell(player, type, text);
	if (result == TalkActionResult::BREAK) {
		return true;
	}

	std::string words{text};

	result = g_spells->playerSaySpell(player, words);
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
	for (Creature* spectator : spectators) {
		if (Player* spectatorPlayer = spectator->getPlayer()) {
			if (!player->getPosition().isInRange(spectatorPlayer->getPosition(), 1, 1)) {
				spectatorPlayer->sendCreatureSay(player, TALKTYPE_WHISPER, "pspsps");
			} else {
				spectatorPlayer->sendCreatureSay(player, TALKTYPE_WHISPER, text);
			}
		}
	}

	// event method
	for (Creature* spectator : spectators) {
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

		Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_YELLTICKS, 30000, 0);
		player->addCondition(condition);
	}

	internalCreatureSay(player, TALKTYPE_YELL, boost::algorithm::to_upper_copy(std::string{text}), false);
	return true;
}

bool Game::playerSpeakTo(Player* player, SpeakClasses type, std::string_view receiver, std::string_view text)
{
	Player* toPlayer = getPlayerByName(receiver);
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
	for (Creature* spectator : spectators) {
		if (spectator->getNpc()) {
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
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendCreatureTurn(creature);
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
	for (Creature* spectator : spectators) {
		if (Player* tmpPlayer = spectator->getPlayer()) {
			if (!ghostMode || tmpPlayer->canSeeCreature(creature)) {
				tmpPlayer->sendCreatureSay(creature, type, text, pos);
			}
		}
	}

	// event method
	if (!echo) {
		for (Creature* spectator : spectators) {
			spectator->onCreatureSay(creature, type, text);
			if (creature != spectator) {
				g_events->eventCreatureOnHear(spectator, creature, text, type);
			}
		}
	}
	return true;
}

void Game::checkCreatureWalk(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (creature && !creature->isDead()) {
		creature->onWalk();
	}
}

void Game::updateCreatureWalk(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (creature && !creature->isDead()) {
		creature->goToFollowCreature();
	}
}

void Game::checkCreatureAttack(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (creature && !creature->isDead()) {
		creature->onAttacking(0);
	}
}

void Game::addCreatureCheck(Creature* creature)
{
	creature->creatureCheck = true;

	if (creature->inCheckCreaturesVector) {
		// already in a vector
		return;
	}

	creature->inCheckCreaturesVector = true;
	checkCreatureLists[uniform_random(0, EVENT_CREATURECOUNT - 1)].push_back(creature);
	creature->incrementReferenceCounter();
}

void Game::removeCreatureCheck(Creature* creature)
{
	if (creature->inCheckCreaturesVector) {
		creature->creatureCheck = false;
	}
}

void Game::checkCreatures(size_t index)
{
	g_scheduler.addEvent(createSchedulerTask(EVENT_CHECK_CREATURE_INTERVAL,
	                                         ([=, this]() { checkCreatures((index + 1) % EVENT_CREATURECOUNT); })));

	auto& checkCreatureList = checkCreatureLists[index];
	auto it = checkCreatureList.begin(), end = checkCreatureList.end();
	while (it != end) {
		Creature* creature = *it;
		if (creature->creatureCheck) {
			if (!creature->isDead()) {
				creature->onThink(EVENT_CREATURE_THINK_INTERVAL);
				creature->onAttacking(EVENT_CREATURE_THINK_INTERVAL);
				creature->executeConditions(EVENT_CREATURE_THINK_INTERVAL);
			}
			++it;
		} else {
			creature->inCheckCreaturesVector = false;
			it = checkCreatureList.erase(it);
			ReleaseCreature(creature);
		}
	}

	cleanup();

#ifdef STATS_ENABLED
	g_stats.playersOnline = getPlayersOnline();
#endif
}

void Game::changeSpeed(Creature* creature, int32_t varSpeedDelta)
{
	int32_t varSpeed = creature->getSpeed() - creature->getBaseSpeed();
	varSpeed += varSpeedDelta;

	creature->setSpeed(varSpeed);

	// send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), false, true);
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendChangeSpeed(creature, creature->getStepSpeed());
	}
}

void Game::setCreatureSpeed(Creature* creature, int32_t speed)
{
	creature->setBaseSpeed(speed);

	//send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), false, true);
	for (Creature* spectator : spectators) {
		spectator->getPlayer()->sendChangeSpeed(creature, creature->getStepSpeed());
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
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendCreatureChangeOutfit(creature, outfit);
	}
}

void Game::internalCreatureChangeVisible(Creature* creature, bool visible)
{
	// send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendCreatureChangeVisible(creature, visible);
	}
}

void Game::changeLight(const Creature* creature)
{
	// send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendCreatureLight(creature);
	}
}

bool Game::combatBlockHit(CombatDamage& damage, Creature* attacker, Creature* target, bool checkDefense,
                          bool checkArmor, bool field, bool ignoreResistances /*= false */)
{
	if (damage.primary.type == COMBAT_NONE && damage.secondary.type == COMBAT_NONE) {
		return true;
	}

	if (target->getPlayer() && target->isInGhostMode()) {
		return true;
	}

	static const auto sendBlockEffect = [this](BlockType_t blockType, CombatType_t combatType,
	                                           const Position& targetPos) {
		if (blockType == BLOCK_DEFENSE) {
			addMagicEffect(targetPos, CONST_ME_POFF);
		} else if (blockType == BLOCK_ARMOR) {
			addMagicEffect(targetPos, CONST_ME_BLOCKHIT);
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
			addMagicEffect(targetPos, hitEffect);
		}
	};

	BlockType_t primaryBlockType, secondaryBlockType;
	if (damage.primary.type != COMBAT_NONE) {
		damage.primary.value = std::abs(damage.primary.value);
		primaryBlockType = target->blockHit(attacker, damage.primary.type, damage.primary.value, checkDefense,
		                                    checkArmor, field, ignoreResistances);

		if (damage.primary.type != COMBAT_HEALING) {
			damage.primary.value = -damage.primary.value;
			sendBlockEffect(primaryBlockType, damage.primary.type, target->getPosition());
		}
	} else {
		primaryBlockType = BLOCK_NONE;
	}

	if (damage.secondary.type != COMBAT_NONE) {
		damage.secondary.value = std::abs(damage.secondary.value);
		secondaryBlockType = target->blockHit(attacker, damage.secondary.type, damage.secondary.value, false, false,
		                                      field, ignoreResistances);
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
			Item* splash = nullptr;
			switch (target->getRace()) {
				case RACE_VENOM:
					color = TEXTCOLOR_LIGHTGREEN;
					effect = CONST_ME_HITBYPOISON;
					splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_SLIME);
					break;
				case RACE_BLOOD:
					color = TEXTCOLOR_RED;
					effect = CONST_ME_DRAWBLOOD;
					if (const Tile* tile = target->getTile()) {
						if (!tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
							splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_BLOOD);
						}
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
				default:
					color = TEXTCOLOR_NONE;
					effect = CONST_ME_NONE;
					break;
			}

			if (splash) {
				internalAddItem(target->getTile(), splash, INDEX_WHEREEVER, FLAG_NOLIMIT);
				splash->startDecaying();
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

bool Game::combatChangeHealth(Creature* attacker, Creature* target, CombatDamage& damage)
{
	const Position& targetPos = target->getPosition();
	if (damage.primary.value > 0) {
		if (target->isDead()) {
			return false;
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
		target->gainHealth(attacker, damage.primary.value);
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
			auto damageString = fmt::format("{:d} hitpoint{:s}", realHealthChange, realHealthChange != 1 ? "s" : "");

			std::string spectatorMessage;

			TextMessage message;
			addAnimatedText(fmt::format("{:+d}", realHealthChange), targetPos,
			                static_cast<TextColor_t>(getInteger(ConfigManager::HEALTH_GAIN_COLOUR)));

			SpectatorVec spectators;
			map.getSpectators(spectators, targetPos, false, true);
			for (Creature* spectator : spectators) {
				assert(dynamic_cast<Player*>(spectator) != nullptr);
				Player* tmpPlayer = static_cast<Player*>(spectator);
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
				addMagicEffect(targetPos, CONST_ME_POFF);
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

		damage.primary.value = std::abs(damage.primary.value);
		damage.secondary.value = std::abs(damage.secondary.value);

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

				targetPlayer->drainMana(attacker, manaDamage);

				map.getSpectators(spectators, targetPos, true, true);
				addMagicEffect(spectators, targetPos, CONST_ME_LOSEENERGY);

				std::string spectatorMessage;

				addAnimatedText(fmt::format("{:+d}", -manaDamage), targetPos,
				                static_cast<TextColor_t>(getInteger(ConfigManager::MANA_GAIN_COLOUR)));

				for (Creature* spectator : spectators) {
				if (!spectator) { continue; }
					assert(dynamic_cast<Player*>(spectator) != nullptr);
					Player* tmpPlayer = static_cast<Player*>(spectator);
				if (!tmpPlayer) { continue; }
					if (tmpPlayer->getPosition().z != targetPos.z) {
						continue;
					}

					if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
						message.type = MESSAGE_STATUS_DEFAULT;
						message.text = fmt::format("{:s} loses {:d} mana due to your attack.",
						                           target->getNameDescription(), manaDamage);
						message.text[0] = static_cast<char>(std::toupper(message.text[0]));
					} else if (tmpPlayer == targetPlayer) {
						message.type = MESSAGE_STATUS_DEFAULT;
						if (!attacker) {
							message.text = fmt::format("You lose {:d} mana.", manaDamage);
						} else if (targetPlayer == attackerPlayer) {
							message.text = fmt::format("You lose {:d} mana due to your own attack.", manaDamage);
						} else {
							message.text = fmt::format("You lose {:d} mana due to an attack by {:s}.", manaDamage,
							                           attacker->getNameDescription());
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
				addAnimatedText(fmt::format("{:+d}", -message.primary.value), targetPos, message.primary.color);
			}
		}

		if (message.secondary.value) {
			combatGetTypeInfo(damage.secondary.type, target, message.secondary.color, hitEffect);
			if (hitEffect != CONST_ME_NONE) {
				addMagicEffect(spectators, targetPos, hitEffect);
			}

			if (message.secondary.color != TEXTCOLOR_NONE) {
				addAnimatedText(fmt::format("{:+d}", -message.secondary.value), targetPos, message.secondary.color);
			}
		}

		if (message.primary.color != TEXTCOLOR_NONE || message.secondary.color != TEXTCOLOR_NONE) {
			auto damageString = fmt::format("{:d} hitpoint{:s}", realDamage, realDamage != 1 ? "s" : "");

			std::string spectatorMessage;

			for (Creature* spectator : spectators) {
				assert(dynamic_cast<Player*>(spectator) != nullptr);
				Player* tmpPlayer = static_cast<Player*>(spectator);
				if (tmpPlayer->getPosition().z != targetPos.z) {
					continue;
				}

				if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
					message.type = MESSAGE_STATUS_DEFAULT;
					message.text =
					    fmt::format("{:s} loses {:s} due to your attack.", target->getNameDescription(), damageString);
					message.text[0] = static_cast<char>(std::toupper(message.text[0]));
				} else if (tmpPlayer == targetPlayer) {
					message.type = MESSAGE_STATUS_DEFAULT;
					if (!attacker) {
						message.text = fmt::format("You lose {:s}.", damageString);
					} else if (targetPlayer == attackerPlayer) {
						message.text = fmt::format("You lose {:s} due to your own attack.", damageString);
					} else {
						message.text = fmt::format("You lose {:s} due to an attack by {:s}.", damageString,
						                           attacker->getNameDescription());
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
				if (rewardBossTracking.find(monsterId) == rewardBossTracking.end()) {
					rewardBossTracking[monsterId] = RewardBossContributionInfo();
				}
				if (attacker->getPlayer()) {
					uint32_t playerGuid = attacker->getPlayer()->getGUID();
					rewardBossTracking[monsterId].playerScoreTable[playerGuid].damageDone += realDamage * ConfigManager::getFloat(ConfigManager::REWARD_RATE_DAMAGE_DONE);
				}
			}
			// rewardboss boss attacking player
			if (attacker && attacker->getMonster() && attacker->getMonster()->isRewardBoss()) {
				uint32_t monsterId = attacker->getMonster()->getID();
				if (rewardBossTracking.find(monsterId) == rewardBossTracking.end()) {
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

		target->drainHealth(attacker, realDamage);
		addCreatureHealth(spectators, target);
	}

	return true;
}

bool Game::combatChangeMana(Creature* attacker, Creature* target, CombatDamage& damage)
{
	Player* targetPlayer = target->getPlayer();
	if (!targetPlayer) {
		return true;
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

		int32_t realManaChange = targetPlayer->getMana();
		targetPlayer->changeMana(manaChange);
		realManaChange = targetPlayer->getMana() - realManaChange;
	} else {
		const Position& targetPos = target->getPosition();
		if (!target->isAttackable()) {
			if (!target->isInGhostMode()) {
				addMagicEffect(targetPos, CONST_ME_POFF);
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
		BlockType_t blockType = target->blockHit(attacker, COMBAT_MANADRAIN, manaLoss);
		if (blockType != BLOCK_NONE) {
			addMagicEffect(targetPos, CONST_ME_POFF);
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

		targetPlayer->drainMana(attacker, manaLoss);

		std::string spectatorMessage;

		TextMessage message;
		addAnimatedText(fmt::format("{:+d}", manaLoss), targetPos,
		                static_cast<TextColor_t>(getInteger(ConfigManager::MANA_LOSS_COLOUR)));

		SpectatorVec spectators;
		map.getSpectators(spectators, targetPos, false, true);
		for (Creature* spectator : spectators) {
			assert(dynamic_cast<Player*>(spectator) != nullptr);
			Player* tmpPlayer = static_cast<Player*>(spectator);
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
	addCreatureHealth(spectators, target);
}

void Game::addCreatureHealth(const SpectatorVec& spectators, const Creature* target)
{
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendCreatureHealth(target);
	}
}

void Game::addAnimatedText(std::string_view message, const Position& pos, TextColor_t color)
{
	if (message.empty()) {
		return;
	}

	SpectatorVec spectators;
	map.getSpectators(spectators, pos, true, true);
	addAnimatedText(spectators, message, pos, color);
}

void Game::addAnimatedText(const SpectatorVec& spectators, std::string_view message, const Position& pos,
                           TextColor_t color)
{
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendAnimatedText(message, pos, color);
	}
}

void Game::addMagicEffect(const Position& pos, uint16_t effect)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, pos, true, true);
	addMagicEffect(spectators, pos, effect);
}

void Game::addMagicEffect(const SpectatorVec& spectators, const Position& pos, uint16_t effect)
{
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendMagicEffect(pos, effect);
	}
}

void Game::addDistanceEffect(const Position& fromPos, const Position& toPos, uint16_t effect)
{
	SpectatorVec spectators, toPosSpectators;
	map.getSpectators(spectators, fromPos, true, true);
	map.getSpectators(toPosSpectators, toPos, true, true);
	spectators.addSpectators(toPosSpectators);

	addDistanceEffect(spectators, fromPos, toPos, effect);
}

void Game::addDistanceEffect(const SpectatorVec& spectators, const Position& fromPos, const Position& toPos,
                             uint16_t effect)
{
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendDistanceShoot(fromPos, toPos, effect);
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
	if (!item) {
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
	if (duration > 0) {
		g_decay.startDecay(item, duration);
	} else {
		internalDecayItem(item);
	}
}

void Game::stopDecay(Item* item)
{
	if (item->hasAttribute(ITEM_ATTRIBUTE_DECAYSTATE)) {
		if (item->hasAttribute(ITEM_ATTRIBUTE_DURATION_TIMESTAMP)) {
			g_decay.stopDecay(item, item->getIntAttr(ITEM_ATTRIBUTE_DURATION_TIMESTAMP));
			item->removeAttribute(ITEM_ATTRIBUTE_DURATION_TIMESTAMP);
		} else {
			item->removeAttribute(ITEM_ATTRIBUTE_DECAYSTATE);
		}
	}
}

void Game::internalDecayItem(Item* item)
{
	const ItemType& it = Item::items[item->getID()];
	const int32_t decayTo = it.decayTo;
	if (decayTo > 0) {
		transformItem(item, decayTo);
	} else {
		ReturnValue ret = internalRemoveItem(item);
		if (ret != RETURNVALUE_NOERROR) {
			LOG_ERROR(fmt::format("[Debug - Game::internalDecayItem] internalDecayItem failed, error code: {}, item id: {}", static_cast<uint32_t>(ret), item->getID()));
		}
	}
}

void Game::checkLight()
{
	g_scheduler.addEvent(createSchedulerTask(EVENT_LIGHTINTERVAL, [this]() { checkLight(); }));
	uint8_t previousLightLevel = lightLevel;
	updateWorldLightLevel();

	if (previousLightLevel != lightLevel) {
		LightInfo lightInfo = getWorldLightInfo();

		for (const auto& it : players) {
			it.second->sendWorldLight(lightInfo);
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
	worldTime = (timeInfo.tm_sec + (timeInfo.tm_min * 60)) / 2.5f;
}

void Game::shutdown()
{
	LOG_INFO("Shutting down...");

	g_scheduler.shutdown();
	g_databaseTasks.shutdown();
	g_dispatcher.shutdown();
#ifdef STATS_ENABLED
	g_stats.shutdown();
#endif
	map.spawns.clear();
	raids.clear();

	cleanup();

	if (serviceManager) {
		serviceManager->stop();
	}

	ConnectionManager::getInstance().closeAll();

	LOG_INFO(">> Shutdown complete.");
}

void Game::cleanup()
{
	// free memory
	for (auto creature : ToReleaseCreatures) {
		creature->decrementReferenceCounter();
	}
	ToReleaseCreatures.clear();

	for (auto item : ToReleaseItems) {
		item->decrementReferenceCounter();
	}
	ToReleaseItems.clear();
}

void Game::ReleaseCreature(Creature* creature) { ToReleaseCreatures.push_back(creature); }

void Game::ReleaseItem(Item* item) { ToReleaseItems.push_back(item); }

void Game::broadcastMessage(std::string_view text, MessageClasses type) const
{
	LOG_INFO(fmt::format("> Broadcasted message: \"{}\".", text));
	for (const auto& it : players) {
		it.second->sendTextMessage(type, text);
	}
}

void Game::updateCreatureWalkthrough(const Creature* creature)
{
	// send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		auto tmpPlayer = static_cast<Player*>(spectator);
		tmpPlayer->sendCreatureWalkthrough(creature, tmpPlayer->canWalkthroughEx(creature));
	}
}

void Game::updateKnownCreature(const Creature* creature)
{
	// send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendUpdateTileCreature(creature);
	}
}

void Game::updateCreatureEmblem(Creature* creature)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		spectator->getPlayer()->sendCreatureEmblem(creature);
	}
}

void Game::updateCreatureSkull(const Creature* creature)
{
	if (getWorldType() != WORLD_TYPE_PVP) {
		return;
	}

	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendCreatureSkull(creature);
	}
}

void Game::updatePlayerShield(Player* player)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, player->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		static_cast<Player*>(spectator)->sendCreatureShield(player);
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

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Player* invitedPlayer = getPlayerByID(invitedId);
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
		party = new Party(player);
	} else if (party->getLeader() != player) {
		return;
	}

	if (!g_events->eventPartyOnInvite(party, invitedPlayer)) {
		if (party->empty()) {
			player->setParty(nullptr);
			delete party;
		}
		return;
	}

	party->invitePlayer(*invitedPlayer);
}

void Game::playerJoinParty(uint32_t playerId, uint32_t leaderId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Player* leader = getPlayerByID(leaderId);
	if (!leader || !leader->isInviting(player)) {
		return;
	}

	Party* party = leader->getParty();
	if (!party || party->getLeader() != leader) {
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Party* party = player->getParty();
	if (!party || party->getLeader() != player) {
		return;
	}

	Player* invitedPlayer = getPlayerByID(invitedId);
	if (!invitedPlayer || !player->isInviting(invitedPlayer)) {
		return;
	}

	party->revokeInvitation(*invitedPlayer);
}

void Game::playerPassPartyLeadership(uint32_t playerId, uint32_t newLeaderId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Party* party = player->getParty();
	if (!party || party->getLeader() != player) {
		return;
	}

	Player* newLeader = getPlayerByID(newLeaderId);
	if (!newLeader || !player->isPartner(newLeader)) {
		return;
	}

	party->passPartyLeadership(newLeader);
}

void Game::playerLeaveParty(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Party* party = player->getParty();
	if (!party || (player->hasCondition(CONDITION_INFIGHT) && player->getZone() != ZONE_PROTECTION)) {
		return;
	}

	party->setSharedExperience(player, sharedExpActive);
}

void Game::sendGuildMotd(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (const auto& guild = player->getGuild()) {
		player->sendChannelMessage("Message of the Day", guild->getMotd(), TALKTYPE_CHANNEL_R1, CHANNEL_GUILD);
	}
}

void Game::kickPlayer(uint32_t playerId, bool displayEffect)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->kickPlayer(displayEffect);
}

void Game::playerReportRuleViolation(uint32_t playerId, std::string_view targetName, uint8_t reportType,
                                     uint8_t reportReason, std::string_view comment, std::string_view translation)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	g_events->eventPlayerOnReportRuleViolation(player, targetName, reportType, reportReason, comment, translation);
}

void Game::playerReportBug(uint32_t playerId, std::string_view message)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	g_events->eventPlayerOnReportBug(player, message);
}

void Game::parsePlayerNetworkMessage(uint32_t playerId, uint8_t recvByte, NetworkMessage_ptr msg)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	g_events->eventPlayerOnNetworkMessage(player, recvByte, msg);
}

void Game::parsePlayerExtendedOpcode(uint32_t playerId, uint8_t opcode, std::string_view buffer)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	for (CreatureEvent* creatureEvent : player->getCreatureEvents(CREATURE_EVENT_EXTENDED_OPCODE)) {
		creatureEvent->executeExtendedOpcode(player, opcode, buffer);
	}
}

void Game::forceAddCondition(uint32_t creatureId, Condition* condition)
{
	Creature* creature = getCreatureByID(creatureId);
	if (!creature) {
		delete condition;
		return;
	}

	creature->addCondition(condition, true);
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
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->hasModalWindowOpen(modalWindowId)) {
		return;
	}

	player->onModalWindowHandled(modalWindowId);

	// offline training, hard-coded
	if (modalWindowId == std::numeric_limits<uint32_t>::max()) {
		if (button == 1) {
			if (choice == SKILL_SWORD || choice == SKILL_AXE || choice == SKILL_CLUB || choice == SKILL_DISTANCE || choice == SKILL_MAGLEVEL) {
				BedItem* bedItem = player->getBedItem();
				if (bedItem && bedItem->sleep(player)) {
					player->setOfflineTrainingSkill(choice);
					return;
				}
			}
		} else {
			player->sendTextMessage(MESSAGE_EVENT_ADVANCE, "Offline training aborted.\nCome back when you feel tired.");
		}

		player->setBedItem(nullptr);
	} else {
		for (auto creatureEvent : player->getCreatureEvents(CREATURE_EVENT_MODALWINDOW)) {
			creatureEvent->executeModalWindow(player, modalWindowId, button, choice);
		}
	}
}

void Game::addPlayer(Player* player)
{
	const std::string& lowercase_name = boost::algorithm::to_lower_copy<std::string>(player->getName());
	mappedPlayerNames[lowercase_name] = player;
	mappedPlayerGuids[player->getGUID()] = player;
	wildcardTree.insert(lowercase_name);
	players[player->getID()] = player;
	checkPlayersRecord();
}

void Game::removePlayer(Player* player)
{
	const std::string& lowercase_name = boost::algorithm::to_lower_copy<std::string>(player->getName());
	mappedPlayerNames.erase(lowercase_name);
	mappedPlayerGuids.erase(player->getGUID());
	wildcardTree.remove(lowercase_name);
	players.erase(player->getID());
}

void Game::addNpc(Npc* npc) { npcs[npc->getID()] = npc; }

void Game::removeNpc(Npc* npc) { npcs.erase(npc->getID()); }

void Game::addMonster(Monster* monster) { monsters[monster->getID()] = monster; }

void Game::removeMonster(Monster* monster) { monsters.erase(monster->getID()); }

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

void Game::internalRemoveItems(std::vector<Item*> itemList, uint32_t amount, bool stackable)
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

BedItem* Game::getBedBySleeper(uint32_t guid) const
{
	auto it = bedSleepersMap.find(guid);
	if (it == bedSleepersMap.end()) {
		return nullptr;
	}
	return it->second;
}

void Game::setBedSleeper(BedItem* bed, uint32_t guid) { bedSleepersMap[guid] = bed; }

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
	return it->second;
}

bool Game::addUniqueItem(uint16_t uniqueId, Item* item)
{
	auto result = uniqueItems.emplace(uniqueId, item);
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
			bool result = g_actions->reload();
			if (result) LOG_INFO("Actions reloaded successfully.");
			return result;
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
			g_creatureEvents->reload();
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
			bool result = g_globalEvents->reload();
			if (result) LOG_INFO("GlobalEvents reloaded successfully.");
			return result;
		}
		case RELOAD_TYPE_ITEMS: {
			bool result = Item::items.reload();
			if (result) LOG_INFO("Items reloaded successfully.");
			return result;
		}
		case RELOAD_TYPE_MONSTERS: {
			g_monsters.reload();
			g_scripts->loadScripts("scripts/monsters", false, true);
			LOG_INFO("Monsters reloaded successfully.");
			return true;
		}
		case RELOAD_TYPE_MOUNTS: {
			bool result = mounts.reload();
			if (result) LOG_INFO("Mounts reloaded successfully.");
			return result;
		}
		case RELOAD_TYPE_MOVEMENTS: {
			bool result = g_moveEvents->reload();
			if (result) LOG_INFO("Movements reloaded successfully.");
			return result;
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
			g_spells->reload();
			g_monsters.reload();
			LOG_INFO("Spells reloaded successfully.");
			return true;
		}

		case RELOAD_TYPE_TALKACTIONS: {
			bool result = g_talkActions->reload();
			if (result) LOG_INFO("TalkActions reloaded successfully.");
			return result;
		}

		case RELOAD_TYPE_WEAPONS: {
			bool result = g_weapons->reload();
			g_weapons->loadDefaults();
			if (result) LOG_INFO("Weapons reloaded successfully.");
			return result;
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
			g_scripts->loadScripts("scripts", false, true);
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
			g_scripts->loadScripts("scripts", false, true);
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
	if (value) {
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
