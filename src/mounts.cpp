// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "mounts.h"

#include "pugicast.h"
#include "tools.h"
#include "logger.h"
#include "game.h"
#include "player.h"
#include "condition.h"
#include <fmt/format.h>

bool Mounts::reload()
{
	mounts.clear();
	clientIdToId.clear();
	nameToId.clear();
	return loadFromXml();
}

bool Mounts::loadFromXml()
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file("data/XML/mounts.xml");
	if (!result) {
		printXMLError("Error - Mounts::loadFromXml", "data/XML/mounts.xml", result);
		return false;
	}

	for (auto mountNode : doc.child("mounts").children()) {
		uint16_t nodeId = pugi::cast<uint16_t>(mountNode.attribute("id").value());
		if (nodeId == 0 || nodeId > std::numeric_limits<uint16_t>::max()) {
			LOG_WARN(fmt::format("[Notice - Mounts::loadFromXml] Mount id \"{}\" is not within 1 and 65535 range", nodeId));
			continue;
		}

		if (getMountByID(nodeId)) {
			LOG_WARN(fmt::format("[Notice - Mounts::loadFromXml] Duplicate mount with id: {}", nodeId));
			continue;
		}

		// Direct insertion into map with value semantics — no heap indirection.
		auto it = mounts.emplace(
		    nodeId, Mount {
				nodeId,
		        pugi::cast<uint16_t>(mountNode.attribute("clientid").value()),
		        mountNode.attribute("name").as_string(),
		        pugi::cast<int32_t>(mountNode.attribute("speed").value()),
		        mountNode.attribute("premium").as_bool()
		    }
		).first;

		Mount& mount = it->second;

		// Populate secondary indexes for O(1) lookups
		clientIdToId[mount.clientId] = nodeId;
		nameToId[asLowerCaseString(mount.name)] = nodeId;

		if (auto typeAttr = mountNode.attribute("type")) {
			mount.type = typeAttr.as_string();
		}

		mount.manaShield = mountNode.attribute("manashield").as_bool()
		                || mountNode.attribute("manaShield").as_bool();

		mount.invisible = mountNode.attribute("invisible").as_bool();

		if (auto asAttr = mountNode.attribute("attackSpeed")) {
			mount.attackSpeed = asAttr.as_int();
		} else if (auto asAttr2 = mountNode.attribute("attackspeed")) {
			mount.attackSpeed = asAttr2.as_int();
		}

		if (auto attr = mountNode.attribute("healthGain")) {
			mount.healthGain   = attr.as_int();
			mount.regeneration = true;
		}

		if (auto attr = mountNode.attribute("healthTicks")) {
			mount.healthTicks  = attr.as_int();
			mount.regeneration = true;
		}

		if (auto attr = mountNode.attribute("manaGain")) {
			mount.manaGain     = attr.as_int();
			mount.regeneration = true;
		}

		if (auto attr = mountNode.attribute("manaTicks")) {
			mount.manaTicks    = attr.as_int();
			mount.regeneration = true;
		}

		if (auto skillsNode = mountNode.child("skills")) {
			for (auto skillNode : skillsNode.children()) {
				const std::string skillName  = skillNode.name();
				const int32_t     skillValue = skillNode.attribute("value").as_int();

				if (skillName == "fist") {
					mount.skills[SKILL_FIST]     += skillValue;
				} else if (skillName == "club") {
					mount.skills[SKILL_CLUB]     += skillValue;
				} else if (skillName == "axe") {
					mount.skills[SKILL_AXE]      += skillValue;
				} else if (skillName == "sword") {
					mount.skills[SKILL_SWORD]    += skillValue;
				} else if (skillName == "distance" || skillName == "dist") {
					mount.skills[SKILL_DISTANCE] += skillValue;
				} else if (skillName == "shielding" || skillName == "shield") {
					mount.skills[SKILL_SHIELD]   += skillValue;
				} else if (skillName == "fishing" || skillName == "fish") {
					mount.skills[SKILL_FISHING]  += skillValue;
				} else if (skillName == "melee") {
					mount.skills[SKILL_FIST]  += skillValue;
					mount.skills[SKILL_CLUB]  += skillValue;
					mount.skills[SKILL_SWORD] += skillValue;
					mount.skills[SKILL_AXE]   += skillValue;
				} else if (skillName == "weapon" || skillName == "weapons") {
					mount.skills[SKILL_CLUB]     += skillValue;
					mount.skills[SKILL_SWORD]    += skillValue;
					mount.skills[SKILL_AXE]      += skillValue;
					mount.skills[SKILL_DISTANCE] += skillValue;
				}
			}
		}

		if (auto statsNode = mountNode.child("stats")) {
			for (auto statNode : statsNode.children()) {
				const std::string statName  = statNode.name();
				const int32_t     statValue = statNode.attribute("value").as_int();

				if (statName == "maxHealth" || statName == "maxhealth") {
					mount.stats[STAT_MAXHITPOINTS]  += statValue;
				} else if (statName == "maxMana" || statName == "maxmana") {
					mount.stats[STAT_MAXMANAPOINTS] += statValue;
				} else if (statName == "magLevel" || statName == "magicLevel"
				        || statName == "magiclevel"|| statName == "ml") {
					mount.stats[STAT_MAGICPOINTS]   += statValue;
				} else if (statName == "cap" || statName == "capacity") {
					mount.stats[STAT_CAPACITY]      += statValue * 100;
				}
			}
		}

		if (auto imbuingNode = mountNode.child("imbuing")) {
			for (auto imbuingChild : imbuingNode.children()) {
				const std::string imbuingName  = imbuingChild.name();
				const double      imbuingValue = imbuingChild.attribute("value").as_double();

				if (imbuingName == "lifeLeechChance" || imbuingName == "lifeleechchance") {
					mount.lifeLeechChance += imbuingValue;
				} else if (imbuingName == "lifeLeechAmount" || imbuingName == "lifeleechamount") {
					mount.lifeLeechAmount += imbuingValue;
				} else if (imbuingName == "manaLeechChance" || imbuingName == "manaleechchance") {
					mount.manaLeechChance += imbuingValue;
				} else if (imbuingName == "manaLeechAmount" || imbuingName == "manaleechamount") {
					mount.manaLeechAmount += imbuingValue;
				} else if (imbuingName == "criticalChance" || imbuingName == "criticalchance") {
					mount.criticalChance  += imbuingValue;
				} else if (imbuingName == "criticalDamage" || imbuingName == "criticaldamage") {
					mount.criticalDamage  += imbuingValue;
				}
			}
		}
	}
	return true;
}

