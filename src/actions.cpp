// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "actions.h"

#include "logger.h"
#include <fmt/format.h>

#include "bed.h"
#include "configmanager.h"
#include "container.h"
#include "enums.h"
#include "game.h"
#include "protocolgame.h"
#include "pugicast.h"
#include "spells.h"
#include "rewardchest.h"
#include "scriptmanager.h"

extern Game g_game;

Actions::Actions() : scriptInterface("Action Interface") { scriptInterface.initState(); }

Actions::~Actions() { clear(false); }

void Actions::clearMap(ActionUseMap& map, bool fromLua)
{
	std::erase_if(map, [fromLua](const auto& entry) { return fromLua == entry.second.fromLua; });
}

void Actions::clear(bool fromLua)
{
	clearMap(useItemMap, fromLua);
	clearMap(uniqueItemMap, fromLua);
	clearMap(actionItemMap, fromLua);

	std::erase_if(positionMap, [fromLua](const auto& entry) { return fromLua == entry.second.fromLua; });

	reInitState(fromLua);
}

LuaScriptInterface& Actions::getScriptInterface() { return scriptInterface; }

std::string_view Actions::getScriptBaseName() const { return "actions"; }

bool Actions::registerLuaEvent(Action* event)
{
	Action_ptr action{event};

	const auto& ids = action->stealItemIdRange();
	const auto& uids = action->stealUniqueIdRange();
	const auto& aids = action->stealActionIdRange();

	bool success = false;
	for (const auto& id : ids) {
		if (!useItemMap.emplace(id, *action).second) {
			LOG_WARN(fmt::format("[Warning - Actions::registerLuaEvent] Duplicate registered item with id: {} in range from id: {}, to id: {}", id, ids.front(), ids.back()));
			continue;
		}

		success = true;
	}

	for (const auto& id : uids) {
		if (!uniqueItemMap.emplace(id, *action).second) {
			LOG_WARN(fmt::format("[Warning - Actions::registerLuaEvent] Duplicate registered item with uid: {} in range from uid: {}, to uid: {}", id, uids.front(), uids.back()));
			continue;
		}

		success = true;
	}

	for (const auto& id : aids) {
		if (!actionItemMap.emplace(id, *action).second) {
			LOG_WARN(fmt::format("[Warning - Actions::registerLuaEvent] Duplicate registered item with aid: {} in range from aid: {}, to aid: {}", id, aids.front(), aids.back()));
			continue;
		}

		success = true;
	}

	if (action->hasPosition()) {
		for (const auto& pos : action->getPositionList()) {
			auto result = positionMap.emplace(pos, *action);
			if (!result.second) {
				LOG_WARN(fmt::format("[Warning - Actions::registerLuaEvent] Duplicate position {}", pos));
			} else {
				success = true;
			}
		}
	}

	if (!success) {
		LOG_WARN("[Warning - Actions::registerLuaEvent] There is no id / aid / uid / position set for this event");
	}
	return success;
}

ReturnValue Actions::canUse(const Player* player, const Position& pos)
{
	if (pos.x != 0xFFFF) {
		const Position& playerPos = player->getPosition();
		if (playerPos.z != pos.z) {
			return playerPos.z > pos.z ? RETURNVALUE_FIRSTGOUPSTAIRS : RETURNVALUE_FIRSTGODOWNSTAIRS;
		}

		if (!playerPos.isInRange(pos, 1, 1)) {
			return RETURNVALUE_TOOFARAWAY;
		}

		if (playerPos.z == pos.z) {
			if (const Tile* tile = g_game.map.getTile(pos)) {
				if (playerPos.y < pos.y && tile->hasProperty(CONST_PROP_ISHORIZONTAL) && tile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
					return RETURNVALUE_THEREISNOWAY;
				}

				if (playerPos.x < pos.x && tile->hasProperty(CONST_PROP_ISVERTICAL) && tile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
					return RETURNVALUE_THEREISNOWAY;
				}
			}

			if (const Tile* playerTile = player->getTile()) {
				if (pos.y < playerPos.y && playerTile->hasProperty(CONST_PROP_ISHORIZONTAL) && playerTile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
					return RETURNVALUE_THEREISNOWAY;
				}

				if (pos.x < playerPos.x && playerTile->hasProperty(CONST_PROP_ISVERTICAL) && playerTile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
					return RETURNVALUE_THEREISNOWAY;
				}
			}
		}
	}
	return RETURNVALUE_NOERROR;
}

ReturnValue Actions::canUse(const Player* player, const Position& pos, const Item* item)
{
	Action* action = getAction(item);
	if (action) {
		return action->canExecuteAction(player, pos);
	}
	return RETURNVALUE_NOERROR;
}

