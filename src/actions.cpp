// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "actions.h"

#include "bed.h"
#include "configmanager.h"
#include "container.h"
#include "enums.h"
#include "game.h"
#include "logger.h"
#include "protocolgame.h"
#include "pugicast.h"
#include "rewardchest.h"
#include "spells.h"

#include <algorithm>
#include <fmt/format.h>
#include <sstream>

extern Game g_game;
extern Spells* g_spells;
extern Actions* g_actions;

Actions::Actions() : scriptInterface("Action Interface") { scriptInterface.initState(); }

Actions::~Actions() { clear(false); }

void Actions::clearMap(ActionUseMap& map, bool fromLua)
{
	for (auto it = map.begin(); it != map.end();) {
		if (fromLua == it->second.fromLua) {
			it = map.erase(it);
		} else {
			++it;
		}
	}
}

void Actions::clear(bool fromLua)
{
	clearMap(useItemMap, fromLua);
	clearMap(uniqueItemMap, fromLua);
	clearMap(actionItemMap, fromLua);

	for (auto it = positionMap.begin(); it != positionMap.end();) {
		if (fromLua == it->second.fromLua) {
			it = positionMap.erase(it);
		} else {
			++it;
		}
	}

	reInitState(fromLua);
}

LuaScriptInterface& Actions::getScriptInterface() { return scriptInterface; }

std::string_view Actions::getScriptBaseName() const { return "actions"; }

Event_ptr Actions::getEvent(std::string_view nodeName)
{
	if (!caseInsensitiveEqual(nodeName, "action")) {
		return nullptr;
	}
	return Event_ptr(new Action(&scriptInterface));
}

bool Actions::registerEvent(Event_ptr event, const pugi::xml_node& node)
{
	Action_ptr action{static_cast<Action*>(event.release())}; // event is guaranteed to be an Action

	bool success = false;
	pugi::xml_attribute attr;
	if ((attr = node.attribute("itemid"))) {
		const std::vector<int32_t>& idList = vectorAtoi(explodeString(attr.as_string(), ";"));
		for (const auto& id : idList) {
			auto result = useItemMap.emplace(static_cast<uint16_t>(id), *action);
			if (!result.second) {
				LOG_WARN(fmt::format("[Warning - Actions::registerEvent] Duplicate registered item with id: {}", id));
				continue;
			}

			success = true;
		}
	}

	if ((attr = node.attribute("uniqueid"))) {
		const std::vector<int32_t>& uidList = vectorAtoi(explodeString(attr.as_string(), ";"));
		for (const auto& uid : uidList) {
			auto result = uniqueItemMap.emplace(static_cast<uint16_t>(uid), *action);
			if (!result.second) {
				LOG_WARN(
				    fmt::format("[Warning - Actions::registerEvent] Duplicate registered item with uniqueid: {}", uid));
				continue;
			}

			success = true;
		}
	}

	if ((attr = node.attribute("actionid"))) {
		const std::vector<int32_t>& aidList = vectorAtoi(explodeString(attr.as_string(), ";"));
		for (const auto& aid : aidList) {
			auto result = actionItemMap.emplace(static_cast<uint16_t>(aid), *action);
			if (!result.second) {
				LOG_WARN(
				    fmt::format("[Warning - Actions::registerEvent] Duplicate registered item with actionid: {}", aid));
				continue;
			}

			success = true;
		}
	}

	if ((attr = node.attribute("fromid"))) {
		if (const auto& toIdAttribute = node.attribute("toid")) {
			uint16_t fromId = pugi::cast<uint16_t>(attr.value());
			uint16_t iterId = fromId;
			uint16_t toId = pugi::cast<uint16_t>(toIdAttribute.value());
			for (; iterId <= toId; iterId++) {
				if (!useItemMap.emplace(iterId, *action).second) {
					LOG_WARN(fmt::format(
					    "[Warning - Actions::registerEvent] Duplicate registered item with id: {} in fromid: {}, toid: {}",
					    iterId, fromId, toId));
					continue;
				}

				success = true;
			}
		} else {
			LOG_WARN(fmt::format("[Warning - Actions::registerEvent] Missing toid in fromid: {}", attr.as_string()));
		}
	}

	if ((attr = node.attribute("fromuid"))) {
		if (const auto& toUidAttribute = node.attribute("touid")) {
			uint16_t fromUid = pugi::cast<uint16_t>(attr.value());
			uint16_t iterUid = fromUid;
			uint16_t toUid = pugi::cast<uint16_t>(toUidAttribute.value());
			for (; iterUid <= toUid; iterUid++) {
				if (!uniqueItemMap.emplace(iterUid, *action).second) {
					LOG_WARN(fmt::format(
					    "[Warning - Actions::registerEvent] Duplicate registered item with unique id: {} in fromuid: {}, touid: {}",
					    iterUid, fromUid, toUid));
					continue;
				}

				success = true;
			}
		} else {
			LOG_WARN(fmt::format("[Warning - Actions::registerEvent] Missing touid in fromuid: {}", attr.as_string()));
		}
	}

	if ((attr = node.attribute("fromaid"))) {
		if (const auto& toAidAttribute = node.attribute("toaid")) {
			uint16_t fromAid = pugi::cast<uint16_t>(attr.value());
			uint16_t iterAid = fromAid;
			uint16_t toAid = pugi::cast<uint16_t>(toAidAttribute.value());
			for (; iterAid <= toAid; iterAid++) {
				if (!actionItemMap.emplace(iterAid, *action).second) {
					LOG_WARN(fmt::format(
					    "[Warning - Actions::registerEvent] Duplicate registered item with action id: {} in fromaid: {}, toaid: {}",
					    iterAid, fromAid, toAid));
					continue;
				}

				success = true;
			}
		} else {
			LOG_WARN(fmt::format("[Warning - Actions::registerEvent] Missing toaid in fromaid: {}", attr.as_string()));
		}
	}

	if (action->hasPosition()) {
		for (const auto& pos : action->getPositionList()) {
			auto result = positionMap.emplace(pos, *action);
			if (!result.second) {
				LOG_WARN(fmt::format("[Warning - Actions::registerEvent] Duplicate position {}", pos));
			} else {
				success = true;
			}
		}
	}

	return success;
}

