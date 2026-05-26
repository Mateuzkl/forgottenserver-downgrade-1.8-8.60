// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "outfit.h"
#include "condition.h"
#include "game.h"
#include "player.h"

#include "pugicast.h"
#include "tools.h"
#include "logger.h"
#include <fmt/format.h>

bool Outfits::loadFromXml()
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file("data/XML/outfits.xml");
	if (!result) {
		printXMLError("Error - Outfits::loadFromXml", "data/XML/outfits.xml", result);
		return false;
	}

	for (auto& outfitNode : doc.child("outfits").children()) {
		pugi::xml_attribute attr;
		if ((attr = outfitNode.attribute("enabled")) && !attr.as_bool()) {
			continue;
		}

		if (!(attr = outfitNode.attribute("type"))) {
			LOG_WARN("[Warning - Outfits::loadFromXml] Missing outfit type.");
			continue;
		}

		uint16_t type = pugi::cast<uint16_t>(attr.value());
		if (type > PLAYERSEX_LAST) {
			LOG_WARN(fmt::format("[Warning - Outfits::loadFromXml] Invalid outfit type {}.", type));
			continue;
		}

		pugi::xml_attribute lookTypeAttribute = outfitNode.attribute("looktype");
		if (!lookTypeAttribute) {
			LOG_WARN("[Warning - Outfits::loadFromXml] Missing looktype on outfit.");
			continue;
		}

		const auto lookType = pugi::cast<uint16_t>(lookTypeAttribute.value());

		auto [it, inserted] = outfits.emplace(
		    std::piecewise_construct,
		    std::forward_as_tuple(lookType),
		    std::forward_as_tuple(
		        outfitNode.attribute("name").as_string(), lookType, static_cast<PlayerSex_t>(type),
		        outfitNode.attribute("premium").as_bool(),
		        outfitNode.attribute("unlocked").as_bool(true)));

		if (!inserted) {
			LOG_WARN(fmt::format("[Warning - Outfits::loadFromXml] Duplicate looktype {} ignored.", lookType));
			continue;
		}

		Outfit& outfit = it->second;

		if (auto fromAttr = outfitNode.attribute("from")) {
			outfit.from = fromAttr.as_string();
		}

		outfit.manaShield = outfitNode.attribute("manaShield").as_bool() ||
		                    outfitNode.attribute("manashield").as_bool();
		outfit.invisible  = outfitNode.attribute("invisible").as_bool();
		outfit.speed      = outfitNode.attribute("speed").as_int();
		outfit.attackSpeed = outfitNode.attribute("attackSpeed").as_int() != 0
		                         ? outfitNode.attribute("attackSpeed").as_int()
		                         : outfitNode.attribute("attackspeed").as_int();

		if (auto healthGainAttr = outfitNode.attribute("healthGain")) {
			outfit.healthGain   = healthGainAttr.as_int();
			outfit.regeneration = true;
		}
		if (auto healthTicksAttr = outfitNode.attribute("healthTicks")) {
			outfit.healthTicks  = healthTicksAttr.as_int();
			outfit.regeneration = true;
		}
		if (auto manaGainAttr = outfitNode.attribute("manaGain")) {
			outfit.manaGain     = manaGainAttr.as_int();
			outfit.regeneration = true;
		}
		if (auto manaTicksAttr = outfitNode.attribute("manaTicks")) {
			outfit.manaTicks    = manaTicksAttr.as_int();
			outfit.regeneration = true;
		}

		if (auto skillsNode = outfitNode.child("skills")) {
			for (auto skillNode : skillsNode.children()) {
				const std::string skillName = skillNode.name();
				const int32_t skillValue    = skillNode.attribute("value").as_int();

				if (skillName == "fist") {
					outfit.skills[SKILL_FIST] += skillValue;
				} else if (skillName == "club") {
					outfit.skills[SKILL_CLUB] += skillValue;
				} else if (skillName == "axe") {
					outfit.skills[SKILL_AXE] += skillValue;
				} else if (skillName == "sword") {
					outfit.skills[SKILL_SWORD] += skillValue;
				} else if (skillName == "distance" || skillName == "dist") {
					outfit.skills[SKILL_DISTANCE] += skillValue;
				} else if (skillName == "shielding" || skillName == "shield") {
					outfit.skills[SKILL_SHIELD] += skillValue;
				} else if (skillName == "fishing" || skillName == "fish") {
					outfit.skills[SKILL_FISHING] += skillValue;
				} else if (skillName == "melee") {
					outfit.skills[SKILL_FIST]  += skillValue;
					outfit.skills[SKILL_CLUB]  += skillValue;
					outfit.skills[SKILL_SWORD] += skillValue;
					outfit.skills[SKILL_AXE]   += skillValue;
				} else if (skillName == "weapon" || skillName == "weapons") {
					outfit.skills[SKILL_CLUB]     += skillValue;
					outfit.skills[SKILL_SWORD]    += skillValue;
					outfit.skills[SKILL_AXE]      += skillValue;
					outfit.skills[SKILL_DISTANCE] += skillValue;
				}
			}
		}

		if (auto statsNode = outfitNode.child("stats")) {
			for (auto statNode : statsNode.children()) {
				const std::string statName = statNode.name();
				const int32_t statValue    = statNode.attribute("value").as_int();

				if (statName == "maxHealth" || statName == "maxhealth") {
					outfit.stats[STAT_MAXHITPOINTS] += statValue;
				} else if (statName == "maxMana" || statName == "maxmana") {
					outfit.stats[STAT_MAXMANAPOINTS] += statValue;
				} else if (statName == "magLevel" || statName == "magicLevel" ||
				           statName == "magiclevel" || statName == "ml") {
					outfit.stats[STAT_MAGICPOINTS] += statValue;
				} else if (statName == "cap" || statName == "capacity") {
					outfit.stats[STAT_CAPACITY] += statValue * 100;
				}
			}
		}

		if (auto imbuingNode = outfitNode.child("imbuing")) {
			for (auto imbuing : imbuingNode.children()) {
				const std::string imbuingName = imbuing.name();
				const int32_t imbuingValue = static_cast<int32_t>(
				    imbuing.attribute("value").as_double());

				if (imbuingName == "lifeLeechChance" || imbuingName == "lifeleechchance") {
					outfit.specialSkills[SPECIALSKILL_LIFELEECHCHANCE] += imbuingValue;
				} else if (imbuingName == "lifeLeechAmount" || imbuingName == "lifeleechamount") {
					outfit.specialSkills[SPECIALSKILL_LIFELEECHAMOUNT] += imbuingValue;
				} else if (imbuingName == "manaLeechChance" || imbuingName == "manaleechchance") {
					outfit.specialSkills[SPECIALSKILL_MANALEECHCHANCE] += imbuingValue;
				} else if (imbuingName == "manaLeechAmount" || imbuingName == "manaleechamount") {
					outfit.specialSkills[SPECIALSKILL_MANALEECHAMOUNT] += imbuingValue;
				} else if (imbuingName == "criticalChance" || imbuingName == "criticalchance") {
					outfit.specialSkills[SPECIALSKILL_CRITICALHITCHANCE] += imbuingValue;
				} else if (imbuingName == "criticalDamage" || imbuingName == "criticaldamage") {
					outfit.specialSkills[SPECIALSKILL_CRITICALHITAMOUNT] += imbuingValue;
				}
			}
		}
	}
	return true;
}

