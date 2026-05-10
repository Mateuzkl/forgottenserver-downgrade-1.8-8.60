// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "bed.h"
#include "chat.h"
#include "combat.h"
#include "configmanager.h"
#include "creatureevent.h"
#include "database.h"
#include "events.h"
#include "familiar.h"
#include "game.h"
#include "house.h"
#include "iologindata.h"
#include "instance_utils.h"
#include "inbox.h"
#include "monster.h"
#include "movement.h"
#include "npc.h"
#include "party.h"
#include "rewardchest.h"
#include "scriptmanager.h"
#include "scheduler.h"
#include "logger.h"
#include <fmt/format.h>
#include "tools.h"
#include "weapons.h"

extern Game g_game;
extern Vocations g_vocations;

namespace {
void trimString(std::string& str) { boost::algorithm::trim(str); }

// std::string asLowerCaseString(const std::string& str) { return boost::algorithm::to_lower_copy<std::string>(str); }

// void toLowerCaseString(std::string& str) { boost::algorithm::to_lower(str); }

bool playerIsMonkVocation(const Vocation* vocation)
{
	return vocation && (vocation->getId() == 9 || vocation->getFromVocation() == 9);
}
} // namespace

MuteCountMap Player::muteCountMap;

uint32_t Player::playerAutoID = 0x10000000;

// storedConditionList is now a per-instance member (see player.h)

Player::Player(ProtocolGame_ptr p) : Creature(), client(std::make_shared<ProtocolSpectator>(std::move(p))), lastPing(OTSYS_TIME()), lastPong(lastPing),
	storeInbox(std::make_shared<StoreInbox>(ITEM_STORE_INBOX))
{
	storeInbox->setParent(this);
	experienceRate.fill(100);
}

Player::~Player()
{
	for (auto& item : inventory) {
		if (item) {
			item->setParent(nullptr);
			item->stopDecaying();
		}
	}

	if (storeInbox) {
		storeInbox->setParent(nullptr);
		storeInbox->stopDecaying();
	}

	setWriteItem(nullptr);
	setEditHouse(nullptr);

	depotLockerMap.clear();
	depotChests.clear();

	// clear stored conditions to prevent memory leak from IOLoginData::loadPlayer
	storedConditionList.clear();
}

void Player::setParty(Party* party)
{
	this->party = party ? party->shared_from_this() : std::shared_ptr<Party>();
}

bool Player::setVocation(uint16_t vocId)
{
	auto voc = g_vocations.getSharedVocation(vocId);
	if (!voc) {
		return false;
	}
	vocation = std::move(voc);

	updateRegeneration();
	setBaseSpeed(vocation->getBaseSpeed()); 
	updateBaseSpeed();
	g_game.changeSpeed(this, 0);
	return true;
}

bool Player::canMoveOwnItems(const Item* item) const
{
	if (isTokenLocked()) {
		return false;
	}

	if (!isTokenProtected()) {
		return true;
	}

	if (!item) {
		return true;
	}

	uint16_t itemId = item->getID();
	const auto& exceptions = ConfigManager::getTokenProtectionExceptions();
	
	if (std::find(exceptions.begin(), exceptions.end(), itemId) != exceptions.end()) {
		return true;
	}

	return false;
}

bool Player::unlockWithToken(const std::string& token)
{
	if (!tokenLocked) {
		return true;
	}

	uint32_t hash = 0;
	for (char c : token) {
		hash = ((hash * 31) + static_cast<uint8_t>(c)) % 4294967296;
	}
	std::string hashStr = fmt::format("{:08x}", hash);
	
	if (tokenHash == hashStr) {
		tokenLocked = false;
		return true;
	}
	return false;
}

bool Player::isPushable() const
{
	if (isAccountManager()) {
		return false;
	}
	if (hasFlag(PlayerFlag_CannotBePushed)) {
		return false;
	}
	return Creature::isPushable();
}

std::string Player::getDescription(int32_t lookDistance) const
{
	std::ostringstream s;
	const bool hideMonkVocation = playerIsMonkVocation(vocation.get()) &&
	                              !ConfigManager::getBoolean(ConfigManager::MONK_VOCATION_ENABLED);

	if (lookDistance == -1) {
		s << "yourself.";

		if (group->access) {
			s << " You are " << group->name << '.';
		} else if (vocation->getId() != VOCATION_NONE && !hideMonkVocation) {
			s << " You are " << vocation->getVocDescription() << " (Level " << level << ").";
		} else {
			s << " You have no vocation (Level " << level << ").";
		}

		if (ConfigManager::getBoolean(ConfigManager::RESET_SYSTEM_ENABLED) && reset > 0) {
			s << " Resets [" << reset << "].";
		}
	} else {
		s << name;
		if (!group->access) {
			s << " (Level " << level << ')';
		}
		s << '.';

		if (sex == PLAYERSEX_FEMALE) {
			s << " She";
		} else {
			s << " He";
		}

		if (group->access) {
			s << " is " << group->name << '.';
		} else if (vocation->getId() != VOCATION_NONE && !hideMonkVocation) {
			s << " is " << vocation->getVocDescription() << '.';
		} else {
			s << " has no vocation.";
		}
		if (ConfigManager::getBoolean(ConfigManager::RESET_SYSTEM_ENABLED) && reset > 0) {
			s << " Resets [" << reset << "].";
		}
	}

	if (auto p = party.lock()) {
		if (lookDistance == -1) {
			s << " Your party has ";
		} else if (sex == PLAYERSEX_FEMALE) {
			s << " She is in a party with ";
		} else {
			s << " He is in a party with ";
		}

		size_t memberCount = p->getMemberCount() + 1;
		if (memberCount == 1) {
			s << "1 member and ";
		} else {
			s << memberCount << " members and ";
		}

		size_t invitationCount = p->getInvitationCount();
		if (invitationCount == 1) {
			s << "1 pending invitation.";
		} else {
			s << invitationCount << " pending invitations.";
		}
	}

	const auto guild = getGuild();
	const auto guildRank = getGuildRank();
	if (!guild || !guildRank) {
		return s.str();
	}

	if (lookDistance == -1) {
		s << " You are ";
	} else if (sex == PLAYERSEX_FEMALE) {
		s << " She is ";
	} else {
		s << " He is ";
	}

	s << guildRank->name << " of the " << guild->getName();
	if (!guildNick.empty()) {
		s << " (" << guildNick << ')';
	}

	size_t memberCount = guild->getMemberCount();
	const auto onlineMembers = guild->getMembersOnlineRefs();
	if (memberCount == 1) {
		s << ", which has 1 member, " << onlineMembers.size() << " of them online.";
	} else {
		s << ", which has " << memberCount << " members, " << onlineMembers.size() << " of them online.";
	}
	return s.str();
}

Item* Player::getInventoryItem(slots_t slot) const
{
	if (slot < CONST_SLOT_FIRST || slot > CONST_SLOT_LAST) {
		return nullptr;
	}
	return inventory[slot].get();
}

Item* Player::getInventoryItem(uint32_t slot) const
{
	if (slot < CONST_SLOT_FIRST || slot > CONST_SLOT_LAST) {
		return nullptr;
	}
	return inventory[slot].get();
}

bool Player::hasInventoryItem(slots_t slot, const std::shared_ptr<const Item>& item) const
{
	if (!item || slot < CONST_SLOT_FIRST || slot > CONST_SLOT_LAST) {
		return false;
	}

	const std::shared_ptr<Item>& inventoryItem = inventory[slot];
	return inventoryItem && std::static_pointer_cast<const Item>(inventoryItem) == item;
}

bool Player::isInventorySlot(slots_t slot) const
{
	return slot >= CONST_SLOT_FIRST && slot <= CONST_SLOT_LAST;
}

void Player::addConditionSuppressions(uint32_t conditions) { conditionSuppressions |= conditions; }

void Player::removeConditionSuppressions(uint32_t conditions) { conditionSuppressions &= ~conditions; }

Item* Player::getWeapon(slots_t slot, bool ignoreAmmo) const
{
	Item* item = inventory[slot].get();
	if (!item) {
		return nullptr;
	}

	WeaponType_t weaponType = item->getWeaponType();
	if (weaponType == WEAPON_NONE || weaponType == WEAPON_SHIELD || weaponType == WEAPON_AMMO || weaponType == WEAPON_QUIVER) {
		return nullptr;
	}

	if (!ignoreAmmo && weaponType == WEAPON_DISTANCE) {
		const ItemType& it = Item::items[item->getID()];
		if (it.ammoType != AMMO_NONE) {
			Item* ammoItem = inventory[CONST_SLOT_AMMO].get();
			if (!ammoItem || ammoItem->getAmmoType() != it.ammoType) {
				Item* rightItem = inventory[CONST_SLOT_RIGHT].get();
				if (rightItem && rightItem->getWeaponType() == WEAPON_QUIVER) {
					Container* quiverContainer = rightItem->getContainer();
					if (quiverContainer) {
						for (ContainerIterator cit = quiverContainer->iterator(); cit.hasNext(); cit.advance()) {
							Item* quiverAmmo = *cit;
							if (quiverAmmo->getAmmoType() == it.ammoType) {
								const Weapon* quiverAmmoWeapon = g_weapons->getWeapon(quiverAmmo);
								if (quiverAmmoWeapon && quiverAmmoWeapon->ammoCheck(this)) {
									return quiverAmmo;
								}
							}
						}
					}
				}
				return nullptr;
			}
			item = ammoItem;
		}
	}
	return item;
}

Item* Player::getWeapon(bool ignoreAmmo /* = false*/) const
{
	Item* item = getWeapon(CONST_SLOT_LEFT, ignoreAmmo);
	if (item) {
		return item;
	}

	item = getWeapon(CONST_SLOT_RIGHT, ignoreAmmo);
	if (item) {
		return item;
	}
	return nullptr;
}

WeaponType_t Player::getWeaponType() const
{
	Item* item = getWeapon();
	if (!item) {
		return WEAPON_NONE;
	}
	return item->getWeaponType();
}

int32_t Player::getWeaponSkill(const Item* item) const
{
	if (!item) {
		return getSkillLevel(SKILL_FIST);
	}

	int32_t attackSkill;

	WeaponType_t weaponType = item->getWeaponType();
	switch (weaponType) {
		case WEAPON_SWORD: {
			attackSkill = getSkillLevel(SKILL_SWORD);
			break;
		}

		case WEAPON_CLUB: {
			attackSkill = getSkillLevel(SKILL_CLUB);
			break;
		}

		case WEAPON_AXE: {
			attackSkill = getSkillLevel(SKILL_AXE);
			break;
		}

		case WEAPON_FIST: {
			attackSkill = getSkillLevel(SKILL_FIST);
			break;
		}

		case WEAPON_DISTANCE: {
			attackSkill = getSkillLevel(SKILL_DISTANCE);
			break;
		}

		default: {
			attackSkill = 0;
			break;
		}
	}
	return attackSkill;
}

int32_t Player::getArmor() const
{
	int32_t armor = 0;

	static const slots_t armorSlots[] = {CONST_SLOT_HEAD, CONST_SLOT_NECKLACE, CONST_SLOT_ARMOR,
	                                     CONST_SLOT_LEGS, CONST_SLOT_FEET,     CONST_SLOT_RING};
	for (slots_t slot : armorSlots) {
		Item* inventoryItem = inventory[slot].get();
		if (inventoryItem) {
			armor += inventoryItem->getArmor();
		}
	}
	return static_cast<int32_t>(armor * vocation->armorMultiplier);
}

float Player::getMitigation() const
{
	if (!vocation || vocation->getId() == VOCATION_NONE) {
		return 0.0f;
	}

	float shieldingSkill = getSkillLevel(SKILL_SHIELD);
	float armor = getArmor();

	const Item *shield, *weapon;
	getShieldAndWeapon(shield, weapon);

	if (shield) {
		return (shieldingSkill * vocation->primaryShieldMultiplier + armor * vocation->mitigationMultiplier) / 100.0f;
	}

	return (shieldingSkill * vocation->secondaryShieldMultiplier + armor * vocation->mitigationMultiplier) / 100.0f;
}

void Player::getShieldAndWeapon(const Item*& shield, const Item*& weapon) const
{
	shield = nullptr;
	weapon = nullptr;

	for (uint32_t slot = CONST_SLOT_RIGHT; slot <= CONST_SLOT_LEFT; slot++) {
		Item* item = inventory[slot].get();
		if (!item) {
			continue;
		}

		switch (item->getWeaponType()) {
			case WEAPON_NONE:
				break;

			case WEAPON_QUIVER:
			case WEAPON_SHIELD: {
				if (!shield || item->getDefense() > shield->getDefense()) {
					shield = item;
				}
				break;
			}

			default: { // weapons that are not shields
				weapon = item;
				break;
			}
		}
	}
}

int32_t Player::getDefense() const
{
	int32_t defenseSkill = getSkillLevel(SKILL_FIST);
	int32_t defenseValue = 7;
	const Item* weapon;
	const Item* shield;
	getShieldAndWeapon(shield, weapon);

	if (weapon) {
		defenseValue = weapon->getDefense() + weapon->getExtraDefense();
		defenseSkill = getWeaponSkill(weapon);
	}

	if (shield) {
		defenseValue = weapon != nullptr ? shield->getDefense() + weapon->getExtraDefense() : shield->getDefense();
		defenseSkill = getSkillLevel(SKILL_SHIELD);
	}

	if (defenseSkill == 0) {
		switch (getFightMode()) {
			case FIGHTMODE_ATTACK:
			case FIGHTMODE_BALANCED:
				return 1;

			case FIGHTMODE_DEFENSE:
				return 2;
		}
	}

	return (defenseSkill / 4. + 2.23) * defenseValue * 0.15 * getDefenseFactor() * vocation->defenseMultiplier;
}

float Player::getAttackFactor() const
{
	switch (getFightMode()) {
		case FIGHTMODE_ATTACK:
			return 1.0f;
		case FIGHTMODE_BALANCED:
			return 1.2f;
		case FIGHTMODE_DEFENSE:
			return 2.0f;
		default:
			return 1.0f;
	}
}

float Player::getDefenseFactor() const
{
	switch (getFightMode()) {
		case FIGHTMODE_ATTACK:
			return (OTSYS_TIME() - lastAttack) < getAttackSpeed() ? 0.5f : 1.0f;
		case FIGHTMODE_BALANCED:
			return (OTSYS_TIME() - lastAttack) < getAttackSpeed() ? 0.75f : 1.0f;
		case FIGHTMODE_DEFENSE:
			return 1.0f;
		default:
			return 1.0f;
	}
}

uint16_t Player::getClientIcons() const
{
	uint16_t icons = 0;
	for (const auto& condition : conditions) {
		if (!isSuppress(condition->getType())) {
			icons |= condition->getIcons();
		}
	}

	if (pzLocked) {
		icons |= ICON_REDSWORDS;
	}

	if (const Tile* playerTile = getTile(); playerTile && playerTile->hasFlag(TILESTATE_PROTECTIONZONE)) {
		icons |= ICON_PIGEON;

		if (hasBitSet(ICON_SWORDS, icons)) {
			icons &= ~ICON_SWORDS;
		}
	}

	// Game client debugs with 10 or more icons
	// so let's prevent that from happening.
	std::bitset<20> icon_bitset(static_cast<uint64_t>(icons));
	for (size_t pos = 0, bits_set = icon_bitset.count(); bits_set >= 10; ++pos) {
		if (icon_bitset[pos]) {
			icon_bitset.reset(pos);
			--bits_set;
		}
	}
	return icon_bitset.to_ulong();
}

void Player::updateInventoryWeight()
{
	inventoryWeight = 0;

	if (hasFlag(PlayerFlag_HasInfiniteCapacity)) {
		return;
	}

	auto finishInventoryWeight = [this](uint64_t totalWeight) {
		inventoryWeight = static_cast<uint32_t>(std::min<uint64_t>(totalWeight, std::numeric_limits<uint32_t>::max()));
	};

	uint64_t totalWeight = 0;
	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		const std::shared_ptr<Item>& inventoryItem = inventory[i];
		if (!inventoryItem) {
			continue;
		}

		totalWeight += inventoryItem->getWeight();
	}

	const std::shared_ptr<const Container> backpack =
	    std::dynamic_pointer_cast<const Container>(inventory[CONST_SLOT_BACKPACK]);
	if (!backpack) {
		finishInventoryWeight(totalWeight);
		return;
	}

	const uint8_t weightReduction = backpack->getWeightReduction();
	if (weightReduction == 0) {
		finishInventoryWeight(totalWeight);
		return;
	}

	const uint64_t containedWeight = backpack->getWeightReductionContentWeight();
	const uint64_t reductionWeight = containedWeight * weightReduction / 100;

	if (reductionWeight >= totalWeight) {
		totalWeight = 0;
	} else {
		totalWeight -= reductionWeight;
	}

	finishInventoryWeight(totalWeight);
}

void Player::reloadEquipmentStats()
{
	for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
		Item* item = inventory[slot].get();
		if (!item) {
			continue;
		}
		g_moveEvents->onPlayerDeEquip(this, item, static_cast<slots_t>(slot));
	}
}

void Player::applyEquipmentStats()
{
	for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
		Item* item = inventory[slot].get();
		if (!item) {
			continue;
		}
		g_moveEvents->onPlayerEquip(this, item, static_cast<slots_t>(slot), false);
	}
	updateInventoryWeight();
	updateItemsLight();
	sendStats();
	sendSkills();
}

void Player::addSkillAdvance(skills_t skill, uint64_t count, bool artificial /*= false*/)
{
	uint64_t currReqTries = vocation->getReqSkillTries(skill, skills[skill].level);
	uint64_t nextReqTries = vocation->getReqSkillTries(skill, skills[skill].level + 1);
	if (currReqTries >= nextReqTries) {
		// player has reached max skill
		return;
	}

	g_events->eventPlayerOnGainSkillTries(this, skill, count, artificial);
	if (count == 0) {
		return;
	}

	bool sendUpdateSkills = false;
	while ((skills[skill].tries + count) >= nextReqTries) {
		count -= nextReqTries - skills[skill].tries;
		skills[skill].level++;
		skills[skill].tries = 0;
		skills[skill].percent = 0;

		sendTextMessage(MESSAGE_EVENT_ADVANCE,
		                fmt::format("You advanced to {:s} level {:d}.", getSkillName(skill), skills[skill].level));

		g_creatureEvents->playerAdvance(this, skill, (skills[skill].level - 1), skills[skill].level);

		sendUpdateSkills = true;
		currReqTries = nextReqTries;
		nextReqTries = vocation->getReqSkillTries(skill, skills[skill].level + 1);
		if (currReqTries >= nextReqTries) {
			count = 0;
			break;
		}
	}

	skills[skill].tries += count;

	uint8_t newPercent;
	if (nextReqTries > currReqTries) {
		newPercent = Player::getBasisPointLevel(skills[skill].tries, nextReqTries) / 100;
	} else {
		newPercent = 0;
	}

	if (skills[skill].percent != newPercent) {
		skills[skill].percent = newPercent;
		sendUpdateSkills = true;
	}

	if (sendUpdateSkills) {
		sendSkills();
	}
}

void Player::removeSkillTries(skills_t skill, uint64_t count, bool notify /* = false*/)
{
	uint16_t oldLevel = skills[skill].level;
	uint8_t oldPercent = skills[skill].percent;

	while (count > skills[skill].tries) {
		count -= skills[skill].tries;

		if (skills[skill].level <= MINIMUM_SKILL_LEVEL) {
			skills[skill].level = MINIMUM_SKILL_LEVEL;
			skills[skill].tries = 0;
			count = 0;
			break;
		}

		skills[skill].tries = vocation->getReqSkillTries(skill, skills[skill].level);
		skills[skill].level--;
	}

	skills[skill].tries = std::max<int32_t>(0, skills[skill].tries - count);
	skills[skill].percent =
	   Player::getBasisPointLevel(skills[skill].tries, vocation->getReqSkillTries(skill, skills[skill].level)) / 100;

	if (notify) {
		bool sendUpdateSkills = false;
		if (oldLevel != skills[skill].level) {
			sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You were downgraded to {:s} level {:d}.",
			                                                   getSkillName(skill), skills[skill].level));
			sendUpdateSkills = true;
		}

		if (sendUpdateSkills || oldPercent != skills[skill].percent) {
			sendSkills();
		}
	}
}

void Player::setVarStats(stats_t stat, int32_t modifier)
{
	varStats[stat] += modifier;

	switch (stat) {
		case STAT_MAXHITPOINTS: {
			if (getHealth() > getMaxHealth()) {
				Creature::changeHealth(getMaxHealth() - getHealth());
			} else {
				g_game.addCreatureHealth(this);
			}
			break;
		}

		case STAT_MAXMANAPOINTS: {
			if (getMana() > getMaxMana()) {
				changeMana(getMaxMana() - getMana());
			}
			break;
		}

		case STAT_CAPACITY: {
			// capacity changed; sendStats() is called by the caller (outfit/condition)
			break;
		}

		default: {
			break;
		}
	}
}

void Player::setLossSkill(bool _skillLoss) { skillLoss = _skillLoss; }

int32_t Player::getDefaultStats(stats_t stat) const
{
	switch (stat) {
		case STAT_MAXHITPOINTS:
			return healthMax;
		case STAT_MAXMANAPOINTS:
			return manaMax;
		case STAT_MAGICPOINTS:
			return getBaseMagicLevel();
		case STAT_CAPACITY:
			return static_cast<int32_t>(capacity);
		default:
			return 0;
	}
}

void Player::addContainer(uint8_t cid, Container* container)
{
	if (cid > 0xF) {
		return;
	}

	auto containerRef = g_game.getContainerSharedRef(container);
	if (!containerRef) {
		openContainers.erase(cid);
		return;
	}

	openContainers[cid] = OpenContainer{containerRef, 0};
}

void Player::closeContainer(uint8_t cid)
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return;
	}

	openContainers.erase(it);
}

void Player::setContainerIndex(uint8_t cid, uint16_t index)
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return;
	}
	it->second.index = index;
}

Container* Player::getContainerByID(uint8_t cid)
{
	auto container = getContainerByIDRef(cid);
	return container.get();
}

ContainerPtr Player::getContainerByIDRef(uint8_t cid)
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return nullptr;
	}
	return it->second.container.lock();
}

int8_t Player::getContainerID(const Container* container) const
{
	for (const auto& it : openContainers) {
		auto openContainer = it.second.container.lock();
		if (openContainer.get() == container) {
			return it.first;
		}
	}
	return -1;
}

uint16_t Player::getContainerIndex(uint8_t cid) const
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return 0;
	}
	return it->second.index;
}

bool Player::canOpenCorpse(uint32_t ownerId) const
{
	if (getID() == ownerId) return true;
	auto p = party.lock();
	return p && p->canOpenCorpse(ownerId);
}

uint16_t Player::getLookCorpse() const
{
	if (sex == PLAYERSEX_FEMALE) {
		return ITEM_FEMALE_CORPSE;
	}
	return ITEM_MALE_CORPSE;
}

void Player::setStorageValue(const uint32_t key, const std::optional<int64_t> value, const bool isSpawn /* = false*/)
{
	Creature::setStorageValue(key, value, isSpawn);
}

bool Player::canSee(const Position& pos) const
{
	if (!client) {
		return false;
	}
	return client->canSee(pos);
}

bool Player::canSeeCreature(const Creature* creature) const
{
	if (creature == this) {
		return true;
	}

	const uint32_t creatureInstanceId = creature->getInstanceID();
	if (creatureInstanceId != 0 && !compareInstance(creatureInstanceId)) {
		return false;
	}

	if (creature->isInGhostMode() && !canSeeGhostMode(creature)) {
		return false;
	}

	if (!creature->getPlayer() && !canSeeInvisibility() && creature->isInvisible()) {
		return false;
	}
	return true;
}

bool Player::canSeeGhostMode(const Creature*) const { return group->access; }

bool Player::canWalkthrough(const Creature* creature) const
{
	if (group->access || creature->isInGhostMode()) {
		return true;
	}

	const uint32_t creatureInstanceId = creature->getInstanceID();
	if (creatureInstanceId != 0 && !compareInstance(creatureInstanceId)) {
		return true;
	}

	const Player* player = creature->getPlayer();
	if (player && (isAccountManager() || player->isAccountManager())) {
		return true;
	}
	if (!player || !getBoolean(ConfigManager::ALLOW_WALKTHROUGH)) {
		return false;
	}

	const Tile* playerTile = player->getTile();
	if (!playerTile || (!playerTile->hasFlag(TILESTATE_PROTECTIONZONE) &&
	                    player->getLevel() > getInteger(ConfigManager::PROTECTION_LEVEL))) {
		return false;
	}

	const Item* playerTileGround = playerTile->getGround();
	if (!playerTileGround || !playerTileGround->hasWalkStack()) {
		return false;
	}

	return true;
}

bool Player::canWalkthroughEx(const Creature* creature) const
{
	if (group->access) {
		return true;
	}

	const uint32_t creatureInstanceId = creature->getInstanceID();
	if (creatureInstanceId != 0 && !compareInstance(creatureInstanceId)) {
		return true;
	}

	const Player* player = creature->getPlayer();
	if (!player || !getBoolean(ConfigManager::ALLOW_WALKTHROUGH)) {
		return false;
	}

	const Tile* playerTile = player->getTile();
	return playerTile && (playerTile->hasFlag(TILESTATE_PROTECTIONZONE) ||
	                      player->getLevel() <= getInteger(ConfigManager::PROTECTION_LEVEL));
}