bool Actions::registerLuaEvent(Action* event)
{
	Action_ptr action{event};

	const auto& ids = action->stealItemIdRange();
	const auto& uids = action->stealUniqueIdRange();
	const auto& aids = action->stealActionIdRange();

	bool success = false;
	for (const auto& id : ids) {
		if (!useItemMap.emplace(id, *action).second) {
			LOG_WARN(fmt::format(
			    "[Warning - Actions::registerLuaEvent] Duplicate registered item with id: {} in range from id: {}, to id: {}",
			    id, ids.front(), ids.back()));
			continue;
		}

		success = true;
	}

	for (const auto& id : uids) {
		if (!uniqueItemMap.emplace(id, *action).second) {
			LOG_WARN(fmt::format(
			    "[Warning - Actions::registerLuaEvent] Duplicate registered item with uid: {} in range from uid: {}, to uid: {}",
			    id, uids.front(), uids.back()));
			continue;
		}

		success = true;
	}

	for (const auto& id : aids) {
		if (!actionItemMap.emplace(id, *action).second) {
			LOG_WARN(fmt::format(
			    "[Warning - Actions::registerLuaEvent] Duplicate registered item with aid: {} in range from aid: {}, to aid: {}",
			    id, aids.front(), aids.back()));
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

ReturnValue Actions::internalUseItem(Player* player, const Position& pos, uint8_t index, Item* item, bool isHotkey)
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

	Action* action = getAction(item);
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
			player->setBedItem(bed);
			player->setOfflineTrainingSkill(Player::SKILL_OFFLINE_AUTO);
			BedItem* bedItem = player->getBedItem();
			if (bedItem) {
				bedItem->sleep(player);
			}
		}

		return RETURNVALUE_NOERROR;
	}

	if (Container* container = item->getContainer()) {
		if (!item->getDoor()) {
			if (const auto tile = item->getTile()) {
				if (const auto houseTile = tile->getHouseTile()) {
					const auto house = houseTile->getHouse();
					if (house && house->getProtected() && !item->getTopParent()->getCreature() &&
					    !house->canModifyItems(player)) {
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
			for (Item* subItem : container->getItemList()) {
				if (subItem->getID() == ITEM_REWARD_CONTAINER) {
					int64_t rewardDate = subItem->getIntAttr(ITEM_ATTRIBUTE_DATE);
					bool foundMatch = false;
					for (Item* rewardItem : myRewardChest.getItemList()) {
						if (rewardItem->getID() == ITEM_REWARD_CONTAINER &&
						    rewardItem->getIntAttr(ITEM_ATTRIBUTE_DATE) == rewardDate) {
							foundMatch = true;
							break;
						}
					}
					if (!foundMatch) {
						return RETURNVALUE_NOTPOSSIBLE;
					}
				}
			}
		} else if (corpseOwner != 0 && !player->canOpenCorpse(corpseOwner)) {
			return RETURNVALUE_YOUARENOTTHEOWNER;
		}

		// Reward chest
		if (RewardChest* rewardchest = container->getRewardChest()) {
			RewardChest& myRewardChest = player->getRewardChest();
			myRewardChest.setParent(rewardchest->getParent()->getTile());
			if (myRewardChest.getItemList().empty()) {
				return RETURNVALUE_REWARDCHESTEMPTY;
			}
			for (Item* rewardItem : myRewardChest.getItemList()) {
				if (rewardItem->getID() == ITEM_REWARD_CONTAINER) {
					Container* rewardContainer = rewardItem->getContainer();
					if (rewardContainer) {
						rewardContainer->setParent(&myRewardChest);
					}
				}
			}
			openContainer = &myRewardChest;
		} else if (item->getID() == ITEM_REWARD_CONTAINER) {
			RewardChest& myRewardChest = player->getRewardChest();
			int64_t rewardDate = item->getIntAttr(ITEM_ATTRIBUTE_DATE);
			for (Item* rewardItem : myRewardChest.getItemList()) {
				if (rewardItem->getID() == ITEM_REWARD_CONTAINER &&
				    rewardItem->getIntAttr(ITEM_ATTRIBUTE_DATE) == rewardDate &&
				    rewardItem->getIntAttr(ITEM_ATTRIBUTE_REWARDID) == item->getIntAttr(ITEM_ATTRIBUTE_REWARDID)) {
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
			player->sendTextWindow(item, it.maxTextLen, true);
		} else {
			player->setWriteItem(nullptr);
			player->sendTextWindow(item, 0, false);
		}

		return RETURNVALUE_NOERROR;
	}

	return RETURNVALUE_CANNOTUSETHISOBJECT;
}

static void showUseHotkeyMessage(Player* player, const Item* item, uint32_t count)
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

bool Actions::useItem(Player* player, const Position& pos, uint8_t index, Item* item, bool isHotkey)
{
	bool fastPotions = getBoolean(ConfigManager::FAST_POTIONS_ENABLED);
	const auto& fastPotionIds = ConfigManager::getFastPotionIds();

	// Check if item is in fast potion list
	bool isFastPotion = false;
	if (fastPotions && !fastPotionIds.empty()) {
		uint16_t itemId = item->getID();
		isFastPotion = std::find(fastPotionIds.begin(), fastPotionIds.end(), itemId) != fastPotionIds.end();
	}

	if (!isFastPotion) {
		if (player->hasCondition(CONDITION_EXHAUST_WEAPON, EXHAUST_OPENCONTAINER)) {
			player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
			return false;
		}
		if (!player->hasFlag(PlayerFlag_HasNoExhaustion)) {
			int32_t cooldown = getInteger(ConfigManager::ACTIONS_DELAY_INTERVAL);
			if (Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_EXHAUST_WEAPON,
			                                                      cooldown, 0, false, EXHAUST_OPENCONTAINER)) {
				player->addCondition(condition);
			}
			player->sendUseItemCooldown(cooldown);
		}
	}

	if (isHotkey) {
		uint16_t subType = item->getSubType();
		showUseHotkeyMessage(player, item,
		                     player->getItemTypeCount(item->getID(), subType != item->getItemCount() ? subType : -1));
	}

	if (!item->getDoor()) {
		if (const auto tile = item->getTile()) {
			if (const auto houseTile = tile->getHouseTile()) {
				const auto house = houseTile->getHouse();
				if (house && house->getProtected() && !item->getTopParent()->getCreature() &&
				    !house->canModifyItems(player)) {
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

bool Actions::useItemEx(Player* player, const Position& fromPos, const Position& toPos, uint8_t toStackPos, Item* item,
                        bool isHotkey, Creature* creature /* = nullptr*/)
{
	uint16_t itemId = item->getID();
	const auto& fastPotionIds = ConfigManager::getFastPotionIds();
	bool isPotion =
	    !fastPotionIds.empty() && std::find(fastPotionIds.begin(), fastPotionIds.end(), itemId) != fastPotionIds.end();

	// Check exhaust per type
	if (isPotion) {
		if (player->hasCondition(CONDITION_EXHAUST_WEAPON, EXHAUST_POTION)) {
			player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
			return false;
		}
		if (getBoolean(ConfigManager::POTION_CAN_EXHAUST_ITEM) &&
		    player->hasCondition(CONDITION_EXHAUST_WEAPON, EXHAUST_USEITEM)) {
			player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
			return false;
		}
	} else {
		if (player->hasCondition(CONDITION_EXHAUST_WEAPON, EXHAUST_USEITEM)) {
			player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
			return false;
		}
	}

	// Set exhaust conditions
	if (!player->hasFlag(PlayerFlag_HasNoExhaustion)) {
		if (isPotion) {
			int32_t potionCooldown = getInteger(ConfigManager::EXHAUST_POTION_INTERVAL);
			if (Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_EXHAUST_WEAPON,
			                                                      potionCooldown, 0, false, EXHAUST_POTION)) {
				player->addCondition(condition);
			}
			if (getBoolean(ConfigManager::POTION_CAN_EXHAUST_ITEM)) {
				int32_t itemCooldown = getInteger(ConfigManager::EX_ACTIONS_DELAY_INTERVAL);
				if (Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_EXHAUST_WEAPON,
				                                                      itemCooldown, 0, false, EXHAUST_USEITEM)) {
					player->addCondition(condition);
				}
				player->sendUseItemCooldown(itemCooldown);
			}
		} else {
			int32_t cooldown = getInteger(ConfigManager::EX_ACTIONS_DELAY_INTERVAL);
			if (Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_EXHAUST_WEAPON,
			                                                      cooldown, 0, false, EXHAUST_USEITEM)) {
				player->addCondition(condition);
			}
			player->sendUseItemCooldown(cooldown);
		}
	}

	Action* action = getAction(item);
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
		showUseHotkeyMessage(player, item,
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

bool Action::configureEvent(const pugi::xml_node& node)
{
	pugi::xml_attribute allowFarUseAttr = node.attribute("allowfaruse");
	if (allowFarUseAttr) {
		allowFarUse = allowFarUseAttr.as_bool();
	}

	pugi::xml_attribute blockWallsAttr = node.attribute("blockwalls");
	if (blockWallsAttr) {
		checkLineOfSight = blockWallsAttr.as_bool();
	}

	pugi::xml_attribute checkFloorAttr = node.attribute("checkfloor");
	if (checkFloorAttr) {
		checkFloor = checkFloorAttr.as_bool();
	}

	return true;
}

bool Action::loadFunction(const pugi::xml_attribute& attr, bool)
{
	LOG_WARN(fmt::format("[Warning - Action::loadFunction] Function \"{}\" does not exist.", attr.as_string()));
	return false;
}

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

bool Action::executeUse(Player* player, Item* item, const Position& fromPosition, Thing* target,
                        const Position& toPosition, bool isHotkey)
{
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

	Lua::pushThing(L, item);
	Lua::pushPosition(L, fromPosition);

	Lua::pushThing(L, target);
	Lua::pushPosition(L, toPosition);

	Lua::pushBoolean(L, isHotkey);
	return scriptInterface->callFunction(6);
}