Mount* Mounts::getMountByID(uint16_t id)
{
	auto it = mounts.find(id);
	return it != mounts.end() ? &it->second : nullptr;
}

Mount* Mounts::getMountByName(std::string_view name)
{
	auto it = nameToId.find(asLowerCaseString(std::string(name)));
	if (it != nameToId.end()) {
		auto mountIt = mounts.find(it->second);
		if (mountIt != mounts.end()) {
			return &mountIt->second;
		}
	}
	return nullptr;
}

Mount* Mounts::getMountByClientID(uint16_t clientId)
{
	auto it = clientIdToId.find(clientId);
	if (it != clientIdToId.end()) {
		auto mountIt = mounts.find(it->second);
		if (mountIt != mounts.end()) {
			return &mountIt->second;
		}
	}
	return nullptr;
}

bool Mounts::addAttributes(uint32_t playerId, uint16_t mountId)
{
	auto player = g_game.getPlayerByID(playerId);
	if (!player) {
		return false;
	}

	Mount* mount = getMountByID(mountId);
	if (!mount) {
		return false;
	}

	if (mount->manaShield) {
		Condition_ptr condition = Condition::createCondition(
		    CONDITIONID_MOUNT, CONDITION_MANASHIELD, -1, 0);
		player->addCondition(std::move(condition));
	}

	if (mount->invisible) {
		Condition_ptr condition = Condition::createCondition(
		    CONDITIONID_MOUNT, CONDITION_INVISIBLE, -1, 0);
		player->addCondition(std::move(condition));
	}

	if (mount->regeneration) {
		Condition_ptr condition = Condition::createCondition(
		    CONDITIONID_MOUNT, CONDITION_REGENERATION, -1, 0);

		if (mount->healthGain) {
			condition->setParam(CONDITION_PARAM_HEALTHGAIN,  mount->healthGain);
		}
		if (mount->healthTicks) {
			condition->setParam(CONDITION_PARAM_HEALTHTICKS, mount->healthTicks);
		}
		if (mount->manaGain) {
			condition->setParam(CONDITION_PARAM_MANAGAIN,    mount->manaGain);
		}
		if (mount->manaTicks) {
			condition->setParam(CONDITION_PARAM_MANATICKS,   mount->manaTicks);
		}

		player->addCondition(std::move(condition));
	}

	bool update = false;

	for (uint32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
		if (mount->skills[i] != 0) {
			player->setVarSkill(static_cast<skills_t>(i), mount->skills[i]);
			update = true;
		}
	}

	if (mount->lifeLeechChance > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_LIFELEECHCHANCE,
		    static_cast<int32_t>(mount->lifeLeechChance));
		update = true;
	}

	if (mount->lifeLeechAmount > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT,
		    static_cast<int32_t>(mount->lifeLeechAmount));
		update = true;
	}

	if (mount->manaLeechChance > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_MANALEECHCHANCE,
		    static_cast<int32_t>(mount->manaLeechChance));
		update = true;
	}

	if (mount->manaLeechAmount > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT,
		    static_cast<int32_t>(mount->manaLeechAmount));
		update = true;
	}

	if (mount->criticalChance > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_CRITICALHITCHANCE,
		    static_cast<int32_t>(mount->criticalChance));
		update = true;
	}

	if (mount->criticalDamage > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT,
		    static_cast<int32_t>(mount->criticalDamage));
		update = true;
	}

	for (uint32_t s = STAT_FIRST; s <= STAT_LAST; ++s) {
		if (mount->stats[s] != 0) {
			player->setVarStats(static_cast<stats_t>(s), mount->stats[s]);
			update = true;
		}
	}

	if (mount->attackSpeed != 0) {
		player->setAttackSpeed(player->getRawAttackSpeed() + mount->attackSpeed);
		update = true;
	}

	if (update) {
		player->sendStats();
		player->sendSkills();
	}

	return true;
}