ReturnValue Actions::canUseFar(const Creature* creature, const Position& toPos, bool checkLineOfSight, bool checkFloor)
{
	if (toPos.x == 0xFFFF) {
		return RETURNVALUE_NOERROR;
	}

	const Position& creaturePos = creature->getPosition();
	if (checkFloor && creaturePos.z != toPos.z) {
		return creaturePos.z > toPos.z ? RETURNVALUE_FIRSTGOUPSTAIRS : RETURNVALUE_FIRSTGODOWNSTAIRS;
	}

	if (!toPos.isInRange(creaturePos, Map::maxClientViewportX - 1, Map::maxClientViewportY - 1)) {
		return RETURNVALUE_TOOFARAWAY;
	}

	if (checkLineOfSight && !g_game.canThrowObjectTo(creaturePos, toPos, checkLineOfSight, checkFloor)) {
		return RETURNVALUE_CANNOTTHROW;
	}

	return RETURNVALUE_NOERROR;
}

Action* Actions::getAction(const Item* item)
{
	if (item->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		auto it = uniqueItemMap.find(item->getUniqueId());
		if (it != uniqueItemMap.end()) {
			return &it->second;
		}
	}

	if (item->hasAttribute(ITEM_ATTRIBUTE_ACTIONID)) {
		auto it = actionItemMap.find(item->getActionId());
		if (it != actionItemMap.end()) {
			return &it->second;
		}
	}

	auto it = useItemMap.find(item->getID());
	if (it != useItemMap.end()) {
		return &it->second;
	}

	// rune items
	return g_spells->getRuneSpell(item->getID());
}

Action* Actions::getAction(const Position& pos)
{
	auto it = positionMap.find(pos);
	if (it != positionMap.end()) {
		return &it->second;
	}
	return nullptr;
}