const Outfit* Outfits::getOutfitByLookType(uint16_t lookType) const
{
	auto it = outfits.find(lookType);
	if (it != outfits.end()) {
		return &it->second;
	}
	return nullptr;
}

const Outfit* Outfits::getOutfitByLookType(uint16_t lookType, PlayerSex_t sex) const
{
	auto it = outfits.find(lookType);
	if (it != outfits.end() && it->second.sex == sex) {
		return &it->second;
	}
	return nullptr;
}

std::vector<ObserverPtr<const Outfit>> Outfits::getOutfits(PlayerSex_t sex) const
{
	std::vector<ObserverPtr<const Outfit>> sexOutfits;
	for (const auto& it : outfits) {
		if (it.second.sex == sex) {
			sexOutfits.push_back(&it.second);
		}
	}
	return sexOutfits;
}

uint32_t Outfits::getOutfitId(PlayerSex_t sex, uint16_t lookType) const
{
	auto it = outfits.find(lookType);
	if (it != outfits.end() && it->second.sex == sex) {
		return it->second.lookType;
	}
	return 0;
}

bool Outfits::addAttributes(uint32_t playerId, uint32_t outfitId, PlayerSex_t sex)
{
	auto player = g_game.getPlayerByID(playerId);
	if (!player) {
		return false;
	}

	auto it = outfits.find(static_cast<uint16_t>(outfitId));
	if (it == outfits.end() || it->second.sex != sex) {
		return false;
	}

	const Outfit& outfit = it->second;

	if (outfit.manaShield) {
		Condition_ptr condition = Condition::createCondition(CONDITIONID_OUTFIT, CONDITION_MANASHIELD, -1, 0);
		player->addCondition(std::move(condition));
	}

	if (outfit.invisible) {
		Condition_ptr condition = Condition::createCondition(CONDITIONID_OUTFIT, CONDITION_INVISIBLE, -1, 0);
		player->addCondition(std::move(condition));
	}

	if (outfit.speed != 0) {
		g_game.changeSpeed(player.get(), outfit.speed);
	}

	if (outfit.regeneration) {
		Condition_ptr condition = Condition::createCondition(CONDITIONID_OUTFIT, CONDITION_REGENERATION, -1, 0);

		if (outfit.healthGain != 0) {
			condition->setParam(CONDITION_PARAM_HEALTHGAIN, outfit.healthGain);
		}
		if (outfit.healthTicks != 0) {
			condition->setParam(CONDITION_PARAM_HEALTHTICKS, outfit.healthTicks);
		}
		if (outfit.manaGain != 0) {
			condition->setParam(CONDITION_PARAM_MANAGAIN, outfit.manaGain);
		}
		if (outfit.manaTicks != 0) {
			condition->setParam(CONDITION_PARAM_MANATICKS, outfit.manaTicks);
		}

		player->addCondition(std::move(condition));
	}

	bool needsUpdate = false;

	for (uint32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
		if (outfit.skills[i] != 0) {
			player->setVarSkill(static_cast<skills_t>(i), outfit.skills[i]);
			needsUpdate = true;
		}
	}

	for (uint32_t s = SPECIALSKILL_FIRST; s <= SPECIALSKILL_LAST; ++s) {
		if (outfit.specialSkills[s] != 0) {
			player->setVarSpecialSkill(static_cast<SpecialSkills_t>(s), outfit.specialSkills[s]);
			needsUpdate = true;
		}
	}

	for (uint32_t s = STAT_FIRST; s <= STAT_LAST; ++s) {
		if (outfit.stats[s] != 0) {
			player->setVarStats(static_cast<stats_t>(s), outfit.stats[s]);
			needsUpdate = true;
		}
	}

	if (outfit.attackSpeed != 0) {
		player->setAttackSpeed(player->getAttackSpeed() + outfit.attackSpeed);
		needsUpdate = true;
	}

	if (needsUpdate) {
		player->sendStats();
		player->sendSkills();
	}

	return true;
}

