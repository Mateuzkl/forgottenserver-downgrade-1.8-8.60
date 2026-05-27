// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "movement.h"

#include "game.h"
#include "game.h"
#include "pugicast.h"
#include "logger.h"
#include <fmt/format.h>

extern Game g_game;
extern Vocations g_vocations;

MoveEvents::MoveEvents() : scriptInterface("MoveEvents Interface") { scriptInterface.initState(); }

MoveEvents::~MoveEvents() { clear(false); }

void MoveEvents::clearMap(MoveListMap& map, bool fromLua)
{
	for (auto it = map.begin(); it != map.end(); ++it) {
		for (int eventType = MOVE_EVENT_STEP_IN; eventType < MOVE_EVENT_LAST; ++eventType) {
			auto& moveEvents = it->second.moveEvent[eventType];
			std::erase_if(moveEvents, [fromLua](const auto& moveEvent) {
				return !(fromLua && moveEvent.fromItem) && (fromLua == moveEvent.fromLua || moveEvent.fromItem);
			});
		}
	}
}

void MoveEvents::clearPosMap(MovePosListMap& map, bool fromLua)
{
	for (auto it = map.begin(); it != map.end(); ++it) {
		for (int eventType = MOVE_EVENT_STEP_IN; eventType < MOVE_EVENT_LAST; ++eventType) {
			auto& moveEvents = it->second.moveEvent[eventType];
			std::erase_if(moveEvents, [fromLua](const auto& moveEvent) {
				return !(fromLua && moveEvent.fromItem) && (fromLua == moveEvent.fromLua || moveEvent.fromItem);
			});
		}
	}
}

void MoveEvents::clear(bool fromLua)
{
	clearMap(itemIdMap, fromLua);
	clearMap(actionIdMap, fromLua);
	clearMap(uniqueIdMap, fromLua);
	clearMap(zoneIdMap, fromLua);
	clearPosMap(positionMap, fromLua);

	reInitState(fromLua);
}

LuaScriptInterface& MoveEvents::getScriptInterface() { return scriptInterface; }

std::string_view MoveEvents::getScriptBaseName() const { return "movements"; }

bool MoveEvents::registerLuaFunction(MoveEvent* event)
{
	MoveEvent_ptr moveEvent{event};

	const auto& ids = moveEvent->stealItemIdRange();
	moveEvent->clearUniqueIdRange();
	moveEvent->clearActionIdRange();
	moveEvent->clearZoneIdRange();
	moveEvent->clearPosList();

	if (ids.empty()) {
		LOG_WARN("[Warning - MoveEvents::registerLuaFunction] No itemid specified.");
		return false;
	}

	const MoveEvent_t eventType = moveEvent->getEventType();
	if (eventType == MOVE_EVENT_ADD_ITEM || eventType == MOVE_EVENT_REMOVE_ITEM) {
		if (moveEvent->getTileItem()) {
			moveEvent->setEventType(eventType == MOVE_EVENT_ADD_ITEM ? MOVE_EVENT_ADD_ITEM_ITEMTILE
			                                                         : MOVE_EVENT_REMOVE_ITEM_ITEMTILE);
		}
	}

	if ((eventType == MOVE_EVENT_EQUIP || eventType == MOVE_EVENT_DEEQUIP) && moveEvent->getSlot() == SLOTP_WHEREEVER) {
		ItemType& it = Item::items.getItemType(ids.front());
		moveEvent->setSlot(it.slotPosition);
	}

	for (const auto& id : ids) {
		if (moveEvent->getEventType() == MOVE_EVENT_EQUIP) {
			ItemType& it = Item::items.getItemType(id);
			it.wieldInfo = moveEvent->getWieldInfo();
			it.minReqLevel = moveEvent->getReqLevel();
			it.minReqMagicLevel = moveEvent->getReqMagLv();
			it.vocationString = moveEvent->getVocationString();
		}
		addEvent(*moveEvent, id, itemIdMap);
	}
	return true;
}