void Player::onReceiveMail() const
{
	if (isNearDepotBox()) {
		sendTextMessage(MESSAGE_EVENT_ADVANCE, "New mail has arrived.");
	}
}

bool Player::isNearDepotBox() const
{
	const Position& pos = getPosition();
	for (int32_t cx = -1; cx <= 1; ++cx) {
		for (int32_t cy = -1; cy <= 1; ++cy) {
			Tile* tile = g_game.map.getTile(pos.x + cx, pos.y + cy, pos.z);
			if (!tile) {
				continue;
			}

			if (tile->hasFlag(TILESTATE_DEPOT)) {
				return true;
			}
		}
	}
	return false;
}

DepotChest* Player::getDepotChest(uint32_t depotId, bool autoCreate)
{
	auto it = depotChests.find(depotId);
	if (it != depotChests.end()) {
		return it->second.get();
	}

	if (!autoCreate) {
		return nullptr;
	}

	auto chest = std::make_shared<DepotChest>(ITEM_DEPOT);
	DepotChest* rawPtr = chest.get();

	depotChests.emplace(depotId, std::move(chest));
	rawPtr->setMaxDepotItems(getMaxDepotItems());
	checkDepotBoxes(rawPtr);
	return rawPtr;
}

DepotLocker* Player::getDepotLocker(uint32_t depotId)
{
	auto it = depotLockerMap.find(depotId);
	if (it != depotLockerMap.end()) {
		it->second->stopDecaying();
		return it->second.get();
	}

	it = depotLockerMap.emplace(depotId, std::make_shared<DepotLocker>(ITEM_LOCKER)).first;
	it->second->setDepotId(static_cast<uint16_t>(depotId));

	bool hasMarket = false;
	bool hasSupplyStash = false;
	bool hasInbox = false;
	for (const auto& item : it->second->getItemList()) {
		if (item->getID() == ITEM_MARKET) {
			hasMarket = true;
		} else if (item->getID() == ITEM_SUPPLY_STASH) {
			hasSupplyStash = true;
		} else if (item->getID() == ITEM_INBOX) {
			hasInbox = true;
		}
	}

	if (!hasMarket && ConfigManager::getBoolean(ConfigManager::MARKET_SYSTEM_ENABLED)) {
		auto market = Item::CreateItem(ITEM_MARKET);
		if (market) {
			it->second->internalAddThing(market.get());
		}
	}

	if (!hasSupplyStash) {
		auto supplyStash = Item::CreateItem(ITEM_SUPPLY_STASH);
		if (supplyStash) {
			it->second->internalAddThing(supplyStash.get());
		}
	}

	if (!hasInbox) {
		auto inbox = Item::CreateItem(ITEM_INBOX);
		if (inbox) {
			it->second->internalAddThing(inbox.get());
		}
	}

	DepotChest* chest = getDepotChest(depotId, true);
	it->second->internalAddThing(chest);

	return it->second.get();
}

void Player::checkDepotBoxes(DepotChest* chest)
{
	if (!chest) {
		return;
	}

	bool hasBox = false;
	for (const auto& item : chest->getItemList()) {
		if (item->getID() == ITEM_DEPOT_BOX_1) {
			hasBox = true;
			break;
		}
	}

	if (!hasBox) {
		for (uint16_t i = ITEM_DEPOT_BOX_17; i >= ITEM_DEPOT_BOX_1; --i) {
			auto box = Item::CreateItem(i);
			if (box) {
				chest->internalAddThing(box.get());
			}
		}
	}
}

RewardChest& Player::getRewardChest()
{
	if (!rewardChest) {
		rewardChest = std::make_shared<RewardChest>(ITEM_REWARD_CHEST);
	}
	return *rewardChest;
}

void Player::sendCancelMessage(ReturnValue message) const { sendCancelMessage(getReturnMessage(message)); }

void Player::sendStats()
{
	if (client) {
		client->sendStats();
		lastStatsTrainingTime = getOfflineTrainingTime() / 60 / 1000;
	}
}

void Player::sendPing()
{
	int64_t timeNow = OTSYS_TIME();

	bool hasLostConnection = false;
	if ((timeNow - lastPing) >= 5000) {
		lastPing = timeNow;
		if (client) {
			client->sendPing();
		} else {
			hasLostConnection = true;
		}
	}

	int64_t noPongTime = timeNow - lastPong;
	if (auto ac = attackedCreature.lock(); (hasLostConnection || noPongTime >= 7000) && ac) {
		setAttackedCreature(nullptr);
	}

	const bool inProtectionZone = getZone() == ZONE_PROTECTION;
	const bool inNoLogoutZone = getTile() && getTile()->hasFlag(TILESTATE_NOLOGOUT);

	int32_t noPongKickTime = vocation->getNoPongKickTime();
	if ((pzLocked || inProtectionZone) && noPongKickTime < 60000) {
		noPongKickTime = 60000;
	}

	if (noPongTime >= noPongKickTime) {
		if (isConnecting || inNoLogoutZone || logoutRequested) {
			return;
		}

		logoutRequested = true;
		if (!g_creatureEvents->playerLogout(this)) {
			logoutRequested = false;
			return;
		}

		if (client) {
			if (client->protocol()) {
				client->logout(true, true);
			} else {
				client->clear();
				g_game.removeCreature(this, true);
			}
		} else {
			g_game.removeCreature(this, true);
		}
	}
}

Item* Player::getWriteItem(uint32_t& windowTextId, uint16_t& maxWriteLen)
{
	windowTextId = this->windowTextId;
	maxWriteLen = this->maxWriteLen;
	return writeItem.lock().get();
}

void Player::setWriteItem(const std::shared_ptr<Item>& item, uint16_t maxWriteLen /*= 0*/)
{
	windowTextId++;

	if (item) {
		writeItem = item;
		this->maxWriteLen = maxWriteLen;
	} else {
		writeItem.reset();
		this->maxWriteLen = 0;
	}
}

House* Player::getEditHouse(uint32_t& windowTextId, uint32_t& listId)
{
	windowTextId = this->windowTextId;
	listId = this->editListId;
	return editHouse.lock().get();
}

void Player::setEditHouse(House* house, uint32_t listId /*= 0*/)
{
	windowTextId++;
	if (house) {
		editHouse = house->shared_from_this();
	} else {
		editHouse.reset();
	}
	editListId = listId;
}

void Player::sendHouseWindow(House* house, uint32_t listId) const
{
	if (!client) {
		return;
	}

	if (auto text = house->getAccessList(listId)) {
		client->sendHouseWindow(windowTextId, text.value());
	}
}

// container
void Player::sendAddContainerItem(const Container* container, const Item* item)
{
	if (!client) {
		return;
	}

	for (auto it = openContainers.begin(); it != openContainers.end();) {
		OpenContainer& openContainer = it->second;
		auto openContainerRef = openContainer.container.lock();
		if (!openContainerRef) {
			it = openContainers.erase(it);
			continue;
		}

		if (openContainerRef.get() != container) {
			++it;
			continue;
		}

		if (openContainer.index >= container->capacity()) {
			item = container->getItemByIndex(openContainer.index);
		}

		if (item) {
			client->sendAddContainerItem(it->first, item);
		}
		++it;
	}
}

void Player::sendUpdateContainerItem(const Container* container, uint16_t slot, const Item* newItem)
{
	if (!client) {
		return;
	}

	for (auto it = openContainers.begin(); it != openContainers.end();) {
		OpenContainer& openContainer = it->second;
		auto openContainerRef = openContainer.container.lock();
		if (!openContainerRef) {
			it = openContainers.erase(it);
			continue;
		}

		if (openContainerRef.get() != container) {
			++it;
			continue;
		}

		if (slot < openContainer.index) {
			++it;
			continue;
		}

		uint16_t pageEnd = openContainer.index + container->capacity();
		if (slot >= pageEnd) {
			++it;
			continue;
		}

		client->sendUpdateContainerItem(it->first, slot, newItem);
		++it;
	}
}

void Player::sendRemoveContainerItem(const Container* container, uint16_t slot)
{
	if (!client) {
		return;
	}

	for (auto it = openContainers.begin(); it != openContainers.end();) {
		OpenContainer& openContainer = it->second;
		auto openContainerRef = openContainer.container.lock();
		if (!openContainerRef) {
			it = openContainers.erase(it);
			continue;
		}

		if (openContainerRef.get() != container) {
			++it;
			continue;
		}

		uint16_t& firstIndex = openContainer.index;
		if (firstIndex > 0 && firstIndex >= container->size() - 1) {
			firstIndex -= static_cast<uint16_t>(container->capacity());
			sendContainer(it->first, container, false, firstIndex);
		}

		client->sendRemoveContainerItem(it->first, std::max<uint16_t>(slot, firstIndex));
		++it;
	}
}

void Player::onUpdateTileItem(const Tile* tile, const Position& pos, const Item* oldItem, const ItemType& oldType,
                              const Item* newItem, const ItemType& newType)
{
	Creature::onUpdateTileItem(tile, pos, oldItem, oldType, newItem, newType);

	if (oldItem != newItem) {
		onRemoveTileItem(tile, pos, oldType, oldItem);
	}

	if (tradeState != TRADE_TRANSFER) {
		if (tradeItem.lock() && oldItem == tradeItem.lock().get()) {
			g_game.internalCloseTrade(this);
		}
	}
}

void Player::onRemoveTileItem(const Tile* tile, const Position& pos, const ItemType& iType, const Item* item)
{
	Creature::onRemoveTileItem(tile, pos, iType, item);

	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(item);

		if (tradeItem.lock()) {
			const Container* container = item->getContainer();
			if (container && container->isHoldingItem(tradeItem.lock().get())) {
				g_game.internalCloseTrade(this);
			}
		}
	}
}

void Player::onCreatureAppear(Creature* creature, bool isLogin)
{
	Creature::onCreatureAppear(creature, isLogin);

	if (isLogin && creature == this) {
		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			Item* item = inventory[slot].get();
			if (item) {
				item->startDecaying();
				g_moveEvents->onPlayerEquip(this, item, static_cast<slots_t>(slot), false);
			}
		}

		for (auto& condition : storedConditionList) {
			addCondition(std::move(condition));
		}
		storedConditionList.clear();

		if (defaultOutfit.lookAddons >= getInteger(ConfigManager::MAX_ADDON_ATTRIBUTES)) {
			uint32_t outfitId = Outfits::getInstance().getOutfitId(sex, defaultOutfit.lookType);
			if (outfitAttributes) {
				Outfits::getInstance().removeAttributes(getID(), outfitId, sex);
				outfitAttributes = false;
			}
			outfitAttributes = Outfits::getInstance().addAttributes(getID(), outfitId, sex);
		}

		updateRegeneration();

		BedItem* bed = g_game.getBedBySleeper(guid);
		if (bed) {
			bed->wakeUp(this);
		}

		if (currentMount != 0) {
			Mount* mount = g_game.mounts.getMountByID(currentMount);
			if (mount && hasMount(mount)) {
				if (mountAttributes) {
					g_game.mounts.removeAttributes(getID(), currentMount);
					mountAttributes = false;
				}
				if (defaultOutfit.lookMount != 0) {
					mountAttributes = g_game.mounts.addAttributes(getID(), currentMount);
					g_game.changeSpeed(this, mount->speed);
				}
			} else {
				currentMount = 0;
				defaultOutfit.lookMount = 0;
				g_game.internalCreatureChangeOutfit(this, defaultOutfit);
			}

#if 0
			addOfflineTrainingTries(static_cast<skills_t>(offlineTrainingSkill), offlineTrainingTime / 1000);
			offlineTrainingTime = 0;
			offlineTrainingSkill = 0;
#endif
			if (offlineTrainingSkill != -1) {
				int32_t offlineTime;
				if (getLastLogout() != 0) {
					offlineTime = static_cast<int32_t>(time(nullptr) - getLastLogout());
				} else {
					offlineTime = 0;
				}

				// Use configurable threshold (default 600 seconds / 10 minutes)
				int64_t threshold = ConfigManager::getInteger(ConfigManager::OFFLINE_TRAINING_THRESHOLD);
				if (offlineTime >= threshold) {
					uint32_t trainingTime = static_cast<uint32_t>(std::min<int32_t>(offlineTime, offlineTrainingTime / 1000));
					if (trainingTime > 0) {
						applyOfflineTraining(trainingTime);
						removeOfflineTrainingTime(trainingTime * 1000);
					}
				}
				offlineTrainingSkill = -1;
			}
		}

		// mounted player moved to pz on login, update mount status
		onChangeZone(getZone());

		if (auto guild = getGuild()) {
			guild->addMember(this);
		}

		int32_t offlineTime;
		if (getLastLogout() != 0) {
			// Not counting more than 21 days to prevent overflow when multiplying with 1000 (for milliseconds).
			offlineTime = std::min<int32_t>(time(nullptr) - getLastLogout(), 86400 * 21);
		} else {
			offlineTime = 0;
		}

		for (Condition* condition : getMuteConditions()) {
			condition->setTicks(condition->getTicks() - (offlineTime * 1000));
			if (condition->getTicks() <= 0) {
				removeCondition(condition);
			}
		}

		IOLoginData::updateOnlineStatus(getGUID(), true, client->isBroadcasting(), client->password(), client->description(), client->spectatorList().size());

		g_scheduler.addEvent(createSchedulerTask(500, [id = getID()]() { Familiar::restoreFamiliarOnLogin(id); }));
	}
}

void Player::onAttackedCreatureDisappear(bool isLogout)
{
	sendCancelTarget();

	if (!isLogout) {
		sendTextMessage(MESSAGE_STATUS_SMALL, "Target lost.");
	}
}

void Player::onFollowCreatureDisappear(bool isLogout)
{
	sendCancelTarget();

	if (!isLogout) {
		sendTextMessage(MESSAGE_STATUS_SMALL, "Target lost.");
	}
}

void Player::onChangeZone(ZoneType_t zone)
{
	if (zone == ZONE_PROTECTION) {
		if (!attackedCreature.expired() && !hasFlag(PlayerFlag_IgnoreProtectionZone)) {
			setAttackedCreature(nullptr);
			onAttackedCreatureDisappear(false);
		}

		// Start stamina regeneration in protection zone
		if (ConfigManager::getBoolean(ConfigManager::STAMINA_PZ)) {
			staminaPzOrangeDelayMs = ConfigManager::getInteger(ConfigManager::STAMINA_ORANGE_DELAY) * 60 * 1000;
			staminaPzGreenDelayMs = ConfigManager::getInteger(ConfigManager::STAMINA_GREEN_DELAY) * 60 * 1000;

			staminaPzActive = true;
			staminaPzTicks = 0;
			uint32_t delay = (staminaMinutes > 2400) ? staminaPzGreenDelayMs / (60 * 1000) : staminaPzOrangeDelayMs / (60 * 1000);
			uint32_t gain = ConfigManager::getInteger(ConfigManager::STAMINA_PZ_GAIN);
			sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("You're in the protection zone. Every {} minutes, gain {} stamina.", delay, gain));
		}
		if (!group->access && isMounted() && !ConfigManager::getBoolean(ConfigManager::ALLOW_MOUNT_IN_PZ)) {
			dismount();
			g_game.internalCreatureChangeOutfit(this, defaultOutfit);
			wasMounted = true;
		}
	} else {
		// Stop stamina regeneration when leaving protection zone
		sendTextMessage(MESSAGE_STATUS_SMALL, "You are no longer refilling stamina, since you left a regeneration zone.");
		staminaPzActive = false;

		if (wasMounted) {
			toggleMount(true);
			wasMounted = false;
		}
	}

	g_game.updateCreatureWalkthrough(this);
	sendIcons();
}

void Player::onAttackedCreatureChangeZone(ZoneType_t zone)
{
	if (zone == ZONE_PROTECTION) {
		if (!hasFlag(PlayerFlag_IgnoreProtectionZone)) {
			setAttackedCreature(nullptr);
			onAttackedCreatureDisappear(false);
		}
	} else if (zone == ZONE_NOPVP) {
		if (auto ac = attackedCreature.lock(); ac && ac->getPlayer()) {
			if (!hasFlag(PlayerFlag_IgnoreProtectionZone)) {
				setAttackedCreature(nullptr);
				onAttackedCreatureDisappear(false);
			}
		}
	} else if (zone == ZONE_NORMAL) {
		// attackedCreature can leave a pvp zone if not pzlocked
		if (g_game.getWorldType() == WORLD_TYPE_NO_PVP) {
			if (auto ac = attackedCreature.lock(); ac && ac->getPlayer()) {
				setAttackedCreature(nullptr);
				onAttackedCreatureDisappear(false);
			}
		}
	}
}

void Player::updateStaminaRegen(int64_t timePassed)
{
	if (!staminaPzActive && !staminaTrainerActive) {
		return;
	}

	// Stamina PZ regeneration
	if (staminaPzActive && staminaMinutes < 2520) {
		staminaPzTicks += timePassed;
		
		// Calculate delay based on current stamina level (cached for efficiency)
		uint32_t delayMs = (staminaMinutes > 2400) ? staminaPzGreenDelayMs : staminaPzOrangeDelayMs;
		
		if (staminaPzTicks >= delayMs) {
			staminaPzTicks -= delayMs;
			uint16_t gain = ConfigManager::getInteger(ConfigManager::STAMINA_PZ_GAIN);
			setStaminaMinutes(staminaMinutes + gain);
			sendStats();
			sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("It has been regenerated {} of stamina due to being in the protection zone.", gain));
			
			if (staminaMinutes >= 2520) {
				staminaPzActive = false;
				sendTextMessage(MESSAGE_STATUS_SMALL, "You are no longer refilling stamina, because your stamina is already full.");
			}
		}
	}

	// Stamina Trainer regeneration
	if (staminaTrainerActive) {
		auto target = getAttackedCreatureShared();
		if (target && isTrainerTarget(target.get())) {
			staminaTrainerTicks += timePassed;

			if (staminaTrainerTicks >= staminaTrainerDelayMs) {
				staminaTrainerTicks -= staminaTrainerDelayMs;
				uint16_t gain = ConfigManager::getInteger(ConfigManager::STAMINA_TRAINER_GAIN);
				setStaminaMinutes(staminaMinutes + gain);
				sendStats();
				sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("It has been regenerated {} of stamina by being in training.", gain));
			}
		} else {
			staminaTrainerActive = false;
		}
	}
}

bool Player::isTrainerTarget(Creature* creature) const
{
	if (!creature) {
		return false;
	}

	std::string_view trainerNames = ConfigManager::getString(ConfigManager::STAMINATRAINER_NAMES);
	if (trainerNames.empty()) {
		return creature->getName() == "Trainer";
	}

	const std::string& targetName = creature->getName();
	size_t pos = 0;
	while (pos < trainerNames.length()) {
		size_t semicolon = trainerNames.find(';', pos);
		std::string_view name;
		if (semicolon == std::string_view::npos) {
			name = trainerNames.substr(pos);
			pos = trainerNames.length();
		} else {
			name = trainerNames.substr(pos, semicolon - pos);
			pos = semicolon + 1;
		}

		// Trim whitespace
		size_t start = name.find_first_not_of(" \t\n\r");
		size_t end = name.find_last_not_of(" \t\n\r");
		if (start != std::string_view::npos) {
			name = name.substr(start, end - start + 1);
		}

		if (!name.empty() && targetName == name) {
			return true;
		}
	}
	return false;
}

void Player::onRemoveCreature(Creature* creature, bool isLogout)
{
	Creature::onRemoveCreature(creature, isLogout);

	if (creature == this) {
		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			Item* item = inventory[slot].get();
			if (item) {
				g_moveEvents->onPlayerDeEquip(this, item, static_cast<slots_t>(slot));
			}
		}

		if (isLogout) {
			loginPosition = getPosition();

			if (getInstanceID() != 0) {
				loginPosition = town->getTemplePosition();
				setInstanceID(0);
			}
		}

		if (accountManager != ACCOUNT_MANAGER_NONE) {
			managerTalkState.fill(false);
			managerData.sex = PLAYERSEX_FEMALE;
			managerData.accountId = 0;
			managerData.vocationId = 0;
			managerData.townId = 0;
			managerData.string1.clear();
			managerData.string2.clear();
			managerData.accountName.clear();
		}

		lastLogout = time(nullptr);

		if (eventWalk != 0) {
			setFollowCreature(nullptr);
		}

		if (!tradePartner.expired()) {
			g_game.internalCloseTrade(this);
		}

		closeShopWindow();

		clearPartyInvitations();

		if (auto p = party.lock()) {
			p->leaveParty(this, true);
		}

		g_chat->removeUserFromAllChannels(*this);

		if (auto guild = getGuild()) {
			guild->removeMember(this);
			this->guild.reset();
		}

		IOLoginData::removeOnlineStatus(guid);

		Familiar::onPlayerLogout(this);

		bool saved = false;
		for (uint32_t tries = 0; tries < 3; ++tries) {
			if (IOLoginData::savePlayer(this)) {
				saved = true;
				break;
			}
		}

		if (!saved) {
			LOG_ERROR(fmt::format("Error while saving player: {}", getName()));
		}
	}
}

void Player::openShopWindow(const std::list<ShopInfo>& shop)
{
	shopItemList = shop;
	sendShop();
	sendSaleItemList();
}

void Player::setShopOwner(Npc* owner, int32_t onBuy, int32_t onSell)
{
	shopOwner = owner ? std::dynamic_pointer_cast<Npc>(owner->shared_from_this()) : std::shared_ptr<Npc>();
	purchaseCallback = onBuy;
	saleCallback = onSell;
}

bool Player::closeShopWindow(bool sendCloseShopWindow /*= true*/)
{
	// unreference callbacks
	int32_t onBuy;
	int32_t onSell;

	Npc* npc = getShopOwner(onBuy, onSell);
	if (!npc) {
		shopItemList.clear();
		return false;
	}

	setShopOwner(nullptr, -1, -1);
	npc->onPlayerEndTrade(this, onBuy, onSell);

	if (sendCloseShopWindow) {
		sendCloseShop();
	}

	shopItemList.clear();
	return true;
}

void Player::onWalk(Direction& dir)
{
	const Position& fromPos = getPosition();
	const Position& toPos = getNextPosition(dir, fromPos);
	if (!g_events->eventPlayerOnStepTile(this, fromPos, toPos)) {
		return;
	}

	Creature::onWalk(dir);
	setNextActionTask(nullptr);
}

void Player::onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos, const Tile* oldTile,
                            const Position& oldPos, bool teleport)
{
	Creature::onCreatureMove(creature, newTile, newPos, oldTile, oldPos, teleport);

	auto follow = followCreature.lock();
	if (hasFollowPath && (creature == follow.get() || (creature == this && follow))) {
		isUpdatingPath = false;
		g_dispatcher.addTask([id = getID()]() { g_game.updateCreatureWalk(id); });
	}

	if (creature != this) {
		return;
	}

	if (tradeState != TRADE_TRANSFER) {
		// check if we should close trade
		if (tradeItem.lock() && !tradeItem.lock()->getPosition().isInRange(getPosition(), 1, 1, 0)) {
			g_game.internalCloseTrade(this);
		}

		if (auto tp = tradePartner.lock()) {
			if (!tp->getPosition().isInRange(getPosition(), 2, 2, 0)) {
				g_game.internalCloseTrade(this);
			}
		}
	}

	if (auto p = party.lock()) {
		p->updateSharedExperience();
	}

	if (teleport || oldPos.z != newPos.z) {
		const int64_t ticks = getInteger(ConfigManager::STAIRHOP_DELAY);
		if (ticks > 0) {
			if (auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_PACIFIED, ticks, 0)) {
				addCondition(std::move(condition));
			}
		}
	}
}

// container
void Player::onAddContainerItem(const Item* item) { checkTradeState(item); }

void Player::onUpdateContainerItem(const Container* container, const Item* oldItem, const Item* newItem)
{
	if (oldItem != newItem) {
		onRemoveContainerItem(container, oldItem);
	}

	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(oldItem);
	}
}

void Player::onRemoveContainerItem(const Container* container, const Item* item)
{
	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(item);

		if (tradeItem.lock()) {
			if (tradeItem.lock()->getParent() != container && container->isHoldingItem(tradeItem.lock().get())) {
				g_game.internalCloseTrade(this);
			}
		}
	}
}

void Player::onCloseContainer(const Container* container)
{
	if (!client) {
		return;
	}

	for (auto it = openContainers.begin(); it != openContainers.end();) {
		auto openContainerRef = it->second.container.lock();
		if (!openContainerRef) {
			it = openContainers.erase(it);
			continue;
		}

		if (openContainerRef.get() == container) {
			client->sendCloseContainer(it->first);
		}
		++it;
	}
}

void Player::onSendContainer(const Container* container)
{
	if (!client) {
		return;
	}

	bool hasParent = dynamic_cast<const Container*>(container->getParent()) != nullptr;
	for (auto it = openContainers.begin(); it != openContainers.end();) {
		OpenContainer& openContainer = it->second;
		auto openContainerRef = openContainer.container.lock();
		if (!openContainerRef) {
			it = openContainers.erase(it);
			continue;
		}

		if (openContainerRef.get() == container) {
			client->sendContainer(it->first, container, hasParent, openContainer.index);
		}
		++it;
	}
}

