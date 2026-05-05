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
	if (hasFlag(PlayerFlag_HasInfiniteCapacity)) {
		return;
	}

	inventoryWeight = 0;
	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		const Item* inventoryItem = inventory[i].get();
		if (!inventoryItem) {
			continue;
		}
		inventoryWeight += inventoryItem->getWeight();
	}

	const Item* backpackItem = inventory[CONST_SLOT_BACKPACK].get();
	if (!backpackItem) {
		return;
	}

	const Container* backpack = backpackItem->getContainer();
	if (!backpack) {
		return;
	}

	const float weightReduction = backpack->getWeightReduction();
	if (weightReduction <= 0.0f) {
		return;
	}

	const float reductionPercent = std::min(weightReduction, 1.0f);
	const uint32_t backpackTotalWeight = backpack->getWeight();
	const uint32_t backpackBaseWeight = backpack->getBaseWeight();
	if (backpackTotalWeight <= backpackBaseWeight) {
		return;
	}

	const uint64_t contentsWeight = backpackTotalWeight - backpackBaseWeight;
	const uint64_t reduction = static_cast<uint64_t>(contentsWeight * reductionPercent);
	const uint64_t capped = std::min<uint64_t>(reduction, inventoryWeight);
	inventoryWeight -= static_cast<uint32_t>(capped);
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