bool MoveEvents::registerLuaEvent(MoveEvent* event)
{
	MoveEvent_ptr moveEvent{event};

	const auto& ids = moveEvent->stealItemIdRange();
	const auto& uids = moveEvent->stealUniqueIdRange();
	const auto& aids = moveEvent->stealActionIdRange();
	const auto& zoneIds = moveEvent->stealZoneIdRange();
	const auto& poss = moveEvent->stealPosList();

	if (ids.empty() && uids.empty() && aids.empty() && zoneIds.empty() && poss.empty()) {
		LOG_WARN("[Warning - MoveEvents::registerLuaEvent] No itemid, uniqueid, actionid, zoneid or pos specified.");
		return false;
	}

	const MoveEvent_t eventType = moveEvent->getEventType();
	if (eventType == MOVE_EVENT_ADD_ITEM || eventType == MOVE_EVENT_REMOVE_ITEM) {
		if (moveEvent->getTileItem()) {
			moveEvent->setEventType(eventType == MOVE_EVENT_ADD_ITEM ? MOVE_EVENT_ADD_ITEM_ITEMTILE
			                                                         : MOVE_EVENT_REMOVE_ITEM_ITEMTILE);
		}
	}

	if (!ids.empty() && (eventType == MOVE_EVENT_EQUIP || eventType == MOVE_EVENT_DEEQUIP) &&
	    moveEvent->getSlot() == SLOTP_WHEREEVER) {
		ItemType& it = Item::items.getItemType(ids.front());
		moveEvent->setSlot(it.slotPosition);
	}

	for (const auto& id : ids) {
		if (moveEvent->getEventType() == MOVE_EVENT_EQUIP) {
			ItemType& it = Item::items.getItemType(id);
			it.wieldInfo = moveEvent->getWieldInfo();
			it.minReqLevel = moveEvent->getReqLevel();
			it.minReqMagicLevel = moveEvent->getReqMagLv();
			it.vocationString = moveEvent->getVocationString();
		}
		addEvent(*moveEvent, id, itemIdMap);
	}

	for (const auto& id : uids) {
		addEvent(*moveEvent, id, uniqueIdMap);
	}

	for (const auto& id : aids) {
		addEvent(*moveEvent, id, actionIdMap);
	}

	for (const auto& id : zoneIds) {
		addEvent(*moveEvent, id, zoneIdMap);
	}

	for (const auto& pos : poss) {
		addEvent(*moveEvent, pos, positionMap);
	}
	return true;
}

void MoveEvents::addEvent(MoveEvent moveEvent, uint16_t id, MoveListMap& map)
{
	auto it = map.find(id);
	if (it == map.end()) {
		MoveEventList moveEventList;
		moveEventList.moveEvent[moveEvent.getEventType()].push_back(std::move(moveEvent));
		map[id] = moveEventList;
	} else {
		std::list<MoveEvent>& moveEventList = it->second.moveEvent[moveEvent.getEventType()];
		for (MoveEvent& existingMoveEvent : moveEventList) {
			if (existingMoveEvent.getSlot() == moveEvent.getSlot()) {
				LOG_WARN(fmt::format("[Warning - MoveEvents::addEvent] Duplicate move event found: {}", id));
			}
		}
		moveEventList.push_back(std::move(moveEvent));
	}
}

MoveEvent* MoveEvents::getEvent(Item* item, MoveEvent_t eventType, slots_t slot)
{
	uint32_t slotp;
	switch (slot) {
		case CONST_SLOT_HEAD:
			slotp = SLOTP_HEAD;
			break;
		case CONST_SLOT_NECKLACE:
			slotp = SLOTP_NECKLACE;
			break;
		case CONST_SLOT_BACKPACK:
			slotp = SLOTP_BACKPACK;
			break;
		case CONST_SLOT_ARMOR:
			slotp = SLOTP_ARMOR;
			break;
		case CONST_SLOT_RIGHT:
			slotp = SLOTP_RIGHT;
			break;
		case CONST_SLOT_LEFT:
			slotp = SLOTP_LEFT;
			break;
		case CONST_SLOT_LEGS:
			slotp = SLOTP_LEGS;
			break;
		case CONST_SLOT_FEET:
			slotp = SLOTP_FEET;
			break;
		case CONST_SLOT_AMMO:
			slotp = SLOTP_AMMO;
			break;
		case CONST_SLOT_RING:
			slotp = SLOTP_RING;
			break;
		default:
			slotp = 0;
			break;
	}

	auto it = itemIdMap.find(item->getID());
	if (it != itemIdMap.end()) {
		std::list<MoveEvent>& moveEventList = it->second.moveEvent[eventType];
		for (MoveEvent& moveEvent : moveEventList) {
			if ((moveEvent.getSlot() & slotp) != 0) {
				return &moveEvent;
			}
		}
	}
	return nullptr;
}

