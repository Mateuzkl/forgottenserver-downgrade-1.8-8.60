// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "luascript.h"
#include "monsters.h"

extern Monsters g_monsters;

namespace {
using namespace Lua;

// MonsterSpell
int luaCreateMonsterSpell(lua_State* L)
{
	// MonsterSpell() will create a Lua-owned MonsterSpell
	auto spell = std::make_unique<MonsterSpell>();
	pushOwnedUserdata<MonsterSpell>(L, std::move(spell));
	setMetatable(L, -1, "MonsterSpell");
	return 1;
}

int luaDeleteMonsterSpell(lua_State* L)
{
	// monsterSpell:delete() or monsterSpell:__gc() or monsterSpell:__close()
	return deleteOwnedUserdata(L);
}

int luaMonsterSpellSetType(lua_State* L)
{
	// monsterSpell:setType(type)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->name = getString(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetScriptName(lua_State* L)
{
	// monsterSpell:setScriptName(name)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->scriptName = getString(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetChance(lua_State* L)
{
	// monsterSpell:setChance(chance)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->chance = getInteger<uint8_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetInterval(lua_State* L)
{
	// monsterSpell:setInterval(interval)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->interval = getInteger<uint16_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetRange(lua_State* L)
{
	// monsterSpell:setRange(range)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->range = getInteger<uint8_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetCombatValue(lua_State* L)
{
	// monsterSpell:setCombatValue(min, max)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->minCombatValue = getInteger<int32_t>(L, 2);
		spell->maxCombatValue = getInteger<int32_t>(L, 3);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetCombatType(lua_State* L)
{
	// monsterSpell:setCombatType(combatType_t)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->combatType = getInteger<CombatType_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetAttackValue(lua_State* L)
{
	// monsterSpell:setAttackValue(attack, skill)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->attack = getInteger<int32_t>(L, 2);
		spell->skill = getInteger<int32_t>(L, 3);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetNeedTarget(lua_State* L)
{
	// monsterSpell:setNeedTarget(bool)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->needTarget = getBoolean(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetNeedDirection(lua_State* L)
{
	// monsterSpell:setNeedDirection(bool)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->needDirection = getBoolean(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetCombatLength(lua_State* L)
{
	// monsterSpell:setCombatLength(length)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->length = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetCombatSpread(lua_State* L)
{
	// monsterSpell:setCombatSpread(spread)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->spread = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetCombatRadius(lua_State* L)
{
	// monsterSpell:setCombatRadius(radius)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->radius = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetCombatRing(lua_State* L)
{
	// monsterSpell:setCombatRing(ring)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->ring = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetConditionType(lua_State* L)
{
	// monsterSpell:setConditionType(type)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->conditionType = getInteger<ConditionType_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetConditionDamage(lua_State* L)
{
	// monsterSpell:setConditionDamage(min, max, start)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->conditionMinDamage = getInteger<int32_t>(L, 2);
		spell->conditionMaxDamage = getInteger<int32_t>(L, 3);
		spell->conditionStartDamage = getInteger<int32_t>(L, 4);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetConditionSpeedChange(lua_State* L)
{
	// monsterSpell:setConditionSpeedChange(minSpeed[, maxSpeed])
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->minSpeedChange = getInteger<int32_t>(L, 2);
		spell->maxSpeedChange = getInteger<int32_t>(L, 3, 0);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetConditionDuration(lua_State* L)
{
	// monsterSpell:setConditionDuration(duration)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->duration = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetConditionDrunkenness(lua_State* L)
{
	// monsterSpell:setConditionDrunkenness(drunkenness)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->drunkenness = getInteger<uint8_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetConditionTickInterval(lua_State* L)
{
	// monsterSpell:setConditionTickInterval(interval)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->tickInterval = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetCombatShootEffect(lua_State* L)
{
	// monsterSpell:setCombatShootEffect(effect)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->shoot = getInteger<ShootType_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetCombatEffect(lua_State* L)
{
	// monsterSpell:setCombatEffect(effect)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->effect = getInteger<MagicEffectClasses>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetOutfit(lua_State* L)
{
	// monsterSpell:setOutfit(outfit)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->outfit = getOutfit(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetOutfitMonster(lua_State* L)
{
	// monsterSpell:setOutfitMonster(name)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		MonsterType* mType = g_monsters.getMonsterType(getString(L, 2));
		if (mType) {
			spell->outfit = mType->info.outfit;
			pushBoolean(L, true);
		} else {
			pushBoolean(L, false);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellSetOutfitItem(lua_State* L)
{
	// monsterSpell:setOutfitItem(itemid)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->outfit = {};
		spell->outfit.lookTypeEx = getInteger<uint16_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterSpellCastSound(lua_State* L)
{
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (!spell) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) lua_pushinteger(L, spell->soundCast);
	else { spell->soundCast = getInteger<uint16_t>(L, 2); pushBoolean(L, true); }
	return 1;
}

int luaMonsterSpellImpactSound(lua_State* L)
{
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (!spell) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) lua_pushinteger(L, spell->soundImpact);
	else { spell->soundImpact = getInteger<uint16_t>(L, 2); pushBoolean(L, true); }
	return 1;
}
} // namespace

void LuaScriptInterface::registerMonsterSpell()
{
	// MonsterSpell
	registerClass("MonsterSpell", "", luaCreateMonsterSpell);
	registerMetaMethod("MonsterSpell", "__gc", luaDeleteMonsterSpell);
	registerMetaMethod("MonsterSpell", "__close", luaDeleteMonsterSpell);
	registerMethod("MonsterSpell", "delete", luaDeleteMonsterSpell);

	registerMethod("MonsterSpell", "setType", luaMonsterSpellSetType);
	registerMethod("MonsterSpell", "castSound", luaMonsterSpellCastSound);
	registerMethod("MonsterSpell", "impactSound", luaMonsterSpellImpactSound);
	registerMethod("MonsterSpell", "setScriptName", luaMonsterSpellSetScriptName);
	registerMethod("MonsterSpell", "setChance", luaMonsterSpellSetChance);
	registerMethod("MonsterSpell", "setInterval", luaMonsterSpellSetInterval);
	registerMethod("MonsterSpell", "setRange", luaMonsterSpellSetRange);
	registerMethod("MonsterSpell", "setCombatValue", luaMonsterSpellSetCombatValue);
	registerMethod("MonsterSpell", "setCombatType", luaMonsterSpellSetCombatType);
	registerMethod("MonsterSpell", "setAttackValue", luaMonsterSpellSetAttackValue);
	registerMethod("MonsterSpell", "setNeedTarget", luaMonsterSpellSetNeedTarget);
	registerMethod("MonsterSpell", "setNeedDirection", luaMonsterSpellSetNeedDirection);
	registerMethod("MonsterSpell", "setCombatLength", luaMonsterSpellSetCombatLength);
	registerMethod("MonsterSpell", "setCombatSpread", luaMonsterSpellSetCombatSpread);
	registerMethod("MonsterSpell", "setCombatRadius", luaMonsterSpellSetCombatRadius);
	registerMethod("MonsterSpell", "setCombatRing", luaMonsterSpellSetCombatRing);
	registerMethod("MonsterSpell", "setConditionType", luaMonsterSpellSetConditionType);
	registerMethod("MonsterSpell", "setConditionDamage", luaMonsterSpellSetConditionDamage);
	registerMethod("MonsterSpell", "setConditionSpeedChange", luaMonsterSpellSetConditionSpeedChange);
	registerMethod("MonsterSpell", "setConditionDuration", luaMonsterSpellSetConditionDuration);
	registerMethod("MonsterSpell", "setConditionDrunkenness", luaMonsterSpellSetConditionDrunkenness);
	registerMethod("MonsterSpell", "setConditionTickInterval", luaMonsterSpellSetConditionTickInterval);
	registerMethod("MonsterSpell", "setCombatShootEffect", luaMonsterSpellSetCombatShootEffect);
	registerMethod("MonsterSpell", "setCombatEffect", luaMonsterSpellSetCombatEffect);
	registerMethod("MonsterSpell", "setOutfit", luaMonsterSpellSetOutfit);
	registerMethod("MonsterSpell", "setOutfitMonster", luaMonsterSpellSetOutfitMonster);
	registerMethod("MonsterSpell", "setOutfitItem", luaMonsterSpellSetOutfitItem);
}
