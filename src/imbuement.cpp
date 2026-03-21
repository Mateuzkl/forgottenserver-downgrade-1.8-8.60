#include "otpch.h"
#include "imbuement.h"
#include "logger.h"
#include "pugicast.h"
#include "tools.h"

#include <fmt/format.h>

bool Imbuement::isSkill() const {
    switch (this->imbuetype){

        case ImbuementType::IMBUEMENT_TYPE_FIST_SKILL :
        case ImbuementType::IMBUEMENT_TYPE_AXE_SKILL :
        case ImbuementType::IMBUEMENT_TYPE_SWORD_SKILL :
        case ImbuementType::IMBUEMENT_TYPE_CLUB_SKILL :
        case ImbuementType::IMBUEMENT_TYPE_DISTANCE_SKILL :
        case ImbuementType::IMBUEMENT_TYPE_FISHING_SKILL :
        case ImbuementType::IMBUEMENT_TYPE_SHIELD_SKILL :
        case ImbuementType::IMBUEMENT_TYPE_MAGIC_LEVEL :
            return true;

        default:
            return false;
    }
}


bool Imbuement::isSpecialSkill() const {
	switch (this->imbuetype){

		case ImbuementType::IMBUEMENT_TYPE_LIFE_LEECH :
		case ImbuementType::IMBUEMENT_TYPE_MANA_LEECH :
		case ImbuementType::IMBUEMENT_TYPE_CRITICAL_CHANCE :
		case ImbuementType::IMBUEMENT_TYPE_CRITICAL_AMOUNT:
			return true;

		default:
			return false;
	}
}


bool Imbuement::isDamage() const {
	switch (this->imbuetype){

		case ImbuementType::IMBUEMENT_TYPE_FIRE_DAMAGE :
		case ImbuementType::IMBUEMENT_TYPE_EARTH_DAMAGE :
		case ImbuementType::IMBUEMENT_TYPE_ICE_DAMAGE :
		case ImbuementType::IMBUEMENT_TYPE_ENERGY_DAMAGE :
		case ImbuementType::IMBUEMENT_TYPE_DEATH_DAMAGE :
		case ImbuementType::IMBUEMENT_TYPE_HOLY_DAMAGE :
			return true;

		default:
			return false;
	}
}


bool Imbuement::isResist() const {
	switch (this->imbuetype){

		case ImbuementType::IMBUEMENT_TYPE_FIRE_RESIST :
		case ImbuementType::IMBUEMENT_TYPE_EARTH_RESIST :
		case ImbuementType::IMBUEMENT_TYPE_ICE_RESIST :
		case ImbuementType::IMBUEMENT_TYPE_ENERGY_RESIST :
		case ImbuementType::IMBUEMENT_TYPE_DEATH_RESIST :
		case ImbuementType::IMBUEMENT_TYPE_HOLY_RESIST :
			return true;

		default:
			return false;
	}
}

bool Imbuement::isStat() const {
	switch (this->imbuetype){

		case ImbuementType::IMBUEMENT_TYPE_SPEED_BOOST :
		case ImbuementType::IMBUEMENT_TYPE_CAPACITY_BOOST :
			return true;

		default:
			return false;
	}
}

bool Imbuement::isEquipDecay() const {
	return this->decaytype == ImbuementDecayType::IMBUEMENT_DECAY_EQUIPPED;
}

bool Imbuement::isInfightDecay() const {
	return this->decaytype == ImbuementDecayType::IMBUEMENT_DECAY_INFIGHT;
}

// ============ Imbuements XML Loader ============