// inventory
void Player::onUpdateInventoryItem(Item* oldItem, Item* newItem)
{
	if (oldItem != newItem) {
		onRemoveInventoryItem(oldItem);
	}

	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(oldItem);
	}
}

void Player::onRemoveInventoryItem(Item* item)
{
	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(item);

		if (tradeItem.lock()) {
			const Container* container = item->getContainer();
			if (container && container->isHoldingItem(tradeItem.lock().get())) {
				g_game.internalCloseTrade(this);
			}
		}
	}
}

void Player::checkTradeState(const Item* item)
{
	if (!tradeItem.lock() || tradeState == TRADE_TRANSFER) {
		return;
	}

	if (tradeItem.lock().get() == item) {
		g_game.internalCloseTrade(this);
	} else {
		const Container* container = dynamic_cast<const Container*>(item->getParent());
		while (container) {
			if (container == tradeItem.lock().get()) {
				g_game.internalCloseTrade(this);
				break;
			}

			container = dynamic_cast<const Container*>(container->getParent());
		}
	}
}

void Player::setNextWalkActionTask(std::unique_ptr<SchedulerTask> task)
{
	if (walkTaskEvent != 0) {
		g_scheduler.stopEvent(walkTaskEvent);
		walkTaskEvent = 0;
	}

	walkTask = std::move(task);
}

void Player::setNextWalkTask(std::unique_ptr<SchedulerTask> task)
{
	if (nextStepEvent != 0) {
		g_scheduler.stopEvent(nextStepEvent);
		nextStepEvent = 0;
	}

	if (task) {
		nextStepEvent = g_scheduler.addEvent(std::move(task));
		resetIdleTime();
	}
}

void Player::setNextActionTask(std::unique_ptr<SchedulerTask> task, bool resetIdleTime /*= true */)
{
	if (actionTaskEvent != 0) {
		g_scheduler.stopEvent(actionTaskEvent);
		actionTaskEvent = 0;
	}

	if (task) {
		actionTaskEvent = g_scheduler.addEvent(std::move(task));
		if (resetIdleTime) {
			this->resetIdleTime();
		}
	}
}

uint32_t Player::getNextActionTime() const { return std::max<int64_t>(SCHEDULER_MINTICKS, nextAction - OTSYS_TIME()); }

void Player::onThink(uint32_t interval)
{
	Creature::onThink(interval);

	if (client && getIP() == 0) {
		if (hasCondition(CONDITION_INFIGHT)) {
			ghostModeStartTime = 0;

			// Stop attacking if the player closes the game (Force Exit)
			if (!attackedCreature.expired()) {
				setAttackedCreature(nullptr);
			}

		} else {
			if (ghostModeStartTime == 0) {
				ghostModeStartTime = OTSYS_TIME();
			} else if (OTSYS_TIME() - ghostModeStartTime >= 60000) {
				kickPlayer(true);
				return;
			}
		}
	} else {
		ghostModeStartTime = 0;
	}

	if (protectionTime > 0) {
		--protectionTime;
	}

	sendPing();
	if (isRemoved()) {
		return;
	}

	if (client && client->isWaitingForUpdate()) {
		IOLoginData::updateOnlineStatus(getGUID(), false, client->isBroadcasting(), client->password(), client->description(), client->spectatorList().size());
		client->setUpdateStatus(false);
	}

	const int64_t timeNow = OTSYS_TIME();
	if (client && !client->isOTCv8 && getIP() != 0 && getBoolean(ConfigManager::DLL_CHECK_KICK)) {
		int64_t checkInterval = getInteger(ConfigManager::DLL_CHECK_KICK_TIME) * 1000;
		if (timeNow - lastDllCheck >= checkInterval) {
			lastDllCheck = timeNow;
			client->sendDllCheck();
		}
	}

	MessageBufferTicks += interval;
	if (MessageBufferTicks >= 1500) {
		MessageBufferTicks = 0;
		addMessageBuffer();
	}

	if (!getTile()->hasFlag(TILESTATE_NOLOGOUT) && !isAccessPlayer() && !(client && client->protocol() && client->protocol()->isSpectator) && !hasCondition(CONDITION_INFIGHT)) {
		idleTime += interval;
		const int32_t kickAfterMinutes = getInteger(ConfigManager::KICK_AFTER_MINUTES);
		if (idleTime > (kickAfterMinutes * 60000) + 60000) {
			kickPlayer(true);
		} else if (client && idleTime == 60000 * kickAfterMinutes) {
			client->sendTextMessage(TextMessage(
			    MESSAGE_STATUS_WARNING,
			    fmt::format(
			        "There was no variation in your behaviour for {:d} minutes. You will be disconnected in one minute if there is no change in your actions until then.",
			        kickAfterMinutes)));
		}
	} else {
		if (hasCondition(CONDITION_INFIGHT)) {
			idleTime = 0;
		}
	}

	// if (isImbued()) { // TODO: Reimplement a check like this to first see if player has any items, then items with imbuements before decaying.
	for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
		Item* item = inventory[slot].get();
		if (item && item->hasImbuements()) {
			item->decayImbuements(hasCondition(CONDITION_INFIGHT));
			sendSkills();
			sendStats();
		}
	}
// } // part of the above TODO:

	if (g_game.getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
		checkSkullTicks(interval / 1000);
	}

	addOfflineTrainingTime(interval);
	if (lastStatsTrainingTime != getOfflineTrainingTime() / 60 / 1000) {
		sendStats();
	}

	// Update stamina regeneration
	updateStaminaRegen(interval);
}

uint32_t Player::isMuted() const
{
	if (hasFlag(PlayerFlag_CannotBeMuted)) {
		return 0;
	}

	int32_t muteTicks = 0;
	for (const auto& condition : conditions) {
		if (condition->getType() == CONDITION_MUTED && condition->getTicks() > muteTicks) {
			muteTicks = condition->getTicks();
		}
	}
	return static_cast<uint32_t>(muteTicks) / 1000;
}

void Player::addMessageBuffer()
{
	if (MessageBufferCount > 0 && getInteger(ConfigManager::MAX_MESSAGEBUFFER) != 0 &&
	    !hasFlag(PlayerFlag_CannotBeMuted)) {
		--MessageBufferCount;
	}
}

void Player::removeMessageBuffer()
{
	if (hasFlag(PlayerFlag_CannotBeMuted)) {
		return;
	}

	const int64_t maxMessageBuffer = getInteger(ConfigManager::MAX_MESSAGEBUFFER);
	if (maxMessageBuffer != 0 && MessageBufferCount <= maxMessageBuffer + 1) {
		if (++MessageBufferCount > maxMessageBuffer) {
			uint32_t muteCount = 1;
			auto it = muteCountMap.find(guid);
			if (it != muteCountMap.end()) {
				muteCount = it->second;
			}

			uint32_t muteTime = 5 * muteCount * muteCount;
			muteCountMap[guid] = muteCount + 1;
			auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_MUTED, muteTime * 1000, 0);
			addCondition(std::move(condition));

			sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("You are muted for {:d} seconds.", muteTime));
		}
	}
}

void Player::drainHealth(const std::shared_ptr<Creature>& attacker, int32_t damage)
{
	Creature::drainHealth(attacker, damage);
	sendStats();
}

void Player::drainMana(const std::shared_ptr<Creature>& attacker, int32_t manaLoss)
{
	onAttacked();
	changeMana(-manaLoss);

	if (attacker) {
		addDamagePoints(attacker, manaLoss);
	}

	sendStats();
}

void Player::addManaSpent(uint64_t amount, bool artificial /*= false*/)
{
	if (hasFlag(PlayerFlag_NotGainMana)) {
		return;
	}

	uint64_t currReqMana = vocation->getReqMana(magLevel);
	uint64_t nextReqMana = vocation->getReqMana(magLevel + 1);
	if (currReqMana >= nextReqMana) {
		// player has reached max magic level
		return;
	}

	g_events->eventPlayerOnGainSkillTries(this, SKILL_MAGLEVEL, amount, artificial);
	if (amount == 0) {
		return;
	}

	bool sendUpdateStats = false;
	while ((manaSpent + amount) >= nextReqMana) {
		amount -= nextReqMana - manaSpent;

		magLevel++;
		manaSpent = 0;

		sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You advanced to magic level {:d}.", magLevel));

		g_creatureEvents->playerAdvance(this, SKILL_MAGLEVEL, magLevel - 1, magLevel);

		sendUpdateStats = true;
		currReqMana = nextReqMana;
		nextReqMana = vocation->getReqMana(magLevel + 1);
		if (currReqMana >= nextReqMana) {
			return;
		}
	}

	manaSpent += amount;

	uint8_t oldPercent = magLevelPercent;
	if (nextReqMana > currReqMana) {
		magLevelPercent = Player::getBasisPointLevel(manaSpent, nextReqMana) / 100;
	} else {
		magLevelPercent = 0;
	}

	if (oldPercent != magLevelPercent) {
		sendUpdateStats = true;
	}

	if (sendUpdateStats) {
		sendStats();
	}
}

void Player::removeManaSpent(uint64_t amount, bool notify /* = false*/)
{
	if (amount == 0) {
		return;
	}

	uint32_t oldLevel = magLevel;
	uint8_t oldPercent = magLevelPercent;

	while (amount > manaSpent && magLevel > 0) {
		amount -= manaSpent;
		manaSpent = vocation->getReqMana(magLevel);
		magLevel--;
	}

	manaSpent -= amount;

	uint64_t nextReqMana = vocation->getReqMana(magLevel + 1);
	if (nextReqMana > vocation->getReqMana(magLevel)) {
		magLevelPercent = Player::getBasisPointLevel(manaSpent, nextReqMana) / 100;
	} else {
		magLevelPercent = 0;
	}

	if (notify) {
		bool sendUpdateStats = false;
		if (oldLevel != magLevel) {
			sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You were downgraded to magic level {:d}.", magLevel));
			sendUpdateStats = true;
		}

		if (sendUpdateStats || oldPercent != magLevelPercent) {
			sendStats();
		}
	}
}

void Player::addExperience(const std::shared_ptr<Creature>& source, uint64_t exp, bool sendText /* = false*/)
{
	uint64_t currLevelExp = Player::getExpForLevel(level);
	uint64_t nextLevelExp = Player::getExpForLevel(level + 1);
	uint64_t rawExp = exp;
	if (currLevelExp >= nextLevelExp) {
		// player has reached max level
		levelPercent = 0;
		sendStats();
		return;
	}

	g_events->eventPlayerOnGainExperience(this, source.get(), exp, rawExp, sendText);
	if (exp == 0) {
		return;
	}

	experience += exp;

	uint32_t prevLevel = level;
	while (experience >= nextLevelExp) {
		++level;
		healthMax += vocation->getHPGain();
		health += vocation->getHPGain();
		manaMax += vocation->getManaGain();
		mana += vocation->getManaGain();
		capacity += vocation->getCapGain();

		currLevelExp = nextLevelExp;
		nextLevelExp = Player::getExpForLevel(level + 1);
		if (currLevelExp >= nextLevelExp) {
			// player has reached max level
			break;
		}
	}

	if (prevLevel != level) {
		health = getMaxHealth();
		mana = getMaxMana();

		updateBaseSpeed();
		setBaseSpeed(getBaseSpeed());

		g_game.changeSpeed(this, 0);
		g_game.addCreatureHealth(this);

		const uint64_t protectionLevel = getInteger(ConfigManager::PROTECTION_LEVEL);
		if (prevLevel < protectionLevel && level >= protectionLevel) {
			g_game.updateCreatureWalkthrough(this);
		}

		if (auto p = party.lock()) {
			p->updateSharedExperience();
		}

		g_creatureEvents->playerAdvance(this, SKILL_LEVEL, prevLevel, level);

		sendTextMessage(MESSAGE_EVENT_ADVANCE,
		                fmt::format("You advanced from Level {:d} to Level {:d}.", prevLevel, level));
	}

	if (nextLevelExp > currLevelExp) {
		levelPercent = Player::getBasisPointLevel(experience - currLevelExp, nextLevelExp - currLevelExp) / 100;
	} else {
		levelPercent = 0;
	}
	sendStats();
}

void Player::removeExperience(uint64_t exp, bool sendText /* = false*/)
{
	if (experience == 0 || exp == 0) {
		return;
	}

	g_events->eventPlayerOnLoseExperience(this, exp);
	if (exp == 0) {
		return;
	}

	uint64_t lostExp = experience;
	experience = std::max<int64_t>(0, experience - exp);

	if (sendText) {
		lostExp -= experience;

		std::string expString = std::to_string(lostExp) + (lostExp != 1 ? " experience points." : " experience point.");

		TextMessage message(MESSAGE_STATUS_DEFAULT, "You lost " + expString);
		sendTextMessage(message);

		SpectatorVec spectators;
		g_game.map.getSpectators(spectators, position, false, true);
		spectators = InstanceUtils::filterByInstance(spectators, getInstanceID());
		g_game.addAnimatedText(spectators, std::to_string(lostExp), position, TEXTCOLOR_RED);
		spectators.erase(this);
		if (!spectators.empty()) {
			message.type = MESSAGE_STATUS_DEFAULT;
			message.text = getName() + " lost " + expString;
			for (const auto& spectator : spectators) {
				static_cast<Player*>(spectator.get())->sendTextMessage(message);
			}
		}
	}

	uint32_t oldLevel = level;
	uint64_t currLevelExp = Player::getExpForLevel(level);

	while (level > 1 && experience < currLevelExp) {
		--level;
		healthMax = std::max<int32_t>(0, healthMax - vocation->getHPGain());
		manaMax = std::max<int32_t>(0, manaMax - vocation->getManaGain());
		capacity = std::max<int32_t>(0, capacity - vocation->getCapGain());
		currLevelExp = Player::getExpForLevel(level);
	}

	if (oldLevel != level) {
		health = getMaxHealth();
		mana = getMaxMana();

		updateBaseSpeed();
		setBaseSpeed(getBaseSpeed());

		g_game.changeSpeed(this, 0);
		g_game.addCreatureHealth(this);

		const uint64_t protectionLevel = getInteger(ConfigManager::PROTECTION_LEVEL);
		if (oldLevel >= protectionLevel && level < protectionLevel) {
			g_game.updateCreatureWalkthrough(this);
		}

		if (auto p = party.lock()) {
			p->updateSharedExperience();
		}

		sendTextMessage(MESSAGE_EVENT_ADVANCE,
		                fmt::format("You were downgraded from Level {:d} to Level {:d}.", oldLevel, level));
	}

	uint64_t nextLevelExp = Player::getExpForLevel(level + 1);
	if (nextLevelExp > currLevelExp) {
		levelPercent = Player::getBasisPointLevel(experience - currLevelExp, nextLevelExp - currLevelExp) / 100;
	} else {
		levelPercent = 0;
	}
	sendStats();
}

uint16_t Player::getBasisPointLevel(uint64_t count, uint64_t nextLevelCount)
{
	if (nextLevelCount == 0) {
		return 0;
	}

	uint16_t result = ((count * 10000.) / nextLevelCount);
	if (result > 10000) {
		return 0;
	}
	return result;
}

void Player::onBlockHit()
{
	if (shieldBlockCount > 0) {
		--shieldBlockCount;

		if (hasShield()) {
			addSkillAdvance(SKILL_SHIELD, 1);
		}
	}
}

void Player::onAttackedCreatureBlockHit(BlockType_t blockType)
{
	lastAttackBlockType = blockType;

	switch (blockType) {
		case BLOCK_NONE: {
			addAttackSkillPoint = true;
			bloodHitCount = 30;
			shieldBlockCount = 30;
			break;
		}

		case BLOCK_DEFENSE:
		case BLOCK_ARMOR: {
			// need to draw blood every 30 hits
			if (bloodHitCount > 0) {
				addAttackSkillPoint = true;
				--bloodHitCount;
			} else {
				addAttackSkillPoint = false;
			}
			break;
		}

		default: {
			addAttackSkillPoint = false;
			break;
		}
	}
}

bool Player::hasShield() const
{
	Item* item = inventory[CONST_SLOT_LEFT].get();
	if (item && item->getWeaponType() == WEAPON_SHIELD) {
		return true;
	}

	item = inventory[CONST_SLOT_RIGHT].get();
	if (item && item->getWeaponType() == WEAPON_SHIELD) {
		return true;
	}
	return false;
}

BlockType_t Player::blockHit(const std::shared_ptr<Creature>& attacker, CombatType_t combatType, int32_t& damage,
                             bool checkDefense /* = false*/, bool checkArmor /* = false*/, bool field /* = false*/,
                             bool ignoreResistances /* = false*/)
{
	BlockType_t blockType =
	    Creature::blockHit(attacker, combatType, damage, checkDefense, checkArmor, field, ignoreResistances);

	if (attacker && combatType != COMBAT_HEALING) {
		sendCreatureSquare(attacker.get(), SQ_COLOR_YELLOW);
	}

	if (blockType != BLOCK_NONE) {
		return blockType;
	}

	if (damage <= 0) {
		damage = 0;
		return BLOCK_ARMOR;
	}

	if (!ignoreResistances) {
		Reflect reflect;

		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_AMMO; ++slot) {
			if (!isItemAbilityEnabled(static_cast<slots_t>(slot))) {
				continue;
			}

			Item* item = inventory[slot].get();
			if (!item) {
				continue;
			}

			const ItemType& it = Item::items[item->getID()];
			if (!it.abilities) {
				if (damage <= 0) {
					damage = 0;
					return BLOCK_ARMOR;
				}

				continue;
			}

			const int16_t& absorbPercent = it.abilities->absorbPercent[combatTypeToIndex(combatType)];
			if (absorbPercent != 0) {
				damage -= std::ceil(damage * (absorbPercent / 100.));

				uint16_t charges = item->getCharges();
				if (charges != 0) {
					g_game.transformItem(item, item->getID(), charges - 1);
				}
			}

			reflect += item->getReflect(combatType);

			if (field) {
				const int16_t& fieldAbsorbPercent = it.abilities->fieldAbsorbPercent[combatTypeToIndex(combatType)];
				if (fieldAbsorbPercent != 0) {
					damage -= std::ceil(damage * (fieldAbsorbPercent / 100.));

					uint16_t charges = item->getCharges();
					if (charges != 0) {
						g_game.transformItem(item, item->getID(), charges - 1);
					}
				}
			}
			if (item->hasImbuements()) {
				for (auto imbuement : item->getImbuements()) {
					switch (imbuement->imbuetype) {
					case ImbuementType::IMBUEMENT_TYPE_FIRE_RESIST:
						if (combatType == COMBAT_FIREDAMAGE) {
							damage -= std::round(damage * (imbuement->value / 100.));
						}
						break;
					case ImbuementType::IMBUEMENT_TYPE_EARTH_RESIST:
						if (combatType == COMBAT_EARTHDAMAGE) {
							damage -= std::round(damage * (imbuement->value / 100.));
						}
						break;
					case ImbuementType::IMBUEMENT_TYPE_ICE_RESIST:
						if (combatType == COMBAT_ICEDAMAGE) {
							damage -= std::round(damage * (imbuement->value / 100.));
						}
						break;
					case ImbuementType::IMBUEMENT_TYPE_ENERGY_RESIST:
						if (combatType == COMBAT_ENERGYDAMAGE) {
							damage -= std::round(damage * (imbuement->value / 100.));
						}
						break;
					case ImbuementType::IMBUEMENT_TYPE_DEATH_RESIST:
						if (combatType == COMBAT_DEATHDAMAGE) {
							damage -= std::round(damage * (imbuement->value / 100.));
						}
						break;
					case ImbuementType::IMBUEMENT_TYPE_HOLY_RESIST:
						if (combatType == COMBAT_HOLYDAMAGE) {
							damage -= std::round(damage * (imbuement->value / 100.));
						}
						break;
					default:
						break;
					}
				}
			}
		}

		if (attacker && reflect.chance > 0 && reflect.percent != 0 && uniform_random(1, 100) <= reflect.chance) {
			CombatDamage reflectDamage;
			reflectDamage.primary.type = combatType;
			reflectDamage.primary.value = -std::round(damage * (reflect.percent / 100.));
			reflectDamage.origin = ORIGIN_REFLECT;
			g_game.combatChangeHealth(this, attacker.get(), reflectDamage);
		}
	}

	if (damage <= 0) {
		damage = 0;
		blockType = BLOCK_ARMOR;
	}
	return blockType;
}

uint32_t Player::getIP() const
{
	if (client) {
		return client->getIP();
	}

	return 0;
}

void Player::death(Creature* lastHitCreature)
{
	loginPosition = town->getTemplePosition();
	setInstanceID(0);

	auto refreshDeathPingWindow = [this]() {
		const int64_t timeNow = OTSYS_TIME();
		lastPing = timeNow;
		lastPong = timeNow;
	};

	auto removeDeathConditions = [this]() {
		bool removedInFight = false;

		auto it = conditions.begin();
		while (it != conditions.end()) {
			const ConditionType_t type = (*it)->getType();
			if (type != CONDITION_INFIGHT && (!(*it)->isPersistent() || (*it)->isConstant())) {
				++it;
				continue;
			}

			auto owned = std::move(*it);
			it = conditions.erase(it);

			if (type == CONDITION_INFIGHT) {
				removedInFight = true;
			}

			owned->endCondition(this);
			onEndCondition(type);
		}

		if (!removedInFight && pzLocked) {
			pzLocked = false;
			onIdleStatus();
			clearAttacked();

			if (getSkull() != SKULL_RED && getSkull() != SKULL_BLACK) {
				setSkull(SKULL_NONE);
			}

			sendIcons();
		}
	};

	auto clearDeathCombatState = [this]() {
		setAttackedCreature(nullptr);
		setFollowCreature(nullptr);
		stopWalk();
		clearAttacked();
	};

	int32_t storedTotalReduceSkillLoss = totalReduceSkillLoss;

	if (skillLoss) {
		bool lastHitPlayer = Player::lastHitIsPlayer(lastHitCreature);

		// Magic level loss
		uint64_t sumMana = 0;
		for (uint32_t i = 1; i <= magLevel; ++i) {
			sumMana += vocation->getReqMana(i);
		}

		totalReduceSkillLoss = storedTotalReduceSkillLoss;

		double deathLossPercent = getLostPercent();
		removeManaSpent(static_cast<uint64_t>((sumMana + manaSpent) * deathLossPercent), false);

		// Skill loss
		for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) { // for each skill
			uint64_t sumSkillTries = 0;
			for (uint16_t c = MINIMUM_SKILL_LEVEL + 1; c <= skills[i].level;
			     ++c) { // sum up all required tries for all skill levels
				sumSkillTries += vocation->getReqSkillTries(static_cast<skills_t>(i), c);
			}

			sumSkillTries += skills[i].tries;

			removeSkillTries(static_cast<skills_t>(i), sumSkillTries * deathLossPercent, false);
		}

		// Level loss
		uint64_t expLoss = static_cast<uint64_t>(experience * deathLossPercent);
		g_events->eventPlayerOnLoseExperience(this, expLoss);

		if (expLoss != 0) {
			uint32_t oldLevel = level;

			if (vocation->getId() == VOCATION_NONE || level > 7) {
				experience -= expLoss;
			}

			while (level > 1 && experience < Player::getExpForLevel(level)) {
				--level;
				healthMax = std::max<int32_t>(0, healthMax - vocation->getHPGain());
				manaMax = std::max<int32_t>(0, manaMax - vocation->getManaGain());
				capacity = std::max<int32_t>(0, capacity - vocation->getCapGain());
			}

			if (oldLevel != level) {
				sendTextMessage(MESSAGE_EVENT_ADVANCE,
				                fmt::format("You were downgraded from Level {:d} to Level {:d}.", oldLevel, level));
			}

			uint64_t currLevelExp = Player::getExpForLevel(level);
			uint64_t nextLevelExp = Player::getExpForLevel(level + 1);
			if (nextLevelExp > currLevelExp) {
				levelPercent = Player::getBasisPointLevel(experience - currLevelExp, nextLevelExp - currLevelExp) / 100;
			} else {
				levelPercent = 0;
			}
		}

		if (blessings.test(5)) {
			if (lastHitPlayer) {
				blessings.reset(5);
			} else {
				blessings.reset();
				blessings.set(5);
			}
		} else {
			blessings.reset();
		}

		if (getSkull() == SKULL_BLACK) {
			health = 40;
			mana = 0;
		} else {
			health = healthMax;
			mana = manaMax;
		}

		removeDeathConditions();
		clearDeathCombatState();
		refreshDeathPingWindow();
		sendStats();
		sendSkills();
		sendReLoginWindow();
		g_creatureEvents->playerLogout(this);
	} else {
		setSkillLoss(true);

		health = healthMax;
		removeDeathConditions();
		clearDeathCombatState();
		g_game.internalTeleport(this, getTemplePosition(), true);
		refreshDeathPingWindow();
		g_game.addCreatureHealth(this);
		refreshWorldView();
		onThink(EVENT_CREATURE_THINK_INTERVAL);
		onIdleStatus();
		sendStats();
	}
}

bool Player::dropCorpse(Creature* lastHitCreature, Creature* mostDamageCreature, bool lastHitUnjustified,
                        bool mostDamageUnjustified)
{
	if (getZone() != ZONE_PVP || !Player::lastHitIsPlayer(lastHitCreature)) {
		return Creature::dropCorpse(lastHitCreature, mostDamageCreature, lastHitUnjustified, mostDamageUnjustified);
	}

	setDropLoot(true);
	return false;
}