ReturnValue Actions::internalUseItem(Player* player, const Position& pos, uint8_t index,
                                     const std::shared_ptr<Item>& item, bool isHotkey)
{
	if (Door* door = item->getDoor()) {
		if (!door->canUse(player)) {
			return RETURNVALUE_NOTPOSSIBLE;
		}
	}

	Action* posAction = getAction(pos);
	if (posAction) {
		ReturnValue ret = posAction->canExecuteAction(player, pos);
		if (ret != RETURNVALUE_NOERROR) {
			return ret;
		}
		if (posAction->isScripted()) {
			if (posAction->executeUse(player, item, pos, nullptr, pos, isHotkey)) {
				return RETURNVALUE_NOERROR;
			}
			if (item->isRemoved()) {
				return RETURNVALUE_CANNOTUSETHISOBJECT;
			}
		} else if (posAction->function && posAction->function(player, item, pos, nullptr, pos, isHotkey)) {
			return RETURNVALUE_NOERROR;
		}
		return RETURNVALUE_CANNOTUSETHISOBJECT;
	}

	Action* action = getAction(item.get());
	if (action) {
		if (action->isScripted()) {
			if (action->executeUse(player, item, pos, nullptr, pos, isHotkey)) {
				return RETURNVALUE_NOERROR;
			}

			if (item->isRemoved()) {
				return RETURNVALUE_CANNOTUSETHISOBJECT;
			}
		} else if (action->function && action->function(player, item, pos, nullptr, pos, isHotkey)) {
			return RETURNVALUE_NOERROR;
		}
	}

	if (BedItem* bed = item->getBed()) {
		if (!bed->canUse(player)) {
			if (!bed->getHouse()) {
				return RETURNVALUE_YOUCANNOTUSETHISBED;
			}

			if (!player->isPremium()) {
				return RETURNVALUE_YOUNEEDPREMIUMACCOUNT;
			}
			return RETURNVALUE_CANNOTUSETHISOBJECT;
		}

			if (bed->trySleep(player)) {
				player->setOfflineTrainingSkill(Player::SKILL_OFFLINE_AUTO);
				bed->sleep(player);
			}

		return RETURNVALUE_NOERROR;
	}

	if (Container* container = item->getContainer()) {
		if (!item->getDoor()) {
			if (const auto tile = item->getTile()) {
				if (const auto houseTile = tile->getHouseTile()) {
					const auto house = houseTile->getHouse();
					if (house && house->getProtected() && !item->getTopParent()->getCreature() && !house->canModifyItems(player)) {
						return RETURNVALUE_CANNOTMOVEITEMISPROTECTED;
					}
				}
			}
		}
		Container* openContainer;

		// depot container
		if (DepotLocker* depot = container->getDepotLocker()) {
			DepotLocker* myDepotLocker = player->getDepotLocker(depot->getDepotId());
			myDepotLocker->setParent(depot->getParent()->getTile());
			openContainer = myDepotLocker;
			player->setLastDepotId(depot->getDepotId());
		} else {
			openContainer = container;
		}

		uint32_t corpseOwner = container->getCorpseOwner();
		if (container->isRewardCorpse()) {
			RewardChest& myRewardChest = player->getRewardChest();
			for (const auto& subItem : container->getItemList()) {
				if (subItem->getID() == ITEM_REWARD_CONTAINER) {
					int64_t rewardDate = subItem->getIntAttr(ITEM_ATTRIBUTE_DATE);
					bool foundMatch = false;
					for (const auto& rewardItem : myRewardChest.getItemList()) {
						if (rewardItem->getID() == ITEM_REWARD_CONTAINER && rewardItem->getIntAttr(ITEM_ATTRIBUTE_DATE) == rewardDate) {
							foundMatch = true;
							break;
						}
					}
					if (!foundMatch) {
						return RETURNVALUE_NOTPOSSIBLE;
					}
				}
			}
		}
		else if (corpseOwner != 0 && !player->canOpenCorpse(corpseOwner)) {
			return RETURNVALUE_YOUARENOTTHEOWNER;
		}

		// Reward chest
		if (RewardChest* rewardchest = container->getRewardChest()) {
			RewardChest& myRewardChest = player->getRewardChest();
			myRewardChest.setParent(rewardchest->getParent()->getTile());
			if (myRewardChest.getItemList().empty()) {
				return RETURNVALUE_REWARDCHESTEMPTY;
			}
			for (const auto& rewardItem : myRewardChest.getItemList()) {
				if (rewardItem->getID() == ITEM_REWARD_CONTAINER) {
					Container* rewardContainer = rewardItem->getContainer();
					if (rewardContainer) {
						rewardContainer->setParent(&myRewardChest);
					}
				}
			}
			openContainer = &myRewardChest;
		}
		else if (item->getID() == ITEM_REWARD_CONTAINER) {
			RewardChest& myRewardChest = player->getRewardChest();
			int64_t rewardDate = item->getIntAttr(ITEM_ATTRIBUTE_DATE);
			for (const auto& rewardItem : myRewardChest.getItemList()) {
				if (rewardItem->getID() == ITEM_REWARD_CONTAINER && rewardItem->getIntAttr(ITEM_ATTRIBUTE_DATE) == rewardDate && rewardItem->getIntAttr(ITEM_ATTRIBUTE_REWARDID) == item->getIntAttr(ITEM_ATTRIBUTE_REWARDID)) {
					Container* rewardContainer = rewardItem->getContainer();
					if (rewardContainer) {
						rewardContainer->setParent(container->getRealParent());
						openContainer = rewardContainer;
					}
					break;
				}
			}
		}

		// open/close container
		int32_t oldContainerId = player->getContainerID(openContainer);
		if (oldContainerId == -1) {
			player->addContainer(index, openContainer);
			player->onSendContainer(openContainer);

			// Stop the loot highlight when the corpse is opened for the first time
			g_game.stopLootHighlight(openContainer);
		} else {
			player->onCloseContainer(openContainer);
			player->closeContainer(static_cast<uint8_t>(oldContainerId));
		}

		return RETURNVALUE_NOERROR;
	}

	const ItemType& it = Item::items[item->getID()];
	if (it.canReadText) {
		if (it.canWriteText) {
			player->setWriteItem(item, it.maxTextLen);
			player->sendTextWindow(item.get(), it.maxTextLen, true);
		} else {
			player->setWriteItem(nullptr);
			player->sendTextWindow(item.get(), 0, false);
		}

		return RETURNVALUE_NOERROR;
	}

	return RETURNVALUE_CANNOTUSETHISOBJECT;
}

static void showUseHotkeyMessage(Player* player, const Item* item, uint32_t count) // raw ptr: lifetime guaranteed by caller
{
	const ItemType& it = Item::items[item->getID()];
	if (!it.showCount) {
		player->sendTextMessage(MESSAGE_INFO_DESCR, fmt::format("Using one of {:s}...", item->getName()));
	} else if (count == 1) {
		player->sendTextMessage(MESSAGE_INFO_DESCR, fmt::format("Using the last {:s}...", item->getName()));
	} else {
		player->sendTextMessage(MESSAGE_INFO_DESCR,
		                        fmt::format("Using one of {:d} {:s}...", count, item->getPluralName()));
	}
}