bool Mounts::removeAttributes(uint32_t playerId, uint16_t mountId)
{
	auto player = g_game.getPlayerByID(playerId);
	if (!player) {
		return false;
	}

	Mount* mount = getMountByID(mountId);
	if (!mount) {
		return false;
	}

	if (mount->manaShield) {
		player->removeCondition(CONDITION_MANASHIELD, CONDITIONID_MOUNT);
	}

	if (mount->invisible) {
		player->removeCondition(CONDITION_INVISIBLE, CONDITIONID_MOUNT);
	}

	if (mount->regeneration) {
		player->removeCondition(CONDITION_REGENERATION, CONDITIONID_MOUNT);
	}

	bool update = false;

	for (uint32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
		if (mount->skills[i] != 0) {
			player->setVarSkill(static_cast<skills_t>(i), -mount->skills[i]);
			update = true;
		}
	}

	if (mount->lifeLeechChance > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_LIFELEECHCHANCE,
		    -static_cast<int32_t>(mount->lifeLeechChance));
		update = true;
	}

	if (mount->lifeLeechAmount > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT,
		    -static_cast<int32_t>(mount->lifeLeechAmount));
		update = true;
	}

	if (mount->manaLeechChance > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_MANALEECHCHANCE,
		    -static_cast<int32_t>(mount->manaLeechChance));
		update = true;
	}

	if (mount->manaLeechAmount > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT,
		    -static_cast<int32_t>(mount->manaLeechAmount));
		update = true;
	}

	if (mount->criticalChance > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_CRITICALHITCHANCE,
		    -static_cast<int32_t>(mount->criticalChance));
		update = true;
	}

	if (mount->criticalDamage > 0.0) {
		player->setVarSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT,
		    -static_cast<int32_t>(mount->criticalDamage));
		update = true;
	}

	for (uint32_t s = STAT_FIRST; s <= STAT_LAST; ++s) {
		if (mount->stats[s] != 0) {
			player->setVarStats(static_cast<stats_t>(s), -mount->stats[s]);
			update = true;
		}
	}

	if (mount->attackSpeed != 0) {
		player->setAttackSpeed(player->getRawAttackSpeed() - mount->attackSpeed);
		update = true;
	}

	if (update) {
		player->sendStats();
		player->sendSkills();
	}

	return true;
}