MoveEvent* MoveEvents::getEvent(Item* item, MoveEvent_t eventType)
{
	MoveListMap::iterator it;

	if (item->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		it = uniqueIdMap.find(item->getUniqueId());
		if (it != uniqueIdMap.end()) {
			std::list<MoveEvent>& moveEventList = it->second.moveEvent[eventType];
			if (!moveEventList.empty()) {
				return &(*moveEventList.begin());
			}
		}
	}

	if (item->hasAttribute(ITEM_ATTRIBUTE_ACTIONID)) {
		it = actionIdMap.find(item->getActionId());
		if (it != actionIdMap.end()) {
			std::list<MoveEvent>& moveEventList = it->second.moveEvent[eventType];
			if (!moveEventList.empty()) {
				return &(*moveEventList.begin());
			}
		}
	}

	it = itemIdMap.find(item->getID());
	if (it != itemIdMap.end()) {
		std::list<MoveEvent>& moveEventList = it->second.moveEvent[eventType];
		if (!moveEventList.empty()) {
			return &(*moveEventList.begin());
		}
	}
	return nullptr;
}

void MoveEvents::addEvent(MoveEvent moveEvent, const Position& pos, MovePosListMap& map)
{
	auto it = map.find(pos);
	if (it == map.end()) {
		MoveEventList moveEventList;
		moveEventList.moveEvent[moveEvent.getEventType()].push_back(std::move(moveEvent));
		map[pos] = moveEventList;
	} else {
		std::list<MoveEvent>& moveEventList = it->second.moveEvent[moveEvent.getEventType()];
		if (!moveEventList.empty()) {
			LOG_WARN(fmt::format("[Warning - MoveEvents::addEvent] Duplicate move event found: {}", pos));
		}

		moveEventList.push_back(std::move(moveEvent));
	}
}

MoveEvent* MoveEvents::getEvent(const Tile* tile, MoveEvent_t eventType)
{
	auto it = positionMap.find(tile->getPosition());
	if (it != positionMap.end()) {
		std::list<MoveEvent>& moveEventList = it->second.moveEvent[eventType];
		if (!moveEventList.empty()) {
			return &(*moveEventList.begin());
		}
	}
	return nullptr;
}

uint32_t MoveEvents::fireZoneStepEvents(Creature* creature, const Tile* tile, MoveEvent_t eventType, const Position& pos)
{
	uint32_t ret = 1;
	for (ZoneId zoneId : tile->getZoneIds()) {
		const auto it = zoneIdMap.find(zoneId);
		if (it == zoneIdMap.end()) {
			continue;
		}

		std::list<MoveEvent>& moveEventList = it->second.moveEvent[eventType];
		for (MoveEvent& moveEvent : moveEventList) {
			ret &= moveEvent.fireStepEvent(creature, nullptr, pos, zoneId);
		}
	}
	return ret;
}

uint32_t MoveEvents::onCreatureMove(Creature* creature, const Tile* tile, MoveEvent_t eventType)
{
	const Position& pos = tile->getPosition();

	uint32_t ret = 1;

	MoveEvent* moveEvent = getEvent(tile, eventType);
	if (moveEvent) {
		ret &= moveEvent->fireStepEvent(creature, nullptr, pos);
	}
	ret &= fireZoneStepEvents(creature, tile, eventType, pos);

	for (size_t i = tile->getFirstIndex(), j = tile->getLastIndex(); i < j; ++i) {
		Thing* thing = tile->getThing(i);
		if (!thing) {
			continue;
		}

		Item* tileItem = thing->getItem();
		if (!tileItem) {
			continue;
		}

		moveEvent = getEvent(tileItem, eventType);
		if (moveEvent) {
			ret &= moveEvent->fireStepEvent(creature, tileItem, pos);
		}
	}
	return ret;
}

ReturnValue MoveEvents::onPlayerEquip(Player* player, Item* item, slots_t slot, bool isCheck)
{
	MoveEvent* moveEvent = getEvent(item, MOVE_EVENT_EQUIP, slot);
	if (!moveEvent) {
		return RETURNVALUE_NOERROR;
	}
	return moveEvent->fireEquip(player, item, slot, isCheck);
}