bool Imbuements::loadFromXml()
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file("data/XML/imbuements.xml");
	if (!result) {
		printXMLError("Error - Imbuements::loadFromXml", "data/XML/imbuements.xml", result);
		return false;
	}

	for (auto& node : doc.child("imbuements").children()) {
		std::string nodeName = node.name();

		if (nodeName == "base") {
			ImbuementBase base;
			base.id = pugi::cast<uint16_t>(node.attribute("id").value());
			base.name = node.attribute("name").as_string();
			base.price = pugi::cast<uint32_t>(node.attribute("price").value());
			base.removeCost = pugi::cast<uint32_t>(node.attribute("removecost").value());
			base.duration = pugi::cast<uint32_t>(node.attribute("duration").value());
			bases[base.id] = base;
		}
		else if (nodeName == "category") {
			ImbuementCategory cat;
			cat.id = pugi::cast<uint16_t>(node.attribute("id").value());
			cat.name = node.attribute("name").as_string();
			cat.agressive = node.attribute("agressive").as_bool();
			categories[cat.id] = cat;
		}
		else if (nodeName == "imbuement") {
			ImbuementDefinition def;
			def.name = node.attribute("name").as_string();
			def.baseId = pugi::cast<uint16_t>(node.attribute("base").value());
			def.categoryId = pugi::cast<uint16_t>(node.attribute("category").value());
			def.iconId = pugi::cast<uint16_t>(node.attribute("iconid").value());
			def.premium = node.attribute("premium").as_bool();
			def.storage = pugi::cast<uint32_t>(node.attribute("storage").value());
			def.scrollId = pugi::cast<uint16_t>(node.attribute("scrollid").value());

			// Resolve base data
			auto baseIt = bases.find(def.baseId);
			if (baseIt != bases.end()) {
				def.price = baseIt->second.price;
				def.removeCost = baseIt->second.removeCost;
				def.duration = baseIt->second.duration;
				def.baseName = baseIt->second.name;
			}

			// Parse child attributes
			for (auto& attr : node.children("attribute")) {
				std::string key = attr.attribute("key").as_string();

				if (key == "description") {
					def.description = attr.attribute("value").as_string();
				}
				else if (key == "effect") {
					def.effectType = attr.attribute("type").as_string();
					def.effectValue = attr.attribute("value").as_string();
					def.effectCombat = attr.attribute("combat").as_string();
					def.bonus = pugi::cast<uint32_t>(attr.attribute("bonus").value());
					def.chance = pugi::cast<uint32_t>(attr.attribute("chance").value());
					def.value = pugi::cast<uint32_t>(attr.attribute("value").value());
				}
				else if (key == "item") {
					ImbuementItemReq req;
					req.itemId = pugi::cast<uint16_t>(attr.attribute("value").value());
					req.count = pugi::cast<uint16_t>(attr.attribute("count").value());
					def.items.push_back(req);
				}
			}

			// Resolve ImbuementType and decay
			resolveImbuementType(def);

			// Index by scrollId if non-zero
			if (def.scrollId != 0) {
				scrollIdIndex[def.scrollId] = definitions.size();
			}

			definitions.push_back(std::move(def));
		}
	}

	LOG_INFO(fmt::format(">> Loaded {:d} imbuement bases, {:d} categories, {:d} definitions.",
		bases.size(), categories.size(), definitions.size()));
	return true;
}

