// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "configmanager.h"
#include "luascript.h"
#include "monsters.h"
#include "script.h"
#include "scriptmanager.h"
#include "spells.h"
#include "logger.h"
#include <fmt/format.h>

extern Monsters g_monsters;

namespace {
using namespace Lua;

// MonsterType
int luaMonsterTypeCreate(lua_State* L)
{
	// MonsterType(name or raceId)
	MonsterType* monsterType;
	if (isInteger(L, 2)) {
		monsterType = g_monsters.getMonsterType(getInteger<uint32_t>(L, 2));
	} else {
		monsterType = g_monsters.getMonsterType(getString(L, 2));
	}

	if (monsterType) {
		pushUserdata<MonsterType>(L, monsterType);
		setMetatable(L, -1, "MonsterType");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeIsAttackable(lua_State* L)
{
	// get: monsterType:isAttackable() set: monsterType:isAttackable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.isAttackable);
		} else {
			monsterType->info.isAttackable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeIsChallengeable(lua_State* L)
{
	// get: monsterType:isChallengeable() set: monsterType:isChallengeable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.isChallengeable);
		} else {
			monsterType->info.isChallengeable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeIsConvinceable(lua_State* L)
{
	// get: monsterType:isConvinceable() set: monsterType:isConvinceable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.isConvinceable);
		} else {
			monsterType->info.isConvinceable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeIsSummonable(lua_State* L)
{
	// get: monsterType:isSummonable() set: monsterType:isSummonable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.isSummonable);
		} else {
			monsterType->info.isSummonable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeIsBlockable(lua_State* L)
{
	// get: monsterType:isBlockable() set: monsterType:isBlockable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.isBlockable);
		} else {
			monsterType->info.isBlockable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeIsIllusionable(lua_State* L)
{
	// get: monsterType:isIllusionable() set: monsterType:isIllusionable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.isIllusionable);
		} else {
			monsterType->info.isIllusionable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeIsHostile(lua_State* L)
{
	// get: monsterType:isHostile() set: monsterType:isHostile(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.isHostile);
		} else {
			monsterType->info.isHostile = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeIsPushable(lua_State* L)
{
	// get: monsterType:isPushable() set: monsterType:isPushable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.pushable);
		} else {
			monsterType->info.pushable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeIsHealthHidden(lua_State* L)
{
	// get: monsterType:isHealthHidden() set: monsterType:isHealthHidden(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.hiddenHealth);
		} else {
			monsterType->info.hiddenHealth = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeIsBoss(lua_State* L)
{
	// get: monsterType:isBoss() set: monsterType:isBoss(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.isBoss);
		} else {
			monsterType->info.isBoss = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeIsRewardBoss(lua_State* L)
{
	// get: monsterType:isRewardBoss() set: monsterType:isRewardBoss(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.isRewardBoss);
		} else {
			monsterType->info.isRewardBoss = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeCanPushItems(lua_State* L)
{
	// get: monsterType:canPushItems() set: monsterType:canPushItems(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.canPushItems);
		} else {
			monsterType->info.canPushItems = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeCanPushCreatures(lua_State* L)
{
	// get: monsterType:canPushCreatures() set: monsterType:canPushCreatures(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.canPushCreatures);
		} else {
			monsterType->info.canPushCreatures = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeCanWalkOnEnergy(lua_State* L)
{
	// get: monsterType:canWalkOnEnergy() set: monsterType:canWalkOnEnergy(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.canWalkOnEnergy);
		} else {
			monsterType->info.canWalkOnEnergy = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeCanWalkOnFire(lua_State* L)
{
	// get: monsterType:canWalkOnFire() set: monsterType:canWalkOnFire(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.canWalkOnFire);
		} else {
			monsterType->info.canWalkOnFire = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeCanWalkOnPoison(lua_State* L)
{
	// get: monsterType:canWalkOnPoison() set: monsterType:canWalkOnPoison(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->info.canWalkOnPoison);
		} else {
			monsterType->info.canWalkOnPoison = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int32_t luaMonsterTypeName(lua_State* L)
{
	// get: monsterType:name() set: monsterType:name(name)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushString(L, monsterType->name);
		} else {
			monsterType->name = getString(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeNameDescription(lua_State* L)
{
	// get: monsterType:nameDescription() set: monsterType:nameDescription(desc)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushString(L, monsterType->nameDescription);
		} else {
			monsterType->nameDescription = getString(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int32_t luaMonsterTypeRaceId(lua_State* L)
{
	// get: monsterType:raceId() set: monsterType:raceId(raceId)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->raceId);
		} else {
			monsterType->raceId = getInteger<uint32_t>(L, 2);
			if (ConfigManager::getBoolean(ConfigManager::BESTIARY_SYSTEM_ENABLED)) {
				g_monsters.registerBestiaryMonster(monsterType);
			}
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeHealth(lua_State* L)
{
	// get: monsterType:health() set: monsterType:health(health)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.health);
		} else {
			monsterType->info.health = getInteger<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeMaxHealth(lua_State* L)
{
	// get: monsterType:maxHealth() set: monsterType:maxHealth(health)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.healthMax);
		} else {
			monsterType->info.healthMax = getInteger<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeRunHealth(lua_State* L)
{
	// get: monsterType:runHealth() set: monsterType:runHealth(health)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.runAwayHealth);
		} else {
			monsterType->info.runAwayHealth = getInteger<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeExperience(lua_State* L)
{
	// get: monsterType:experience() set: monsterType:experience(exp)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.experience);
		} else {
			monsterType->info.experience = getInteger<uint64_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeSkull(lua_State* L)
{
	// get: monsterType:skull() set: monsterType:skull(str/constant)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.skull);
		} else {
			if (isInteger(L, 2)) {
				monsterType->info.skull = getInteger<Skulls_t>(L, 2);
			} else {
				monsterType->info.skull = getSkullType(getString(L, 2));
			}
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeEmblem(lua_State* L)
{
	// get: monsterType:emblem() set: monsterType:emblem(str/constant)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.emblem);
		} else {
			if (isInteger(L, 2)) {
				monsterType->info.emblem = getInteger<GuildEmblems_t>(L, 2);
			} else {
				monsterType->info.emblem = getEmblemType(getString(L, 2));
			}
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeCombatImmunities(lua_State* L)
{
	// get: monsterType:combatImmunities() set: monsterType:combatImmunities(immunity)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.damageImmunities);
		} else {
			std::string immunity = getString(L, 2);
			if (immunity == "physical") {
				monsterType->info.damageImmunities |= COMBAT_PHYSICALDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "energy") {
				monsterType->info.damageImmunities |= COMBAT_ENERGYDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "fire") {
				monsterType->info.damageImmunities |= COMBAT_FIREDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "poison" || immunity == "earth") {
				monsterType->info.damageImmunities |= COMBAT_EARTHDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "drown") {
				monsterType->info.damageImmunities |= COMBAT_DROWNDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "ice") {
				monsterType->info.damageImmunities |= COMBAT_ICEDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "holy") {
				monsterType->info.damageImmunities |= COMBAT_HOLYDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "death") {
				monsterType->info.damageImmunities |= COMBAT_DEATHDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "agony") {
				monsterType->info.damageImmunities |= COMBAT_AGONYDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "lifedrain") {
				monsterType->info.damageImmunities |= COMBAT_LIFEDRAIN;
				pushBoolean(L, true);
			} else if (immunity == "manadrain") {
				monsterType->info.damageImmunities |= COMBAT_MANADRAIN;
				pushBoolean(L, true);
			} else {
				LOG_WARN(fmt::format("[Warning - Monsters::loadMonster] Unknown immunity name {} for monster: {}", immunity, monsterType->name));
				lua_pushnil(L);
			}
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeConditionImmunities(lua_State* L)
{
	// get: monsterType:conditionImmunities() set: monsterType:conditionImmunities(immunity)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.conditionImmunities);
		} else {
			std::string immunity = getString(L, 2);
			if (immunity == "physical") {
				monsterType->info.conditionImmunities |= CONDITION_BLEEDING;
				pushBoolean(L, true);
			} else if (immunity == "energy") {
				monsterType->info.conditionImmunities |= CONDITION_ENERGY;
				pushBoolean(L, true);
			} else if (immunity == "fire") {
				monsterType->info.conditionImmunities |= CONDITION_FIRE;
				pushBoolean(L, true);
			} else if (immunity == "poison" || immunity == "earth") {
				monsterType->info.conditionImmunities |= CONDITION_POISON;
				pushBoolean(L, true);
			} else if (immunity == "drown") {
				monsterType->info.conditionImmunities |= CONDITION_DROWN;
				pushBoolean(L, true);
			} else if (immunity == "ice") {
				monsterType->info.conditionImmunities |= CONDITION_FREEZING;
				pushBoolean(L, true);
			} else if (immunity == "holy") {
				monsterType->info.conditionImmunities |= CONDITION_DAZZLED;
				pushBoolean(L, true);
			} else if (immunity == "death") {
				monsterType->info.conditionImmunities |= CONDITION_CURSED;
				pushBoolean(L, true);
			} else if (immunity == "agony") {
				monsterType->info.conditionImmunities |= CONDITION_AGONY;
				pushBoolean(L, true);
			} else if (immunity == "paralyze") {
				monsterType->info.conditionImmunities |= CONDITION_PARALYZE;
				pushBoolean(L, true);
			} else if (immunity == "root" || immunity == "rooted") {
				monsterType->info.conditionImmunities |= CONDITION_ROOTED;
				pushBoolean(L, true);
			} else if (immunity == "fear" || immunity == "feared") {
				monsterType->info.conditionImmunities |= CONDITION_FEARED;
				pushBoolean(L, true);
			} else if (immunity == "outfit") {
				monsterType->info.conditionImmunities |= CONDITION_OUTFIT;
				pushBoolean(L, true);
			} else if (immunity == "drunk") {
				monsterType->info.conditionImmunities |= CONDITION_DRUNK;
				pushBoolean(L, true);
			} else if (immunity == "invisible" || immunity == "invisibility") {
				monsterType->info.conditionImmunities |= CONDITION_INVISIBLE;
				pushBoolean(L, true);
			} else if (immunity == "bleed") {
				monsterType->info.conditionImmunities |= CONDITION_BLEEDING;
				pushBoolean(L, true);
			} else {
				LOG_WARN(fmt::format("[Warning - Monsters::loadMonster] Unknown immunity name {} for monster: {}", immunity, monsterType->name));
				lua_pushnil(L);
			}
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeGetAttackList(lua_State* L)
{
	// monsterType:getAttackList()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, monsterType->info.attackSpells.size(), 0);

	int index = 0;
	for (const auto& spellBlock : monsterType->info.attackSpells) {
		lua_createtable(L, 0, 8);

		setField(L, "chance", spellBlock.chance);
		setField(L, "isCombatSpell", spellBlock.combatSpell ? 1 : 0);
		setField(L, "isMelee", spellBlock.isMelee ? 1 : 0);
		setField(L, "minCombatValue", spellBlock.minCombatValue);
		setField(L, "maxCombatValue", spellBlock.maxCombatValue);
		setField(L, "range", spellBlock.range);
		if (spellBlock.spell) {
			if (spellBlock.combatSpell) {
				pushUserdata<CombatSpell>(L, static_cast<CombatSpell*>(spellBlock.spell));
			} else {
				pushUserdata<Spell>(L, static_cast<Spell*>(spellBlock.spell));
			}
			lua_setfield(L, -2, "spell");
		}

		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaMonsterTypeAddAttack(lua_State* L)
{
	// monsterType:addAttack(monsterspell)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		MonsterSpell* spell = getUserdata<MonsterSpell>(L, 2);
		if (spell) {
			spellBlock_t sb;
			if (g_monsters.deserializeSpell(spell, sb, monsterType->name)) {
				monsterType->info.attackSpells.push_back(std::move(sb));
			} else {
				LOG_WARN(fmt::format("{} [Warning - Monsters::loadMonster] Cant load spell. {}", monsterType->name, spell->name));
			}
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeGetDefenseList(lua_State* L)
{
	// monsterType:getDefenseList()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, monsterType->info.defenseSpells.size(), 0);

	int index = 0;
	for (const auto& spellBlock : monsterType->info.defenseSpells) {
		lua_createtable(L, 0, 8);

		setField(L, "chance", spellBlock.chance);
		setField(L, "isCombatSpell", spellBlock.combatSpell ? 1 : 0);
		setField(L, "isMelee", spellBlock.isMelee ? 1 : 0);
		setField(L, "minCombatValue", spellBlock.minCombatValue);
		setField(L, "maxCombatValue", spellBlock.maxCombatValue);
		setField(L, "range", spellBlock.range);
		if (spellBlock.spell) {
			if (spellBlock.combatSpell) {
				pushUserdata<CombatSpell>(L, static_cast<CombatSpell*>(spellBlock.spell));
			} else {
				pushUserdata<Spell>(L, static_cast<Spell*>(spellBlock.spell));
			}
			lua_setfield(L, -2, "spell");
		}

		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaMonsterTypeAddDefense(lua_State* L)
{
	// monsterType:addDefense(monsterspell)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		MonsterSpell* spell = getUserdata<MonsterSpell>(L, 2);
		if (spell) {
			spellBlock_t sb;
			if (g_monsters.deserializeSpell(spell, sb, monsterType->name)) {
				monsterType->info.defenseSpells.push_back(std::move(sb));
			} else {
				LOG_WARN(fmt::format("{} [Warning - Monsters::loadMonster] Cant load spell. {}", monsterType->name, spell->name));
			}
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeGetElementList(lua_State* L)
{
	// monsterType:getElementList()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, monsterType->info.elementMap.size(), 0);
	for (const auto& elementEntry : monsterType->info.elementMap) {
		lua_pushinteger(L, elementEntry.second);
		lua_rawseti(L, -2, elementEntry.first);
	}
	return 1;
}

int luaMonsterTypeAddElement(lua_State* L)
{
	// monsterType:addElement(type, percent)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		CombatType_t element = getInteger<CombatType_t>(L, 2);
		monsterType->info.elementMap[element] = getInteger<int32_t>(L, 3);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeGetVoices(lua_State* L)
{
	// monsterType:getVoices()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	int index = 0;
	lua_createtable(L, monsterType->info.voiceVector.size(), 0);
	for (const auto& voiceBlock : monsterType->info.voiceVector) {
		lua_createtable(L, 0, 2);
		setField(L, "text", voiceBlock.text);
		setField(L, "yellText", voiceBlock.yellText);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaMonsterTypeAddVoice(lua_State* L)
{
	// monsterType:addVoice(sentence, interval, chance, yell)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		voiceBlock_t voice;
		voice.text = getString(L, 2);
		monsterType->info.yellSpeedTicks = getInteger<uint32_t>(L, 3);
		monsterType->info.yellChance = getInteger<uint32_t>(L, 4);
		voice.yellText = getBoolean(L, 5);
		monsterType->info.voiceVector.push_back(voice);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeGetLoot(lua_State* L)
{
	// monsterType:getLoot()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	pushLoot(L, monsterType->info.lootItems);
	return 1;
}

int luaMonsterTypeAddLoot(lua_State* L)
{
	// monsterType:addLoot(loot)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		Loot* loot = getUserdata<Loot>(L, 2);
		if (loot) {
			monsterType->loadLoot(monsterType, loot->lootBlock);
			pushBoolean(L, true);
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeGetCreatureEvents(lua_State* L)
{
	// monsterType:getCreatureEvents()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	int index = 0;
	lua_createtable(L, monsterType->info.scripts.size(), 0);
	for (std::string_view creatureEvent : monsterType->info.scripts) {
		pushString(L, creatureEvent);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaMonsterTypeRegisterEvent(lua_State* L)
{
	// monsterType:registerEvent(name)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		monsterType->info.scripts.push_back(getString(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeOnThink(lua_State* L)
{
	// monsterType:onThink(callback)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		int32_t id = g_scripts->getScriptInterface().getEvent();
		if (id != -1) {
			monsterType->info.thinkEvent = id;
			monsterType->info.scriptInterface = &g_scripts->getScriptInterface();
			pushBoolean(L, true);
			return 1;
		}
		pushBoolean(L, false);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeOnAppear(lua_State* L)
{
	// monsterType:onAppear(callback)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		int32_t id = g_scripts->getScriptInterface().getEvent();
		if (id != -1) {
			monsterType->info.creatureAppearEvent = id;
			monsterType->info.scriptInterface = &g_scripts->getScriptInterface();
			pushBoolean(L, true);
			return 1;
		}
		pushBoolean(L, false);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeOnDisappear(lua_State* L)
{
	// monsterType:onDisappear(callback)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		int32_t id = g_scripts->getScriptInterface().getEvent();
		if (id != -1) {
			monsterType->info.creatureDisappearEvent = id;
			monsterType->info.scriptInterface = &g_scripts->getScriptInterface();
			pushBoolean(L, true);
			return 1;
		}
		pushBoolean(L, false);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeOnMove(lua_State* L)
{
	// monsterType:onMove(callback)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		int32_t id = g_scripts->getScriptInterface().getEvent();
		if (id != -1) {
			monsterType->info.creatureMoveEvent = id;
			monsterType->info.scriptInterface = &g_scripts->getScriptInterface();
			pushBoolean(L, true);
			return 1;
		}
		pushBoolean(L, false);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeOnSay(lua_State* L)
{
	// monsterType:onSay(callback)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		int32_t id = g_scripts->getScriptInterface().getEvent();
		if (id != -1) {
			monsterType->info.creatureSayEvent = id;
			monsterType->info.scriptInterface = &g_scripts->getScriptInterface();
			pushBoolean(L, true);
			return 1;
		}
		pushBoolean(L, false);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeOnDeath(lua_State* L)
{
	// monsterType:onDeath(callback)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		int32_t id = g_scripts->getScriptInterface().getEvent();
		if (id != -1) {
			monsterType->info.deathEvent = id;
			monsterType->info.scriptInterface = &g_scripts->getScriptInterface();
			pushBoolean(L, true);
			return 1;
		}
		pushBoolean(L, false);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeOnPlayerAttack(lua_State* L)
{
	// monsterType:onPlayerAttack(callback)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		int32_t id = g_scripts->getScriptInterface().getEvent();
		if (id != -1) {
			monsterType->info.playerAttackEvent = id;
			monsterType->info.scriptInterface = &g_scripts->getScriptInterface();
			pushBoolean(L, true);
			return 1;
		}
		pushBoolean(L, false);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeEventType(lua_State* L)
{
	// monstertype:eventType(event)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		monsterType->info.eventType = getInteger<MonstersEvent_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeGetSummonList(lua_State* L)
{
	// monsterType:getSummonList()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	int index = 0;
	lua_createtable(L, monsterType->info.summons.size(), 0);
	for (const auto& summonBlock : monsterType->info.summons) {
		lua_createtable(L, 0, 6);
		setField(L, "name", summonBlock.name);
		setField(L, "speed", summonBlock.speed);
		setField(L, "chance", summonBlock.chance);
		setField(L, "max", summonBlock.max);
		setField(L, "effect", summonBlock.effect);
		setField(L, "masterEffect", summonBlock.masterEffect);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaMonsterTypeAddSummon(lua_State* L)
{
	// monsterType:addSummon(name, interval, chance[, max = uint32_t[, effect = CONST_ME_TELEPORT[, masterEffect =
	// CONST_ME_NONE]]])
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		summonBlock_t summon;
		summon.name = getString(L, 2);
		summon.speed = getInteger<uint32_t>(L, 3);
		summon.chance = getInteger<uint32_t>(L, 4);
		summon.max = getInteger<uint32_t>(L, 5, std::numeric_limits<uint32_t>::max());
		summon.effect = getInteger<MagicEffectClasses>(L, 6, CONST_ME_TELEPORT);
		summon.masterEffect = getInteger<MagicEffectClasses>(L, 7, CONST_ME_NONE);
		monsterType->info.summons.push_back(summon);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeMaxSummons(lua_State* L)
{
	// get: monsterType:maxSummons() set: monsterType:maxSummons(ammount)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.maxSummons);
		} else {
			monsterType->info.maxSummons = getInteger<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeArmor(lua_State* L)
{
	// get: monsterType:armor() set: monsterType:armor(armor)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.armor);
		} else {
			monsterType->info.armor = getInteger<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeDefense(lua_State* L)
{
	// get: monsterType:defense() set: monsterType:defense(defense)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.defense);
		} else {
			monsterType->info.defense = getInteger<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeMitigation(lua_State* L)
{
	// get: monsterType:mitigation() set: monsterType:mitigation(mitigation)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->info.mitigation);
		} else {
			monsterType->info.mitigation = getNumber<float>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeOutfit(lua_State* L)
{
	// get: monsterType:outfit() set: monsterType:outfit(outfit)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushOutfit(L, monsterType->info.outfit);
		} else {
			monsterType->info.outfit = getOutfit(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeRace(lua_State* L)
{
	// get: monsterType:race() set: monsterType:race(race)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	std::string race = getString(L, 2);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.race);
		} else {
			if (race == "venom") {
				monsterType->info.race = RACE_VENOM;
			} else if (race == "blood") {
				monsterType->info.race = RACE_BLOOD;
			} else if (race == "undead") {
				monsterType->info.race = RACE_UNDEAD;
			} else if (race == "fire") {
				monsterType->info.race = RACE_FIRE;
			} else if (race == "energy") {
				monsterType->info.race = RACE_ENERGY;
			} else if (race == "ink") {
				monsterType->info.race = RACE_INK;
			} else {
				LOG_WARN(fmt::format("[Warning - Monsters::loadMonster] Unknown race type {}.", race));
				lua_pushnil(L);
				return 1;
			}
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeCorpseId(lua_State* L)
{
	// get: monsterType:corpseId() set: monsterType:corpseId(id)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.lookcorpse);
		} else {
			monsterType->info.lookcorpse = getInteger<uint16_t>(L, 2);
			lua_pushboolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeManaCost(lua_State* L)
{
	// get: monsterType:manaCost() set: monsterType:manaCost(mana)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.manaCost);
		} else {
			monsterType->info.manaCost = getInteger<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeBaseSpeed(lua_State* L)
{
	// get: monsterType:baseSpeed() set: monsterType:baseSpeed(speed)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.baseSpeed);
		} else {
			monsterType->info.baseSpeed = getInteger<uint32_t>(L, 2) * 2;
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeLight(lua_State* L)
{
	// get: monsterType:light() set: monsterType:light(color, level)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}
	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, monsterType->info.light.level);
		lua_pushinteger(L, monsterType->info.light.color);
		return 2;
	} else {
		monsterType->info.light.color = getInteger<uint8_t>(L, 2);
		monsterType->info.light.level = getInteger<uint8_t>(L, 3);
		pushBoolean(L, true);
	}
	return 1;
}

int luaMonsterTypeStaticAttackChance(lua_State* L)
{
	// get: monsterType:staticAttackChance() set: monsterType:staticAttackChance(chance)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.staticAttackChance);
		} else {
			monsterType->info.staticAttackChance = getInteger<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeTargetDistance(lua_State* L)
{
	// get: monsterType:targetDistance() set: monsterType:targetDistance(distance)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.targetDistance);
		} else {
			monsterType->info.targetDistance = getInteger<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeYellChance(lua_State* L)
{
	// get: monsterType:yellChance() set: monsterType:yellChance(chance)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.yellChance);
		} else {
			monsterType->info.yellChance = getInteger<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeYellSpeedTicks(lua_State* L)
{
	// get: monsterType:yellSpeedTicks() set: monsterType:yellSpeedTicks(rate)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.yellSpeedTicks);
		} else {
			monsterType->info.yellSpeedTicks = getInteger<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeChangeTargetChance(lua_State* L)
{
	// get: monsterType:changeTargetChance() set: monsterType:changeTargetChance(chance)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.changeTargetChance);
		} else {
			monsterType->info.changeTargetChance = getInteger<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeChangeTargetSpeed(lua_State* L)
{
	// get: monsterType:changeTargetSpeed() set: monsterType:changeTargetSpeed(speed)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.changeTargetSpeed);
		} else {
			monsterType->info.changeTargetSpeed = getInteger<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeStrategiesTarget(lua_State* L)
{
	// get: monsterType:strategiesTarget() set: monsterType:strategiesTarget(table)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_newtable(L);
			setField(L, "nearest", monsterType->info.targetStrategies.nearest);
			setField(L, "health", monsterType->info.targetStrategies.health);
			setField(L, "damage", monsterType->info.targetStrategies.damage);
			setField(L, "random", monsterType->info.targetStrategies.random);
		} else {
			if (lua_istable(L, 2)) {
				monsterType->info.targetStrategies.nearest = getField<uint32_t>(L, 2, "nearest");
				monsterType->info.targetStrategies.health = getField<uint32_t>(L, 2, "health");
				monsterType->info.targetStrategies.damage = getField<uint32_t>(L, 2, "damage");
				monsterType->info.targetStrategies.random = getField<uint32_t>(L, 2, "random");
				pushBoolean(L, true);
			} else {
				pushBoolean(L, false);
			}
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeFaction(lua_State* L)
{
	// get: monsterType:faction() set: monsterType:faction(faction)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.faction);
		} else {
			const int32_t faction = getInteger<int32_t>(L, 2);
			if (faction < FACTION_DEFAULT || faction > FACTION_LAST) {
				reportErrorFunc(L, "Invalid faction");
				pushBoolean(L, false);
			} else {
				monsterType->info.faction = static_cast<Faction_t>(faction);
				pushBoolean(L, true);
			}
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeEnemyFactions(lua_State* L)
{
	// get: monsterType:enemyFactions() set: monsterType:enemyFactions({faction1, faction2, ...})
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) == 1) {
		lua_newtable(L);
		int index = 1;
		for (Faction_t f : monsterType->info.enemyFactions) {
			lua_pushinteger(L, f);
			lua_rawseti(L, -2, index++);
		}
	} else {
		monsterType->info.enemyFactions.clear();
		if (lua_istable(L, 2)) {
			lua_pushnil(L);
			while (lua_next(L, 2) != 0) {
				if (lua_isnumber(L, -1)) {
					const int32_t faction = getInteger<int32_t>(L, -1);
					if (faction > FACTION_DEFAULT && faction <= FACTION_LAST) {
						monsterType->info.enemyFactions.insert(static_cast<Faction_t>(faction));
					}
				}
				lua_pop(L, 1);
			}
		}
		pushBoolean(L, true);
	}
	return 1;
}

int luaMonsterTypeMinLevel(lua_State* L)
{
	// get: monsterType:minLevel() set: monsterType:minLevel(level)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.minLevel);
		} else {
			const int32_t minLevel = getInteger<int32_t>(L, 2);
			if (monsterType->info.maxLevel > 0 && minLevel > monsterType->info.maxLevel) {
				reportErrorFunc(L, "minLevel cannot be greater than maxLevel");
				pushBoolean(L, false);
				return 1;
			}

			monsterType->info.minLevel = minLevel;
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMonsterTypeMaxLevel(lua_State* L)
{
	// get: monsterType:maxLevel() set: monsterType:maxLevel(level)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushinteger(L, monsterType->info.maxLevel);
		} else {
			const int32_t maxLevel = getInteger<int32_t>(L, 2);
			if (monsterType->info.minLevel > 0 && maxLevel < monsterType->info.minLevel) {
				reportErrorFunc(L, "maxLevel cannot be less than minLevel");
				pushBoolean(L, false);
				return 1;
			}

			monsterType->info.maxLevel = maxLevel;
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}
} // namespace

void LuaScriptInterface::registerMonsterType()
{
	// MonsterType
	registerClass("MonsterType", "", luaMonsterTypeCreate);
	registerMetaMethod("MonsterType", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("MonsterType", "isAttackable", luaMonsterTypeIsAttackable);
	registerMethod("MonsterType", "isChallengeable", luaMonsterTypeIsChallengeable);
	registerMethod("MonsterType", "isConvinceable", luaMonsterTypeIsConvinceable);
	registerMethod("MonsterType", "isSummonable", luaMonsterTypeIsSummonable);
	registerMethod("MonsterType", "isBlockable", luaMonsterTypeIsBlockable);
	registerMethod("MonsterType", "isIllusionable", luaMonsterTypeIsIllusionable);
	registerMethod("MonsterType", "isHostile", luaMonsterTypeIsHostile);
	registerMethod("MonsterType", "isPushable", luaMonsterTypeIsPushable);
	registerMethod("MonsterType", "isHealthHidden", luaMonsterTypeIsHealthHidden);
	registerMethod("MonsterType", "isBoss", luaMonsterTypeIsBoss);
	registerMethod("MonsterType", "isRewardBoss", luaMonsterTypeIsRewardBoss);

	registerMethod("MonsterType", "canPushItems", luaMonsterTypeCanPushItems);
	registerMethod("MonsterType", "canPushCreatures", luaMonsterTypeCanPushCreatures);

	registerMethod("MonsterType", "canWalkOnEnergy", luaMonsterTypeCanWalkOnEnergy);
	registerMethod("MonsterType", "canWalkOnFire", luaMonsterTypeCanWalkOnFire);
	registerMethod("MonsterType", "canWalkOnPoison", luaMonsterTypeCanWalkOnPoison);

	registerMethod("MonsterType", "name", luaMonsterTypeName);
	registerMethod("MonsterType", "nameDescription", luaMonsterTypeNameDescription);
	registerMethod("MonsterType", "raceId", luaMonsterTypeRaceId);

	registerMethod("MonsterType", "health", luaMonsterTypeHealth);
	registerMethod("MonsterType", "maxHealth", luaMonsterTypeMaxHealth);
	registerMethod("MonsterType", "runHealth", luaMonsterTypeRunHealth);
	registerMethod("MonsterType", "experience", luaMonsterTypeExperience);
	registerMethod("MonsterType", "skull", luaMonsterTypeSkull);
	registerMethod("MonsterType", "emblem", luaMonsterTypeEmblem);

	registerMethod("MonsterType", "combatImmunities", luaMonsterTypeCombatImmunities);
	registerMethod("MonsterType", "conditionImmunities", luaMonsterTypeConditionImmunities);

	registerMethod("MonsterType", "getAttackList", luaMonsterTypeGetAttackList);
	registerMethod("MonsterType", "addAttack", luaMonsterTypeAddAttack);

	registerMethod("MonsterType", "getDefenseList", luaMonsterTypeGetDefenseList);
	registerMethod("MonsterType", "addDefense", luaMonsterTypeAddDefense);

	registerMethod("MonsterType", "getElementList", luaMonsterTypeGetElementList);
	registerMethod("MonsterType", "addElement", luaMonsterTypeAddElement);

	registerMethod("MonsterType", "getVoices", luaMonsterTypeGetVoices);
	registerMethod("MonsterType", "addVoice", luaMonsterTypeAddVoice);

	registerMethod("MonsterType", "getLoot", luaMonsterTypeGetLoot);
	registerMethod("MonsterType", "addLoot", luaMonsterTypeAddLoot);

	registerMethod("MonsterType", "getCreatureEvents", luaMonsterTypeGetCreatureEvents);
	registerMethod("MonsterType", "registerEvent", luaMonsterTypeRegisterEvent);

	registerMethod("MonsterType", "eventType", luaMonsterTypeEventType);
	registerMethod("MonsterType", "onThink", luaMonsterTypeOnThink);
	registerMethod("MonsterType", "onAppear", luaMonsterTypeOnAppear);
	registerMethod("MonsterType", "onDisappear", luaMonsterTypeOnDisappear);
	registerMethod("MonsterType", "onMove", luaMonsterTypeOnMove);
	registerMethod("MonsterType", "onSay", luaMonsterTypeOnSay);
	registerMethod("MonsterType", "onDeath", luaMonsterTypeOnDeath);
	registerMethod("MonsterType", "onPlayerAttack", luaMonsterTypeOnPlayerAttack);

	registerMethod("MonsterType", "getSummonList", luaMonsterTypeGetSummonList);
	registerMethod("MonsterType", "addSummon", luaMonsterTypeAddSummon);

	registerMethod("MonsterType", "maxSummons", luaMonsterTypeMaxSummons);

	registerMethod("MonsterType", "armor", luaMonsterTypeArmor);
	registerMethod("MonsterType", "defense", luaMonsterTypeDefense);
	registerMethod("MonsterType", "mitigation", luaMonsterTypeMitigation);
	registerMethod("MonsterType", "outfit", luaMonsterTypeOutfit);
	registerMethod("MonsterType", "race", luaMonsterTypeRace);
	registerMethod("MonsterType", "corpseId", luaMonsterTypeCorpseId);
	registerMethod("MonsterType", "manaCost", luaMonsterTypeManaCost);
	registerMethod("MonsterType", "baseSpeed", luaMonsterTypeBaseSpeed);
	registerMethod("MonsterType", "light", luaMonsterTypeLight);

	registerMethod("MonsterType", "staticAttackChance", luaMonsterTypeStaticAttackChance);
	registerMethod("MonsterType", "targetDistance", luaMonsterTypeTargetDistance);
	registerMethod("MonsterType", "yellChance", luaMonsterTypeYellChance);
	registerMethod("MonsterType", "yellSpeedTicks", luaMonsterTypeYellSpeedTicks);
	registerMethod("MonsterType", "changeTargetChance", luaMonsterTypeChangeTargetChance);
	registerMethod("MonsterType", "changeTargetSpeed", luaMonsterTypeChangeTargetSpeed);
	registerMethod("MonsterType", "strategiesTarget", luaMonsterTypeStrategiesTarget);

	registerMethod("MonsterType", "faction", luaMonsterTypeFaction);
	registerMethod("MonsterType", "enemyFactions", luaMonsterTypeEnemyFactions);
	registerMethod("MonsterType", "minLevel", luaMonsterTypeMinLevel);
	registerMethod("MonsterType", "maxLevel", luaMonsterTypeMaxLevel);
}