std::shared_ptr<Item> Player::getCorpse(Creature* lastHitCreature, Creature* mostDamageCreature)
{
	auto corpse = Creature::getCorpse(lastHitCreature, mostDamageCreature);
	if (corpse && corpse->getContainer()) {
		size_t killersSize = getKillers().size();

		if (lastHitCreature) {
			if (!mostDamageCreature) {
				corpse->setSpecialDescription(
				    fmt::format("You recognize {:s}. {:s} was killed by {:s}{:s}", getNameDescription(),
				                getSex() == PLAYERSEX_FEMALE ? "She" : "He", lastHitCreature->getNameDescription(),
				                killersSize > 1 ? " and others." : "."));
			} else if (lastHitCreature != mostDamageCreature) {
				corpse->setSpecialDescription(
				    fmt::format("You recognize {:s}. {:s} was killed by {:s}, {:s}{:s}", getNameDescription(),
				                getSex() == PLAYERSEX_FEMALE ? "She" : "He", mostDamageCreature->getNameDescription(),
				                lastHitCreature->getNameDescription(), killersSize > 2 ? " and others." : "."));
			} else {
				corpse->setSpecialDescription(
				    fmt::format("You recognize {:s}. {:s} was killed by {:s} and others.", getNameDescription(),
				                getSex() == PLAYERSEX_FEMALE ? "She" : "He", mostDamageCreature->getNameDescription()));
			}
		} else if (mostDamageCreature) {
			if (killersSize > 1) {
				corpse->setSpecialDescription(fmt::format(
				    "You recognize {:s}. {:s} was killed by something evil, {:s}, and others", getNameDescription(),
				    getSex() == PLAYERSEX_FEMALE ? "She" : "He", mostDamageCreature->getNameDescription()));
			} else {
				corpse->setSpecialDescription(fmt::format(
				    "You recognize {:s}. {:s} was killed by something evil and others", getNameDescription(),
				    getSex() == PLAYERSEX_FEMALE ? "She" : "He", mostDamageCreature->getNameDescription()));
			}
		} else {
			corpse->setSpecialDescription(fmt::format("You recognize {:s}. {:s} was killed by something evil {:s}",
			                                          getNameDescription(), getSex() == PLAYERSEX_FEMALE ? "She" : "He",
			                                          killersSize ? " and others." : "."));
		}
	}
	return corpse;
}

void Player::addCombatExhaust(uint32_t ticks)
{
	auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_EXHAUST_COMBAT, ticks, 0);
	addCondition(std::move(condition));
}

void Player::addHealExhaust(uint32_t ticks)
{
	auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_EXHAUST_HEAL, ticks, 0);
	addCondition(std::move(condition));
}

void Player::addInFightTicks(bool pzlock /*= false*/)
{
	if (hasFlag(PlayerFlag_NotGainInFight)) {
		return;
	}

	if (pzlock) {
		pzLocked = true;
	}

	auto condition =
	    Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, getInteger(ConfigManager::PZ_LOCKED), 0);
	addCondition(std::move(condition));
}

// Account Manager functionality removed

void Player::removeList()
{
	g_game.removePlayer(this);

	for (const auto& player : g_game.getPlayers()) {
		player->notifyStatusChange(this, VIPSTATUS_OFFLINE);
	}
}

void Player::addList()
{
	for (const auto& player : g_game.getPlayers()) {
		player->notifyStatusChange(this, VIPSTATUS_ONLINE);
	}

	g_game.addPlayer(this);
}

void Player::kickPlayer(bool displayEffect)
{
	g_creatureEvents->playerLogout(this);
	if (client) {
		client->logout(displayEffect, true);
	}
	g_game.removeCreature(this);
}

void Player::notifyStatusChange(Player* loginPlayer, VipStatus_t status)
{
	if (!client) {
		return;
	}

	auto it = VIPList.find(loginPlayer->guid);
	if (it == VIPList.end()) {
		return;
	}

	client->sendUpdatedVIPStatus(loginPlayer->guid, status);
}

bool Player::removeVIP(uint32_t vipGuid)
{
	if (VIPList.erase(vipGuid) == 0) {
		return false;
	}

	IOLoginData::removeVIPEntry(accountNumber, vipGuid);
	return true;
}

bool Player::addVIP(uint32_t vipGuid, std::string_view vipName, VipStatus_t status)
{
	if (VIPList.size() >= getMaxVIPEntries()) {
		sendTextMessage(MESSAGE_STATUS_SMALL, "You cannot add more buddies.");
		return false;
	}

	auto result = VIPList.insert(vipGuid);
	if (!result.second) {
		sendTextMessage(MESSAGE_STATUS_SMALL, "This player is already in your list.");
		return false;
	}

	IOLoginData::addVIPEntry(accountNumber, vipGuid);
	if (client) {
		client->sendVIP(vipGuid, vipName, status);
	}
	return true;
}

bool Player::addVIPInternal(uint32_t vipGuid)
{
	if (VIPList.size() >= getMaxVIPEntries()) {
		return false;
	}

	return VIPList.insert(vipGuid).second;
}

// close container and its child containers
void Player::autoCloseContainers(const Container* container)
{
	std::vector<uint32_t> closeList;
	closeList.reserve(openContainers.size());
	for (const auto& it : openContainers) {
		auto openContainerRef = it.second.container.lock();
		Container* tmpContainer = openContainerRef.get();
		if (!tmpContainer) {
			closeList.push_back(it.first);
			continue;
		}

		while (tmpContainer) {
			if (tmpContainer->isRemoved() || tmpContainer == container) {
				closeList.push_back(it.first);
				break;
			}

			tmpContainer = dynamic_cast<Container*>(tmpContainer->getParent());
		}
	}

	for (uint32_t containerId : closeList) {
		closeContainer(static_cast<uint8_t>(containerId));
		if (client) {
			client->sendCloseContainer(static_cast<uint8_t>(containerId));
		}
	}
}

bool Player::hasCapacity(const Item* item, uint32_t count) const
{
	if (hasFlag(PlayerFlag_CannotPickupItem)) {
		return false;
	}

	if (hasFlag(PlayerFlag_HasInfiniteCapacity) || item->getTopParent() == this) {
		return true;
	}

	uint32_t itemWeight = item->getContainer() != nullptr ? item->getWeight() : item->getBaseWeight();
	if (item->isStackable()) {
		itemWeight *= count;
	}
	return itemWeight <= getFreeCapacity();
}

ReturnValue Player::queryAdd(int32_t index, const Thing& thing, uint32_t count, uint32_t flags, Creature*) const
{
	const Item* item = thing.getItem();
	if (item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	bool childIsOwner = hasBitSet(FLAG_CHILDISOWNER, flags);
	if (childIsOwner) {
		// a child container is querying the player, just check if enough capacity
		bool skipLimit = hasBitSet(FLAG_NOLIMIT, flags);
		if (skipLimit || hasCapacity(item, count)) {
			return RETURNVALUE_NOERROR;
		}
		return RETURNVALUE_NOTENOUGHCAPACITY;
	}

	if (!item->isPickupable()) {
		return RETURNVALUE_CANNOTPICKUP;
	}

	if (item->isStoreItem()) {
		return RETURNVALUE_ITEMCANNOTBEMOVEDTHERE;
	}

	ReturnValue ret = RETURNVALUE_NOERROR;

	const int32_t& slotPosition = item->getSlotPosition();
	if ((slotPosition & SLOTP_HEAD) || (slotPosition & SLOTP_NECKLACE) || (slotPosition & SLOTP_BACKPACK) ||
	    (slotPosition & SLOTP_ARMOR) || (slotPosition & SLOTP_LEGS) || (slotPosition & SLOTP_FEET) ||
	    (slotPosition & SLOTP_RING)) {
		ret = RETURNVALUE_CANNOTBEDRESSED;
	} else if (slotPosition & SLOTP_TWO_HAND) {
		ret = RETURNVALUE_PUTTHISOBJECTINBOTHHANDS;
	} else if ((slotPosition & SLOTP_RIGHT) || (slotPosition & SLOTP_LEFT)) {
		if (!getBoolean(ConfigManager::CLASSIC_EQUIPMENT_SLOTS)) {
			ret = RETURNVALUE_CANNOTBEDRESSED;
		} else {
			ret = RETURNVALUE_PUTTHISOBJECTINYOURHAND;
		}
	}

	switch (index) {
		case CONST_SLOT_HEAD: {
			if (slotPosition & SLOTP_HEAD) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_NECKLACE: {
			if (slotPosition & SLOTP_NECKLACE) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_BACKPACK: {
			if (slotPosition & SLOTP_BACKPACK) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_ARMOR: {
			if (slotPosition & SLOTP_ARMOR) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_RIGHT: {
			if (slotPosition & SLOTP_RIGHT) {
				if (!getBoolean(ConfigManager::CLASSIC_EQUIPMENT_SLOTS)) {
					if (item->getWeaponType() != WEAPON_SHIELD && item->getWeaponType() != WEAPON_QUIVER) {
						ret = RETURNVALUE_CANNOTBEDRESSED;
					} else {
						const Item* leftItem = inventory[CONST_SLOT_LEFT].get();
						if (leftItem) {
							if ((leftItem->getSlotPosition() | slotPosition) & SLOTP_TWO_HAND) {
								if (leftItem->getWeaponType() == WEAPON_DISTANCE && item->getWeaponType() == WEAPON_QUIVER) {
									ret = RETURNVALUE_NOERROR;
								} else {
									ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
								}
							} else {
								ret = RETURNVALUE_NOERROR;
							}
						} else {
							ret = RETURNVALUE_NOERROR;
						}
					}
				} else if (slotPosition & SLOTP_TWO_HAND) {
					if (inventory[CONST_SLOT_LEFT] && inventory[CONST_SLOT_LEFT].get() != item) {
						ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
					} else {
						ret = RETURNVALUE_NOERROR;
					}
				} else if (inventory[CONST_SLOT_LEFT]) {
					const Item* leftItem = inventory[CONST_SLOT_LEFT].get();
					WeaponType_t type = item->getWeaponType(), leftType = leftItem->getWeaponType();

					if (leftItem->getSlotPosition() & SLOTP_TWO_HAND) {
						if (leftType == WEAPON_DISTANCE && type == WEAPON_QUIVER) {
							ret = RETURNVALUE_NOERROR;
						} else {
							ret = RETURNVALUE_DROPTWOHANDEDITEM;
						}
					} else if (item == leftItem && count == item->getItemCount()) {
						ret = RETURNVALUE_NOERROR;
					} else if (leftType == WEAPON_SHIELD && type == WEAPON_SHIELD) {
						ret = RETURNVALUE_CANONLYUSEONESHIELD;
					} else if (leftType == WEAPON_NONE || type == WEAPON_NONE || leftType == WEAPON_SHIELD ||
					           leftType == WEAPON_AMMO || type == WEAPON_SHIELD || type == WEAPON_AMMO ||
					           type == WEAPON_QUIVER || leftType == WEAPON_QUIVER) {
						ret = RETURNVALUE_NOERROR;
					} else {
						ret = RETURNVALUE_CANONLYUSEONEWEAPON;
					}
				} else {
					ret = RETURNVALUE_NOERROR;
				}
			}
			break;
		}

		case CONST_SLOT_LEFT: {
			if (slotPosition & SLOTP_LEFT) {
				if (!getBoolean(ConfigManager::CLASSIC_EQUIPMENT_SLOTS)) {
					WeaponType_t type = item->getWeaponType();
					if (type == WEAPON_NONE || type == WEAPON_SHIELD || type == WEAPON_QUIVER) {
						ret = RETURNVALUE_CANNOTBEDRESSED;
					} else if (inventory[CONST_SLOT_RIGHT] && (slotPosition & SLOTP_TWO_HAND)) {
						const Item* rightItem = inventory[CONST_SLOT_RIGHT].get();
						if (rightItem->getWeaponType() == WEAPON_QUIVER && item->getWeaponType() == WEAPON_DISTANCE) {
							ret = RETURNVALUE_NOERROR;
						} else {
							ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
						}
					} else {
						ret = RETURNVALUE_NOERROR;
					}
				} else if (slotPosition & SLOTP_TWO_HAND) {
					if (inventory[CONST_SLOT_RIGHT] && inventory[CONST_SLOT_RIGHT].get() != item) {
						ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
					} else {
						ret = RETURNVALUE_NOERROR;
					}
				} else if (inventory[CONST_SLOT_RIGHT]) {
					const Item* rightItem = inventory[CONST_SLOT_RIGHT].get();
					WeaponType_t type = item->getWeaponType(), rightType = rightItem->getWeaponType();

					if (rightItem->getSlotPosition() & SLOTP_TWO_HAND) {
						ret = RETURNVALUE_DROPTWOHANDEDITEM;
					} else if (item == rightItem && count == item->getItemCount()) {
						ret = RETURNVALUE_NOERROR;
					} else if (rightType == WEAPON_SHIELD && type == WEAPON_SHIELD) {
						ret = RETURNVALUE_CANONLYUSEONESHIELD;
					} else if (rightType == WEAPON_NONE || type == WEAPON_NONE || rightType == WEAPON_SHIELD ||
					           rightType == WEAPON_AMMO || type == WEAPON_SHIELD || type == WEAPON_AMMO ||
					           type == WEAPON_QUIVER || rightType == WEAPON_QUIVER) {
						ret = RETURNVALUE_NOERROR;
					} else {
						ret = RETURNVALUE_CANONLYUSEONEWEAPON;
					}
				} else {
					ret = RETURNVALUE_NOERROR;
				}
			}
			break;
		}

		case CONST_SLOT_LEGS: {
			if (slotPosition & SLOTP_LEGS) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_FEET: {
			if (slotPosition & SLOTP_FEET) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_RING: {
			if (slotPosition & SLOTP_RING) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_AMMO: {
			if ((slotPosition & SLOTP_AMMO) || getBoolean(ConfigManager::CLASSIC_EQUIPMENT_SLOTS)) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_WHEREEVER:
		case -1:
			ret = RETURNVALUE_NOTENOUGHROOM;
			break;

		default:
			ret = RETURNVALUE_NOTPOSSIBLE;
			break;
	}

	if (ret != RETURNVALUE_NOERROR && ret != RETURNVALUE_NOTENOUGHROOM) {
		return ret;
	}

	// check if enough capacity
	if (!hasCapacity(item, count)) {
		return RETURNVALUE_NOTENOUGHCAPACITY;
	}

	ret = g_moveEvents->onPlayerEquip(const_cast<Player*>(this), const_cast<Item*>(item), static_cast<slots_t>(index),
	                                  true);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	// need an exchange with source? (destination item is swapped with currently moved item)
	const Item* inventoryItem = getInventoryItem(static_cast<slots_t>(index));
	if (inventoryItem && (!inventoryItem->isStackable() || inventoryItem->getID() != item->getID())) {
		if (!ConfigManager::getBoolean(ConfigManager::CLASSIC_EQUIPMENT_SLOTS)) {
			const Cylinder* cylinder = item->getTopParent();
			if (cylinder && (dynamic_cast<const DepotChest*>(cylinder) || dynamic_cast<const Player*>(cylinder))) {
				return RETURNVALUE_NEEDEXCHANGE;
			}
			return RETURNVALUE_NOTENOUGHROOM;	
		}

		return RETURNVALUE_NEEDEXCHANGE;
	}
	return ret;
}

ReturnValue Player::queryMaxCount(int32_t index, const Thing& thing, uint32_t count, uint32_t& maxQueryCount,
                                  uint32_t flags) const
{
	const Item* item = thing.getItem();
	if (item == nullptr) {
		maxQueryCount = 0;
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (index == INDEX_WHEREEVER) {
		uint32_t n = 0;
		for (int32_t slotIndex = CONST_SLOT_FIRST; slotIndex <= CONST_SLOT_LAST; ++slotIndex) {
			Item* inventoryItem = inventory[slotIndex].get();
			if (inventoryItem) {
				if (Container* subContainer = inventoryItem->getContainer()) {
					uint32_t queryCount = 0;
					subContainer->queryMaxCount(INDEX_WHEREEVER, *item, item->getItemCount(), queryCount, flags);
					n += queryCount;

					// iterate through all items, including sub-containers (deep search)
					for (ContainerIterator it = subContainer->iterator(); it.hasNext(); it.advance()) {
						if (Container* tmpContainer = (*it)->getContainer()) {
							queryCount = 0;
							tmpContainer->queryMaxCount(INDEX_WHEREEVER, *item, item->getItemCount(), queryCount,
							                            flags);
							n += queryCount;
						}
					}
				} else if (inventoryItem->isStackable() && item->equals(inventoryItem) &&
				           inventoryItem->getItemCount() < inventoryItem->getStackSize()) {
					uint32_t remainder = (inventoryItem->getStackSize() - inventoryItem->getItemCount());

					if (queryAdd(slotIndex, *item, remainder, flags) == RETURNVALUE_NOERROR) {
						n += remainder;
					}
				}
			} else if (queryAdd(slotIndex, *item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) { // empty slot
				if (item->isStackable()) {
					n += item->getStackSize();
				} else {
					++n;
				}
			}
		}

		maxQueryCount = n;
	} else {
		const Item* destItem = nullptr;

		const Thing* destThing = getThing(index);
		if (destThing) {
			destItem = destThing->getItem();
		}

		if (destItem) {
			if (destItem->isStackable() && item->equals(destItem) && destItem->getItemCount() < 100) {
				maxQueryCount = 100 - destItem->getItemCount();
			} else {
				maxQueryCount = 0;
			}
		} else if (queryAdd(index, *item, count, flags) == RETURNVALUE_NOERROR) { // empty slot
			if (item->isStackable()) {
				maxQueryCount = 100;
			} else {
				maxQueryCount = 1;
			}

			return RETURNVALUE_NOERROR;
		}
	}

	if (maxQueryCount < count) {
		return RETURNVALUE_NOTENOUGHROOM;
	}
	return RETURNVALUE_NOERROR;
}

ReturnValue Player::queryRemove(const Thing& thing, uint32_t count, uint32_t flags, Creature* /*= nullptr*/) const
{
	int32_t index = getThingIndex(&thing);
	if (index == -1) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	const Item* item = thing.getItem();
	if (item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (count == 0 || (item->isStackable() && count > item->getItemCount())) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!item->isMoveable() && !hasBitSet(FLAG_IGNORENOTMOVEABLE, flags)) {
		return RETURNVALUE_NOTMOVEABLE;
	}

	return RETURNVALUE_NOERROR;
}

Cylinder* Player::queryDestination(int32_t& index, const Thing& thing, Item** destItem, uint32_t& flags)
{
	if (index == 0 /*drop to capacity window*/ || index == INDEX_WHEREEVER) {
		*destItem = nullptr;

		const Item* item = thing.getItem();
		if (item == nullptr) {
			return this;
		}

		bool autoStack = !((flags & FLAG_IGNOREAUTOSTACK) == FLAG_IGNOREAUTOSTACK);
		bool isStackable = item->isStackable();

		std::vector<Container*> containers;

		for (uint32_t slotIndex = CONST_SLOT_FIRST; slotIndex <= CONST_SLOT_LAST; ++slotIndex) {
			Item* inventoryItem = inventory[slotIndex].get();
			if (inventoryItem) {
			if (inventoryItem == tradeItem.lock().get()) {
					continue;
				}

				if (inventoryItem == item) {
					continue;
				}

				if (autoStack && isStackable) {
					// try find an already existing item to stack with
					if (queryAdd(slotIndex, *item, item->getItemCount(), 0) == RETURNVALUE_NOERROR) {
						if (inventoryItem->equals(item) &&
						    inventoryItem->getItemCount() < inventoryItem->getStackSize()) {
							index = slotIndex;
							*destItem = inventoryItem;
							return this;
						}
					}

					if (Container* subContainer = inventoryItem->getContainer()) {
						containers.push_back(subContainer);
					}
				} else if (Container* subContainer = inventoryItem->getContainer()) {
					containers.push_back(subContainer);
				}
			} else if (queryAdd(slotIndex, *item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) { // empty slot
				index = slotIndex;
				*destItem = nullptr;
				return this;
			}
		}

		size_t i = 0;
		while (i < containers.size()) {
			Container* tmpContainer = containers[i++];
			if (!autoStack || !isStackable) {
				// we need to find first empty container as fast as we can for non-stackable items
				uint32_t n = tmpContainer->capacity() -
				             std::min(tmpContainer->capacity(), static_cast<uint32_t>(tmpContainer->size()));
				while (n) {
					if (tmpContainer->queryAdd(tmpContainer->capacity() - n, *item, item->getItemCount(), flags) ==
					    RETURNVALUE_NOERROR) {
						index = tmpContainer->capacity() - n;
						*destItem = nullptr;
						return tmpContainer;
					}

					--n;
				}

				for (const auto& tmpContainerItem : tmpContainer->getItemList()) {
					if (Container* subContainer = tmpContainerItem->getContainer()) {
						containers.push_back(subContainer);
					}
				}

				continue;
			}

			uint32_t n = 0;

			for (const auto& tmpItem : tmpContainer->getItemList()) {
				if (tmpItem.get() == tradeItem.lock().get()) {
					continue;
				}

				if (tmpItem.get() == item) {
					continue;
				}

				// try find an already existing item to stack with
				if (tmpItem->equals(item) && tmpItem->getItemCount() < tmpItem->getStackSize()) {
					index = n;
					*destItem = tmpItem.get();
					return tmpContainer;
				}

				if (Container* subContainer = tmpItem->getContainer()) {
					containers.push_back(subContainer);
				}

				n++;
			}

			if (n < tmpContainer->capacity() &&
			    tmpContainer->queryAdd(n, *item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) {
				index = n;
				*destItem = nullptr;
				return tmpContainer;
			}
		}

		return this;
	}

	Thing* destThing = getThing(index);
	if (destThing) {
		*destItem = destThing->getItem();
	}

	Cylinder* subCylinder = dynamic_cast<Cylinder*>(destThing);
	if (subCylinder) {
		index = INDEX_WHEREEVER;
		*destItem = nullptr;
		return subCylinder;
	}
	return this;
}

void Player::addThing(int32_t index, Thing* thing)
{
	if (index < CONST_SLOT_FIRST || index > CONST_SLOT_LAST) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	item->setParent(this);
	inventory[index] = item->shared_from_this();

	// send to client
	sendInventoryItem(static_cast<slots_t>(index), item);
}

void Player::updateThing(Thing* thing, uint16_t itemId, uint32_t count)
{
	int32_t index = getThingIndex(thing);
	if (index == -1) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	item->setID(itemId);
	item->setSubType(static_cast<uint16_t>(count));

	// send to client
	sendInventoryItem(static_cast<slots_t>(index), item);

	// event methods
	onUpdateInventoryItem(item, item);
}

void Player::replaceThing(uint32_t index, Thing* thing)
{
	if (index > CONST_SLOT_LAST) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* oldItem = getInventoryItem(static_cast<slots_t>(index));
	if (!oldItem) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	// send to client
	sendInventoryItem(static_cast<slots_t>(index), item);

	// event methods
	onUpdateInventoryItem(oldItem, item);

	item->setParent(this);

	inventory[index] = item->shared_from_this();
}

void Player::removeThing(Thing* thing, uint32_t count)
{
	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	int32_t index = getThingIndex(thing);
	if (index == -1) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	if (item->isStackable()) {
		if (count == item->getItemCount()) {
			// send change to client
			sendInventoryItem(static_cast<slots_t>(index), nullptr);

			// event methods
			onRemoveInventoryItem(item);

			item->setParent(nullptr);
			inventory[index].reset();
		} else {
			uint8_t newCount = static_cast<uint8_t>(std::max<int32_t>(0, item->getItemCount() - count));
			item->setItemCount(newCount);

			// send change to client
			sendInventoryItem(static_cast<slots_t>(index), item);

			// event methods
			onUpdateInventoryItem(item, item);
		}
	} else {
		// send change to client
		sendInventoryItem(static_cast<slots_t>(index), nullptr);

		// event methods
		onRemoveInventoryItem(item);

		item->setParent(nullptr);
		inventory[index].reset();
	}
}

int32_t Player::getThingIndex(const Thing* thing) const
{
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		if (inventory[i].get() == thing) {
			return i;
		}
	}
	return -1;
}

size_t Player::getFirstIndex() const { return CONST_SLOT_FIRST; }

size_t Player::getLastIndex() const { return CONST_SLOT_LAST + 1; }

uint32_t Player::getItemTypeCount(uint16_t itemId, int32_t subType /*= -1*/, bool ignoreEquipped /*= false*/) const
{
	uint32_t count = 0;
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i].get();
		if (!item) {
			continue;
		}

		if (!ignoreEquipped && item->getID() == itemId) {
			count += Item::countByType(item, subType);
		}

		if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				if ((*it)->getID() == itemId) {
					count += Item::countByType(*it, subType);
				}
			}
		}
	}
	return count;
}

bool Player::removeItemOfType(uint16_t itemId, uint32_t amount, int32_t subType, bool ignoreEquipped /* = false*/) const
{
	if (amount == 0) {
		return true;
	}

	std::vector<Item*> itemList;

	uint32_t count = 0;
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i].get();
		if (!item) {
			continue;
		}

		if (!ignoreEquipped && item->getID() == itemId) {
			uint32_t itemCount = Item::countByType(item, subType);
			if (itemCount == 0) {
				continue;
			}

			itemList.push_back(item);

			count += itemCount;
			if (count >= amount) {
				g_game.internalRemoveItems(std::move(itemList), amount, Item::items[itemId].stackable);
				return true;
			}
		} else if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				Item* containerItem = *it;
				if (containerItem->getID() == itemId) {
					uint32_t itemCount = Item::countByType(containerItem, subType);
					if (itemCount == 0) {
						continue;
					}

					itemList.push_back(containerItem);

					count += itemCount;
					if (count >= amount) {
						g_game.internalRemoveItems(std::move(itemList), amount, Item::items[itemId].stackable);
						return true;
					}
				}
			}
		}
	}
	return false;
}

std::unordered_map<uint32_t, uint32_t>& Player::getAllItemTypeCount(std::unordered_map<uint32_t, uint32_t>& countMap) const
{
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i].get();
		if (!item) {
			continue;
		}

		countMap[item->getID()] += Item::countByType(item, -1);

		if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				countMap[(*it)->getID()] += Item::countByType(*it, -1);
			}
		}
	}
	return countMap;
}