bool Outfits::removeAttributes(uint32_t playerId, uint32_t outfitId, PlayerSex_t sex)
{
	auto player = g_game.getPlayerByID(playerId);
	if (!player) {
		return false;
	}

	auto it = outfits.find(static_cast<uint16_t>(outfitId));
	if (it == outfits.end() || it->second.sex != sex) {
		return false;
	}

	const Outfit& outfit = it->second;

	if (outfit.manaShield) {
		player->removeCondition(CONDITION_MANASHIELD, CONDITIONID_OUTFIT);
	}
	if (outfit.invisible) {
		player->removeCondition(CONDITION_INVISIBLE, CONDITIONID_OUTFIT);
	}
	if (outfit.speed != 0) {
		g_game.changeSpeed(player.get(), -outfit.speed);
	}
	if (outfit.regeneration) {
		player->removeCondition(CONDITION_REGENERATION, CONDITIONID_OUTFIT);
	}

	bool needsUpdate = false;

	for (uint32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
		if (outfit.skills[i] != 0) {
			player->setVarSkill(static_cast<skills_t>(i), -outfit.skills[i]);
			needsUpdate = true;
		}
	}

	for (uint32_t s = SPECIALSKILL_FIRST; s <= SPECIALSKILL_LAST; ++s) {
		if (outfit.specialSkills[s] != 0) {
			player->setVarSpecialSkill(static_cast<SpecialSkills_t>(s), -outfit.specialSkills[s]);
			needsUpdate = true;
		}
	}

	for (uint32_t s = STAT_FIRST; s <= STAT_LAST; ++s) {
		if (outfit.stats[s] != 0) {
			player->setVarStats(static_cast<stats_t>(s), -outfit.stats[s]);
			needsUpdate = true;
		}
	}

	if (outfit.attackSpeed != 0) {
		player->setAttackSpeed(player->getAttackSpeed() - outfit.attackSpeed);
		needsUpdate = true;
	}

	if (needsUpdate) {
		player->sendStats();
		player->sendSkills();
	}

	return true;
}