ReturnValue MoveEvents::onPlayerDeEquip(Player* player, Item* item, slots_t slot)
{
	MoveEvent* moveEvent = getEvent(item, MOVE_EVENT_DEEQUIP, slot);
	if (!moveEvent) {
		// If the item does not have an event, we make sure to reset the slot, since some items transform into items
		// without events.
		player->setItemAbility(slot, false);
		return RETURNVALUE_NOERROR;
	}
	return moveEvent->fireEquip(player, item, slot, false);
}

uint32_t MoveEvents::onItemMove(Item* item, Tile* tile, bool isAdd)
{
	MoveEvent_t eventType1, eventType2;
	if (isAdd) {
		eventType1 = MOVE_EVENT_ADD_ITEM;
		eventType2 = MOVE_EVENT_ADD_ITEM_ITEMTILE;
	} else {
		eventType1 = MOVE_EVENT_REMOVE_ITEM;
		eventType2 = MOVE_EVENT_REMOVE_ITEM_ITEMTILE;
	}

	uint32_t ret = 1;
	MoveEvent* moveEvent = getEvent(tile, eventType1);
	if (moveEvent) {
		ret &= moveEvent->fireAddRemItem(item, nullptr, tile->getPosition());
	}

	moveEvent = getEvent(item, eventType1);
	if (moveEvent) {
		ret &= moveEvent->fireAddRemItem(item, nullptr, tile->getPosition());
	}

	for (size_t i = tile->getFirstIndex(), j = tile->getLastIndex(); i < j; ++i) {
		Thing* thing = tile->getThing(i);
		if (!thing) {
			continue;
		}

		Item* tileItem = thing->getItem();
		if (!tileItem || tileItem == item) {
			continue;
		}

		moveEvent = getEvent(tileItem, eventType2);
		if (moveEvent) {
			ret &= moveEvent->fireAddRemItem(item, tileItem, tile->getPosition());
		}
	}
	return ret;
}

MoveEvent::MoveEvent(LuaScriptInterface* interface) : Event(interface) {}

std::string_view MoveEvent::getScriptEventName() const
{
	switch (eventType) {
		case MOVE_EVENT_STEP_IN:
			return "onStepIn";
		case MOVE_EVENT_STEP_OUT:
			return "onStepOut";
		case MOVE_EVENT_EQUIP:
			return "onEquip";
		case MOVE_EVENT_DEEQUIP:
			return "onDeEquip";
		case MOVE_EVENT_ADD_ITEM:
			return "onAddItem";
		case MOVE_EVENT_REMOVE_ITEM:
			return "onRemoveItem";
		default:
			LOG_ERROR("[Error - MoveEvent::getScriptEventName] Invalid event type");
			return "";
	}
}

uint32_t MoveEvent::StepInField(Creature* creature, Item* item, const Position&)
{
	auto itemRef = item ? item->weak_from_this().lock() : nullptr;
	auto creatureRef = creature ? creature->weak_from_this().lock() : nullptr;
	auto field = std::dynamic_pointer_cast<MagicField>(itemRef);
	if (field && creatureRef && !field->isRemoved() && !creatureRef->isRemoved()) {
		if (field->getInstanceID() != creatureRef->getInstanceID()) {
			return 1;
		}
		field->onStepInField(creatureRef);
		return 1;
	}

	return static_cast<uint32_t>(LuaErrorCode::ITEM_NOT_FOUND);
}

uint32_t MoveEvent::StepOutField(Creature*, Item*, const Position&) { return 1; }

uint32_t MoveEvent::AddItemField(Item* item, Item*, const Position&)
{
	auto itemRef = item ? item->weak_from_this().lock() : nullptr;
	auto field = std::dynamic_pointer_cast<MagicField>(itemRef);
	if (field && !field->isRemoved()) {
		Tile* tile = field->getTile();
		if (!tile) {
			return 1;
		}

		if (CreatureVector* creatures = tile->getCreatures()) {
			uint32_t fieldInstance = field->getInstanceID();
			const CreatureVector creatureRefs = *creatures;
			for (const auto& creature : creatureRefs) {
				if (!creature || creature->isRemoved() || creature->getTile() != tile ||
				    creature->getInstanceID() != fieldInstance) {
					continue;
				}
				field->onStepInField(creature);
			}
		}
		return 1;
	}
	return static_cast<uint32_t>(LuaErrorCode::ITEM_NOT_FOUND);
}