Thing* Player::getThing(size_t index) const
{
	if (index >= CONST_SLOT_FIRST && index <= CONST_SLOT_LAST) {
		return inventory[index].get();
	}
	return nullptr;
}

void Player::postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index,
                                 cylinderlink_t link /*= LINK_OWNER*/)
{
	if (link == LINK_OWNER) {
		// calling movement scripts
		g_moveEvents->onPlayerEquip(this, thing->getItem(), static_cast<slots_t>(index), false);
		if (isInventorySlot(static_cast<slots_t>(index))) {
			Item* item = thing->getItem();
			if (item && item->hasImbuements()) {
				addItemImbuements(thing->getItem(), static_cast<slots_t>(index));
			}
		}
		g_events->eventPlayerOnUpdateInventory(this, thing->getItem(), static_cast<slots_t>(index), true);
	}

	bool requireListUpdate = false;

	if (link == LINK_OWNER || link == LINK_TOPPARENT) {
		const Item* i = (oldParent ? oldParent->getItem() : nullptr);

		// Check if we owned the old container too, so we don't need to do anything,
		// as the list was updated in postRemoveNotification
		assert(i ? i->getContainer() != nullptr : true);

		if (i) {
			requireListUpdate = static_cast<const Container*>(i)->getHoldingPlayer() != this;
		} else {
			requireListUpdate = oldParent != this;
		}

		updateInventoryWeight();
		updateItemsLight();
		sendStats();
	}

	if (const Item* item = thing->getItem()) {
		if (const Container* container = item->getContainer()) {
			onSendContainer(container);
		}

		if (!shopOwner.expired() && requireListUpdate) {
			updateSaleShopList(item);
		}
	} else if (const Creature* creature = thing->getCreature()) {
		if (creature == this) {
			// check containers
			std::vector<ContainerPtr> containers;

			for (const auto& it : openContainers) {
				auto container = it.second.container.lock();
				if (!container) {
					continue;
				}
				if (!container->getPosition().isInRange(getPosition(), 1, 1, 0)) {
					containers.push_back(container);
				}
			}

			for (const auto& container : containers) {
				autoCloseContainers(container.get());
			}
		}
	}
}

void Player::postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index,
                                    cylinderlink_t link /*= LINK_OWNER*/)
{
	if (link == LINK_OWNER) {
		// calling movement scripts
		g_moveEvents->onPlayerDeEquip(this, thing->getItem(), static_cast<slots_t>(index));
		if (isInventorySlot(static_cast<slots_t>(index))) {
			Item* item = thing->getItem();
			if (item && item->hasImbuements()) {
				removeItemImbuements(thing->getItem(), static_cast<slots_t>(index));
			}
		}
		g_events->eventPlayerOnUpdateInventory(this, thing->getItem(), static_cast<slots_t>(index), false);
	}

	bool requireListUpdate = false;

	if (link == LINK_OWNER || link == LINK_TOPPARENT) {
		const Item* i = (newParent ? newParent->getItem() : nullptr);

		// Check if we owned the old container too, so we don't need to do anything,
		// as the list was updated in postRemoveNotification
		assert(i ? i->getContainer() != nullptr : true);

		if (i) {
			requireListUpdate = static_cast<const Container*>(i)->getHoldingPlayer() != this;
		} else {
			requireListUpdate = newParent != this;
		}

		updateInventoryWeight();
		updateItemsLight();
		sendStats();
	}

	if (const Item* item = thing->getItem()) {
		if (const Container* container = item->getContainer()) {
			if (container->isRemoved() || !getPosition().isInRange(container->getPosition(), 1, 1, 0)) {
				autoCloseContainers(container);
			} else if (container->getTopParent() == this) {
				onSendContainer(container);
			} else if (const Container* topContainer = dynamic_cast<const Container*>(container->getTopParent())) {
				if (const DepotChest* depotChest = dynamic_cast<const DepotChest*>(topContainer)) {
					bool isOwner = false;

					for (const auto& it : depotChests) {
						it.second->stopDecaying();
						if (it.second.get() == depotChest) {
							isOwner = true;
							onSendContainer(container);
						}
					}

					if (!isOwner) {
						autoCloseContainers(container);
					}
				} else {
					onSendContainer(container);
				}
			} else {
				autoCloseContainers(container);
			}
		}

		if (!shopOwner.expired() && requireListUpdate) {
			updateSaleShopList(item);
		}
	}
}

bool Player::updateSaleShopList(const Item* item)
{
	uint16_t itemId = item->getID();
	bool isCurrency = false;
	for (const auto& it : Item::items.currencyItems) {
		if (it.second == itemId) {
			isCurrency = true;
			break;
		}
	}

	if (!isCurrency) {
		auto it = std::find_if(shopItemList.begin(), shopItemList.end(), [itemId](const ShopInfo& shopInfo) {
			return shopInfo.itemId == itemId && shopInfo.sellPrice != 0;
		});
		if (it == shopItemList.end()) {
			const Container* container = item->getContainer();
			if (!container) {
				return false;
			}

			const auto& items = container->getItemList();
			return std::any_of(items.begin(), items.end(),
			                   [this](const std::shared_ptr<Item>& containerItem) { return updateSaleShopList(containerItem.get()); });
		}
	}

	if (client) {
		client->sendSaleItemList(shopItemList);
	}
	return true;
}

bool Player::hasShopItemForSale(uint32_t itemId, uint8_t subType) const
{
	const ItemType& itemType = Item::items[itemId];
	return std::any_of(shopItemList.begin(), shopItemList.end(), [&](const ShopInfo& shopInfo) {
		return shopInfo.itemId == itemId && shopInfo.buyPrice > 0 &&
		       (!itemType.isFluidContainer() || shopInfo.subType == subType);
	});
}

void Player::internalAddThing(Thing* thing) { internalAddThing(0, thing); }

void Player::internalAddThing(uint32_t index, Thing* thing)
{
	Item* item = thing->getItem();
	if (!item) {
		return;
	}

	// index == 0 means we should equip this item at the most appropriate slot (no action required here)
	if (index > CONST_SLOT_WHEREEVER && index <= CONST_SLOT_LAST) {
		if (inventory[index]) {
			return;
		}

		inventory[index] = item->shared_from_this();
		item->setParent(this);
	}
}

bool Player::setFollowCreature(Creature* creature)
{
	if (!Creature::setFollowCreature(creature)) {
		setFollowCreature(nullptr);
		setAttackedCreature(nullptr);

		sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		sendCancelTarget();
		stopWalk();
		return false;
	}
	return true;
}

bool Player::setAttackedCreature(Creature* creature)
{
	if (!Creature::setAttackedCreature(creature)) {
		sendCancelTarget();
		// Stop stamina trainer regeneration if we stop attacking
		staminaTrainerActive = false;
		return false;
	}

	if (isChasingEnabled() && creature) {
		auto follow = followCreature.lock();
		if (follow.get() != creature) {
			// chase opponent
			setFollowCreature(creature);
		}
	} else if (!followCreature.expired()) {
		setFollowCreature(nullptr);
	}

	if (creature) {
		g_dispatcher.addTask([id = getID()]() { g_game.checkCreatureAttack(id); });
	} else {
		// Stop stamina trainer regeneration if we stop attacking
		staminaTrainerActive = false;
	}
	return true;
}

void Player::goToFollowCreature()
{
	if (!walkTask) {
		if ((OTSYS_TIME() - lastFailedFollow) < 2000) {
			return;
		}

		Creature::goToFollowCreature();

		if (!followCreature.expired() && !hasFollowPath) {
			lastFailedFollow = OTSYS_TIME();
		}
	}
}

void Player::getPathSearchParams(const Creature* creature, FindPathParams& fpp) const
{
	Creature::getPathSearchParams(creature, fpp);
	fpp.fullPathSearch = true;
}

void Player::doAttacking(uint32_t)
{
	if (lastAttack == 0) {
		lastAttack = OTSYS_TIME() - getAttackSpeed() - 1;
	}

	if (hasCondition(CONDITION_PACIFIED)) {
		return;
	}

	if ((OTSYS_TIME() - lastAttack) >= getAttackSpeed()) {
		bool result = false;

		Item* tool = getWeapon();
		const Weapon* weapon = g_weapons->getWeapon(tool);
		uint32_t delay = getAttackSpeed();
		bool classicSpeed = getBoolean(ConfigManager::CLASSIC_ATTACK_SPEED);
		bool allowAutoAttackWithoutExhaustion = getBoolean(ConfigManager::ALLOW_AUTO_ATTACK_WITHOUT_EXHAUSTION);

		auto ac = attackedCreature.lock();
		if (weapon) {
			if (!weapon->interruptSwing()) {
				result = weapon->useWeapon(this, tool, ac.get());
			} else if (!classicSpeed && !allowAutoAttackWithoutExhaustion && !canDoAction()) {
				delay = getNextActionTime();
			} else {
				result = weapon->useWeapon(this, tool, ac.get());
			}
		} else {
			result = Weapon::useFist(this, ac.get());
		}

		auto task = createSchedulerTask(std::max<uint32_t>(SCHEDULER_MINTICKS, delay),
		                                          [id = getID()]() { g_game.checkCreatureAttack(id); });

		if (!classicSpeed && !allowAutoAttackWithoutExhaustion) {
			setNextActionTask(std::move(task), false);
		} else {
			g_scheduler.addEvent(std::move(task));
		}

		if (result) {
			lastAttack = OTSYS_TIME();
		}
	}
}

void Player::maintainAttackFlow()
{
	if (!attackedCreature.expired() && !hasCondition(CONDITION_PACIFIED)) {
		bool allowAutoAttackWithoutExhaustion = getBoolean(ConfigManager::ALLOW_AUTO_ATTACK_WITHOUT_EXHAUSTION);
		if (!canDoAction() && !allowAutoAttackWithoutExhaustion) {
			return;
		}

		if ((OTSYS_TIME() - lastAttack) >= getAttackSpeed()) {
			lastAttack = OTSYS_TIME() - getAttackSpeed() + 100;

			auto task = createSchedulerTask(100, [id = getID()]() { g_game.checkCreatureAttack(id); });
			bool classicSpeed = getBoolean(ConfigManager::CLASSIC_ATTACK_SPEED);

			if (!classicSpeed && !allowAutoAttackWithoutExhaustion) {
				setNextActionTask(std::move(task), false);
			} else {
				g_scheduler.addEvent(std::move(task));
			}
		}
	}
}

uint64_t Player::getGainedExperience(const std::shared_ptr<Creature>& attacker) const
{
	if (getBoolean(ConfigManager::EXPERIENCE_FROM_PLAYERS)) {
		Player* attackerPlayer = attacker ? attacker->getPlayer() : nullptr;
		if (attackerPlayer && attacker.get() != this && skillLoss &&
		    std::abs(static_cast<int64_t>(attackerPlayer->getLevel() - level)) <=
		        getInteger(ConfigManager::EXP_FROM_PLAYERS_LEVEL_RANGE)) {
			return std::max<uint64_t>(0, std::floor(getLostExperience() * getDamageRatio(attacker) * 0.75));
		}
	}
	return 0;
}

void Player::onFollowCreature(const Creature* creature)
{
	if (!creature) {
		stopWalk();
	}
}

void Player::setChaseMode(bool mode)
{
	bool prevChaseMode = chaseMode;
	chaseMode = mode;

	if (prevChaseMode != chaseMode) {
		if (chaseMode) {
			auto follow = followCreature.lock();
			auto attacked = attackedCreature.lock();
			if (!follow && attacked) {
				// chase opponent
				setFollowCreature(attacked.get());
			}
		} else if (!attackedCreature.expired()) {
			setFollowCreature(nullptr);
			cancelNextWalk = true;
		}
	}
}

void Player::setFightMode(fightMode_t stance, bool chase, bool secure)
{
	fightMode = stance;
	chaseMode = chase;
	secureMode = secure;

	g_events->eventPlayerOnFightModeChanged(this, fightMode, chaseMode, secureMode);
}

void Player::onWalkAborted()
{
	setNextWalkActionTask(nullptr);
	sendCancelWalk();
}

void Player::onWalkComplete()
{
	if (walkTask) {
		walkTaskEvent = g_scheduler.addEvent(std::move(walkTask));
	}
}

void Player::stopWalk() { cancelNextWalk = true; }

LightInfo Player::getCreatureLight() const
{
	if (internalLight.level > itemsLight.level) {
		return internalLight;
	}
	return itemsLight;
}

int32_t Player::getStepSpeed() const
{
	if (group && group->access) {
		return ConfigManager::getInteger(ConfigManager::MAX_GOD_SPEED);
	}

	const int32_t minSpeed = ConfigManager::getInteger(ConfigManager::PLAYER_MIN_SPEED);
	const int32_t maxSpeed = ConfigManager::getInteger(ConfigManager::PLAYER_MAX_SPEED);
	return std::max<int32_t>(minSpeed, std::min<int32_t>(maxSpeed, getSpeed()));
}

void Player::updateBaseSpeed()
{
	if (!hasFlag(PlayerFlag_SetMaxSpeed)) {
		const int32_t speedPerLevel = ConfigManager::getInteger(ConfigManager::PLAYER_SPEED_PER_LEVEL);
		baseSpeed = vocation->getBaseSpeed() + (speedPerLevel * (level - 1));
	} else {
		baseSpeed = ConfigManager::getInteger(ConfigManager::PLAYER_MAX_SPEED);
	}
}

void Player::updateItemsLight(bool internal /*=false*/)
{
	LightInfo maxLight;

	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		Item* item = inventory[i].get();
		if (item) {
			LightInfo curLight = item->getLightInfo();

			if (curLight.level > maxLight.level) {
				maxLight = std::move(curLight);
			}
		}
	}

	if (itemsLight.level != maxLight.level || itemsLight.color != maxLight.color) {
		itemsLight = maxLight;

		if (!internal) {
			g_game.changeLight(this);
		}
	}
}

void Player::onAddCondition(ConditionType_t type)
{
	Creature::onAddCondition(type);
	sendIcons();
}

void Player::onAddCombatCondition(ConditionType_t type)
{
	switch (type) {
		case CONDITION_POISON:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are poisoned.");
			break;

		case CONDITION_DROWN:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are drowning.");
			break;

		case CONDITION_PARALYZE:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are paralyzed.");
			break;

		case CONDITION_ROOTED:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are rooted.");
			break;

		case CONDITION_FEARED:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are feared.");
			break;

		case CONDITION_DRUNK:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are drunk.");
			break;

		case CONDITION_CURSED:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are cursed.");
			break;

		case CONDITION_AGONY:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are agonyed.");
			break;

		case CONDITION_FREEZING:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are freezing.");
			break;

		case CONDITION_DAZZLED:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are dazzled.");
			break;

		case CONDITION_BLEEDING:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are bleeding.");
			break;

		default:
			break;
	}
}

void Player::onEndCondition(ConditionType_t type)
{
	Creature::onEndCondition(type);

	if (type == CONDITION_INFIGHT) {
		onIdleStatus();
		pzLocked = false;
		clearAttacked();

		updateSkullAfterPzLockEnded();
	}

	sendIcons();
}

void Player::onCombatRemoveCondition(Condition* condition)
{
	// Creature::onCombatRemoveCondition(condition);
	if (condition->getId() > 0) {
		// Means the condition is from an item, id == slot
		if (g_game.getWorldType() == WORLD_TYPE_PVP_ENFORCED) {
			Item* item = getInventoryItem(static_cast<slots_t>(condition->getId()));
			if (item) {
				// 25% chance to destroy the item
				if (25 >= uniform_random(1, 100)) {
					g_game.internalRemoveItem(item);
				}
			}
		}
	} else {
		if (!canDoAction()) {
			const uint32_t delay = getNextActionTime();
			const int32_t ticks = delay - (delay % EVENT_CREATURE_THINK_INTERVAL);
			if (ticks < 0) {
				removeCondition(condition);
			} else {
				condition->setTicks(ticks);
			}
		} else {
			removeCondition(condition);
		}
	}
}

void Player::onAttackedCreature(const std::shared_ptr<Creature>& target, bool addFightTicks /* = true */)
{
	Creature::onAttackedCreature(target);

	// Check if attacking a Trainer for stamina regeneration
	if (ConfigManager::getBoolean(ConfigManager::STAMINA_TRAINER) && target && isTrainerTarget(target.get())) {
		staminaTrainerDelayMs = ConfigManager::getInteger(ConfigManager::STAMINA_TRAINER_DELAY) * 60 * 1000;
		staminaTrainerActive = true;
		staminaTrainerTicks = 0;
	}

	if (!target) {
		return;
	}

	if (target->getZone() == ZONE_PVP) {
		return;
	}

	if (target.get() == this) {
		if (addFightTicks) {
			addInFightTicks();
		}
		return;
	}

	if (hasFlag(PlayerFlag_NotGainInFight)) {
		return;
	}

	Player* targetPlayer = target->getPlayer();
	if (targetPlayer && !isPartner(targetPlayer) && !isGuildMate(targetPlayer)) {
		if (!pzLocked && g_game.getWorldType() == WORLD_TYPE_PVP_ENFORCED) {
			pzLocked = true;
			sendIcons();
		}

		targetPlayer->addInFightTicks();

		if (getSkull() == SKULL_NONE && getSkullClient(targetPlayer) == SKULL_YELLOW) {
			addAttacked(targetPlayer);
			targetPlayer->sendCreatureSkull(this);
		} else {
			if (!targetPlayer->hasAttacked(this)) {
				if (!pzLocked && g_game.getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
					pzLocked = true;
					sendIcons();
				}

				if (!Combat::isInPvpZone(this, targetPlayer) && !isInWar(targetPlayer)) {
					addAttacked(targetPlayer);

					if (targetPlayer->getSkull() == SKULL_NONE && getSkull() == SKULL_NONE) {
						setSkull(SKULL_WHITE);
					}

					if (getSkull() == SKULL_NONE) {
						targetPlayer->sendCreatureSkull(this);
					}
				}
			}
		}
	}

	if (addFightTicks) {
		addInFightTicks();
	}
}

void Player::onAttacked()
{
	Creature::onAttacked();

	addInFightTicks();
}

void Player::onIdleStatus()
{
	Creature::onIdleStatus();

	if (auto p = party.lock()) {
		p->clearPlayerPoints(this);
	}
}

void Player::onPlacedCreature()
{
	// scripting event - onLogin
	if (!g_creatureEvents->playerLogin(this)) {
		kickPlayer(true);
	}
}

void Player::onAttackedCreatureDrainHealth(const std::shared_ptr<Creature>& target, int32_t points)
{
	Creature::onAttackedCreatureDrainHealth(target, points);

	if (target) {
		if (auto p = party.lock(); p && !Combat::isPlayerCombat(target.get())) {
			Monster* tmpMonster = target->getMonster();
			if (tmpMonster && tmpMonster->isHostile()) {
				// We have fulfilled a requirement for shared experience
				p->updatePlayerTicks(this, points);
			}
		}
	}
}

void Player::onTargetCreatureGainHealth(const std::shared_ptr<Creature>& target, int32_t points)
{
	if (target && !party.expired()) {
		Player* tmpPlayer = nullptr;

		if (target->getPlayer()) {
			tmpPlayer = target->getPlayer();
		} else if (auto targetMaster = target->getMaster()) {
			if (Player* targetMasterPlayer = targetMaster->getPlayer()) {
				tmpPlayer = targetMasterPlayer;
			}
		}

		if (isPartner(tmpPlayer)) {
			party.lock()->updatePlayerTicks(this, points);
		}
	}
}

bool Player::onKilledCreature(const std::shared_ptr<Creature>& target, bool lastHit /* = true*/)
{
	bool unjustified = false;

	if (hasFlag(PlayerFlag_NotGenerateLoot)) {
		target->setDropLoot(false);
	}

	Creature::onKilledCreature(target, lastHit);

	Player* targetPlayer = target->getPlayer();
	if (!targetPlayer) {
		return false;
	}

	if (targetPlayer->getZone() == ZONE_PVP) {
		targetPlayer->setDropLoot(false);
		targetPlayer->setSkillLoss(false);
	} else if (!hasFlag(PlayerFlag_NotGainInFight) && !isPartner(targetPlayer)) {
		if (!Combat::isInPvpZone(this, targetPlayer) && hasAttacked(targetPlayer) && !targetPlayer->hasAttacked(this) &&
		    !isGuildMate(targetPlayer) && targetPlayer != this) {
			if (targetPlayer->getSkull() == SKULL_NONE && !isInWar(targetPlayer)) {
				unjustified = true;
				addUnjustifiedDead(targetPlayer);
			}

			if (lastHit && hasCondition(CONDITION_INFIGHT)) {
				addInFightTicks(true);
				sendIcons();
			}
		}
	}

	// Register guild war kill
	if (lastHit && isInWar(targetPlayer)) {
		IOGuild::registerGuildWarKill(this, targetPlayer);
	}

	return unjustified;
}

void Player::gainExperience(uint64_t gainExp, const std::shared_ptr<Creature>& source)
{
	if (hasFlag(PlayerFlag_NotGainExperience) || gainExp == 0 || staminaMinutes == 0) {
		return;
	}

	addExperience(source, gainExp, true);
}

void Player::onGainExperience(uint64_t gainExp, const std::shared_ptr<Creature>& target)
{
	if (hasFlag(PlayerFlag_NotGainExperience)) {
		return;
	}

	if (auto p = party.lock(); target && !target->getPlayer() && p && p->isSharedExperienceActive() &&
	    p->isSharedExperienceEnabled()) {
		p->shareExperience(gainExp, target);
		// We will get a share of the experience through the sharing mechanism
		return;
	}

	Creature::onGainExperience(gainExp, target);
	gainExperience(gainExp, target);
}

void Player::onGainSharedExperience(uint64_t gainExp, const std::shared_ptr<Creature>& source)
{
	gainExperience(gainExp, source);
}

bool Player::isImmune(CombatType_t type) const
{
	if (hasFlag(PlayerFlag_CannotBeAttacked)) {
		return true;
	}
	return Creature::isImmune(type);
}

bool Player::isImmune(ConditionType_t type) const
{
	if (hasFlag(PlayerFlag_CannotBeAttacked)) {
		return true;
	}
	if (type == CONDITION_ROOTED && isRootImmune()) {
		return true;
	}
	if (type == CONDITION_FEARED && isFearImmune()) {
		return true;
	}
	return Creature::isImmune(type);
}

void Player::setRootImmunity()
{
	rootImmunityEnd = OTSYS_TIME() + 30000;
}

bool Player::isRootImmune() const
{
	return OTSYS_TIME() <= rootImmunityEnd;
}

void Player::setFearImmunity()
{
	fearImmunityEnd = OTSYS_TIME() + 11000;
}

bool Player::isFearImmune() const
{
	return OTSYS_TIME() <= fearImmunityEnd;
}

bool Player::isAttackable() const
{
	if (isAccountManager()) {
		return false;
	}
	return !hasFlag(PlayerFlag_CannotBeAttacked);
}

bool Player::lastHitIsPlayer(Creature* lastHitCreature)
{
	if (!lastHitCreature) {
		return false;
	}

	if (lastHitCreature->getPlayer()) {
		return true;
	}

	auto lastHitMaster = lastHitCreature->getMaster();
	return lastHitMaster && lastHitMaster->getPlayer();
}

void Player::changeHealth(int32_t healthChange, bool sendHealthChange /* = true*/)
{
	Creature::changeHealth(healthChange, sendHealthChange);
	sendStats();
}

void Player::changeMana(int32_t manaChange)
{
	if (!hasFlag(PlayerFlag_HasInfiniteMana)) {
		if (manaChange > 0) {
			mana += std::min<int32_t>(manaChange, getMaxMana() - mana);
		} else {
			mana = std::max<int32_t>(0, mana + manaChange);
		}
	}

	sendStats();
}

void Player::changeSoul(int32_t soulChange)
{
	if (soulChange > 0) {
		soul += static_cast<uint8_t>(std::min<int32_t>(soulChange, vocation->getSoulMax() - soul));
	} else {
		soul = static_cast<uint8_t>(std::max<int32_t>(0, soul + soulChange));
	}

	sendStats();
}

