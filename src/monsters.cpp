// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "monsters.h"

#include "combat.h"
#include "game.h"
#include "matrixarea.h"
#include "monster.h"
#include "scriptmanager.h"
#include "spells.h"
#include "weapons.h"
#include "logger.h"
#include <fmt/format.h>
#include "weapons.h"

extern Game g_game;
extern Monsters g_monsters;

spellBlock_t::spellBlock_t() = default;
spellBlock_t::~spellBlock_t() = default;

spellBlock_t::spellBlock_t(spellBlock_t&& other) noexcept :
    ownedSpell(std::move(other.ownedSpell)),
    spell(ownedSpell ? ownedSpell.get() : other.spell),
    chance(other.chance),
    speed(other.speed),
    range(other.range),
    minCombatValue(other.minCombatValue),
    maxCombatValue(other.maxCombatValue),
    combatSpell(other.combatSpell),
    isMelee(other.isMelee)
{
	other.spell = nullptr;
	other.combatSpell = false;
	other.isMelee = false;
}

spellBlock_t& spellBlock_t::operator=(spellBlock_t&& other) noexcept
{
	if (this == &other) {
		return *this;
	}

	ownedSpell = std::move(other.ownedSpell);
	spell = ownedSpell ? ownedSpell.get() : other.spell;
	chance = other.chance;
	speed = other.speed;
	range = other.range;
	minCombatValue = other.minCombatValue;
	maxCombatValue = other.maxCombatValue;
	combatSpell = other.combatSpell;
	isMelee = other.isMelee;

	other.spell = nullptr;
	other.combatSpell = false;
	other.isMelee = false;
	return *this;
}

void MonsterType::loadLoot(MonsterType* monsterType, LootBlock lootBlock)
{
	if (lootBlock.childLoot.empty()) {
		bool isContainer = Item::items[lootBlock.id].isContainer();
		if (isContainer) {
			for (LootBlock child : lootBlock.childLoot) {
				lootBlock.childLoot.push_back(child);
			}
		}
		monsterType->info.lootItems.push_back(lootBlock);
	} else {
		monsterType->info.lootItems.push_back(lootBlock);
	}
}

bool Monsters::reload()
{
	loaded = false;

	scriptInterface.reset();
	bestiaryMonsters.clear();

	loaded = true;
	return true;
}

Condition_ptr Monsters::getDamageCondition(ConditionType_t conditionType, int32_t maxDamage, int32_t minDamage,
                                            int32_t startDamage, uint32_t tickInterval)
{
	auto condition = Condition::createCondition(CONDITIONID_COMBAT, conditionType, 0, 0);
	condition->setParam(CONDITION_PARAM_TICKINTERVAL, tickInterval);
	condition->setParam(CONDITION_PARAM_MINVALUE, minDamage);
	condition->setParam(CONDITION_PARAM_MAXVALUE, maxDamage);
	condition->setParam(CONDITION_PARAM_STARTVALUE, startDamage);
	condition->setParam(CONDITION_PARAM_DELAYED, 1);
	return condition;
}