uint32_t MoveEvent::RemoveItemField(Item*, Item*, const Position&) { return 1; }

ReturnValue MoveEvent::EquipItem(MoveEvent* moveEvent, Player* player, Item* item, slots_t slot, bool isCheck)
{
	if (!player->hasFlag(PlayerFlag_IgnoreWeaponCheck) && moveEvent->getWieldInfo() != 0) {
		if (!moveEvent->hasVocationEquipSet(player->getVocationId())) {
			return RETURNVALUE_YOUDONTHAVEREQUIREDPROFESSION;
		}

		if (player->getLevel() < moveEvent->getReqLevel()) {
			return RETURNVALUE_NOTENOUGHLEVEL;
		}

		if (player->getMagicLevel() < moveEvent->getReqMagLv()) {
			return RETURNVALUE_NOTENOUGHMAGICLEVEL;
		}
		
		if (moveEvent->getRequiredResets() > 0 && player->getResetCount() < moveEvent->getRequiredResets()) {
			return RETURNVALUE_NOTENOUGHRESET;
		}

		const ItemType& it = Item::items[item->getID()];
		if (it.minReqReset > 0 && player->getResetCount() < it.minReqReset) {
			return RETURNVALUE_NOTENOUGHRESET;
		}

		if (moveEvent->isPremium() && !player->isPremium()) {
			return RETURNVALUE_YOUNEEDPREMIUMACCOUNT;
		}
	}

	if (isCheck) {
		return RETURNVALUE_NOERROR;
	}

	if (player->isItemAbilityEnabled(slot)) {
		return RETURNVALUE_NOERROR;
	}

	const ItemType& it = Item::items[item->getID()];
	if (it.transformEquipTo != 0) {
		Item* newItem = g_game.transformItem(item, it.transformEquipTo);
		g_game.startDecay(newItem);
	} else {
		player->setItemAbility(slot, true);
	}

	if (!it.abilities) {
		return RETURNVALUE_NOERROR;
	}

	if (it.abilities->invisible) {
		auto condition = Condition::createCondition(static_cast<ConditionId_t>(slot), CONDITION_INVISIBLE, -1, 0);
		player->addCondition(std::move(condition));
	}

	if (it.abilities->manaShield) {
		auto condition =
		    Condition::createCondition(static_cast<ConditionId_t>(slot), CONDITION_MANASHIELD, -1, 0);
		player->addCondition(std::move(condition));
	}

	if (it.abilities->speed != 0) {
		g_game.changeSpeed(player, it.abilities->speed);
	}

	if (it.abilities->conditionSuppressions != 0) {
		player->addConditionSuppressions(it.abilities->conditionSuppressions);
		player->sendIcons();
	}

	if (it.abilities->regeneration) {
		auto condition = Condition::createCondition(static_cast<ConditionId_t>(slot), CONDITION_REGENERATION, -1, 0);

		if (it.abilities->healthGain != 0) {
			condition->setParam(CONDITION_PARAM_HEALTHGAIN, it.abilities->healthGain);
		}

		if (it.abilities->healthGainPercent != 0) {
			condition->setParam(CONDITION_PARAM_HEALTHGAINPERCENT, it.abilities->healthGainPercent);
		}

		if (it.abilities->healthTicks != 0) {
			condition->setParam(CONDITION_PARAM_HEALTHTICKS, it.abilities->healthTicks);
		}

		if (it.abilities->manaGain != 0) {
			condition->setParam(CONDITION_PARAM_MANAGAIN, it.abilities->manaGain);
		}

		if (it.abilities->manaGainPercent != 0) {
			condition->setParam(CONDITION_PARAM_MANAGAINPERCENT, it.abilities->manaGainPercent);
		}

		if (it.abilities->manaTicks != 0) {
			condition->setParam(CONDITION_PARAM_MANATICKS, it.abilities->manaTicks);
		}

		player->addCondition(std::move(condition));
	}

	// skill modifiers
	bool needUpdateSkills = false;

	for (int32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
		if (it.abilities->skills[i]) {
			needUpdateSkills = true;
			player->setVarSkill(static_cast<skills_t>(i), it.abilities->skills[i]);
		}
	}

	for (int32_t i = 0; i < COMBAT_COUNT; ++i) {
		if (it.abilities->specialMagicLevelSkill[i]) {
			player->setSpecialMagicLevelSkill(indexToCombatType(i), it.abilities->specialMagicLevelSkill[i]);
		}
	}

	for (int32_t i = SPECIALSKILL_FIRST; i <= SPECIALSKILL_LAST; ++i) {
		if (it.abilities->specialSkills[i]) {
			needUpdateSkills = true;
			player->setVarSpecialSkill(static_cast<SpecialSkills_t>(i), it.abilities->specialSkills[i]);
		}
	}

	if (needUpdateSkills) {
		player->sendSkills();
	}

	// stat modifiers
	bool needUpdateStats = false;

	for (int32_t s = STAT_FIRST; s <= STAT_LAST; ++s) {
		if (it.abilities->stats[s]) {
			needUpdateStats = true;
			player->setVarStats(static_cast<stats_t>(s), it.abilities->stats[s]);
		}

		if (it.abilities->statsPercent[s]) {
			needUpdateStats = true;
			player->setVarStats(static_cast<stats_t>(s),
			                    static_cast<int32_t>(player->getDefaultStats(static_cast<stats_t>(s)) *
			                                         ((it.abilities->statsPercent[s] - 100) / 100.f)));
		}
	}

	if (needUpdateStats) {
		player->sendStats();
		player->sendSkills();
	}

	// Apply reduceSkillLoss
	if (it.abilities->reduceSkillLoss != 0) {
		player->totalReduceSkillLoss += it.abilities->reduceSkillLoss;

	}

	// Apply dropBonus
	if (it.abilities->dropBonus != 0) {
		player->totalDropBonus += it.abilities->dropBonus;
	}

	// experience rates
	for (uint8_t e = static_cast<uint8_t>(ExperienceRateType::BASE);
	     e <= static_cast<uint8_t>(ExperienceRateType::STAMINA); ++e) {
		if (it.abilities->experienceRate[e] != 0) {
			player->addExperienceRate(static_cast<ExperienceRateType>(e), it.abilities->experienceRate[e]);
		}
	}

	return RETURNVALUE_NOERROR;
}

