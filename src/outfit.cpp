// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "outfit.h"

#include "logger.h"
#include "pugicast.h"
#include "tools.h"

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
		outfits.emplace(std::piecewise_construct, std::forward_as_tuple(lookType),
		                std::forward_as_tuple(outfitNode.attribute("name").as_string(), lookType,
		                                      static_cast<PlayerSex_t>(type), outfitNode.attribute("premium").as_bool(),
		                                      outfitNode.attribute("unlocked").as_bool(true)));
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

const std::vector<const Outfit*> Outfits::getOutfits(PlayerSex_t sex) const
{
	std::vector<const Outfit*> sexOutfits;
	for (const auto& it : outfits) {
		if (it.second.sex == sex) {
			sexOutfits.push_back(&it.second);
		}
	}

	return sexOutfits;
}