bool Monsters::deserializeSpell(MonsterSpell* spell, spellBlock_t& sb, const std::string& description)
{
	if (!spell->scriptName.empty()) {
		spell->isScripted = true;
	} else if (!spell->name.empty()) {
		spell->isScripted = false;
	} else {
		return false;
	}

	sb.speed = std::max<uint32_t>(1000, spell->interval);

	if (spell->chance > 100) {
		sb.chance = 100;
	} else {
		sb.chance = spell->chance;
	}

	if (spell->range > (Map::maxViewportX * 2)) {
		spell->range = Map::maxViewportX * 2;
	}
	sb.range = spell->range;

	sb.minCombatValue = spell->minCombatValue;
	sb.maxCombatValue = spell->maxCombatValue;
	if (std::abs(sb.minCombatValue) > std::abs(sb.maxCombatValue)) {
		int32_t value = sb.maxCombatValue;
		sb.maxCombatValue = sb.minCombatValue;
		sb.minCombatValue = value;
	}

	sb.spell = g_spells->getSpellByName(spell->name);
	if (sb.spell) {
		return true;
	}

	if (spell->isScripted) {
		auto combatSpell = std::make_unique<CombatSpell>(nullptr, spell->needTarget, spell->needDirection);
		if (!combatSpell->loadScript(fmt::format("data/{}/scripts/{}", g_spells->getScriptBaseName(), spell->scriptName))) {
			LOG_WARN("cannot find file");
			return false;
		}

		if (!combatSpell->loadScriptCombat()) {
			return false;
		}

		combatSpell->getCombat()->setPlayerCombatValues(COMBAT_FORMULA_DAMAGE, sb.minCombatValue, 0, sb.maxCombatValue, 0);
		sb.ownedSpell = std::move(combatSpell);
	} else {
		Combat_ptr combat = std::make_shared<Combat>();

		if (spell->length > 0) {
			spell->spread = std::max<int32_t>(0, spell->spread);

			auto area = std::make_unique<AreaCombat>();
			area->setupArea(spell->length, spell->spread);
			combat->setArea(std::move(area));

			spell->needDirection = true;
		}

		if (spell->radius > 0) {
			auto area = std::make_unique<AreaCombat>();
			area->setupArea(spell->radius);
			combat->setArea(std::move(area));
		}

		if (spell->ring > 0) {
			auto area = std::make_unique<AreaCombat>();
			area->setupAreaRing(spell->ring);
			combat->setArea(std::move(area));
		}

		if (spell->conditionType != CONDITION_NONE) {
			ConditionType_t conditionType = spell->conditionType;

			uint32_t tickInterval = 2000;
			if (spell->tickInterval != 0) {
				tickInterval = spell->tickInterval;
			}

			int32_t conMinDamage = std::abs(spell->conditionMinDamage);
			int32_t conMaxDamage = std::abs(spell->conditionMaxDamage);
			int32_t startDamage = std::abs(spell->conditionStartDamage);

			auto condition = getDamageCondition(conditionType, conMaxDamage, conMinDamage, startDamage, tickInterval);
			combat->addCondition(std::move(condition));
			if (spell->combatType == COMBAT_UNDEFINEDDAMAGE) {
				spell->combatType = Combat::ConditionToDamageType(spell->conditionType);
			}
		}

		std::string tmpName = asLowerCaseString(spell->name);

		if (tmpName == "melee") {
			sb.isMelee = true;

			if (spell->attack > 0 && spell->skill > 0) {
				sb.minCombatValue = 0;
				sb.maxCombatValue = -Weapons::getMaxMeleeDamage(spell->skill, spell->attack);
			}

			sb.range = 1;
			combat->setParam(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE);
			combat->setParam(COMBAT_PARAM_BLOCKARMOR, 1);
			combat->setParam(COMBAT_PARAM_BLOCKSHIELD, 1);
			combat->setOrigin(ORIGIN_MELEE);
		} else if (tmpName == "combat" || tmpName == "condition") {
			if (tmpName == "condition" && spell->conditionType == CONDITION_NONE) {
				LOG_ERROR(fmt::format("[Error - Monsters::deserializeSpell] - {} - Condition is not set for: {}", description, spell->name));
			}

			if (spell->combatType == COMBAT_UNDEFINEDDAMAGE) {
				if (tmpName == "combat") {
					LOG_WARN(fmt::format("[Warning - Monsters::deserializeSpell] - {} - spell has undefined damage", description));
				}
				spell->combatType = COMBAT_PHYSICALDAMAGE;
			}

			if (spell->combatType == COMBAT_PHYSICALDAMAGE) {
				combat->setParam(COMBAT_PARAM_BLOCKARMOR, 1);
				combat->setOrigin(ORIGIN_RANGED);
			} else if (spell->combatType == COMBAT_HEALING) {
				combat->setParam(COMBAT_PARAM_AGGRESSIVE, 0);
			}
			combat->setParam(COMBAT_PARAM_TYPE, spell->combatType);
		} else if (tmpName == "speed") {
			int32_t duration = 10000;

			if (spell->duration != 0) {
				duration = spell->duration;
			}

			int32_t minSpeedChange = spell->minSpeedChange;
			int32_t maxSpeedChange = spell->maxSpeedChange;

			if (minSpeedChange == 0) {
				minSpeedChange = sb.minCombatValue;
			}

			if (maxSpeedChange == 0) {
				maxSpeedChange = sb.maxCombatValue;
			}

			if (minSpeedChange == 0 && maxSpeedChange == 0) {
				LOG_ERROR(fmt::format("[Error - Monsters::deserializeSpell] - {} - missing speedchange/minspeedchange value", description));
				return false;
			}

			if (minSpeedChange < -1000) {
				LOG_WARN(fmt::format("[Warning - Monsters::deserializeSpell] - {} - you cannot reduce a creatures speed below -1000 (100%)", description));
				minSpeedChange = -1000;
			}

			if (maxSpeedChange == 0) {
				maxSpeedChange = minSpeedChange; // static speedchange value
			}

			ConditionType_t conditionType;
			if (minSpeedChange >= 0) {
				conditionType = CONDITION_HASTE;
				combat->setParam(COMBAT_PARAM_AGGRESSIVE, 0);
			} else {
				conditionType = CONDITION_PARALYZE;
			}

			auto condition = Condition::createCondition(CONDITIONID_COMBAT, conditionType, duration, 0);
			static_cast<ConditionSpeed*>(condition.get())->setFormulaVars(minSpeedChange / 1000.0, 0, maxSpeedChange / 1000.0, 0);
			combat->addCondition(std::move(condition));
		} else if (tmpName == "outfit") {
			int32_t duration = 10000;

			if (spell->duration != 0) {
				duration = spell->duration;
			}

			auto condition = Condition::createCondition(CONDITIONID_COMBAT, CONDITION_OUTFIT, duration, 0);
			static_cast<ConditionOutfit*>(condition.get())->setOutfit(spell->outfit);
			combat->setParam(COMBAT_PARAM_AGGRESSIVE, 0);
			combat->addCondition(std::move(condition));
		} else if (tmpName == "invisible") {
			int32_t duration = 10000;

			if (spell->duration != 0) {
				duration = spell->duration;
			}

			auto condition = Condition::createCondition(CONDITIONID_COMBAT, CONDITION_INVISIBLE, duration, 0);
			combat->setParam(COMBAT_PARAM_AGGRESSIVE, 0);
			combat->addCondition(std::move(condition));
		} else if (tmpName == "root" || tmpName == "rooted") {
			int32_t duration = 3000;

			if (spell->duration != 0) {
				duration = spell->duration;
			}

			auto condition = Condition::createCondition(CONDITIONID_COMBAT, CONDITION_ROOTED, duration, 0);
			combat->addCondition(std::move(condition));
		} else if (tmpName == "fear" || tmpName == "soulwars fear") {
			int32_t duration = 6000;

			if (spell->duration != 0) {
				duration = spell->duration;
			}

			auto condition = Condition::createCondition(CONDITIONID_COMBAT, CONDITION_FEARED, duration, 0);
			combat->addCondition(std::move(condition));
		} else if (tmpName == "drunk") {
			int32_t duration = 10000;
			uint8_t drunkenness = 25;

			if (spell->duration != 0) {
				duration = spell->duration;
			}

			if (spell->drunkenness != 0) {
				drunkenness = spell->drunkenness;
			}

		auto condition = Condition::createCondition(CONDITIONID_COMBAT, CONDITION_DRUNK, duration, drunkenness);
			combat->addCondition(std::move(condition));
		} else if (tmpName == "firefield") {
			combat->setParam(COMBAT_PARAM_CREATEITEM, ITEM_FIREFIELD_PVP_FULL);
		} else if (tmpName == "poisonfield") {
			combat->setParam(COMBAT_PARAM_CREATEITEM, ITEM_POISONFIELD_PVP);
		} else if (tmpName == "energyfield") {
			combat->setParam(COMBAT_PARAM_CREATEITEM, ITEM_ENERGYFIELD_PVP);
		} else if (tmpName == "outfit") {
			//
		} else if (tmpName == "effect") {
			//
		} else if (tmpName == "strength") {
			//
		} else {
			LOG_ERROR(fmt::format("[Error - Monsters::deserializeSpell] - {} - Unknown spell name: {}", description, spell->name));
		}

		if (spell->needTarget) {
			if (spell->shoot != CONST_ANI_NONE) {
				combat->setParam(COMBAT_PARAM_DISTANCEEFFECT, spell->shoot);
			}
		}

		if (spell->effect != CONST_ME_NONE) {
			combat->setParam(COMBAT_PARAM_EFFECT, spell->effect);
		}

		combat->setPlayerCombatValues(COMBAT_FORMULA_DAMAGE, sb.minCombatValue, 0, sb.maxCombatValue, 0);
		sb.ownedSpell = std::make_unique<CombatSpell>(combat, spell->needTarget, spell->needDirection);
	}

	sb.spell = sb.ownedSpell.get();
	if (sb.spell) {
		sb.combatSpell = true;
	}
	return true;
}