ReturnValue MoveEvent::DeEquipItem(MoveEvent*, Player* player, Item* item, slots_t slot, bool)
{
	if (!player->isItemAbilityEnabled(slot)) {
		return RETURNVALUE_NOERROR;
	}

	player->setItemAbility(slot, false);

	const ItemType& it = Item::items[item->getID()];
	if (it.transformDeEquipTo != 0) {
		g_game.transformItem(item, it.transformDeEquipTo);
		g_game.startDecay(item);
	}

	if (!it.abilities) {
		return RETURNVALUE_NOERROR;
	}

	if (it.abilities->invisible) {
		player->removeCondition(CONDITION_INVISIBLE, static_cast<ConditionId_t>(slot));
	}

	if (it.abilities->manaShield) {
		player->removeCondition(CONDITION_MANASHIELD, static_cast<ConditionId_t>(slot));
	}

	if (it.abilities->speed != 0) {
		g_game.changeSpeed(player, -it.abilities->speed);
	}

	if (it.abilities->conditionSuppressions != 0) {
		player->removeConditionSuppressions(it.abilities->conditionSuppressions);
		player->sendIcons();
	}

	if (it.abilities->regeneration) {
		player->removeCondition(CONDITION_REGENERATION, static_cast<ConditionId_t>(slot));
	}

	// skill modifiers
	bool needUpdateSkills = false;

	for (int32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
		if (it.abilities->skills[i] != 0) {
			needUpdateSkills = true;
			player->setVarSkill(static_cast<skills_t>(i), -it.abilities->skills[i]);
		}
	}

	for (int32_t i = 0; i < COMBAT_COUNT; ++i) {
		if (it.abilities->specialMagicLevelSkill[i] != 0) {
			player->setSpecialMagicLevelSkill(indexToCombatType(i), -it.abilities->specialMagicLevelSkill[i]);
		}
	}

	for (int32_t i = SPECIALSKILL_FIRST; i <= SPECIALSKILL_LAST; ++i) {
		if (it.abilities->specialSkills[i] != 0) {
			needUpdateSkills = true;
			player->setVarSpecialSkill(static_cast<SpecialSkills_t>(i), -it.abilities->specialSkills[i]);
		}
	}

	if (needUpdateSkills) {
		player->sendSkills();
	}

	// stat modifiers
	bool needUpdateStats = false;

	for (int32_t s = STAT_FIRST; s <= STAT_LAST; ++s) {
		if (it.abilities->stats[s]) {
			needUpdateStats = true;
			player->setVarStats(static_cast<stats_t>(s), -it.abilities->stats[s]);
		}

		if (it.abilities->statsPercent[s]) {
			needUpdateStats = true;
			player->setVarStats(static_cast<stats_t>(s),
			                    -static_cast<int32_t>(player->getDefaultStats(static_cast<stats_t>(s)) *
			                                          ((it.abilities->statsPercent[s] - 100) / 100.f)));
		}
	}

	if (needUpdateStats) {
		player->sendStats();
		player->sendSkills();
	}

	// Remove reduceSkillLoss
	if (it.abilities->reduceSkillLoss != 0) {
		player->totalReduceSkillLoss -= it.abilities->reduceSkillLoss;

	}

	// Remove dropBonus
	if (it.abilities->dropBonus != 0) {
		player->totalDropBonus -= it.abilities->dropBonus;
	}

	// experience rates
	for (uint8_t e = static_cast<uint8_t>(ExperienceRateType::BASE);
	     e <= static_cast<uint8_t>(ExperienceRateType::STAMINA); ++e) {
		if (it.abilities->experienceRate[e] != 0) {
			player->addExperienceRate(static_cast<ExperienceRateType>(e), -it.abilities->experienceRate[e]);
		}
	}

	return RETURNVALUE_NOERROR;
}