bool Player::canWear(uint32_t lookType, uint8_t addons) const
{
	if (group->access) {
		return true;
	}

	const Outfit* outfit = Outfits::getInstance().getOutfitByLookType(static_cast<uint16_t>(lookType));
	if (!outfit) {
		return false;
	}

	if (!ConfigManager::getBoolean(ConfigManager::MONK_VOCATION_ENABLED) && outfit->name == "Monk") {
		return false;
	}

	if (outfit->premium && !isPremium()) {
		return false;
	}

	if (outfit->unlocked && addons == 0) {
		return true;
	}

	for (const auto& [outfitLookType, addon] : outfits) {
		if (outfitLookType == lookType) {
			if (addon == addons || addon == 3 || addons == 0) {
				return true;
			}
			return false; // have lookType on list and addons don't match
		}
	}
	return false;
}

bool Player::hasOutfit(uint32_t lookType, uint8_t addons)
{
	const Outfit* outfit = Outfits::getInstance().getOutfitByLookType(static_cast<uint16_t>(lookType));
	if (!outfit) {
		return false;
	}

	if (!ConfigManager::getBoolean(ConfigManager::MONK_VOCATION_ENABLED) && outfit->name == "Monk") {
		return false;
	}

	if (outfit->unlocked && addons == 0) {
		return true;
	}

	for (const auto& [outfitLookType, addon] : outfits) {
		if (outfitLookType == lookType) {
			if (addon == addons || addon == 3 || addons == 0) {
				return true;
			}
			return false; // have lookType on list and addons don't match
		}
	}
	return false;
}

void Player::addOutfit(uint16_t lookType, uint8_t addons)
{
	for (auto& [outfit, addon] : outfits) {
		if (outfit == lookType) {
			addon |= addons;
			return;
		}
	}
	outfits.emplace(lookType, addons);
}

bool Player::removeOutfit(uint16_t lookType)
{
	return outfits.erase(lookType) != 0;
}

bool Player::removeOutfitAddon(uint16_t lookType, uint8_t addons)
{
	for (auto& [outfit, addon] : outfits) {
		if (outfit == lookType) {
			addon &= ~addons;
			return true;
		}
	}
	return false;
}

bool Player::getOutfitAddons(const Outfit& outfit, uint8_t& addons) const
{
	if (group->access) {
		addons = 3;
		return true;
	}

	if (outfit.premium && !isPremium()) {
		return false;
	}

	for (const auto& [lookType, addon] : outfits) {
		if (lookType != outfit.lookType) {
			continue;
		}

		addons = addon;
		return true;
	}

	if (!outfit.unlocked) {
		return false;
	}

	addons = 0;
	return true;
}

bool Player::changeOutfit(Outfit_t outfit, bool checkList)
{
	if (checkList && !canWear(outfit.lookType, outfit.lookAddons)) {
		return false;
	}

	if (outfitAttributes) {
		uint32_t oldId = Outfits::getInstance().getOutfitId(sex, defaultOutfit.lookType);
		Outfits::getInstance().removeAttributes(getID(), oldId, sex);
		outfitAttributes = false;
	}

	defaultOutfit = outfit;

	if (outfit.lookAddons >= getInteger(ConfigManager::MAX_ADDON_ATTRIBUTES)) {
		uint32_t outfitId = Outfits::getInstance().getOutfitId(sex, outfit.lookType);
		outfitAttributes = Outfits::getInstance().addAttributes(getID(), outfitId, sex);
	} else {
		outfitAttributes = false;
	}

	return true;
}

void Player::setSex(PlayerSex_t newSex) { sex = newSex; }

Skulls_t Player::getSkull() const
{
	if (hasFlag(PlayerFlag_NotGainInFight)) {
		return SKULL_NONE;
	}
	return skull;
}

void Player::doReset() // reset system
{
	++reset;
	uint32_t bonusReset = reset * getInteger(ConfigManager::RESET_STATBONUS);
	capacity += bonusReset;

	// Reset to level 8 stats
	experience = Player::getExpForLevel(8);
	level = 8;
	levelPercent = 0;

	if (getBoolean(ConfigManager::RESET_SKILLS)) {
		magLevel = 0;
		magLevelPercent = 0;
		manaSpent = 0;

		for (int i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
			skills[i].level = 10;
			skills[i].tries = 0;
			skills[i].percent = 0;
		}
	}

	health = getMaxHealth();
	mana = getMaxMana();

	// Persist immediately
	Database::getInstance().executeQuery(
		fmt::format("UPDATE `players` SET `reset` = {:d} WHERE `id` = {:d}", reset, getGUID()));

	sendStats();
	sendSkills();
}

Skulls_t Player::getSkullClient(const Creature* creature) const
{
	if (!creature) {
		return SKULL_NONE;
	}

	// Influenced monsters always show green skull regardless of world type
	if (const Monster* monster = creature->getMonster()) {
		if (monster->isInfluenced()) {
			return SKULL_GREEN;
		}
	}

	if (g_game.getWorldType() != WORLD_TYPE_PVP) {
		return SKULL_NONE;
	}

	const Player* player = creature->getPlayer();
	if (!player || player->getSkull() != SKULL_NONE) {
		return Creature::getSkullClient(creature);
	}

	if (player->hasAttacked(this)) {
		return SKULL_YELLOW;
	}

	if (auto p = party.lock(); p && p == player->party.lock()) {
		return SKULL_GREEN;
	}
	return Creature::getSkullClient(creature);
}

bool Player::hasAttacked(const Player* attacked) const
{
	if (hasFlag(PlayerFlag_NotGainInFight) || !attacked) {
		return false;
	}

	return attackedSet.contains(attacked->guid);
}

void Player::addAttacked(const Player* attacked)
{
	if (hasFlag(PlayerFlag_NotGainInFight) || !attacked || attacked == this) {
		return;
	}

	attackedSet.insert(attacked->guid);
}

void Player::removeAttacked(const Player* attacked)
{
	if (!attacked || attacked == this) {
		return;
	}

	auto it = attackedSet.find(attacked->guid);
	if (it != attackedSet.end()) {
		attackedSet.erase(it);
	}
}

void Player::clearAttacked() { attackedSet.clear(); }

void Player::updateSkullAfterPzLockEnded()
{
	const Skulls_t currentSkull = getSkull();

	if (currentSkull == SKULL_YELLOW || currentSkull == SKULL_WHITE) {
		setSkull(SKULL_NONE);
		sendIcons();
		return;
	}

	if (currentSkull == SKULL_RED) {
		if (skullTicks < 1) {
			setSkull(SKULL_NONE);
			sendIcons();
		}
	}
}

void Player::addUnjustifiedDead(const Player* attacked)
{
	if (hasFlag(PlayerFlag_NotGainInFight) || attacked == this || g_game.getWorldType() == WORLD_TYPE_PVP_ENFORCED) {
		return;
	}

	sendTextMessage(MESSAGE_EVENT_ADVANCE, "Warning! The murder of " + attacked->getName() + " was not justified.");

	skullTicks += getInteger(ConfigManager::FRAG_TIME);

	if (getSkull() != SKULL_BLACK) {
		if (getInteger(ConfigManager::KILLS_TO_BLACK) != 0 &&
		    skullTicks > (getInteger(ConfigManager::KILLS_TO_BLACK) - 1) * getInteger(ConfigManager::FRAG_TIME)) {
			setSkull(SKULL_BLACK);
			sendIcons();
		} else if (getSkull() != SKULL_RED && getInteger(ConfigManager::KILLS_TO_RED) != 0 &&
		           skullTicks > (getInteger(ConfigManager::KILLS_TO_RED) - 1) * getInteger(ConfigManager::FRAG_TIME)) {
			setSkull(SKULL_RED);
			sendIcons();
		}
	}
}

void Player::checkSkullTicks(int64_t ticks)
{
	if (skullTicks < 1) {
		if (skull == SKULL_RED && !pzLocked) {
			setSkull(SKULL_NONE);
			sendIcons();
		} else if (skull == SKULL_BLACK && !hasCondition(CONDITION_INFIGHT)) {
			setSkull(SKULL_NONE);
			sendIcons();
		}
		return;
	}

	if (getZone() == ZONE_PROTECTION) {
		return;
	}

	int64_t newTicks = skullTicks - ticks;
	if (newTicks < 0) {
		skullTicks = 0;
	} else {
		skullTicks = newTicks;
	}

	if (skull == SKULL_RED && skullTicks < 1) {
		if (!pzLocked) {
			setSkull(SKULL_NONE);
			sendIcons();
		}
	} else if (skull == SKULL_BLACK && skullTicks < 1 && !hasCondition(CONDITION_INFIGHT)) {
		setSkull(SKULL_NONE);
		sendIcons();
	}
}

bool Player::isPromoted() const
{
	uint16_t promotedVocation = g_vocations.getPromotedVocation(vocation->getId());
	return promotedVocation == VOCATION_NONE && vocation->getId() != promotedVocation;
}

double Player::getLostPercent() const
{
	int64_t deathLosePercent = getInteger(ConfigManager::DEATH_LOSE_PERCENT);
	if (deathLosePercent != -1) {
		if (isPromoted()) {
			deathLosePercent -= 3;
		}

		deathLosePercent -= blessings.count();
		deathLosePercent -= totalReduceSkillLoss;

		return std::max<int32_t>(0, deathLosePercent) / 100.;
	}

	double lossPercent;
	if (level >= 25) {
		double tmpLevel = level + (levelPercent / 100.);
		lossPercent =
		    static_cast<double>((tmpLevel + 50) * 50 * ((tmpLevel * tmpLevel) - (5 * tmpLevel) + 8)) / experience;
	} else {
		lossPercent = 10;
	}

	double percentReduction = 0;
	if (isPromoted()) {
		percentReduction += 30;
	}
	percentReduction += blessings.count() * 8;
	percentReduction += totalReduceSkillLoss;

	return lossPercent * (1 - (percentReduction / 100.)) / 100.;
}

void Player::learnInstantSpell(std::string_view spellName)
{
	if (!hasLearnedInstantSpell(spellName)) {
		learnedInstantSpellList.push_front(std::string{spellName});
	}
}

void Player::forgetInstantSpell(const std::string& spellName) { learnedInstantSpellList.remove(spellName); }

bool Player::hasLearnedInstantSpell(std::string_view spellName) const
{
	if (hasFlag(PlayerFlag_CannotUseSpells)) {
		return false;
	}

	if (hasFlag(PlayerFlag_IgnoreSpellCheck)) {
		return true;
	}

	for (std::string_view learnedSpellName : learnedInstantSpellList) {
		if (caseInsensitiveEqual(learnedSpellName, spellName)) {
			return true;
		}
	}
	return false;
}

bool Player::isInWar(const Player* player) const
{
	const auto guild = getGuild();
	if (!player || !guild) {
		return false;
	}

	const auto& playerGuild = player->getGuild();
	if (!playerGuild) {
		return false;
	}

	return isInWarList(playerGuild->getId()) && player->isInWarList(guild->getId());
}

bool Player::isInWarList(uint32_t guildId) const
{
	return std::find(guildWarVector.begin(), guildWarVector.end(), guildId) != guildWarVector.end();
}

bool Player::isPremium() const
{
	if (getBoolean(ConfigManager::FREE_PREMIUM) || hasFlag(PlayerFlag_IsAlwaysPremium)) {
		return true;
	}

	return premiumEndsAt > time(nullptr);
}

void Player::setPremiumTime(time_t premiumEndsAt) { this->premiumEndsAt = premiumEndsAt; }

PartyShields_t Player::getPartyShield(const Player* player) const
{
	if (!player) {
		return SHIELD_NONE;
	}

	if (auto p = party.lock()) {
		if (p->getLeader().get() == player) {
			if (p->isSharedExperienceActive()) {
				if (p->isSharedExperienceEnabled()) {
					return SHIELD_YELLOW_SHAREDEXP;
				}

				if (p->canUseSharedExperience(player)) {
					return SHIELD_YELLOW_NOSHAREDEXP;
				}

				return SHIELD_YELLOW_NOSHAREDEXP_BLINK;
			}

			return SHIELD_YELLOW;
		}

		if (player->party.lock() == p) {
			if (p->isSharedExperienceActive()) {
				if (p->isSharedExperienceEnabled()) {
					return SHIELD_BLUE_SHAREDEXP;
				}

				if (p->canUseSharedExperience(player)) {
					return SHIELD_BLUE_NOSHAREDEXP;
				}

				return SHIELD_BLUE_NOSHAREDEXP_BLINK;
			}

			return SHIELD_BLUE;
		}

		if (isInviting(player)) {
			return SHIELD_WHITEBLUE;
		}
	}

	if (player->isInviting(this)) {
		return SHIELD_WHITEYELLOW;
	}

	return SHIELD_NONE;
}

bool Player::isInviting(const Player* player) const
{
	auto p = party.lock();
	if (!player || !p || p->getLeader().get() != this) {
		return false;
	}
	return p->isPlayerInvited(player);
}

bool Player::isPartner(const Player* player) const
{
	if (!player || party.expired() || player == this) {
		return false;
	}
	return party.lock() == player->party.lock();
}

bool Player::isGuildMate(const Player* player) const
{
	const auto guild = getGuild();
	if (!player || !guild) {
		return false;
	}
	return guild == player->getGuild();
}

void Player::sendPlayerPartyIcons(Player* player)
{
	sendCreatureShield(player);
	sendCreatureSkull(player);
}

bool Player::addPartyInvitation(Party* party)
{
	for (const auto& weakParty : invitePartyList) {
		if (auto shared = weakParty.lock()) {
			if (shared.get() == party) {
				return false;
			}
		}
	}

	invitePartyList.push_front(party->shared_from_this());
	return true;
}

void Player::removePartyInvitation(Party* party)
{
	invitePartyList.remove_if([party](const std::weak_ptr<Party>& weakParty) {
		if (weakParty.expired()) return true;
		return weakParty.lock().get() == party;
	});
}

void Player::clearPartyInvitations()
{
	for (const auto& weakParty : invitePartyList) {
		if (auto invitingParty = weakParty.lock()) {
			invitingParty->removeInvite(*this, false);
		}
	}
	invitePartyList.clear();
}

GuildEmblems_t Player::getGuildEmblem(const Player* player) const
{
	if (!player) {
		return GUILDEMBLEM_NONE;
	}

	if (player->getEmblem() != GUILDEMBLEM_NONE) {
		return player->getEmblem();
	}

	const auto& playerGuild = player->getGuild();
	if (!playerGuild) {
		return GUILDEMBLEM_NONE;
	}

	if (getGuild() == playerGuild) {
		return GUILDEMBLEM_ALLY;
	} else if (isInWar(player)) {
		return GUILDEMBLEM_ENEMY;
	}

	return GUILDEMBLEM_NEUTRAL;
}

void Player::reloadWarList(bool updateVisuals)
{
	const auto guild = getGuild();
	if (!guild) {
		return;
	}

	guildWarVector.clear();

	Database& db = Database::getInstance();
	std::ostringstream query;
	query << "SELECT `guild1`, `guild2` FROM `guild_wars` WHERE (`guild1` = " << guild->getId() << " OR `guild2` = " << guild->getId() << ") AND `status` = 1";

	DBResult_ptr result = db.storeQuery(query.str());
	if (result) {
		do {
			uint32_t guild1 = result->getNumber<uint32_t>("guild1");
			uint32_t guild2 = result->getNumber<uint32_t>("guild2");
			if (guild1 == guild->getId()) {
				guildWarVector.push_back(guild2);
			} else {
				guildWarVector.push_back(guild1);
			}
		} while (result->next());
	}

	if (updateVisuals) {
		g_game.updateCreatureEmblem(this);
	}
}

uint16_t Player::getRandomMount() const
{
	std::vector<uint16_t> mountsId;
	for (const auto& [id, mount] : g_game.mounts.getMounts()) {
		if (hasMount(&mount)) {
			mountsId.push_back(mount.id);
		}
	}

	if (mountsId.empty()) {
		return 0;
	}

	return mountsId[uniform_random(0, mountsId.size() - 1)];
}

uint16_t Player::getCurrentMount() const { return currentMount; }

void Player::setCurrentMount(uint16_t mountId) { currentMount = mountId; }

bool Player::toggleMount(bool mount)
{
	if ((OTSYS_TIME() - lastToggleMount) < 3000 && !wasMounted) {
		int32_t msLeft = 3000 - static_cast<int32_t>(OTSYS_TIME() - lastToggleMount);
		double seconds = msLeft / 1000.0;
		std::string msg = "You must wait " + fmt::format("{:.1f}", seconds) + "s before using the mount again.";
		sendTextMessage(MESSAGE_STATUS_SMALL, msg);
		return false;
	}

	if (mount) {
		if (isMounted()) {
			return false;
		}

		if (!group->access && getTile() && getTile()->hasFlag(TILESTATE_PROTECTIONZONE)
						   && !ConfigManager::getBoolean(ConfigManager::ALLOW_MOUNT_IN_PZ)) {
			sendCancelMessage(RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE);
			return false;
		}

		const Outfit* playerOutfit = Outfits::getInstance().getOutfitByLookType(defaultOutfit.lookType);
		if (!playerOutfit) {
			return false;
		}

		uint16_t currentMountId = getCurrentMount();
		if (currentMountId == 0) {
			sendOutfitWindow();
			return false;
		}

		Mount* currentMount = g_game.mounts.getMountByID(currentMountId);
		if (!currentMount) {
			return false;
		}

		if (!hasMount(currentMount)) {
			setCurrentMount(0);
			sendOutfitWindow();
			return false;
		}

		if (currentMount->premium && !isPremium()) {
			sendCancelMessage(RETURNVALUE_YOUNEEDPREMIUMACCOUNT);
			return false;
		}

		if (hasCondition(CONDITION_OUTFIT)) {
			sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return false;
		}

		defaultOutfit.lookMount = currentMount->clientId;

		if (currentMount->speed != 0) {
			g_game.changeSpeed(this, currentMount->speed);
		}

		changeMount(currentMount->id, false);
	} else {
		if (!isMounted()) {
			return false;
		}

		dismount();
	}

	g_game.internalCreatureChangeOutfit(this, defaultOutfit);
	lastToggleMount = OTSYS_TIME();
	return true;
}

bool Player::tameMount(uint16_t mountId)
{
	Mount* mount = g_game.mounts.getMountByID(mountId);
	if (!mount || hasMount(mount)) {
		return false;
	}

	mounts.insert(mountId);
	return true;
}

bool Player::untameMount(uint16_t mountId)
{
	Mount* mount = g_game.mounts.getMountByID(mountId);
	if (!mount || hasMount(mount)) {
		return false;
	}

	mounts.erase(mountId);

	if (getCurrentMount() == mountId) {
		if (isMounted()) {
			dismount();
			g_game.internalCreatureChangeOutfit(this, defaultOutfit);
		}

		setCurrentMount(0);
	}

	return true;
}

bool Player::hasMount(const Mount* mount) const
{
	if (isAccessPlayer()) {
		return true;
	}

	if (mount->premium && !isPremium()) {
		return false;
	}

	return mounts.contains(mount->id);
}

bool Player::hasMounts() const
{
	for (const auto& [id, mount] : g_game.mounts.getMounts()) {
		if (hasMount(&mount)) {
			return true;
		}
	}
	return false;
}

void Player::dismount()
{
	Mount* mount = g_game.mounts.getMountByID(getCurrentMount());
	if (mount && mount->speed > 0) {
		g_game.changeSpeed(this, -mount->speed);
	}

	if (mountAttributes && mount) {
		g_game.mounts.removeAttributes(getID(), mount->id);
		mountAttributes = false;
	}

	defaultOutfit.lookMount = 0;
}

bool Player::changeMount(uint16_t mountId, bool checkList)
{
	Mount* mount = g_game.mounts.getMountByID(mountId);
	if (!mount) {
		return false;
	}

	if (checkList && !hasMount(mount)) {
		return false;
	}

	if (mountAttributes) {
		Mount* currentMount = g_game.mounts.getMountByID(getCurrentMount());
		if (currentMount) {
			mountAttributes = !g_game.mounts.removeAttributes(getID(), currentMount->id);
		}
	}

	mountAttributes = g_game.mounts.addAttributes(getID(), mountId);
	setCurrentMount(mountId);
	return true;
}

bool Player::hasModalWindowOpen(uint32_t modalWindowId) const
{
	return find(modalWindows.begin(), modalWindows.end(), modalWindowId) != modalWindows.end();
}

void Player::onModalWindowHandled(uint32_t modalWindowId) { modalWindows.remove(modalWindowId); }

void Player::sendModalWindow(const ModalWindow& modalWindow)
{
	if (!client) {
		return;
	}

	modalWindows.push_front(modalWindow.id);
	client->sendModalWindow(modalWindow);
}

void Player::clearModalWindows() { modalWindows.clear(); }

void Player::sendClosePrivate(uint16_t channelId)
{
	if (channelId == CHANNEL_GUILD || channelId == CHANNEL_PARTY) {
		g_chat->removeUserFromChannel(*this, channelId);
	}

	if (client) {
		client->sendClosePrivate(channelId);
	}
}

uint64_t Player::getMoney() const
{
	std::vector<const Container*> containers;
	uint64_t moneyCount = 0;

	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		Item* item = inventory[i].get();
		if (!item) {
			continue;
		}

		const Container* container = item->getContainer();
		if (container) {
			containers.push_back(container);
		} else {
			moneyCount += item->getWorth();
		}
	}

	size_t i = 0;
	while (i < containers.size()) {
		const Container* container = containers[i++];
		for (const auto& item : container->getItemList()) {
			const Container* tmpContainer = item->getContainer();
			if (tmpContainer) {
				containers.push_back(tmpContainer);
			} else {
				moneyCount += item->getWorth();
			}
		}
	}
	return moneyCount;
}

size_t Player::getMaxVIPEntries() const
{
	if (group->maxVipEntries != 0) {
		return group->maxVipEntries;
	}

	return getInteger(isPremium() ? ConfigManager::VIP_PREMIUM_LIMIT : ConfigManager::VIP_FREE_LIMIT);
}

size_t Player::getMaxDepotItems() const
{
	if (group->maxDepotItems != 0) {
		return group->maxDepotItems;
	}

	return getInteger(isPremium() ? ConfigManager::DEPOT_PREMIUM_LIMIT : ConfigManager::DEPOT_FREE_LIMIT);
}

std::forward_list<Condition*> Player::getMuteConditions() const
{
	std::forward_list<Condition*> muteConditions;
	for (const auto& condition : conditions) {
		if (condition->getTicks() <= 0) {
			continue;
		}

		ConditionType_t type = condition->getType();
		if (type != CONDITION_MUTED && type != CONDITION_CHANNELMUTEDTICKS && type != CONDITION_YELLTICKS) {
			continue;
		}

		muteConditions.push_front(condition.get());
	}
	return muteConditions;
}

void Player::setGuild(Guild_ptr guild)
{
	auto oldGuild = getGuild();
	if (guild == oldGuild) {
		return;
	}

	this->guildNick.clear();
	this->guild.reset();
	this->guildRank.reset();

	if (guild) {
		const auto& rank = guild->getRankByLevel(Guild::MEMBER_RANK_LEVEL_DEFAULT);
		if (!rank) {
			return;
		}

		this->guild = guild;
		this->guildRank = rank;
		guild->addMember(this);
	}

	if (oldGuild) {
		oldGuild->removeMember(this);
	}
}

// Autoloot
void Player::sendAutoLootWindow() const
{
	if (!client) {
		return;
	}

	std::string currentText = autolootConfig.text;

	client->sendHouseWindow(std::numeric_limits<uint32_t>().max(), currentText);
}

void Player::parseAutoLootWindow(const std::string& text)
{
	if (text.size() > 4096) {
		sendTextMessage(MESSAGE_STATUS_CONSOLE_RED, "AutoLoot: configuration is too long.");
		return;
	}

	if (!ConfigManager::getBoolean(ConfigManager::AUTOLOOT_ENABLED)) {
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "AutoLoot is currently disabled.");
		return;
	}

	if (text.empty()) {
		autolootConfig.lootAnything = false;
		autolootConfig.text.clear();
		autolootConfig.itemList.clear();
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "AutoLoot list cleared.");
		return;
	}

	if (autolootConfig.text == text) {
		return;
	}

	std::set<uint16_t> oldItems;
	for (const auto& pair : autolootConfig.itemList) {
		oldItems.insert(pair.first);
	}

	size_t maxItems = isPremium() ? ConfigManager::getInteger(ConfigManager::AUTOLOOT_MAXITEMS_PREMIUM) : ConfigManager::getInteger(ConfigManager::AUTOLOOT_MAXITEMS_FREE);
	std::string blockConfig = std::string(ConfigManager::getString(ConfigManager::AUTOLOOT_BLOCKIDS));
	std::vector<std::string_view> blockIdStrings = explodeString(blockConfig, ";");
	std::set<uint16_t> blockedIds;
	for (const auto& str : blockIdStrings) {
		if (str.empty()) continue;
		try {
			blockedIds.insert(static_cast<uint16_t>(std::stoi(std::string(str))));
		} catch (...) {
			continue;
		}
	}

	autolootConfig.itemList.clear();

	std::istringstream stream(text);
	std::string line;
	StringVector vec;
	std::pair<std::map<uint16_t, std::pair<uint16_t, bool>>::iterator, bool> ret;
	std::ostringstream addedItems;
	bool firstAddedItem = true;
	bool limitReached = false;

	while (getline(stream, line)) {
		trimString(line);

		if (line.empty()) {
			continue;
		}

		if (line.front() == '*') {
			autolootConfig.lootAnything = true;
			autolootConfig.text = text;
			sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "AutoLoot configuration saved: Loot All enabled.");
			return;
		} else {
			autolootConfig.lootAnything = false;
		}

		if (autolootConfig.itemList.size() >= maxItems) {
			limitReached = true;
			continue;
		}

		std::vector<std::string_view> parts = explodeString(line, ",", 1);
		if (parts.empty()) {
			continue;
		}

		std::string itemName(parts[0]);
		uint16_t itemId = Item::items.getItemIdByName(itemName);
		if (itemId == 0) {
			continue;
		}

		if (blockedIds.contains(itemId)) {
			continue; 
		}

		uint16_t backpackId = 0;
		if (parts.size() > 1) {
			std::string backpackName(parts[1]);
			trimString(backpackName);
			backpackId = Item::items.getItemIdByName(backpackName);
		}

		ret = autolootConfig.itemList.insert(std::make_pair(itemId, std::make_pair(backpackId, line.front() != '#')));
		if (ret.second) {
			if (line.front() != '#' && !oldItems.contains(itemId)) {
				if (!firstAddedItem) {
					addedItems << ", ";
				}
				addedItems << itemName;
				firstAddedItem = false;
			}
		}
	}
	autolootConfig.text = text;
	
	std::ostringstream removedItems;
	bool firstRemovedItem = true;
	for (uint16_t oldId : oldItems) {
		if (!autolootConfig.itemList.contains(oldId)) {
			if (!firstRemovedItem) {
				removedItems << ", ";
			}
			removedItems << Item::items[oldId].name;
			firstRemovedItem = false;
		}
	}

	if (!firstAddedItem) {
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, fmt::format("AutoLoot added: {:s}", addedItems.str()));
	}
	
	if (!firstRemovedItem) {
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, fmt::format("AutoLoot removed: {:s}", removedItems.str()));
	}

	if (limitReached) {
		sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("AutoLoot limit reached ({:d} items). Some items were not added.", maxItems));
	}

	if (firstAddedItem && firstRemovedItem && !limitReached) {
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "AutoLoot configuration saved.");
	}
}