bool MonsterType::loadCallback(LuaScriptInterface* scriptInterface)
{
	int32_t id = scriptInterface->getEvent();
	if (id == -1) {
		LOG_WARN("[Warning - MonsterType::loadCallback] Event not found.");
		return false;
	}

	info.scriptInterface = scriptInterface;
	if (info.eventType == MONSTERS_EVENT_THINK) {
		info.thinkEvent = id;
	} else if (info.eventType == MONSTERS_EVENT_APPEAR) {
		info.creatureAppearEvent = id;
	} else if (info.eventType == MONSTERS_EVENT_DISAPPEAR) {
		info.creatureDisappearEvent = id;
	} else if (info.eventType == MONSTERS_EVENT_MOVE) {
		info.creatureMoveEvent = id;
	} else if (info.eventType == MONSTERS_EVENT_SAY) {
		info.creatureSayEvent = id;
	} else if (info.eventType == MONSTERS_EVENT_DEATH) {
		info.deathEvent = id;
	}
	return true;
}

MonsterType* Monsters::getMonsterType(const std::string& name)
{
	std::string lowerCaseName = asLowerCaseString(name);

	auto it = monsters.find(lowerCaseName);
	if (it == monsters.end()) {
		return nullptr;
	}
	return it->second.get();
}

std::shared_ptr<MonsterType> Monsters::getSharedMonsterType(const std::string& name)
{
	std::string lowerCaseName = asLowerCaseString(name);

	auto it = monsters.find(lowerCaseName);
	if (it == monsters.end()) {
		return nullptr;
	}
	return it->second;
}

MonsterType* Monsters::getMonsterType(uint32_t raceId)
{
	auto it = bestiaryMonsters.find(raceId);
	if (it != bestiaryMonsters.end()) {
		return getMonsterType(it->second);
	}
	return nullptr;
}

bool Monsters::registerBestiaryMonster(const MonsterType* mType)
{
	auto [it, success] = bestiaryMonsters.insert_or_assign(mType->raceId, mType->name);
	if (!success && it->second != mType->name) {
		LOG_WARN(fmt::format("[Warning - Monsters::registerBestiaryMonster] Monster raceId {} already exists for {} but was overwritten by {}.", mType->raceId, it->second, mType->name));
	}
	return success;
}

Monsters::Monsters() = default;

Monsters::~Monsters()
{
	monsters.clear();
	bestiaryMonsters.clear();
}