bool Actions::useItem(Player* player, const Position& pos, uint8_t index, const std::shared_ptr<Item>& item, bool isHotkey)
{
	if (!item) [[unlikely]] {
		return false;
	}

	if (player->hasCondition(CONDITION_EXHAUST_WEAPON, EXHAUST_OPENCONTAINER)) {
		return false;
	}
	if (!player->hasFlag(PlayerFlag_HasNoExhaustion)) {
		int32_t cooldown = getInteger(ConfigManager::ACTIONS_DELAY_INTERVAL);
		if (auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_EXHAUST_WEAPON, cooldown, 0, false, EXHAUST_OPENCONTAINER)) {
			player->addCondition(std::move(condition));
		}
		player->sendUseItemCooldown(cooldown);
	}

	if (isHotkey) {
		uint16_t subType = item->getSubType();
		showUseHotkeyMessage(player, item.get(),
		                     player->getItemTypeCount(item->getID(), subType != item->getItemCount() ? subType : -1));
	}

	if (!item->getDoor()) {
		if (const auto tile = item->getTile()) {
			if (const auto houseTile = tile->getHouseTile()) {
				const auto house = houseTile->getHouse();
				if (house && house->getProtected() && !item->getTopParent()->getCreature() && !house->canModifyItems(player)) {
					player->sendCancelMessage(RETURNVALUE_CANNOTMOVEITEMISPROTECTED);
					return false;
				}
			}
		}
	}

	if (getBoolean(ConfigManager::ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS)) {
		if (const auto tile = item->getTile()) {
			if (const auto houseTile = tile->getHouseTile()) {
				if (!item->getTopParent()->getCreature() && !houseTile->getHouse()->isInvited(player)) {
					player->sendCancelMessage(RETURNVALUE_PLAYERISNOTINVITED);
					return false;
				}
			}
		}
	}

	ReturnValue ret = internalUseItem(player, pos, index, item, isHotkey);
	if (ret == RETURNVALUE_YOUCANNOTUSETHISBED) {
		g_game.internalCreatureSay(player, TALKTYPE_MONSTER_SAY, getReturnMessage(ret), false);
		return false;
	}

	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
		return false;
	}

	return true;
}

bool Actions::useItemEx(Player* player, const Position& fromPos, const Position& toPos, uint8_t toStackPos,
                        const std::shared_ptr<Item>& item, bool isHotkey, Creature* creature /* = nullptr*/)
{
	if (!item) [[unlikely]] {
		return false;
	}

	// Determine exhaust channel: runes use their own, potions/items use theirs
	bool isRune = g_spells->getRuneSpell(item->getID()) != nullptr;
	Exhaust_t exhaustType = isRune ? EXHAUST_RUNE : EXHAUST_USEITEM;

	// Check exhaust per type: potion, rune, and generic item use are independent
	if (player->hasCondition(CONDITION_EXHAUST_WEAPON, exhaustType)) {
		return false;
	}
	if (!isRune && player->hasCondition(CONDITION_EXHAUST_WEAPON, EXHAUST_POTION)) {
		return false;
	}

	// Set exhaust condition
	if (!player->hasFlag(PlayerFlag_HasNoExhaustion)) {
		int32_t cooldown = getInteger(ConfigManager::EX_ACTIONS_DELAY_INTERVAL);
		if (auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_EXHAUST_WEAPON, cooldown, 0, false, exhaustType)) {
			player->addCondition(std::move(condition));
		}
		player->sendUseItemCooldown(cooldown);
	}

	Action* action = getAction(item.get());
	if (!action) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return false;
	}

	ReturnValue ret = action->canExecuteAction(player, toPos);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
		return false;
	}

	if (isHotkey) {
		uint16_t subType = item->getSubType();
		showUseHotkeyMessage(player, item.get(),
		                     player->getItemTypeCount(item->getID(), subType != item->getItemCount() ? subType : -1));
	}

	if (action->executeUse(player, item, fromPos, action->getTarget(player, creature, toPos, toStackPos), toPos,
	                       isHotkey)) {
		return true;
	}

	if (!action->hasOwnErrorHandler()) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
	}

	return false;
}

Action::Action(LuaScriptInterface* interface) :
    Event(interface), function(nullptr), allowFarUse(false), checkFloor(true), checkLineOfSight(true)
{}

std::string_view Action::getScriptEventName() const { return "onUse"; }

ReturnValue Action::canExecuteAction(const Player* player, const Position& toPos)
{
	if (allowFarUse) {
		return g_actions->canUseFar(player, toPos, checkLineOfSight, checkFloor);
	}
	return g_actions->canUse(player, toPos);
}

Thing* Action::getTarget(Player* player, Creature* targetCreature, const Position& toPosition, uint8_t toStackPos) const
{
	if (targetCreature) {
		return targetCreature;
	}
	return g_game.internalGetThing(player, toPosition, toStackPos, 0, STACKPOS_USETARGET);
}

bool Action::executeUse(Player* player, const std::shared_ptr<Item>& item, const Position& fromPosition, Thing* target,
                        const Position& toPosition, bool isHotkey)
{
	if (!item) [[unlikely]] {
		return false;
	}

	// onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - Action::executeUse] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushThing(L, item.get());
	Lua::pushPosition(L, fromPosition, 0, player->getInstanceID());

	Lua::pushThing(L, target);
	Lua::pushPosition(L, toPosition, 0, player->getInstanceID());

	Lua::pushBoolean(L, isHotkey);
	return scriptInterface->callFunction(6);
}