Container* Player::findNonEmptyContainer(uint16_t itemId)
{
	Container* mainBackpack = inventory[CONST_SLOT_BACKPACK] ? inventory[CONST_SLOT_BACKPACK]->getContainer() : nullptr;
	if (!mainBackpack) {
		return nullptr;
	}

	std::vector<Container*> containers;
	for (size_t i = mainBackpack->getFirstIndex(), j = mainBackpack->getLastIndex(); i < j; ++i) {
		Thing* thing = mainBackpack->getThing(i);
		if (!thing) {
			continue;
		}

		Item* item = thing->getItem();
		if (!item) {
			continue;
		}

		Container* container = item->getContainer();
		if (!container) {
			continue;
		}

		if (container->getID() == itemId && container->size() != container->capacity()) {
			return container;
		}

		containers.push_back(container);
	}

	size_t i = 0;
	while (i < containers.size()) {
		Container* container = containers[i++];
		for (const auto& item : container->getItemList()) {
			Container* subContainer = item->getContainer();
			if (subContainer) {
				if (subContainer->getID() == itemId && subContainer->size() != subContainer->capacity()) {
					return subContainer;
				}

				containers.push_back(subContainer);
			}
		}
	}
	return nullptr;
}

Container* Player::findGoldPouch() const
{
	if (storeInbox) {
		for (const auto& storeItem : storeInbox->getItemList()) {
			if (storeItem->getID() == ITEM_GOLD_POUCH) {
				return storeItem->getContainer();
			}

			if (const auto container = storeItem->getContainer()) {
				for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
					Item* item = *it;
					if (item && item->getID() == ITEM_GOLD_POUCH) {
						return item->getContainer();
					}
				}
			}
		}
	}

	for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
		Item* slotItem = inventory[slot].get();
		if (!slotItem) {
			continue;
		}

		if (slotItem->getID() == ITEM_GOLD_POUCH) {
			return slotItem->getContainer();
		}

		Container* container = slotItem->getContainer();
		if (!container) {
			continue;
		}

		for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
			Item* item = *it;
			if (item && item->getID() == ITEM_GOLD_POUCH) {
				return item->getContainer();
			}
		}
	}
	return nullptr;
}

Container* Player::getOrCreateGoldPouchPage(Container* pouch)
{
	return pouch;
}

void Player::lootCorpse(Container* container)
{
	if (!container) {
		return;
	}

	if (container->getCorpseOwner() != id) {
		sendCancelMessage(RETURNVALUE_YOUARENOTTHEOWNER);
		return;
	}

	if (!autolootConfig.enabled) {
		return;
	}

	if (!findGoldPouch()) {
		sendTextMessage(MESSAGE_EVENT_ORANGE, "You need a Gold Pouch to use AutoLoot.");
		return;
	}

	auto goldPouchDestination = findGoldPouch();
	auto storeInboxDestination = getStoreInbox();
	if (!storeInboxDestination) {
		sendTextMessage(MESSAGE_EVENT_ORANGE, "Your store inbox is unavailable.");
		return;
	}

	std::vector<std::pair<Item*, uint16_t>> toMove;

	AutoLootMap::iterator iter;
	for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
		Item* item = *it;
		if (autolootConfig.lootAnything) {
			toMove.push_back(std::make_pair(item, 0));
		} else {
			iter = autolootConfig.itemList.find(item->getID());
			if (iter != autolootConfig.itemList.end() && iter->second.second) {
				toMove.push_back(std::make_pair(item, iter->second.first));
			}
		}
	}

	std::string moneyConfig = std::string(ConfigManager::getString(ConfigManager::AUTOLOOT_MONEYIDS));
	std::vector<std::string_view> moneyIdStrings = explodeString(moneyConfig, ";");
	std::set<uint16_t> moneyIds;
	for (const auto& str : moneyIdStrings) {
		if (str.empty()) continue;
		try {
			moneyIds.insert(static_cast<uint16_t>(std::stoi(std::string(str))));
		} catch (...) {
			continue;
		}
	}

	uint64_t totalDepositValue = 0;
	std::vector<Item*> itemsToRemove;

	for (const auto& pair : toMove) {
		Item* item = pair.first;
		uint16_t itemId = item->getID();
		uint32_t value = 0;

		if (moneyIds.contains(itemId)) {
			if (itemId == 2160) {
				value = item->getItemCount() * 10000;
			} else if (itemId == 2152) {
				value = item->getItemCount() * 100;
			} else if (itemId == 2148) {
				value = item->getItemCount();
			} else {
				value = item->getWorth();
			}
		}

		if (value > 0) {
			totalDepositValue += value;
			itemsToRemove.push_back(item);
			continue;
		}

		ReturnValue ret;
		Container* primaryDestination = goldPouchDestination ? goldPouchDestination : storeInboxDestination;
		Container* fallbackDestination = storeInboxDestination;
		bool usedGoldPouch = (goldPouchDestination != nullptr);

		ret = g_game.internalMoveItem(container, primaryDestination, INDEX_WHEREEVER, item,
		                                          item->getItemCount(), nullptr);
		if (ret == RETURNVALUE_NOERROR) {
			continue;
		}
		if (ret == RETURNVALUE_NOTENOUGHCAPACITY) {
			sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have enough capacity to autoloot this item.");
			continue;
		}

		if (usedGoldPouch && fallbackDestination != primaryDestination) {
			ret = g_game.internalMoveItem(container, fallbackDestination, INDEX_WHEREEVER, item,
			                                          item->getItemCount(), nullptr);
			if (ret == RETURNVALUE_NOERROR) {
				sendTextMessage(MESSAGE_STATUS_SMALL, "Your gold pouch is full. Item sent to store inbox.");
				continue;
			}
			if (ret == RETURNVALUE_NOTENOUGHCAPACITY) {
				sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have enough capacity to autoloot this item.");
				continue;
			}
		}

		auto backpackItem = getInventoryItem(CONST_SLOT_BACKPACK);
		auto backpack = backpackItem ? backpackItem->getContainer() : nullptr;
		if (backpack) {
			ret = g_game.internalMoveItem(container, backpack, INDEX_WHEREEVER, item, item->getItemCount(), nullptr);
			if (ret == RETURNVALUE_NOERROR) {
				sendTextMessage(MESSAGE_STATUS_SMALL, "Your store inbox is full. Item sent to backpack.");
				continue;
			}
			if (ret == RETURNVALUE_NOTENOUGHCAPACITY) {
				sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have enough capacity to autoloot this item.");
				continue;
			}
		}
		
		sendTextMessage(MESSAGE_STATUS_SMALL, "Your containers are full. Item left in corpse.");
	}

	if (autolootConfig.goldEnabled) {
		std::unordered_set<Item*> alreadyQueued(itemsToRemove.begin(), itemsToRemove.end());
		for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
			Item* goldItem = *it;
			uint64_t worth = static_cast<uint64_t>(goldItem->getWorth());
			if (worth > 0 && !alreadyQueued.contains(goldItem)) {
				totalDepositValue += worth;
				itemsToRemove.push_back(goldItem);
				alreadyQueued.insert(goldItem);
			}
		}
	}

	if (totalDepositValue > 0) {
		setBankBalance(bankBalance + totalDepositValue);
		for (Item* item : itemsToRemove) {
			g_game.internalRemoveItem(item, item->getItemCount());
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, fmt::format("AutoLoot: Deposited {:d} gold to your bank account.", totalDepositValue));
	}
}

void Player::updateRegeneration()
{
	if (!vocation) {
		return;
	}

	Condition* condition = getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT);
	if (condition) {
		condition->setParam(CONDITION_PARAM_HEALTHGAIN, vocation->getHealthGainAmount());
		condition->setParam(CONDITION_PARAM_HEALTHTICKS, vocation->getHealthGainTicks() * 1000);
		condition->setParam(CONDITION_PARAM_MANAGAIN, vocation->getManaGainAmount());
		condition->setParam(CONDITION_PARAM_MANATICKS, vocation->getManaGainTicks() * 1000);
	}
}