void Imbuements::resolveImbuementType(ImbuementDefinition& def)
{
	// Default decay: skills/leech/critical/damage = INFIGHT, speed/capacity = EQUIPPED
	def.decayType = IMBUEMENT_DECAY_INFIGHT;

	if (def.effectType == "skill") {
		const std::string& v = def.effectValue;
		if (v == "sword")         { def.imbuementType = IMBUEMENT_TYPE_SWORD_SKILL;     def.resolvedValue = def.bonus; }
		else if (v == "axe")      { def.imbuementType = IMBUEMENT_TYPE_AXE_SKILL;       def.resolvedValue = def.bonus; }
		else if (v == "club")     { def.imbuementType = IMBUEMENT_TYPE_CLUB_SKILL;      def.resolvedValue = def.bonus; }
		else if (v == "distance") { def.imbuementType = IMBUEMENT_TYPE_DISTANCE_SKILL;  def.resolvedValue = def.bonus; }
		else if (v == "shield")   { def.imbuementType = IMBUEMENT_TYPE_SHIELD_SKILL;    def.resolvedValue = def.bonus; }
		else if (v == "fist")     { def.imbuementType = IMBUEMENT_TYPE_FIST_SKILL;      def.resolvedValue = def.bonus; }
		else if (v == "fishing")  { def.imbuementType = IMBUEMENT_TYPE_FISHING_SKILL;   def.resolvedValue = def.bonus; }
		else if (v == "magicpoints") { def.imbuementType = IMBUEMENT_TYPE_MAGIC_LEVEL;  def.resolvedValue = def.bonus; }
		else if (v == "lifeleech")   { def.imbuementType = IMBUEMENT_TYPE_LIFE_LEECH;   def.resolvedValue = def.bonus; }
		else if (v == "manaleech")   { def.imbuementType = IMBUEMENT_TYPE_MANA_LEECH;   def.resolvedValue = def.bonus; }
		else if (v == "critical") {
			def.imbuementType = IMBUEMENT_TYPE_CRITICAL_CHANCE;
			def.resolvedValue = (static_cast<uint32_t>(def.bonus) << 16) | (static_cast<uint32_t>(def.chance) & 0xFFFF);
		}
	}
	else if (def.effectType == "damage") {
		const std::string& c = def.effectCombat;
		if (c == "fire")        { def.imbuementType = IMBUEMENT_TYPE_FIRE_DAMAGE;   }
		else if (c == "earth")  { def.imbuementType = IMBUEMENT_TYPE_EARTH_DAMAGE;  }
		else if (c == "ice")    { def.imbuementType = IMBUEMENT_TYPE_ICE_DAMAGE;    }
		else if (c == "energy") { def.imbuementType = IMBUEMENT_TYPE_ENERGY_DAMAGE; }
		else if (c == "death")  { def.imbuementType = IMBUEMENT_TYPE_DEATH_DAMAGE;  }
		else if (c == "holy")   { def.imbuementType = IMBUEMENT_TYPE_HOLY_DAMAGE;   }
		def.resolvedValue = def.value;
	}
	else if (def.effectType == "reduction") {
		const std::string& c = def.effectCombat;
		if (c == "fire")        { def.imbuementType = IMBUEMENT_TYPE_FIRE_RESIST;   }
		else if (c == "earth")  { def.imbuementType = IMBUEMENT_TYPE_EARTH_RESIST;  }
		else if (c == "ice")    { def.imbuementType = IMBUEMENT_TYPE_ICE_RESIST;    }
		else if (c == "energy") { def.imbuementType = IMBUEMENT_TYPE_ENERGY_RESIST; }
		else if (c == "death")  { def.imbuementType = IMBUEMENT_TYPE_DEATH_RESIST;  }
		else if (c == "holy")   { def.imbuementType = IMBUEMENT_TYPE_HOLY_RESIST;   }
		def.resolvedValue = def.value;
	}
	else if (def.effectType == "speed") {
		def.imbuementType = IMBUEMENT_TYPE_SPEED_BOOST;
		def.decayType = IMBUEMENT_DECAY_EQUIPPED;
		def.resolvedValue = def.value;
	}
	else if (def.effectType == "capacity") {
		def.imbuementType = IMBUEMENT_TYPE_CAPACITY_BOOST;
		def.decayType = IMBUEMENT_DECAY_EQUIPPED;
		def.resolvedValue = def.value;
	}
	else if (def.effectType == "vibrancy") {
		def.imbuementType = IMBUEMENT_TYPE_PARALYSIS_DEFLECTION;
		def.resolvedValue = def.chance;
	}
}

const ImbuementBase* Imbuements::getBaseById(uint16_t id) const
{
	auto it = bases.find(id);
	return it != bases.end() ? &it->second : nullptr;
}

const ImbuementCategory* Imbuements::getCategoryById(uint16_t id) const
{
	auto it = categories.find(id);
	return it != categories.end() ? &it->second : nullptr;
}

const ImbuementDefinition* Imbuements::getDefinitionByScrollId(uint16_t scrollId) const
{
	auto it = scrollIdIndex.find(scrollId);
	if (it != scrollIdIndex.end() && it->second < definitions.size()) {
		return &definitions[it->second];
	}
	return nullptr;
}