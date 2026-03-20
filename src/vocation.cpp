// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "vocation.h"

#include "logger.h"
#include "player.h"
#include "pugicast.h"
#include "tools.h"

#include <fmt/format.h>

bool Vocations::loadFromXml()
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file("data/XML/vocations.xml");
	if (!result) {
		printXMLError("Error - Vocations::loadFromXml", "data/XML/vocations.xml", result);
		return false;
	}

	for (auto vocationNode : doc.child("vocations").children()) {
		pugi::xml_attribute attr = vocationNode.attribute("id");
		if (!attr) {
			LOG_WARN("[Warning - Vocations::loadFromXml] Missing vocation id");
			continue;
		}

		uint16_t id = pugi::cast<uint16_t>(attr.value());
		auto res = vocationsMap.emplace(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(id));
		Vocation& voc = res.first->second;

		vocationNode.remove_attribute("id");
		for (auto attrNode : vocationNode.attributes()) {
			const char* attrName = attrNode.name();
			if (caseInsensitiveEqual(attrName, "name")) {
				voc.name = attrNode.as_string();
			} else if (caseInsensitiveEqual(attrName, "allowpvp")) {
				voc.allowPvp = attrNode.as_bool();
			} else if (caseInsensitiveEqual(attrName, "clientid")) {
				voc.clientId = pugi::cast<uint16_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "description")) {
				voc.description = attrNode.as_string();
			} else if (caseInsensitiveEqual(attrName, "gaincap")) {
				voc.gainCap = pugi::cast<uint32_t>(attrNode.value()) * 100;
			} else if (caseInsensitiveEqual(attrName, "gainhp")) {
				voc.gainHP = pugi::cast<uint32_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "gainmana")) {
				voc.gainMana = pugi::cast<uint32_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "gainhpticks")) {
				voc.gainHealthTicks = pugi::cast<uint32_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "gainhpamount")) {
				voc.gainHealthAmount = pugi::cast<uint32_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "gainmanaticks")) {
				voc.gainManaTicks = pugi::cast<uint32_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "gainmanaamount")) {
				voc.gainManaAmount = pugi::cast<uint32_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "manamultiplier")) {
				voc.manaMultiplier = pugi::cast<float>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "attackspeed")) {
				voc.attackSpeed = pugi::cast<uint32_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "basespeed")) {
				voc.baseSpeed = pugi::cast<uint32_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "soulmax")) {
				voc.soulMax = pugi::cast<uint16_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "gainsoulticks")) {
				voc.gainSoulTicks = pugi::cast<uint16_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "fromvoc")) {
				voc.fromVocation = pugi::cast<uint32_t>(attrNode.value());
			} else if (caseInsensitiveEqual(attrName, "nopongkicktime")) {
				voc.noPongKickTime = pugi::cast<uint32_t>(attrNode.value()) * 1000;
			} else if (caseInsensitiveEqual(attrName, "skillloss") || caseInsensitiveEqual(attrName, "lossskill")) {
				voc.setLossSkill(attrNode.as_bool());
			} else {
				LOG_WARN(fmt::format("[Notice - Vocations::loadFromXml] Unknown attribute: \"{}\" for vocation: {}",
				                     attrName, voc.id));
			}
		}

		for (auto childNode : vocationNode.children()) {
			if (caseInsensitiveEqual(childNode.name(), "skill")) {
				if ((attr = childNode.attribute("id"))) {
					uint16_t skillId = pugi::cast<uint16_t>(attr.value());
					if (skillId <= SKILL_LAST) {
						voc.skillMultipliers[skillId] = pugi::cast<double>(childNode.attribute("multiplier").value());
					} else {
						LOG_WARN(fmt::format("[Notice - Vocations::loadFromXml] No valid skill id: {} for vocation: {}",
						                     skillId, voc.id));
					}
				} else {
					LOG_WARN(
					    fmt::format("[Notice - Vocations::loadFromXml] Missing skill id for vocation: {}", voc.id));
				}
			} else if (caseInsensitiveEqual(childNode.name(), "formula")) {
				if ((attr = childNode.attribute("meleeDamage"))) {
					voc.meleeDamageMultiplier = pugi::cast<float>(attr.value());
				}

				if ((attr = childNode.attribute("distDamage"))) {
					voc.distDamageMultiplier = pugi::cast<float>(attr.value());
				}

				if ((attr = childNode.attribute("defense"))) {
					voc.defenseMultiplier = pugi::cast<float>(attr.value());
				}

				if ((attr = childNode.attribute("armor"))) {
					voc.armorMultiplier = pugi::cast<float>(attr.value());
				}

				if ((attr = childNode.attribute("wandDamage"))) {
					voc.wandDamageMultiplier = pugi::cast<float>(attr.value());
				}
			}
		}
	}

	size_t vocationCount = vocationsMap.size();
	LOG_INFO(fmt::format(">> Loading Vocations [\033[1;32m{}\033[0m]...", vocationCount));

	return true;
}

Vocation* Vocations::getVocation(uint16_t id)
{
	auto it = vocationsMap.find(id);
	if (it == vocationsMap.end()) {
		LOG_WARN(fmt::format("[Warning - Vocations::getVocation] Vocation {} not found.", id));
		return nullptr;
	}
	return &it->second;
}

std::optional<uint16_t> Vocations::getVocationId(std::string_view name) const
{
	auto it = std::find_if(vocationsMap.begin(), vocationsMap.end(), [&name](auto it) {
		return name.size() == it.second.name.size() &&
		       std::equal(name.begin(), name.end(), it.second.name.begin(),
		                  [](char a, char b) { return std::tolower(a) == std::tolower(b); });
	});

	if (it != vocationsMap.end()) {
		return std::make_optional(it->first);
	}
	return std::nullopt;
}

uint16_t Vocations::getPromotedVocation(uint16_t id) const
{
	auto it = std::find_if(vocationsMap.begin(), vocationsMap.end(),
	                       [id](auto it) { return it.second.fromVocation == id && it.first != id; });
	return it != vocationsMap.end() ? it->first : static_cast<uint16_t>(VOCATION_NONE);
}

inline const uint32_t skillBase[SKILL_LAST + 1] = {50, 50, 50, 50, 30, 100, 20};

uint64_t Vocation::getReqSkillTries(skills_t skill, uint16_t level) const
{
	if (skill > SKILL_LAST) {
		return 0;
	}
	return skillBase[skill] *
	       std::pow(skillMultipliers[skill], static_cast<int32_t>(level - (MINIMUM_SKILL_LEVEL + 1)));
}

uint64_t Vocation::getReqMana(uint32_t magLevel) const
{
	if (magLevel == 0) {
		return 0;
	}
	return 1600 * std::pow(manaMultiplier, static_cast<int32_t>(magLevel - 1));
}