void Player::addItemImbuements(Item* item, slots_t slot) {
	if (item->hasImbuements()) {
		const std::vector<std::shared_ptr<Imbuement>>& imbuementList = item->getImbuements();
		for (auto& imbue : imbuementList) {
			if (imbue->imbuetype == ImbuementType::IMBUEMENT_TYPE_CAPACITY_BOOST && slot != CONST_SLOT_BACKPACK) {
				continue;
			}

			if (imbue->isSkill()) {
				switch (imbue->imbuetype) {
				case ImbuementType::IMBUEMENT_TYPE_FIST_SKILL:
					setVarSkill(SKILL_FIST, static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_CLUB_SKILL:
					setVarSkill(SKILL_CLUB, static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_SWORD_SKILL:
					setVarSkill(SKILL_SWORD, static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_AXE_SKILL:
					setVarSkill(SKILL_AXE, static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_DISTANCE_SKILL:
					setVarSkill(SKILL_DISTANCE, static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_SHIELD_SKILL:
					setVarSkill(SKILL_SHIELD, static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_FISHING_SKILL:
					setVarSkill(SKILL_FISHING, static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_MAGIC_LEVEL:
					setVarStats(STAT_MAGICPOINTS, static_cast<int32_t>(imbue->value));
					break;
				default:
					break;
				}
			}

			if (imbue->isSpecialSkill()) {
				switch (imbue->imbuetype) {
				case ImbuementType::IMBUEMENT_TYPE_MANA_LEECH:
					setVarSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT, static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_LIFE_LEECH:
					setVarSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT, static_cast<int32_t>(imbue->value));
					break;
					case ImbuementType::IMBUEMENT_TYPE_CRITICAL_CHANCE: {
						uint16_t chance = static_cast<uint16_t>(imbue->value & 0xFFFF);
						uint16_t amount = static_cast<uint16_t>((imbue->value >> 16) & 0xFFFF);
						setVarSpecialSkill(SPECIALSKILL_CRITICALHITCHANCE, static_cast<int32_t>(chance));
						setVarSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT, static_cast<int32_t>(amount));
					break;
				}
				case ImbuementType::IMBUEMENT_TYPE_CRITICAL_AMOUNT:
					setVarSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT, static_cast<int32_t>(imbue->value));
					break;
				default:
					break;
				}
			}

			if (imbue->isStat()) {
				switch (imbue->imbuetype) {
				case ImbuementType::IMBUEMENT_TYPE_CAPACITY_BOOST:
					setVarStats(STAT_CAPACITY, static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_SPEED_BOOST:
					g_game.changeSpeed(this, static_cast<int32_t>(imbue->value));
					break;
				default:
					break;
				}
			}
		}
	}
	sendSkills();
	sendStats();
}

void Player::removeItemImbuements(Item* item, slots_t slot) {
	if (item->hasImbuements()) {
		const std::vector<std::shared_ptr<Imbuement>>& imbuementList = item->getImbuements();
		for (auto& imbue : imbuementList) {
			if (imbue->imbuetype == ImbuementType::IMBUEMENT_TYPE_CAPACITY_BOOST && slot != CONST_SLOT_BACKPACK) {
				continue;
			}

			if (imbue->isSkill()) {
				switch (imbue->imbuetype) {
				case ImbuementType::IMBUEMENT_TYPE_FIST_SKILL:
					setVarSkill(SKILL_FIST, -static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_CLUB_SKILL:
					setVarSkill(SKILL_CLUB, -static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_SWORD_SKILL:
					setVarSkill(SKILL_SWORD, -static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_AXE_SKILL:
					setVarSkill(SKILL_AXE, -static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_DISTANCE_SKILL:
					setVarSkill(SKILL_DISTANCE, -static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_SHIELD_SKILL:
					setVarSkill(SKILL_SHIELD, -static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_FISHING_SKILL:
					setVarSkill(SKILL_FISHING, -static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_MAGIC_LEVEL:
					setVarStats(STAT_MAGICPOINTS, -static_cast<int32_t>(imbue->value));
					break;
				default:
					break;
				}
			}

			if (imbue->isSpecialSkill()) {
				switch (imbue->imbuetype) {
				case ImbuementType::IMBUEMENT_TYPE_MANA_LEECH:
					setVarSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT, -static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_LIFE_LEECH:
					setVarSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT, -static_cast<int32_t>(imbue->value));
					break;
					case ImbuementType::IMBUEMENT_TYPE_CRITICAL_CHANCE: {
						uint16_t critChance = static_cast<uint16_t>(imbue->value & 0xFFFF);
						uint16_t critAmount = static_cast<uint16_t>((imbue->value >> 16) & 0xFFFF);
						setVarSpecialSkill(SPECIALSKILL_CRITICALHITCHANCE, -static_cast<int32_t>(critChance));
						setVarSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT, -static_cast<int32_t>(critAmount));
					break;
				}
				case ImbuementType::IMBUEMENT_TYPE_CRITICAL_AMOUNT:
					setVarSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT, -static_cast<int32_t>(imbue->value));
					break;
				default:
					break;
				}
			}

			if (imbue->isStat()) {
				switch (imbue->imbuetype) {
				case ImbuementType::IMBUEMENT_TYPE_CAPACITY_BOOST:
					setVarStats(STAT_CAPACITY, -static_cast<int32_t>(imbue->value));
					break;
				case ImbuementType::IMBUEMENT_TYPE_SPEED_BOOST:
					g_game.changeSpeed(this, -static_cast<int32_t>(imbue->value));
					break;
				default:
					break;
				}
			}
		}
	}
	sendSkills();
	sendStats();
}

void Player::removeImbuementEffect(std::shared_ptr<Imbuement> imbue) {


	if (imbue->isSkill()) {
		switch (imbue->imbuetype) {
		case ImbuementType::IMBUEMENT_TYPE_FIST_SKILL:
			setVarSkill(SKILL_FIST, -static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_CLUB_SKILL:
			setVarSkill(SKILL_CLUB, -static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_SWORD_SKILL:
			setVarSkill(SKILL_SWORD, -static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_AXE_SKILL:
			setVarSkill(SKILL_AXE, -static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_DISTANCE_SKILL:
			setVarSkill(SKILL_DISTANCE, -static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_SHIELD_SKILL:
			setVarSkill(SKILL_SHIELD, -static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_FISHING_SKILL:
			setVarSkill(SKILL_FISHING, -static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_MAGIC_LEVEL:
			setVarStats(STAT_MAGICPOINTS, -static_cast<int32_t>(imbue->value));
			break;
		default:
			break;
		}
	}

	if (imbue->isSpecialSkill()) {
		switch (imbue->imbuetype) {
		case ImbuementType::IMBUEMENT_TYPE_MANA_LEECH:
			setVarSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT, -static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_LIFE_LEECH:
			setVarSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT, -static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_CRITICAL_CHANCE: {
			uint16_t chance = static_cast<uint16_t>(imbue->value & 0xFFFF);
			uint16_t amount = static_cast<uint16_t>((imbue->value >> 16) & 0xFFFF);
			setVarSpecialSkill(SPECIALSKILL_CRITICALHITCHANCE, -static_cast<int32_t>(chance));
			setVarSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT, -static_cast<int32_t>(amount));
			break;
		}
		case ImbuementType::IMBUEMENT_TYPE_CRITICAL_AMOUNT:
			setVarSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT, -static_cast<int32_t>(imbue->value));
			break;
		default:
			break;
		}
	}

	if (imbue->isStat()) {
		switch (imbue->imbuetype) {
		case ImbuementType::IMBUEMENT_TYPE_CAPACITY_BOOST:
			setVarStats(STAT_CAPACITY, -static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_SPEED_BOOST:
			g_game.changeSpeed(this, -static_cast<int32_t>(imbue->value));
			break;
		default:
			break;
		}
	}
	sendSkills();
	sendStats();
}

void Player::addImbuementEffect(std::shared_ptr<Imbuement> imbue) {


	if (imbue->isSkill()) {
		switch (imbue->imbuetype) {
		case ImbuementType::IMBUEMENT_TYPE_FIST_SKILL:
			setVarSkill(SKILL_FIST, static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_CLUB_SKILL:
			setVarSkill(SKILL_CLUB, static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_SWORD_SKILL:
			setVarSkill(SKILL_SWORD, static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_AXE_SKILL:
			setVarSkill(SKILL_AXE, static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_DISTANCE_SKILL:
			setVarSkill(SKILL_DISTANCE, static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_SHIELD_SKILL:
			setVarSkill(SKILL_SHIELD, static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_FISHING_SKILL:
			setVarSkill(SKILL_FISHING, static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_MAGIC_LEVEL:
			setVarStats(STAT_MAGICPOINTS, static_cast<int32_t>(imbue->value));
			break;
		default:
			break;
		}
	}

	if (imbue->isSpecialSkill()) {
		switch (imbue->imbuetype) {
		case ImbuementType::IMBUEMENT_TYPE_MANA_LEECH:
			setVarSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT, static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_LIFE_LEECH:
			setVarSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT, static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_CRITICAL_CHANCE: {
				uint16_t chance = static_cast<uint16_t>(imbue->value & 0xFFFF);
				uint16_t amount = static_cast<uint16_t>((imbue->value >> 16) & 0xFFFF);
				setVarSpecialSkill(SPECIALSKILL_CRITICALHITCHANCE, static_cast<int32_t>(chance));
				setVarSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT, static_cast<int32_t>(amount));
			break;
		}
		case ImbuementType::IMBUEMENT_TYPE_CRITICAL_AMOUNT:
			setVarSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT, static_cast<int32_t>(imbue->value));
			break;
		default:
			break;
		}
	}

	if (imbue->isStat()) {
		switch (imbue->imbuetype) {
		case ImbuementType::IMBUEMENT_TYPE_CAPACITY_BOOST:
			setVarStats(STAT_CAPACITY, static_cast<int32_t>(imbue->value));
			break;
		case ImbuementType::IMBUEMENT_TYPE_SPEED_BOOST:
			g_game.changeSpeed(this, static_cast<int32_t>(imbue->value));
			break;
		default:
			break;
		}
	}
	sendSkills();
	sendStats();
}

bool Player::checkText(std::string_view text, std::string_view match) const
{
	return caseInsensitiveEqual(text, match);
}

void Player::resetTalkState(size_t from, size_t to)
{
	if (to == 0 || to > managerTalkState.size()) {
		to = managerTalkState.size();
	}
	std::fill(managerTalkState.begin() + from, managerTalkState.begin() + to, false);
}

void Player::handleNamelockManager(const std::string& text, std::ostringstream& msg, bool& /*shouldShowHelp*/)
{
	if (!managerTalkState[1]) {
		std::string newName = text;
		trimString(newName);

		if (newName.length() < 4) {
			msg << "Account Manager: Your name you want is too short, please select a longer name.";
		} else if (newName.length() > 20) {
			msg << "Account Manager: The name you want is too long, please select a shorter name.";
		} else if (!validateAndFormatPlayerName(newName)) {
			msg << "Account Manager: That name seems to contain invalid symbols, please choose another name.";
		} else if (IOLoginData::playerNameExists(newName)) {
			msg << "Account Manager: A player with that name already exists, please choose another name.";
		} else {
			std::string tmp = asLowerCaseString(newName);
			if (tmp.substr(0, std::min(static_cast<size_t>(4), tmp.length())) != "god " &&
			    tmp.substr(0, std::min(static_cast<size_t>(3), tmp.length())) != "cm " &&
			    tmp.substr(0, std::min(static_cast<size_t>(3), tmp.length())) != "gm ") {
				managerData.string1 = newName;
				managerTalkState[1] = true;
				managerTalkState[2] = true;
				msg << "Account Manager: " << newName << ", are you sure? Type 'yes' or 'no'.";
			} else {
				msg << "Account Manager: Your character is not a staff member, please tell me another name!";
			}
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	if (checkText(text, "no") && managerTalkState[2]) {
		managerTalkState[1] = false;
		managerTalkState[2] = false;
		managerData.string1.clear();
		msg << "Account Manager: What else would you like to name your character?";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	if (checkText(text, "yes") && managerTalkState[2]) {
		if (!IOLoginData::playerNameExists(managerData.string1)) {
			uint32_t guid = getGUID();
			if (Database::getInstance().executeQuery(
			        fmt::format("UPDATE `players` SET `name` = {:s} WHERE `id` = {:d}",
			                    Database::getInstance().escapeString(managerData.string1), guid))) {
				Database::getInstance().executeQuery(
				    fmt::format("DELETE FROM `player_namelocks` WHERE `player_id` = {:d}", guid));

				if (House* house = g_game.map.houses.getHouseByPlayerId(guid)) {
					house->updateOwnerName(managerData.string1);
				}

				managerTalkState[1] = true;
				managerTalkState[2] = false;
				msg << "Account Manager: Your character has been successfully renamed, you should now be able to login at it without any problems.";
			} else {
				managerTalkState[1] = false;
				managerTalkState[2] = false;
				msg << "Account Manager: Failed to change your name, please try again.";
			}
		} else {
			managerTalkState[1] = false;
			managerTalkState[2] = false;
			managerData.string1.clear();
			msg << "Account Manager: A player with that name already exists, please choose another name.";
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	msg << "Account Manager: Sorry, but I can't understand you, please try to repeat that!";
	sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
}

void Player::handleAccountManager(const std::string& text, std::ostringstream& msg, bool& /*shouldShowHelp*/)
{
	msg << "Account Manager: ";

	if (checkText(text, "cancel") || checkText(text, "account")) {
		managerTalkState[1] = true;
		for (int8_t i = 2; i <= 12; i++) {
			managerTalkState[i] = false;
		}
		msg << "Do you want to change your 'password', 'recovery key', 'character' or 'delete'?";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "delete") && managerTalkState[1]) {
		managerTalkState[1] = false;
		managerTalkState[2] = true;
		msg << "Which character would you like to delete?";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (managerTalkState[2]) {
		std::string tmp = text;
		trimString(tmp);
		if (!validateAndFormatPlayerName(tmp)) {
			msg << "That name contains invalid characters, try to say your name again, you might have typed it wrong.";
		} else {
			managerTalkState[2] = false;
			managerTalkState[3] = true;
			managerData.string1 = tmp;
			msg << "Do you really want to delete the character named " << managerData.string1
			    << "? Type 'yes' or 'no'.";
		}

		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "yes") && managerTalkState[3]) {
		uint32_t playerId = IOLoginData::getGuidByName(managerData.string1);

		if (playerId && IOLoginData::deletePlayer(playerId)) {
			msg << "Your character has been deleted.";
		} else {
			msg << "An error occured while deleting your character. Either the character does not belong to you or it doesn't exist.";
		}

		managerTalkState[1] = true;
		for (int8_t i = 2; i <= 12; i++) {
			managerTalkState[i] = false;
		}

		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "no") && managerTalkState[3]) {
		managerTalkState[1] = true;
		managerTalkState[3] = false;

		msg << "Tell me what character you want to delete.";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());

		return;
	} else if (checkText(text, "password") && managerTalkState[1]) {
		managerTalkState[1] = false;
		managerTalkState[4] = true;
		msg << "Tell me your new password please.";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (managerTalkState[4]) {
		std::string tmp = text;
		trimString(tmp);
		if (tmp.length() < 6) {
			msg << "That password is too short, at least 6 digits are required. Please select a longer password.";
		} else {
			managerTalkState[4] = false;
			managerTalkState[5] = true;
			managerData.string1 = tmp;
			msg << "Should '" << managerData.string1 << "' be your new password? Type 'yes' or 'no'.";
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "yes") && managerTalkState[5]) {
		managerTalkState[1] = true;
		for (int8_t i = 2; i <= 12; i++) {
			managerTalkState[i] = false;
		}

		IOLoginData::setPassword(managerData.accountId, managerData.string1);
		msg << "Your password has been changed.";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "no") && managerTalkState[5]) {
		managerTalkState[1] = true;
		for (int8_t i = 2; i <= 12; i++) {
			managerTalkState[i] = false;
		}
		msg << "Then not.";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "character")) {
		std::vector<std::string> characters = IOLoginData::getPlayersByAccountId(managerData.accountId);
		if (characters.size() >= 15) {
			managerTalkState[1] = true;
			for (int8_t i = 2; i <= 12; i++) {
				managerTalkState[i] = false;
			}
			msg << "Your account reached the limit of 15 players; you can {delete} a character if you want to create a new one.";
		} else {
			managerTalkState[1] = false;
			managerTalkState[6] = true;
			msg << "What would you like as your character name?";
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (managerTalkState[6]) {
		managerData.string1 = text;
		trimString(managerData.string1);
		if (managerData.string1.length() < 4) {
			msg << "Your name you want is too short, please select a longer name.";
		} else if (managerData.string1.length() > 20) {
			msg << "The name you want is too long, please select a shorter name.";
		} else if (!validateAndFormatPlayerName(managerData.string1)) {
			msg << "That name seems to contain invalid symbols, please choose another name.";
		} else if (IOLoginData::playerNameExists(managerData.string1)) {
			msg << "A player with that name already exists, please choose another name.";
		} else {
			std::string tmp = managerData.string1;
			toLowerCaseString(tmp);
			if (tmp.substr(0, 4) != "god " && tmp.substr(0, 3) != "cm " && tmp.substr(0, 3) != "gm ") {
				managerTalkState[6] = false;
				managerTalkState[7] = true;
				msg << managerData.string1 << ", are you sure? Type 'yes' or 'no'.";
			} else {
				msg << "Your character is not a staff member, please tell me another name!";
			}
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "no") && managerTalkState[7]) {
		managerTalkState[6] = true;
		managerTalkState[7] = false;
		msg << "What else would you like to name your character?";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "yes") && managerTalkState[7]) {
		managerTalkState[7] = false;
		managerTalkState[8] = true;
		msg << "Should your character be 'male' or 'female'.";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (managerTalkState[8] && (checkText(text, "female") || checkText(text, "male"))) {
		managerTalkState[8] = false;
		managerTalkState[9] = true;
		if (checkText(text, "female")) {
			msg << "A female, are you sure? Type 'yes' or 'no'.";
			;
			managerData.sex = PLAYERSEX_FEMALE;
		} else {
			msg << "A male, are you sure? Type 'yes' or 'no'.";
			managerData.sex = PLAYERSEX_MALE;
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "no") && managerTalkState[9]) {
		managerTalkState[8] = true;
		managerTalkState[9] = false;
		msg << "Tell me... would you like to be 'male' or 'female'?";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "yes") && managerTalkState[9]) {
		if (ConfigManager::getBoolean(ConfigManager::START_CHOOSEVOC)) {
			managerTalkState[9] = false;
			managerTalkState[11] = true;

			msg << "What do you want to be... ";
			bool firstPart = true;
			size_t count = 0;
			const auto& vocationsMap = g_vocations.getVocationsMap();

			for (const auto& it : vocationsMap) {
				const Vocation& voc = *it.second;
				if (voc.getFromVocation() == voc.getId() && voc.getId() != 0) {
					count++;
				}
			}

			size_t current = 0;
			for (const auto& it : vocationsMap) {
				const Vocation& voc = *it.second;
				if (voc.getFromVocation() == voc.getId() && voc.getId() != 0) {
					if (!firstPart) {
						current++;
						if (current == count) {
							msg << " or ";
						} else {
							msg << ", ";
						}
					}
					msg << voc.getVocName();
					firstPart = false;
				}
			}
			msg << ".";
		} else if (!IOLoginData::playerNameExists(managerData.string1)) {
			managerTalkState[1] = true;
			for (int8_t i = 2; i <= 12; i++) {
				managerTalkState[i] = false;
			}

			if (IOLoginData::createPlayer(managerData.accountId, managerData.string1, 1, managerData.sex)) {
				msg << "Your character has been created.";
			} else {
				msg << "Your character couldn't be created, please try again.";
			}
		} else {
			managerTalkState[6] = true;
			managerTalkState[9] = false;
			msg << "A player with that name already exists, please choose another name.";
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (managerTalkState[11]) {
		auto vocationIdOpt = g_vocations.getVocationId(text);
		if (vocationIdOpt.has_value()) {
			uint16_t vocationId = vocationIdOpt.value();
			Vocation* vocation = g_vocations.getVocation(vocationId);
			if (vocation && vocation->getFromVocation() == vocation->getId() && vocation->getId() != 0) {
				msg << "So you would like to be " << vocation->getVocName() << "... are you sure? Type 'yes' or 'no'.";
				managerData.vocationId = vocation->getId();
				managerTalkState[11] = false;
				managerTalkState[12] = true;
			} else {
				msg << "I don't understand what vocation you would like to be... could you please repeat it?";
			}
		} else {
			msg << "I don't understand what vocation you would like to be... could you please repeat it?";
		}

		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "yes") && managerTalkState[12]) {
		if (!IOLoginData::playerNameExists(managerData.string1)) {
			managerTalkState[1] = true;
			for (int8_t i = 2; i <= 12; i++) {
				managerTalkState[i] = false;
			}

			Vocation* vocation = g_vocations.getVocation(managerData.vocationId);
			if (!vocation) {
				msg << "Your character couldn't be created due to an invalid vocation, please try again.";
			} else if (IOLoginData::createPlayer(managerData.accountId, managerData.string1, managerData.vocationId,
			                                     managerData.sex)) {
				msg << "Your character has been created.";
			} else {
				msg << "Your character couldn't be created, please try again.";
			}
		} else {
			managerTalkState[6] = true;
			managerTalkState[12] = false;
			msg << "A player with that name already exists, please choose another name.";
		}

		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "no") && managerTalkState[12]) {
		managerTalkState[11] = true;
		managerTalkState[12] = false;
		msg << "No? Then what would you like to be?";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "recovery key") && managerTalkState[1]) {
		managerTalkState[1] = false;
		managerTalkState[10] = true;
		msg << "Would you like a recovery key?";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "yes") && managerTalkState[10]) {
		std::string recoveryKey = generateRecoveryKey(4, 4);
		if (IOLoginData::setRecoveryKey(managerData.accountId, recoveryKey)) {
			msg << "Your recovery key is: " << recoveryKey
			    << ". IMPORTANT: Write this down, it will be hashed and cannot be recovered!";
		} else {
			msg << "Sorry, you already have a recovery key, for security reasons I may not give you a new one.";
		}

		managerTalkState[1] = true;
		for (int8_t i = 2; i <= 12; i++) {
			managerTalkState[i] = false;
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else if (checkText(text, "no") && managerTalkState[10]) {
		msg << "Then not.";
		managerTalkState[1] = true;
		for (int8_t i = 2; i <= 12; i++) {
			managerTalkState[i] = false;
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	} else {
		msg << "Please read the latest message that I have specified, I don't understand the current requested action.";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}
}

void Player::handleNewAccountManager(const std::string& text, std::ostringstream& msg, bool& /*shouldShowHelp*/)
{
	msg << "Account Manager: ";

	if (checkText(text, "account") && !managerTalkState[1]) {
		msg << "What would you like your password to be?";
		managerTalkState[1] = true;
		managerTalkState[2] = true;
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	if (checkText(text, "recover") && !managerTalkState[1]) {
		msg << "What is your account name?";
		managerTalkState[1] = true;
		managerTalkState[6] = true;
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	if (managerTalkState[6]) {
		std::string tmp = text;
		trimString(tmp);

		if (tmp.length() < 3) {
			msg << "That account name is too short, at least 3 digits are required. Please select a longer account name.";
			sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
			return;
		}

		if (!IOLoginData::accountNameExists(tmp)) {
			msg << "An account with that name does not exist, please try another account name or type 'account' to create a new account.";
			sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
			return;
		}

		managerData.accountName = tmp;
		managerTalkState[6] = false;
		managerTalkState[7] = true;
		msg << "What is your recovery key?";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	if (managerTalkState[7]) {
		std::string recoveryKey = text;
		trimString(recoveryKey);

		Database& db = Database::getInstance();
		DBResult_ptr result = db.storeQuery(fmt::format("SELECT `id`, `secret` FROM `accounts` WHERE `name` = {:s}",
		                                                db.escapeString(managerData.accountName)));

		if (!result) {
			msg << "Account not found. Please try again or type 'cancel' to start over.";
			managerTalkState[7] = false;
			managerTalkState[6] = true;
			sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
			return;
		}

		uint32_t accId = result->getNumber<uint32_t>("id");
		std::string storedKeyHash = std::string{result->getString("secret")};

		if (storedKeyHash.empty()) {
			msg << "This account does not have a recovery key set. Please contact an administrator.";
			managerTalkState[7] = false;
			managerTalkState[1] = false;
			sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
			return;
		}

		std::string inputKeyHash = transformToSHA1Hex(recoveryKey);
		if (storedKeyHash != inputKeyHash) {
			msg << "The recovery key you entered is incorrect. Please try again or type 'cancel' to start over.";
			sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
			return;
		}

		std::string newPassword = generateSecurePassword(12);

		if (IOLoginData::setPassword(accId, newPassword)) {
			msg << "Your account has been recovered! Your new password is: " << newPassword
			    << ". Please write it down before you logout.";
			managerTalkState[7] = false;
			managerTalkState[1] = false;
		} else {
			msg << "Sorry, an error occurred while recovering your account. Please try again later.";
			managerTalkState[7] = false;
			managerTalkState[1] = false;
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	if (managerTalkState[2]) {
		std::string tmp = text;
		trimString(tmp);
		if (tmp.length() < 6) {
			msg << "That password is too short, at least 6 digits are required. Please select a longer password.";
			sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
			return;
		}

		managerData.string1 = tmp;
		managerTalkState[3] = true;
		managerTalkState[2] = false;
		msg << managerData.string1 << " is it? 'yes' or 'no'?";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	if (checkText(text, "yes") && managerTalkState[3]) {
		if (ConfigManager::getBoolean(ConfigManager::GENERATE_ACCOUNT_NUMBER)) {
			uint32_t newAccountId = 0;
			bool accountCreated = false;
			int maxAttempts = 100;

			for (int attempt = 0; attempt < maxAttempts && !accountCreated; ++attempt) {
				managerData.accountName = std::to_string(uniform_random(2, 9)) + std::to_string(uniform_random(2, 9)) +
				                          std::to_string(uniform_random(2, 9)) + std::to_string(uniform_random(2, 9)) +
				                          std::to_string(uniform_random(2, 9)) + std::to_string(uniform_random(2, 9)) +
				                          std::to_string(uniform_random(2, 9));

				accountCreated = IOLoginData::createAccount(managerData.accountName, managerData.string1, newAccountId);
			}

			if (accountCreated) {
				accountManager = ACCOUNT_MANAGER_ACCOUNT;
				managerData.accountId = newAccountId;
				accountNumber = newAccountId;

				managerTalkState[1] = true;
				for (int8_t i = 2; i <= 5; i++) {
					managerTalkState[i] = false;
				}

				msg << "Your account has been created, you may manage it now, but remember your account name: '"
				    << managerData.accountName << "' and password: '" << managerData.string1
				    << "'! If the account name is too hard to remember, please note it somewhere.";
				sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());

				std::ostringstream msgWelcome;
				msgWelcome
				    << "Account Manager: Do you want to change your 'password', 'recovery key', 'character' or 'delete'?";
				sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msgWelcome.str());
				return;
			} else {
				msg << "Sorry, your account could not be created (all account numbers taken or system error). Please contact an administrator.";

				for (int8_t i = 2; i <= 5; i++) {
					managerTalkState[i] = false;
				}

				LOG_ERROR(fmt::format("[Error] Failed to create account after {} attempts", maxAttempts));
			}
		} else {
			msg << "What would you like your account name to be?";
			managerTalkState[3] = false;
			managerTalkState[4] = true;
		}

		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	if (checkText(text, "no") && managerTalkState[3]) {
		managerTalkState[2] = true;
		managerTalkState[3] = false;
		msg << "What would you like your password to be then?";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	if (managerTalkState[4]) {
		std::string tmp = text;
		trimString(tmp);
		if (tmp.length() < 3) {
			msg << "That account name is too short, at least 3 digits are required. Please select a longer account name.";
		} else if (tmp.length() > 25) {
			msg << "That account name is too long, not more than 25 digits are required. Please select a shorter account name.";
		} else {
			std::string lowerTmp = tmp;
			std::string lowerPassword = managerData.string1;
			toLowerCaseString(lowerTmp);
			toLowerCaseString(lowerPassword);
			if (lowerTmp == lowerPassword) {
				msg << "Your account name cannot be same as password, please choose another one.";
			} else {
				managerData.accountName = tmp;
				msg << managerData.accountName << ", are you sure? Type 'yes' to confirm or 'no' to cancel.";
				managerTalkState[4] = false;
				managerTalkState[5] = true;
			}
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	if (checkText(text, "yes") && managerTalkState[5]) {
		if (!IOLoginData::accountNameExists(managerData.accountName)) {
			uint32_t newAccountId = 0;
			if (IOLoginData::createAccount(managerData.accountName, managerData.string1, newAccountId)) {
				accountManager = ACCOUNT_MANAGER_ACCOUNT;
				managerData.accountId = newAccountId;
				accountNumber = newAccountId;

				managerTalkState[1] = true;
				for (int8_t i = 2; i <= 5; i++) {
					managerTalkState[i] = false;
				}

				msg << "Your account has been created, you may manage it now, but remember your account name: '"
				    << managerData.accountName << "' and password: '" << managerData.string1 << "'!";
				sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());

				std::ostringstream msgWelcome;
				msgWelcome
				    << "Account Manager: Do you want to change your 'password', 'recovery key', 'character' or 'delete'?";
				sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msgWelcome.str());
				return;
			} else {
				msg << "Your account could not be created, please try again.";
				for (int8_t i = 2; i <= 5; i++) {
					managerTalkState[i] = false;
				}
			}
		} else {
			msg << "An account with that name already exists, please try another account name.";
			managerTalkState[4] = true;
			managerTalkState[5] = false;
		}
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	if (checkText(text, "no") && managerTalkState[5]) {
		managerTalkState[5] = false;
		managerTalkState[4] = true;
		msg << "What else would you like as your account name?";
		sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
		return;
	}

	// Allow "character" command even in NEW mode if account already exists
	if (checkText(text, "character")) {
		if (managerData.accountId != 0) {
			// Account already exists, switch to ACCOUNT mode
			accountManager = ACCOUNT_MANAGER_ACCOUNT;
			managerTalkState[1] = true;
			for (int8_t i = 2; i <= 12; i++) {
				managerTalkState[i] = false;
			}
			std::vector<std::string> characters = IOLoginData::getPlayersByAccountId(managerData.accountId);
			if (characters.size() >= 15) {
				msg << "Your account reached the limit of 15 players; you can 'delete' a character if you want to create a new one.";
			} else {
				managerTalkState[1] = false;
				managerTalkState[6] = true;
				msg << "What would you like as your character name?";
			}
			sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
			return;
		} else {
			msg << "You need to create an account first. Type 'account' to create an account.";
			sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
			return;
		}
	}

	msg << "Sorry, but I can't understand you, please try to repeat that!";
	sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg.str());
}

void Player::manageAccount(const std::string& text)
{
	if (!isAccountManager()) {
		return;
	}

	std::ostringstream msg;
	bool shouldShowHelp = false;

	switch (accountManager) {
		case ACCOUNT_MANAGER_NAMELOCK:
			handleNamelockManager(text, msg, shouldShowHelp);
			break;

		case ACCOUNT_MANAGER_ACCOUNT:
			handleAccountManager(text, msg, shouldShowHelp);
			break;

		case ACCOUNT_MANAGER_NEW:
			handleNewAccountManager(text, msg, shouldShowHelp);
			break;

		default:
			break;
	}
}

bool Player::addOfflineTrainingTries(skills_t skill, uint64_t tries)
{
	if (tries == 0 || skill == SKILL_LEVEL) {
		return false;
	}

	bool sendUpdate = false;
	uint32_t oldSkillValue, newSkillValue;
	long double oldPercentToNextLevel, newPercentToNextLevel;

	if (skill == SKILL_MAGLEVEL) {
		uint64_t currReqMana = vocation->getReqMana(magLevel);
		uint64_t nextReqMana = vocation->getReqMana(magLevel + 1);

		if (currReqMana >= nextReqMana) {
			return false;
		}

		oldSkillValue = magLevel;
		oldPercentToNextLevel = static_cast<long double>(manaSpent * 100) / nextReqMana;

		g_events->eventPlayerOnGainSkillTries(this, SKILL_MAGLEVEL, tries, true);
		uint32_t currMagLevel = magLevel;

		while ((manaSpent + tries) >= nextReqMana) {
			tries -= nextReqMana - manaSpent;

			magLevel++;
			manaSpent = 0;

			g_creatureEvents->playerAdvance(this, SKILL_MAGLEVEL, magLevel - 1, magLevel);

			sendUpdate = true;
			currReqMana = nextReqMana;
			nextReqMana = vocation->getReqMana(magLevel + 1);

			if (currReqMana >= nextReqMana) {
				tries = 0;
				break;
			}
		}

		manaSpent += tries;

		if (magLevel != currMagLevel) {
			sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You advanced to magic level {:d}.", magLevel));
		}

		uint8_t newPercent;
		if (nextReqMana > currReqMana) {
			newPercent = Player::getBasisPointLevel(manaSpent, nextReqMana) / 100;
		} else {
			newPercent = 0;
			newPercentToNextLevel = 0;
		}

		if (newPercent != magLevelPercent) {
			magLevelPercent = newPercent;
			sendUpdate = true;
		}

		newSkillValue = magLevel;
	} else {
		uint64_t currReqTries = vocation->getReqSkillTries(skill, skills[skill].level);
		uint64_t nextReqTries = vocation->getReqSkillTries(skill, skills[skill].level + 1);
		if (currReqTries >= nextReqTries) {
			return false;
		}

		oldSkillValue = skills[skill].level;
		oldPercentToNextLevel = static_cast<long double>(skills[skill].tries * 100) / nextReqTries;

		g_events->eventPlayerOnGainSkillTries(this, skill, tries, true);
		uint32_t currSkillLevel = skills[skill].level;

		while ((skills[skill].tries + tries) >= nextReqTries) {
			tries -= nextReqTries - skills[skill].tries;

			skills[skill].level++;
			skills[skill].tries = 0;
			skills[skill].percent = 0;

			g_creatureEvents->playerAdvance(this, skill, (skills[skill].level - 1), skills[skill].level);

			sendUpdate = true;
			currReqTries = nextReqTries;
			nextReqTries = vocation->getReqSkillTries(skill, skills[skill].level + 1);

			if (currReqTries >= nextReqTries) {
				tries = 0;
				break;
			}
		}

		skills[skill].tries += tries;

		if (currSkillLevel != skills[skill].level) {
			sendTextMessage(MESSAGE_EVENT_ADVANCE,
			                fmt::format("You advanced to {:s} level {:d}.", getSkillName(skill), skills[skill].level));
		}

		uint8_t newPercent;
		if (nextReqTries > currReqTries) {
			newPercent = Player::getBasisPointLevel(skills[skill].tries, nextReqTries);
		} else {
			newPercent = 0;
			newPercentToNextLevel = 0;
		}

		if (skills[skill].percent != newPercent) {
			skills[skill].percent = newPercent;
			sendUpdate = true;
		}

		newSkillValue = skills[skill].level;
	}

	if (sendUpdate) {
		sendSkills();
	}

	sendTextMessage(
	    MESSAGE_EVENT_ADVANCE,
	    fmt::format(
	        "Your {:s} skill changed from level {:d} (with {:.2f}% progress towards level {:d}) to level {:d} (with {:.2f}% progress towards level {:d})",
	        ucwords(getSkillName(skill)), oldSkillValue, oldPercentToNextLevel, (oldSkillValue + 1), newSkillValue,
	        newPercentToNextLevel, (newSkillValue + 1)));
	return sendUpdate;
}

void Player::addReset(uint32_t count /*= 1*/)
{
	reset += count;
	Database::getInstance().executeQuery(fmt::format("UPDATE `players` SET `reset` = {:d} WHERE `id` = {:d}", reset, getGUID()));
}

void Player::setResetCount(uint32_t count)
{
	reset = count;
	Database::getInstance().executeQuery(fmt::format("UPDATE `players` SET `reset` = {:d} WHERE `id` = {:d}", reset, getGUID()));
}

double Player::getResetExpReduction() const
{
	if (!ConfigManager::getBoolean(ConfigManager::RESET_SYSTEM_ENABLED)) {
		return 1.0;
	}

	int32_t reductionPerReset = ConfigManager::getInteger(ConfigManager::RESET_REDUCTION_PERCENTAGE);
	double multiplier = 1.0 - (static_cast<double>(reset * reductionPerReset) / 100.0);
	return std::max<double>(0.1, multiplier);
}

void Player::applyOfflineTraining(uint32_t trainingTime)
{
	float efficiency = ConfigManager::getFloat(ConfigManager::OFFLINE_TRAINING_EFFICIENCY);
	uint64_t tries = static_cast<uint64_t>(trainingTime * efficiency);
	if (tries == 0) {
		return;
	}

	if (offlineTrainingSkill == SKILL_OFFLINE_AUTO) {
		if (isSorcerer() || isDruid()) {
			float mageML = ConfigManager::getFloat(ConfigManager::OFFLINE_TRAINING_MAGE_ML);
			addOfflineTrainingTries(SKILL_MAGLEVEL, static_cast<uint64_t>(tries * mageML));
		} else if (isPaladin()) {
			float paladinDist = ConfigManager::getFloat(ConfigManager::OFFLINE_TRAINING_PALADIN_DIST);
			float paladinML = ConfigManager::getFloat(ConfigManager::OFFLINE_TRAINING_PALADIN_ML);
			float paladinShield = ConfigManager::getFloat(ConfigManager::OFFLINE_TRAINING_PALADIN_SHIELD);

			addOfflineTrainingTries(SKILL_DISTANCE, static_cast<uint64_t>(tries * paladinDist));
			addOfflineTrainingTries(SKILL_MAGLEVEL, static_cast<uint64_t>(tries * paladinML));
			addOfflineTrainingTries(SKILL_SHIELD, static_cast<uint64_t>(tries * paladinShield));
		} else if (isKnight()) {
			float knightMelee = ConfigManager::getFloat(ConfigManager::OFFLINE_TRAINING_KNIGHT_MELEE);
			float knightShield = ConfigManager::getFloat(ConfigManager::OFFLINE_TRAINING_KNIGHT_SHIELD);

			skills_t bestMelee = SKILL_SWORD;
			uint32_t maxLevel = skills[SKILL_SWORD].level;

			if (skills[SKILL_AXE].level > maxLevel) {
				maxLevel = skills[SKILL_AXE].level;
				bestMelee = SKILL_AXE;
			}

			if (skills[SKILL_CLUB].level > maxLevel) {
				maxLevel = skills[SKILL_CLUB].level;
				bestMelee = SKILL_CLUB;
			}

			addOfflineTrainingTries(bestMelee, static_cast<uint64_t>(tries * knightMelee));
			addOfflineTrainingTries(SKILL_SHIELD, static_cast<uint64_t>(tries * knightShield));
		} else if (isMonk()) {
			float monkMelee = ConfigManager::getFloat(ConfigManager::OFFLINE_TRAINING_MONK_MELEE);
			float monkShield = ConfigManager::getFloat(ConfigManager::OFFLINE_TRAINING_MONK_SHIELD);

			addOfflineTrainingTries(SKILL_FIST, static_cast<uint64_t>(tries * monkMelee));
			addOfflineTrainingTries(SKILL_SHIELD, static_cast<uint64_t>(tries * monkShield));
		}
	} else {
		// Manual mode
		addOfflineTrainingTries(static_cast<skills_t>(offlineTrainingSkill), tries);
	}
}

Inbox* Player::getInbox()
{
	if (!town) {
		return nullptr;
	}
	return getInbox(town->getID());
}

Inbox* Player::getInbox(uint32_t depotId)
{
	DepotLocker* depotLocker = getDepotLocker(depotId);
	if (!depotLocker) {
		return nullptr;
	}

	for (const auto& item : depotLocker->getItemList()) {
		if (item->getID() == ITEM_INBOX) {
			return static_cast<Inbox*>(item.get());
		}
	}
	return nullptr;
}

void Player::clearCooldowns()
{
	for (auto it = conditions.begin(); it != conditions.end(); ++it) {
		const auto& condition = *it;
		if (!condition) {
			continue;
		}

		ConditionType_t type = condition->getType();
		if (type == CONDITION_SPELLCOOLDOWN || type == CONDITION_SPELLGROUPCOOLDOWN) {
			uint32_t subId = condition->getSubId();
			condition->setTicks(0);
			if (type == CONDITION_SPELLGROUPCOOLDOWN) {
				sendSpellGroupCooldown(static_cast<SpellGroup_t>(subId), 0);
			} else {
				sendSpellCooldown(static_cast<uint8_t>(subId), 0);
			}
		}
	}
}
