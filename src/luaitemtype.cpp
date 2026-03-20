// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "item.h"
#include "items.h"
#include "luascript.h"

namespace {
using namespace Lua;

// ItemType
int luaItemTypeCreate(lua_State* L)
{
	// ItemType(id or name)
	uint32_t id;
	if (isInteger(L, 2)) {
		id = getInteger<uint32_t>(L, 2);
	} else if (isString(L, 2)) {
		id = Item::items.getItemIdByName(getString(L, 2));
	} else {
		lua_pushnil(L);
		return 1;
	}

	const ItemType& itemType = Item::items[id];
	pushUserdata<const ItemType>(L, &itemType);
	setMetatable(L, -1, "ItemType");
	return 1;
}

int luaItemTypeIsCorpse(lua_State* L)
{
	// itemType:isCorpse()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->corpseType != RACE_NONE);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsDoor(lua_State* L)
{
	// itemType:isDoor()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->isDoor());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsContainer(lua_State* L)
{
	// itemType:isContainer()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->isContainer());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsFluidContainer(lua_State* L)
{
	// itemType:isFluidContainer()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->isFluidContainer());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsMovable(lua_State* L)
{
	// itemType:isMovable()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->moveable);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsRune(lua_State* L)
{
	// itemType:isRune()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->isRune());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsStackable(lua_State* L)
{
	// itemType:isStackable()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->stackable);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsReadable(lua_State* L)
{
	// itemType:isReadable()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->canReadText);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsWritable(lua_State* L)
{
	// itemType:isWritable()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->canWriteText);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsBlocking(lua_State* L)
{
	// itemType:isBlocking()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->blockProjectile || itemType->blockSolid);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsGroundTile(lua_State* L)
{
	// itemType:isGroundTile()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->isGroundTile());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsMagicField(lua_State* L)
{
	// itemType:isMagicField()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->isMagicField());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsUseable(lua_State* L)
{
	// itemType:isUseable()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->isUseable());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeIsPickupable(lua_State* L)
{
	// itemType:isPickupable()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->isPickupable());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetType(lua_State* L)
{
	// itemType:getType()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->type);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetGroup(lua_State* L)
{
	// itemType:getGroup()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->group);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetId(lua_State* L)
{
	// itemType:getId()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->id);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetClientId(lua_State* L)
{
	// itemType:getClientId()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->clientId);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetName(lua_State* L)
{
	// itemType:getName()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushString(L, itemType->name);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetPluralName(lua_State* L)
{
	// itemType:getPluralName()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushString(L, itemType->getPluralName());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetArticle(lua_State* L)
{
	// itemType:getArticle()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushString(L, itemType->article);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetDescription(lua_State* L)
{
	// itemType:getDescription()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushString(L, itemType->description);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetSlotPosition(lua_State* L)
{
	// itemType:getSlotPosition()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->slotPosition);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetCharges(lua_State* L)
{
	// itemType:getCharges()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->charges);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetFluidSource(lua_State* L)
{
	// itemType:getFluidSource()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->fluidSource);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetCapacity(lua_State* L)
{
	// itemType:getCapacity()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->maxItems);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetWeight(lua_State* L)
{
	// itemType:getWeight([count = 1])
	uint16_t count = getInteger<uint16_t>(L, 2, 1);

	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (!itemType) {
		lua_pushnil(L);
		return 1;
	}

	uint64_t weight = static_cast<uint64_t>(itemType->weight) * std::max<int32_t>(1, count);
	lua_pushinteger(L, weight);
	return 1;
}

int luaItemTypeGetWorth(lua_State* L)
{
	// itemType:getWorth()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (!itemType) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, itemType->worth);
	return 1;
}

int luaItemTypeGetStackSize(lua_State* L)
{
	// itemType:getStackSize()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (!itemType) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, itemType->stackable ? itemType->stackSize : 1);
	return 1;
}

int luaItemTypeGetHitChance(lua_State* L)
{
	// itemType:getHitChance()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->hitChance);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetMaxHitChance(lua_State* L)
{
	// itemType:getMaxHitChance()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->maxHitChance);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetShootRange(lua_State* L)
{
	// itemType:getShootRange()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->shootRange);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetAttack(lua_State* L)
{
	// itemType:getAttack()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->attack);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetAttackSpeed(lua_State* L)
{
	// itemType:getAttackSpeed()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->attackSpeed);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetDefense(lua_State* L)
{
	// itemType:getDefense()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->defense);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetExtraDefense(lua_State* L)
{
	// itemType:getExtraDefense()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->extraDefense);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetArmor(lua_State* L)
{
	// itemType:getArmor()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->armor);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetReduceSkillLoss(lua_State* L)
{
	// itemType:getReduceSkillLoss()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->reduceSkillLoss);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetWeaponType(lua_State* L)
{
	// itemType:getWeaponType()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->weaponType);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetAmmoType(lua_State* L)
{
	// itemType:getAmmoType()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->ammoType);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetCorpseType(lua_State* L)
{
	// itemType:getCorpseType()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->corpseType);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetRotateTo(lua_State* L)
{
	// itemType:getRotateTo()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->rotateTo);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetAbilities(lua_State* L)
{
	// itemType:getAbilities()
	ItemType* itemType = getUserdata<ItemType>(L, 1);
	if (itemType) {
		Abilities& abilities = itemType->getAbilities();
		lua_createtable(L, 12, 13);
		setField(L, "healthGain", abilities.healthGain);
		setField(L, "healthGainPercent", abilities.healthGainPercent);
		setField(L, "healthTicks", abilities.healthTicks);
		setField(L, "manaGain", abilities.manaGain);
		setField(L, "manaGainPercent", abilities.manaGainPercent);
		setField(L, "manaTicks", abilities.manaTicks);
		setField(L, "conditionImmunities", abilities.conditionImmunities);
		setField(L, "conditionSuppressions", abilities.conditionSuppressions);
		setField(L, "speed", abilities.speed);
		setField(L, "elementDamage", abilities.elementDamage);
		setField(L, "elementType", abilities.elementType);

		lua_pushboolean(L, abilities.manaShield);
		lua_setfield(L, -2, "manaShield");
		lua_pushboolean(L, abilities.invisible);
		lua_setfield(L, -2, "invisible");
		lua_pushboolean(L, abilities.regeneration);
		lua_setfield(L, -2, "regeneration");

		// Stats
		lua_createtable(L, 0, STAT_LAST + 1);
		for (int32_t i = STAT_FIRST; i <= STAT_LAST; i++) {
			lua_pushinteger(L, abilities.stats[i]);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "stats");

		// Stats percent
		lua_createtable(L, 0, STAT_LAST + 1);
		for (int32_t i = STAT_FIRST; i <= STAT_LAST; i++) {
			lua_pushinteger(L, abilities.statsPercent[i]);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "statsPercent");

		// Skills
		lua_createtable(L, 0, SKILL_LAST + 1);
		for (int32_t i = SKILL_FIRST; i <= SKILL_LAST; i++) {
			lua_pushinteger(L, abilities.skills[i]);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "skills");

		// Special skills
		lua_createtable(L, 0, SPECIALSKILL_LAST + 1);
		for (int32_t i = SPECIALSKILL_FIRST; i <= SPECIALSKILL_LAST; i++) {
			lua_pushinteger(L, abilities.specialSkills[i]);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "specialSkills");

		// Field absorb percent
		lua_createtable(L, 0, COMBAT_COUNT);
		for (int32_t i = 0; i < COMBAT_COUNT; i++) {
			lua_pushinteger(L, abilities.fieldAbsorbPercent[i]);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "fieldAbsorbPercent");

		// Absorb percent
		lua_createtable(L, 0, COMBAT_COUNT);
		for (int32_t i = 0; i < COMBAT_COUNT; i++) {
			lua_pushinteger(L, abilities.absorbPercent[i]);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "absorbPercent");

		// special magic level
		lua_createtable(L, 0, COMBAT_COUNT);
		for (int32_t i = 0; i < COMBAT_COUNT; i++) {
			lua_pushinteger(L, abilities.specialMagicLevelSkill[i]);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "specialMagicLevel");

		// Damage boost percent
		lua_createtable(L, 0, COMBAT_COUNT);
		for (int32_t i = 0; i < COMBAT_COUNT; i++) {
			lua_pushinteger(L, abilities.boostPercent[i]);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "boostPercent");

		// Reflect chance
		lua_createtable(L, 0, COMBAT_COUNT);
		for (int32_t i = 0; i < COMBAT_COUNT; i++) {
			lua_pushinteger(L, abilities.reflect[i].chance);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "reflectChance");

		// Reflect percent
		lua_createtable(L, 0, COMBAT_COUNT);
		for (int32_t i = 0; i < COMBAT_COUNT; i++) {
			lua_pushinteger(L, abilities.reflect[i].percent);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "reflectPercent");

		// Experience Rates
		lua_createtable(L, 0, static_cast<size_t>(ExperienceRateType::BONUS));
		for (uint8_t e = static_cast<uint8_t>(ExperienceRateType::BASE);
		     e <= static_cast<uint8_t>(ExperienceRateType::STAMINA); e++) {
			lua_pushinteger(L, abilities.experienceRate[e]);
			lua_rawseti(L, -2, e);
		}
		lua_setfield(L, -2, "experienceRate");
	}
	return 1;
}

int luaItemTypeHasShowAttributes(lua_State* L)
{
	// itemType:hasShowAttributes()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->showAttributes);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeHasShowCount(lua_State* L)
{
	// itemType:hasShowCount()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->showCount);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeHasShowCharges(lua_State* L)
{
	// itemType:hasShowCharges()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->showCharges);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeHasShowDuration(lua_State* L)
{
	// itemType:hasShowDuration()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->showDuration);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeHasAllowDistRead(lua_State* L)
{
	// itemType:hasAllowDistRead()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->allowDistRead);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetWieldInfo(lua_State* L)
{
	// itemType:getWieldInfo()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->wieldInfo);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetDurationMin(lua_State* L)
{
	// itemType:getDurationMin()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->decayTimeMin);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetDurationMax(lua_State* L)
{
	// itemType:getDurationMax()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->decayTimeMax);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetLevelDoor(lua_State* L)
{
	// itemType:getLevelDoor()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->levelDoor);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetRuneSpellName(lua_State* L)
{
	// itemType:getRuneSpellName()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType && itemType->isRune()) {
		pushString(L, itemType->runeSpellName);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetVocationString(lua_State* L)
{
	// itemType:getVocationString()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushString(L, itemType->vocationString);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetMinReqLevel(lua_State* L)
{
	// itemType:getMinReqLevel()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->minReqLevel);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetMinReqMagicLevel(lua_State* L)
{
	// itemType:getMinReqMagicLevel()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->minReqMagicLevel);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetElementType(lua_State* L)
{
	// itemType:getElementType()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (!itemType) {
		lua_pushnil(L);
		return 1;
	}

	auto& abilities = itemType->abilities;
	if (abilities) {
		lua_pushinteger(L, abilities->elementType);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetElementDamage(lua_State* L)
{
	// itemType:getElementDamage()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (!itemType) {
		lua_pushnil(L);
		return 1;
	}

	auto& abilities = itemType->abilities;
	if (abilities) {
		lua_pushinteger(L, abilities->elementDamage);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetTransformEquipId(lua_State* L)
{
	// itemType:getTransformEquipId()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->transformEquipTo);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetTransformDeEquipId(lua_State* L)
{
	// itemType:getTransformDeEquipId()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->transformDeEquipTo);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetDestroyId(lua_State* L)
{
	// itemType:getDestroyId()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->destroyTo);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetDecayId(lua_State* L)
{
	// itemType:getDecayId()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->decayTo);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetRequiredLevel(lua_State* L)
{
	// itemType:getRequiredLevel()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->minReqLevel);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeHasSubType(lua_State* L)
{
	// itemType:hasSubType()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		pushBoolean(L, itemType->hasSubType());
	} else {
		lua_pushnil(L);
	}
	return 1;
}
int luaItemTypeGetClassification(lua_State* L)
{
	// itemType:getClassification()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushnumber(L, itemType->classification);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetTier(lua_State* L)
{
	// itemType:getTier()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushnumber(L, itemType->tier);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaItemTypeGetElementalBond(lua_State* L)
{
	// itemType:getElementalBond()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (!itemType) {
		lua_pushnil(L);
		return 1;
	}

	if (!itemType->elementalBond.empty()) {
		pushString(L, itemType->elementalBond);
	} else {
		pushString(L, "physical");
	}
	return 1;
}

int luaItemTypeGetImbuementSlot(lua_State* L)
{
	// itemType:getImbuementSlot()
	const ItemType* itemType = getUserdata<const ItemType>(L, 1);
	if (itemType) {
		lua_pushinteger(L, itemType->imbuementSlot);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

} // namespace

void LuaScriptInterface::registerItemType()
{
	// ItemType
	registerClass("ItemType", "", luaItemTypeCreate);
	registerMetaMethod("ItemType", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("ItemType", "isCorpse", luaItemTypeIsCorpse);
	registerMethod("ItemType", "isDoor", luaItemTypeIsDoor);
	registerMethod("ItemType", "isContainer", luaItemTypeIsContainer);
	registerMethod("ItemType", "isFluidContainer", luaItemTypeIsFluidContainer);
	registerMethod("ItemType", "isMovable", luaItemTypeIsMovable);
	registerMethod("ItemType", "isRune", luaItemTypeIsRune);
	registerMethod("ItemType", "isStackable", luaItemTypeIsStackable);
	registerMethod("ItemType", "isReadable", luaItemTypeIsReadable);
	registerMethod("ItemType", "isWritable", luaItemTypeIsWritable);
	registerMethod("ItemType", "isBlocking", luaItemTypeIsBlocking);
	registerMethod("ItemType", "isGroundTile", luaItemTypeIsGroundTile);
	registerMethod("ItemType", "isMagicField", luaItemTypeIsMagicField);
	registerMethod("ItemType", "isUseable", luaItemTypeIsUseable);
	registerMethod("ItemType", "isPickupable", luaItemTypeIsPickupable);

	registerMethod("ItemType", "getType", luaItemTypeGetType);
	registerMethod("ItemType", "getGroup", luaItemTypeGetGroup);
	registerMethod("ItemType", "getId", luaItemTypeGetId);
	registerMethod("ItemType", "getClientId", luaItemTypeGetClientId);
	registerMethod("ItemType", "getName", luaItemTypeGetName);
	registerMethod("ItemType", "getPluralName", luaItemTypeGetPluralName);
	registerMethod("ItemType", "getArticle", luaItemTypeGetArticle);
	registerMethod("ItemType", "getDescription", luaItemTypeGetDescription);
	registerMethod("ItemType", "getSlotPosition", luaItemTypeGetSlotPosition);

	registerMethod("ItemType", "getCharges", luaItemTypeGetCharges);
	registerMethod("ItemType", "getFluidSource", luaItemTypeGetFluidSource);
	registerMethod("ItemType", "getCapacity", luaItemTypeGetCapacity);
	registerMethod("ItemType", "getWeight", luaItemTypeGetWeight);
	registerMethod("ItemType", "getWorth", luaItemTypeGetWorth);
	registerMethod("ItemType", "getStackSize", luaItemTypeGetStackSize);

	registerMethod("ItemType", "getHitChance", luaItemTypeGetHitChance);
	registerMethod("ItemType", "getMaxHitChance", luaItemTypeGetMaxHitChance);
	registerMethod("ItemType", "getShootRange", luaItemTypeGetShootRange);

	registerMethod("ItemType", "getAttack", luaItemTypeGetAttack);
	registerMethod("ItemType", "getAttackSpeed", luaItemTypeGetAttackSpeed);
	registerMethod("ItemType", "getClassification", luaItemTypeGetClassification);
	registerMethod("ItemType", "getTier", luaItemTypeGetTier);
	registerMethod("ItemType", "getDefense", luaItemTypeGetDefense);
	registerMethod("ItemType", "getExtraDefense", luaItemTypeGetExtraDefense);
	registerMethod("ItemType", "getArmor", luaItemTypeGetArmor);
	registerMethod("ItemType", "getReduceSkillLoss", luaItemTypeGetReduceSkillLoss);
	registerMethod("ItemType", "getWeaponType", luaItemTypeGetWeaponType);

	registerMethod("ItemType", "getElementType", luaItemTypeGetElementType);
	registerMethod("ItemType", "getElementDamage", luaItemTypeGetElementDamage);
	registerMethod("ItemType", "getElementalBond", luaItemTypeGetElementalBond);

	registerMethod("ItemType", "getTransformEquipId", luaItemTypeGetTransformEquipId);
	registerMethod("ItemType", "getTransformDeEquipId", luaItemTypeGetTransformDeEquipId);
	registerMethod("ItemType", "getDestroyId", luaItemTypeGetDestroyId);
	registerMethod("ItemType", "getDecayId", luaItemTypeGetDecayId);
	registerMethod("ItemType", "getRequiredLevel", luaItemTypeGetRequiredLevel);
	registerMethod("ItemType", "getAmmoType", luaItemTypeGetAmmoType);
	registerMethod("ItemType", "getCorpseType", luaItemTypeGetCorpseType);
	registerMethod("ItemType", "getRotateTo", luaItemTypeGetRotateTo);

	registerMethod("ItemType", "getAbilities", luaItemTypeGetAbilities);

	registerMethod("ItemType", "hasShowAttributes", luaItemTypeHasShowAttributes);
	registerMethod("ItemType", "hasShowCount", luaItemTypeHasShowCount);
	registerMethod("ItemType", "hasShowCharges", luaItemTypeHasShowCharges);
	registerMethod("ItemType", "hasShowDuration", luaItemTypeHasShowDuration);
	registerMethod("ItemType", "hasAllowDistRead", luaItemTypeHasAllowDistRead);
	registerMethod("ItemType", "getWieldInfo", luaItemTypeGetWieldInfo);
	registerMethod("ItemType", "getDurationMin", luaItemTypeGetDurationMin);
	registerMethod("ItemType", "getDurationMax", luaItemTypeGetDurationMax);
	registerMethod("ItemType", "getLevelDoor", luaItemTypeGetLevelDoor);
	registerMethod("ItemType", "getRuneSpellName", luaItemTypeGetRuneSpellName);
	registerMethod("ItemType", "getVocationString", luaItemTypeGetVocationString);
	registerMethod("ItemType", "getMinReqLevel", luaItemTypeGetMinReqLevel);
	registerMethod("ItemType", "getMinReqMagicLevel", luaItemTypeGetMinReqMagicLevel);

	registerMethod("ItemType", "getImbuementSlot", luaItemTypeGetImbuementSlot);

	registerMethod("ItemType", "hasSubType", luaItemTypeHasSubType);
}
