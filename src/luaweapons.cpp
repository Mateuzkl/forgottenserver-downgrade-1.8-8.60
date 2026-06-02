// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "luascript.h"
#include "script.h"
#include "scriptmanager.h"
#include "weapons.h"
#include "logger.h"
#include <fmt/format.h>

namespace {
using namespace Lua;

// Weapon
int luaCreateWeapon(lua_State* L)
{
	// Weapon(type)
	if (LuaScriptInterface::getScriptEnv()->getScriptInterface() != &g_scripts->getScriptInterface()) {
		reportErrorFunc(L, "Weapons can only be registered in the Scripts interface.");
		lua_pushnil(L);
		return 1;
	}

	WeaponType_t type = getInteger<WeaponType_t>(L, 2);
	switch (type) {
		case WEAPON_SWORD:
		case WEAPON_AXE:
		case WEAPON_CLUB: {
			auto weapon = std::make_unique<WeaponMelee>(LuaScriptInterface::getScriptEnv()->getScriptInterface());
			weapon->weaponType = type;
			weapon->fromLua = true;
			pushOwnedUserdataAs<Weapon>(L, std::move(weapon));
			setMetatable(L, -1, "Weapon");
			break;
		}
		case WEAPON_DISTANCE:
		case WEAPON_AMMO: {
			auto weapon = std::make_unique<WeaponDistance>(LuaScriptInterface::getScriptEnv()->getScriptInterface());
			weapon->weaponType = type;
			weapon->fromLua = true;
			pushOwnedUserdataAs<Weapon>(L, std::move(weapon));
			setMetatable(L, -1, "Weapon");
			break;
		}
		case WEAPON_WAND: {
			auto weapon = std::make_unique<WeaponWand>(LuaScriptInterface::getScriptEnv()->getScriptInterface());
			weapon->weaponType = type;
			weapon->fromLua = true;
			pushOwnedUserdataAs<Weapon>(L, std::move(weapon));
			setMetatable(L, -1, "Weapon");
			break;
		}
		default: {
			lua_pushnil(L);
			break;
		}
	}
	return 1;
}

int luaWeaponAction(lua_State* L)
{
	// weapon:action(callback)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		std::string typeName = getString(L, 2);
		std::string tmpStr = asLowerCaseString(typeName);
		if (tmpStr == "removecount") {
			weapon->action = WEAPONACTION_REMOVECOUNT;
		} else if (tmpStr == "removecharge") {
			weapon->action = WEAPONACTION_REMOVECHARGE;
		} else if (tmpStr == "move") {
			weapon->action = WEAPONACTION_MOVE;
		} else {
			LOG_ERROR(fmt::format("Error: [Weapon::action] No valid action {}", typeName));
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponRegister(lua_State* L)
{
	// weapon:register()
	if (auto weapon = getUserdata<Weapon>(L, 1)) {
		if (weapon->weaponType == WEAPON_DISTANCE || weapon->weaponType == WEAPON_AMMO) {
			weapon = getUserdata<WeaponDistance>(L, 1);
		} else if (weapon->weaponType == WEAPON_WAND) {
			weapon = getUserdata<WeaponWand>(L, 1);
		} else {
			weapon = getUserdata<WeaponMelee>(L, 1);
		}

		if (!weapon) {
			lua_pushnil(L);
			return 1;
		}

		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.weaponType = weapon->weaponType;

		if (weapon->getWieldInfo() != 0) {
			it.wieldInfo = weapon->getWieldInfo();
			it.vocationString = weapon->getVocationString();
			it.minReqLevel = weapon->getReqLevel();
			it.minReqMagicLevel = weapon->getReqMagLv();
		}

		weapon->configureWeapon(it);

		std::unique_ptr<Weapon> ownedWeapon(releaseOwnedUserdata<Weapon>(L, 1));
		pushBoolean(L, g_weapons->registerLuaEvent(std::move(ownedWeapon)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponOnUseWeapon(lua_State* L)
{
	// weapon:onUseWeapon(callback)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		if (!weapon->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponUnproperly(lua_State* L)
{
	// weapon:wieldedUnproperly(bool)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setWieldUnproperly(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponLevel(lua_State* L)
{
	// weapon:level(lvl)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setRequiredLevel(getInteger<uint32_t>(L, 2));
		weapon->setWieldInfo(WIELDINFO_LEVEL);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponMagicLevel(lua_State* L)
{
	// weapon:magicLevel(lvl)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setRequiredMagLevel(getInteger<uint32_t>(L, 2));
		weapon->setWieldInfo(WIELDINFO_MAGLV);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponMana(lua_State* L)
{
	// weapon:mana(mana)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setMana(getInteger<uint32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponManaPercent(lua_State* L)
{
	// weapon:manaPercent(percent)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setManaPercent(getInteger<uint32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponHealth(lua_State* L)
{
	// weapon:health(health)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setHealth(getInteger<int32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponHealthPercent(lua_State* L)
{
	// weapon:healthPercent(percent)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setHealthPercent(getInteger<uint32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponSoul(lua_State* L)
{
	// weapon:soul(soul)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setSoul(getInteger<uint32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponBreakChance(lua_State* L)
{
	// weapon:breakChance(percent)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setBreakChance(getInteger<uint8_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponWandDamage(lua_State* L)
{
	// weapon:damage(damage[min, max]) only use this if the weapon is a wand!
	WeaponWand* weapon = getUserdata<WeaponWand>(L, 1);
	if (weapon) {
		weapon->setMinChange(getInteger<uint32_t>(L, 2));
		if (lua_gettop(L) > 2) {
			weapon->setMaxChange(getInteger<uint32_t>(L, 3));
		} else {
			weapon->setMaxChange(getInteger<uint32_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponElement(lua_State* L)
{
	// weapon:element(combatType)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		if (!getInteger<CombatType_t>(L, 2)) {
			std::string element = getString(L, 2);
			std::string tmpStrValue = asLowerCaseString(element);
			if (tmpStrValue == "earth") {
				weapon->params.combatType = COMBAT_EARTHDAMAGE;
			} else if (tmpStrValue == "ice") {
				weapon->params.combatType = COMBAT_ICEDAMAGE;
			} else if (tmpStrValue == "energy") {
				weapon->params.combatType = COMBAT_ENERGYDAMAGE;
			} else if (tmpStrValue == "fire") {
				weapon->params.combatType = COMBAT_FIREDAMAGE;
			} else if (tmpStrValue == "death") {
				weapon->params.combatType = COMBAT_DEATHDAMAGE;
			} else if (tmpStrValue == "holy") {
				weapon->params.combatType = COMBAT_HOLYDAMAGE;
			} else {
				LOG_WARN(fmt::format("[Warning - weapon:element] Type \"{}\" does not exist.", element));
				pushBoolean(L, false);
				return 1;
			}
		} else {
			weapon->params.combatType = getInteger<CombatType_t>(L, 2);
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponPremium(lua_State* L)
{
	// weapon:premium(bool)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setNeedPremium(getBoolean(L, 2));
		weapon->setWieldInfo(WIELDINFO_PREMIUM);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponVocation(lua_State* L)
{
	// weapon:vocation(vocName[, showInDescription = false, lastVoc = false])
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->addVocationWeaponSet(getString(L, 2));
		weapon->setWieldInfo(WIELDINFO_VOCREQ);
		std::string tmp;
		bool showInDescription = getBoolean(L, 3, false);
		bool lastVoc = getBoolean(L, 4, false);

		if (showInDescription) {
			if (weapon->getVocationString().empty()) {
				tmp = asLowerCaseString(getString(L, 2));
				tmp += "s";
				weapon->setVocationString(tmp);
			} else {
				tmp = weapon->getVocationString();
				if (lastVoc) {
					tmp += " and ";
				} else {
					tmp += ", ";
				}
				tmp += asLowerCaseString(getString(L, 2));
				tmp += "s";
				weapon->setVocationString(tmp);
			}
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponId(lua_State* L)
{
	// weapon:id(id)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setID(getInteger<uint16_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponAttack(lua_State* L)
{
	// weapon:attack(atk)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.attack = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponDefense(lua_State* L)
{
	// weapon:defense(defense[, extraDefense])
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.defense = getInteger<int32_t>(L, 2);
		if (lua_gettop(L) > 2) {
			it.extraDefense = getInteger<int32_t>(L, 3);
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponRange(lua_State* L)
{
	// weapon:range(range)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.shootRange = getInteger<uint8_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponCharges(lua_State* L)
{
	// weapon:charges(charges[, showCharges = true])
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		bool showCharges = getBoolean(L, 3, true);
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);

		it.charges = getInteger<uint32_t>(L, 2);
		it.showCharges = showCharges;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponDuration(lua_State* L)
{
	// weapon:duration(duration[, showDuration = true])
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		bool showDuration = getBoolean(L, 3, true);
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);

		if (isTable(L, 2)) {
			it.decayTimeMin = getField<uint32_t>(L, 2, "min");
			it.decayTimeMax = getField<uint32_t>(L, 2, "max");
		} else {
			it.decayTimeMin = getInteger<uint32_t>(L, 2);
		}

		it.showDuration = showDuration;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponDecayTo(lua_State* L)
{
	// weapon:decayTo([itemid = 0])
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t itemid = getInteger<uint16_t>(L, 2, 0);
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);

		it.decayTo = itemid;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponTransformEquipTo(lua_State* L)
{
	// weapon:transformEquipTo(itemid)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.transformEquipTo = getInteger<uint16_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponTransformDeEquipTo(lua_State* L)
{
	// weapon:transformDeEquipTo(itemid)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.transformDeEquipTo = getInteger<uint16_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponShootType(lua_State* L)
{
	// weapon:shootType(type)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.shootType = getInteger<ShootType_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponSlotType(lua_State* L)
{
	// weapon:slotType(slot)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		std::string slot = getString(L, 2);

		if (slot == "two-handed") {
			it.slotPosition |= SLOTP_TWO_HAND;
		} else {
			it.slotPosition |= SLOTP_HAND;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponAmmoType(lua_State* L)
{
	// weapon:ammoType(type)
	WeaponDistance* weapon = getUserdata<WeaponDistance>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		std::string type = getString(L, 2);

		if (type == "arrow") {
			it.ammoType = AMMO_ARROW;
		} else if (type == "bolt") {
			it.ammoType = AMMO_BOLT;
		} else {
			LOG_WARN(fmt::format("[Warning - weapon:ammoType] Type \"{}\" does not exist.", type));
			lua_pushnil(L);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponHitChance(lua_State* L)
{
	// weapon:hitChance(chance)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.hitChance = getInteger<int8_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponMaxHitChance(lua_State* L)
{
	// weapon:maxHitChance(max)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.maxHitChance = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaWeaponExtraElement(lua_State* L)
{
	// weapon:extraElement(atk, combatType)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		CombatType_t elementType = getInteger<CombatType_t>(L, 3);

		if (!elementType) {
			std::string element = getString(L, 3);
			std::string tmpStrValue = asLowerCaseString(element);
			if (tmpStrValue == "earth") {
				elementType = COMBAT_EARTHDAMAGE;
			} else if (tmpStrValue == "ice") {
				elementType = COMBAT_ICEDAMAGE;
			} else if (tmpStrValue == "energy") {
				elementType = COMBAT_ENERGYDAMAGE;
			} else if (tmpStrValue == "fire") {
				elementType = COMBAT_FIREDAMAGE;
			} else if (tmpStrValue == "death") {
				elementType = COMBAT_DEATHDAMAGE;
			} else if (tmpStrValue == "holy") {
				elementType = COMBAT_HOLYDAMAGE;
			} else {
				LOG_WARN(fmt::format("[Warning - weapon:extraElement] Type \"{}\" does not exist.", element));
				pushBoolean(L, false);
				return 1;
			}
		}

		Abilities& abilities = it.getAbilities();
		abilities.elementDamage = getInteger<uint16_t>(L, 2);
		abilities.elementType = elementType;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaDeleteWeapon(lua_State* L)
{
	return deleteOwnedUserdata(L);
}
} // namespace

void LuaScriptInterface::registerWeapons()
{
	// Weapon
	registerClass("Weapon", "", luaCreateWeapon);
	registerMetaMethod("Weapon", "__gc", luaDeleteWeapon);
	registerMetaMethod("Weapon", "__close", luaDeleteWeapon);
	registerMethod("Weapon", "delete", luaDeleteWeapon);

	registerMethod("Weapon", "action", luaWeaponAction);
	registerMethod("Weapon", "register", luaWeaponRegister);
	registerMethod("Weapon", "id", luaWeaponId);
	registerMethod("Weapon", "level", luaWeaponLevel);
	registerMethod("Weapon", "magicLevel", luaWeaponMagicLevel);
	registerMethod("Weapon", "mana", luaWeaponMana);
	registerMethod("Weapon", "manaPercent", luaWeaponManaPercent);
	registerMethod("Weapon", "health", luaWeaponHealth);
	registerMethod("Weapon", "healthPercent", luaWeaponHealthPercent);
	registerMethod("Weapon", "soul", luaWeaponSoul);
	registerMethod("Weapon", "breakChance", luaWeaponBreakChance);
	registerMethod("Weapon", "premium", luaWeaponPremium);
	registerMethod("Weapon", "wieldUnproperly", luaWeaponUnproperly);
	registerMethod("Weapon", "vocation", luaWeaponVocation);
	registerMethod("Weapon", "onUseWeapon", luaWeaponOnUseWeapon);
	registerMethod("Weapon", "element", luaWeaponElement);
	registerMethod("Weapon", "attack", luaWeaponAttack);
	registerMethod("Weapon", "defense", luaWeaponDefense);
	registerMethod("Weapon", "range", luaWeaponRange);
	registerMethod("Weapon", "charges", luaWeaponCharges);
	registerMethod("Weapon", "duration", luaWeaponDuration);
	registerMethod("Weapon", "decayTo", luaWeaponDecayTo);
	registerMethod("Weapon", "transformEquipTo", luaWeaponTransformEquipTo);
	registerMethod("Weapon", "transformDeEquipTo", luaWeaponTransformDeEquipTo);
	registerMethod("Weapon", "slotType", luaWeaponSlotType);
	registerMethod("Weapon", "hitChance", luaWeaponHitChance);
	registerMethod("Weapon", "extraElement", luaWeaponExtraElement);

	// exclusively for distance weapons
	registerMethod("Weapon", "ammoType", luaWeaponAmmoType);
	registerMethod("Weapon", "maxHitChance", luaWeaponMaxHitChance);

	// exclusively for wands
	registerMethod("Weapon", "damage", luaWeaponWandDamage);

	// exclusively for wands & distance weapons
	registerMethod("Weapon", "shootType", luaWeaponShootType);
}