MoveEvent_t MoveEvent::getEventType() const { return eventType; }

void MoveEvent::setEventType(MoveEvent_t type) { eventType = type; }

uint32_t MoveEvent::fireStepEvent(Creature* creature, Item* item, const Position& pos, ZoneId zoneId)
{
	if (scripted) {
		return executeStep(creature, item, pos, zoneId);
	}
	return stepFunction(creature, item, pos);
}

bool MoveEvent::executeStep(Creature* creature, Item* item, const Position& pos, ZoneId zoneId)
{
	// onStepIn(creature, item, pos, fromPosition)
	// onStepOut(creature, item, pos, fromPosition)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - MoveEvent::executeStep] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);
	Lua::pushThing(L, item);
	Lua::pushPosition(L, pos);
	Lua::pushPosition(L, creature->getLastPosition());
	if (zoneId != 0) {
		lua_pushinteger(L, zoneId);
		return scriptInterface->callFunction(5);
	}

	return scriptInterface->callFunction(4);
}

ReturnValue MoveEvent::fireEquip(Player* player, Item* item, slots_t slot, bool isCheck)
{
	ReturnValue ret = RETURNVALUE_NOERROR;
	if (equipFunction) {
		ret = equipFunction(this, player, item, slot, isCheck);
	}
	if (scripted && (ret == RETURNVALUE_NOERROR) && !executeEquip(player, item, slot, isCheck)) {
		ret = RETURNVALUE_CANNOTBEDRESSED;
	}
	return ret;
}

bool MoveEvent::executeEquip(Player* player, Item* item, slots_t slot, bool isCheck)
{
	// onEquip(player, item, slot, isCheck)
	// onDeEquip(player, item, slot, isCheck)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - MoveEvent::executeEquip] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");
	Lua::pushThing(L, item);
	lua_pushinteger(L, slot);
	Lua::pushBoolean(L, isCheck);

	return scriptInterface->callFunction(4);
}

uint32_t MoveEvent::fireAddRemItem(Item* item, Item* tileItem, const Position& pos)
{
	if (scripted) {
		return executeAddRemItem(item, tileItem, pos);
	}
	return moveFunction(item, tileItem, pos);
}

bool MoveEvent::executeAddRemItem(Item* item, Item* tileItem, const Position& pos)
{
	// onAddItem(moveitem, tileitem, pos)
	// onRemoveItem(moveitem, tileitem, pos)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - MoveEvent::executeAddRemItem] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	Lua::pushThing(L, item);
	Lua::pushThing(L, tileItem);
	Lua::pushPosition(L, pos);

	return scriptInterface->callFunction(3);
}
