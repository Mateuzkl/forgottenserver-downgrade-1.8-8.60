// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "store/store_catalog.h"

#include "item.h"
#include "logger.h"
#include "pugicast.h"

#include <algorithm>
#include <charconv>
#include <pugixml.hpp>
#include <sstream>
#include <unordered_set>

// ─── StoreOfferType string ↔ enum ───────────────────────────────────────────

namespace {

struct OfferTypeEntry
{
	std::string_view str;
	StoreOfferType type;
};

constexpr OfferTypeEntry offerTypeTable[] = {
    {"item", StoreOfferType::Item},
    {"house", StoreOfferType::House},
    {"outfit", StoreOfferType::Outfit},
    {"mount", StoreOfferType::Mount},
    {"premium", StoreOfferType::Premium},
    {"battlepass", StoreOfferType::BattlePass},
    {"expboost", StoreOfferType::ExpBoost},
    {"xpboost", StoreOfferType::ExpBoost},
    {"blessing", StoreOfferType::Blessing},
    {"bless", StoreOfferType::Blessing},
    {"prey_wildcard", StoreOfferType::PreyWildcard},
    {"changename", StoreOfferType::ChangeName},
    {"sexchange", StoreOfferType::SexChange},
    {"hireling", StoreOfferType::Hireling},
    {"hireling_skill", StoreOfferType::HirelingSkill},
    {"hireling_outfit", StoreOfferType::HirelingOutfit},
    {"bounty_kill_boost", StoreOfferType::BountyKillBoost},
    {"weekly_kill_boost", StoreOfferType::WeeklyKillBoost},
    {"weekly_reduced_items", StoreOfferType::WeeklyReducedItems},
    {"weekly_task_expansion", StoreOfferType::WeeklyTaskExpansion},
};

std::string toLower(std::string_view sv)
{
	std::string s(sv);
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
	return s;
}

bool parseCompleteU16(std::string_view sv, uint16_t& out)
{
	if (sv.empty()) {
		return false;
	}
	uint64_t val = 0;
	auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
	if (ec != std::errc{} || ptr != sv.data() + sv.size() || val > std::numeric_limits<uint16_t>::max()) {
		return false;
	}
	out = static_cast<uint16_t>(val);
	return true;
}

bool parseCompleteU32(std::string_view sv, uint32_t& out)
{
	if (sv.empty()) {
		return false;
	}
	uint64_t val = 0;
	auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
	if (ec != std::errc{} || ptr != sv.data() + sv.size() || val > std::numeric_limits<uint32_t>::max()) {
		return false;
	}
	out = static_cast<uint32_t>(val);
	return true;
}

bool parseCompleteI64(std::string_view sv, int64_t& out)
{
	if (sv.empty()) {
		return false;
	}
	int64_t val = 0;
	auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
	if (ec != std::errc{} || ptr != sv.data() + sv.size()) {
		return false;
	}
	out = val;
	return true;
}

bool parseItemList(std::string_view value, std::vector<uint16_t>& items)
{
	items.clear();
	if (value.empty()) {
		return true;
	}
	// Parse comma-or-space-separated item IDs.
	std::string str(value);
	std::istringstream iss(str);
	std::string token;
	while (iss >> token) {
		// Also split on commas.
		std::istringstream tokenStream(token);
		std::string sub;
		while (std::getline(tokenStream, sub, ',')) {
			if (sub.empty()) {
				continue;
			}
			uint16_t id = 0;
			if (!parseCompleteU16(sub, id) || id == 0) {
				return false;
			}
			items.push_back(id);
		}
	}
	return true;
}

} // namespace

std::optional<StoreOfferType> parseStoreOfferType(std::string_view typeStr)
{
	const auto lower = toLower(typeStr);
	for (const auto& [str, type] : offerTypeTable) {
		if (lower == str) {
			return type;
		}
	}
	return std::nullopt;
}

std::string_view storeOfferTypeToString(StoreOfferType type) noexcept
{
	// Return the first (canonical) string for this type.
	for (const auto& [str, t] : offerTypeTable) {
		if (t == type) {
			return str;
		}
	}
	return "item";
}

std::optional<StoreHighlightState> parseStoreHighlightState(std::string_view stateStr)
{
	const auto lower = toLower(stateStr);
	if (lower == "0" || lower == "none" || lower == "state_none") {
		return StoreHighlightState::None;
	}
	if (lower == "1" || lower == "new" || lower == "state_new") {
		return StoreHighlightState::New;
	}
	if (lower == "2" || lower == "sale" || lower == "state_sale") {
		return StoreHighlightState::Sale;
	}
	if (lower == "3" || lower == "timed" || lower == "state_timed") {
		return StoreHighlightState::Timed;
	}
	return std::nullopt;
}

// ─── StoreCatalog ────────────────────────────────────────────────────────────

const StoreOffer* StoreCatalog::findOffer(uint32_t id) const noexcept
{
	auto it = offerById_.find(id);
	return it != offerById_.end() ? it->second : nullptr;
}

std::span<const StoreCategory> StoreCatalog::categories() const noexcept
{
	return categories_;
}

std::span<const StoreBanner> StoreCatalog::banners() const noexcept
{
	return banners_;
}

const StoreCatalog::OutfitOfferInfo* StoreCatalog::findOutfitByLookType(uint16_t lookType) const noexcept
{
	auto it = outfitByLookType_.find(lookType);
	return it != outfitByLookType_.end() ? &it->second : nullptr;
}

std::shared_ptr<const StoreCatalog> StoreCatalog::loadFromXML(std::string_view path)
{
	pugi::xml_document doc;
	const auto result = doc.load_file(std::string(path).c_str());
	if (!result) {
		LOG_ERROR(fmt::format("[StoreCatalog::loadFromXML] Failed to load XML '{}': {}", path, result.description()));
		return nullptr;
	}

	auto root = doc.child("store");
	if (!root) {
		LOG_ERROR(fmt::format("[StoreCatalog::loadFromXML] Missing <store> root element in '{}'", path));
		return nullptr;
	}

	// Use new + shared_ptr because constructor is private.
	auto catalog = std::shared_ptr<StoreCatalog>(new StoreCatalog());
	std::unordered_set<uint32_t> seenOfferIds;
	bool hasFatalError = false;

	for (auto categoryNode : root.children("category")) {
		StoreCategory category;
		category.name = categoryNode.attribute("name").as_string("");
		category.icon = categoryNode.attribute("icon").as_string("");
		category.parent = categoryNode.attribute("parent").as_string("");
		category.description = categoryNode.attribute("description").as_string("");
		const auto categoryStateAttr = categoryNode.attribute("state");
		if (!categoryStateAttr.empty()) {
			const auto state = parseStoreHighlightState(categoryStateAttr.as_string());
			if (!state) {
				LOG_ERROR(fmt::format(
				    "[StoreCatalog::loadFromXML] Category '{}' has invalid highlight state '{}'.",
				    category.name, categoryStateAttr.as_string()));
				hasFatalError = true;
				continue;
			}
			category.state = *state;
		}

		if (category.name.empty()) {
			LOG_WARN("[StoreCatalog::loadFromXML] Category with empty name, skipping.");
			continue;
		}

		for (auto offerNode : categoryNode.children("offer")) {
			StoreOffer offer;
			const auto idAttr = offerNode.attribute("id");
			if (idAttr.empty() || !parseCompleteU32(idAttr.as_string(), offer.id) || offer.id == 0) {
				LOG_ERROR(fmt::format("[StoreCatalog::loadFromXML] Malformed or missing offer id '{}' in category '{}'.",
				                     idAttr.as_string(), category.name));
				hasFatalError = true;
				continue;
			}

			if (seenOfferIds.count(offer.id)) {
				LOG_ERROR(fmt::format(
				    "[StoreCatalog::loadFromXML] Duplicate offer id={} in category '{}'. Catalog is unsafe.",
				    offer.id, category.name));
				hasFatalError = true;
				continue;
			}
			seenOfferIds.insert(offer.id);

			offer.name = offerNode.attribute("name").as_string("Unknown");
			offer.icon = offerNode.attribute("icon").as_string("");
			const auto offerStateAttr = offerNode.attribute("state");
			if (!offerStateAttr.empty()) {
				const auto state = parseStoreHighlightState(offerStateAttr.as_string());
				if (!state) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has invalid highlight state '{}'.",
					    offer.id, offerStateAttr.as_string()));
					hasFatalError = true;
					continue;
				}
				offer.state = *state;
			}

			pugi::xml_attribute validUntilAttr = offerNode.attribute("saleValidUntilTimestamp");
			if (validUntilAttr.empty()) {
				validUntilAttr = offerNode.attribute("validuntil");
			}
			if (!validUntilAttr.empty()) {
				if (!parseCompleteU32(validUntilAttr.as_string(), offer.saleValidUntilTimestamp)) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has malformed expiration timestamp '{}'.",
					    offer.id, validUntilAttr.as_string()));
					hasFatalError = true;
					continue;
				}
				if (!storeHighlightHasExpiration(offer.state) && offer.saleValidUntilTimestamp != 0) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} defines an expiration without state SALE or TIMED.",
					    offer.id));
					hasFatalError = true;
					continue;
				}
			}

			const auto priceAttr = offerNode.attribute("price");
			if (!priceAttr.empty()) {
				if (!parseCompleteU32(priceAttr.as_string(), offer.price)) {
					LOG_ERROR(fmt::format("[StoreCatalog::loadFromXML] Malformed offer price '{}' in offer id={}.",
					                     priceAttr.as_string(), offer.id));
					hasFatalError = true;
					continue;
				}
			}

			if (offer.price == 0) {
				LOG_ERROR(fmt::format(
				    "[StoreCatalog::loadFromXML] Offer id={} '{}' has price=0.", offer.id, offer.name));
				hasFatalError = true;
				continue;
			}

			// Parse type string → enum.
			const std::string_view typeStr = offerNode.attribute("type").as_string("item");
			auto maybeType = parseStoreOfferType(typeStr);
			if (!maybeType) {
				LOG_ERROR(fmt::format(
				    "[StoreCatalog::loadFromXML] Offer id={} '{}' has unknown type '{}'. Catalog is unsafe.",
				    offer.id, offer.name, typeStr));
				hasFatalError = true;
				continue;
			}
			offer.type = *maybeType;

			const auto eidAttr = offerNode.attribute("eid");
			if (!eidAttr.empty()) {
				if (!parseCompleteU16(eidAttr.as_string(), offer.displayId)) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has malformed or out-of-range eid '{}'.",
					    offer.id, eidAttr.as_string()));
					hasFatalError = true;
					continue;
				}
			}

			const auto itemIdAttr = offerNode.attribute("itemid");
			if (!itemIdAttr.empty()) {
				if (!parseCompleteU16(itemIdAttr.as_string(), offer.itemId)) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has malformed or out-of-range itemid '{}'.",
					    offer.id, itemIdAttr.as_string()));
					hasFatalError = true;
					continue;
				}
			}

			const auto countAttr = offerNode.attribute("count");
			if (!countAttr.empty()) {
				if (!parseCompleteU16(countAttr.as_string(), offer.count)) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has malformed or out-of-range count '{}'.",
					    offer.id, countAttr.as_string()));
					hasFatalError = true;
					continue;
				}
				if (offer.count == 0) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has count=0.", offer.id));
					hasFatalError = true;
					continue;
				}
			} else {
				offer.count = 1;
			}

			offer.description = offerNode.attribute("description").as_string("");

			const auto valAttr = offerNode.attribute("value");
			if (!valAttr.empty()) {
				if (!parseCompleteI64(valAttr.as_string(), offer.value)) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has malformed value '{}'.",
					    offer.id, valAttr.as_string()));
					hasFatalError = true;
					continue;
				}
			}

			const auto fvalAttr = offerNode.attribute("femalevalue");
			if (!fvalAttr.empty()) {
				if (!parseCompleteI64(fvalAttr.as_string(), offer.femaleValue)) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has malformed femalevalue '{}'.",
					    offer.id, fvalAttr.as_string()));
					hasFatalError = true;
					continue;
				}
			}

			if (offer.type == StoreOfferType::Outfit) {
				if (offer.value < 0 || offer.value > std::numeric_limits<uint16_t>::max()) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has out-of-range outfit look type value {}.",
					    offer.id, offer.value));
					hasFatalError = true;
					continue;
				}
				if (offer.femaleValue < 0 || offer.femaleValue > std::numeric_limits<uint16_t>::max()) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has out-of-range outfit look type femalevalue {}.",
					    offer.id, offer.femaleValue));
					hasFatalError = true;
					continue;
				}
			} else if (offer.type == StoreOfferType::Mount) {
				if (offer.value <= 0 || offer.value > std::numeric_limits<uint16_t>::max()) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has out-of-range mount id value {}.",
					    offer.id, offer.value));
					hasFatalError = true;
					continue;
				}
			} else if (offer.type == StoreOfferType::Premium) {
				if (offer.value <= 0 || offer.value > 36500) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has invalid premium days value {}.",
					    offer.id, offer.value));
					hasFatalError = true;
					continue;
				}
			} else if (offer.type == StoreOfferType::ExpBoost) {
				if (offer.value < 0 || offer.value > std::numeric_limits<uint16_t>::max()) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has out-of-range XP boost duration {}.",
					    offer.id, offer.value));
					hasFatalError = true;
					continue;
				}
			} else if (offer.type == StoreOfferType::PreyWildcard) {
				if (offer.value <= 0 || offer.value > std::numeric_limits<uint16_t>::max()) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has invalid Prey Wildcard amount {}.",
					    offer.id, offer.value));
					hasFatalError = true;
					continue;
				}
			} else if (offer.type == StoreOfferType::Blessing) {
				if (offer.value != -1 && (offer.value < 1 || offer.value > 5)) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has invalid blessing value {}.",
					    offer.id, offer.value));
					hasFatalError = true;
					continue;
				}
			}

			uint16_t addonValue = 0;
			const auto addonAttr = offerNode.attribute("addon");
			if (!addonAttr.empty() &&
			    (!parseCompleteU16(addonAttr.as_string(), addonValue) || addonValue > 3)) {
				LOG_ERROR(fmt::format(
				    "[StoreCatalog::loadFromXML] Offer id={} has malformed or out-of-range addon '{}'.",
				    offer.id, addonAttr.as_string()));
				hasFatalError = true;
				continue;
			}
			offer.addon = static_cast<uint8_t>(addonValue);

			// Parse multi-item list for house offers.
			const auto itemsAttr = offerNode.attribute("items");
			if (!itemsAttr.empty()) {
				if (!parseItemList(itemsAttr.as_string(), offer.items)) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} has malformed or out-of-range items attribute '{}'.",
					    offer.id, itemsAttr.as_string()));
					hasFatalError = true;
					continue;
				}
				if (offer.items.size() > std::numeric_limits<uint16_t>::max()) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} contains too many house items.", offer.id));
					hasFatalError = true;
					continue;
				}
			}

			if (offer.type == StoreOfferType::Item) {
				if (offer.itemId == 0 || Item::items[offer.itemId].id == 0) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] Offer id={} references invalid itemid {}.",
					    offer.id, offer.itemId));
					hasFatalError = true;
					continue;
				}
			} else if (offer.type == StoreOfferType::House) {
				if (offer.items.empty() && offer.itemId == 0) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] House offer id={} has no delivery items.", offer.id));
					hasFatalError = true;
					continue;
				}

				const bool invalidItem = !offer.items.empty()
				    ? std::any_of(offer.items.begin(), offer.items.end(), [](uint16_t itemId) {
					      return Item::items[itemId].id == 0;
				      })
				    : Item::items[offer.itemId].id == 0;
				if (invalidItem) {
					LOG_ERROR(fmt::format(
					    "[StoreCatalog::loadFromXML] House offer id={} references an invalid item.", offer.id));
					hasFatalError = true;
					continue;
				}
			}

			category.offers.push_back(std::move(offer));
		}

		catalog->categories_.push_back(std::move(category));
	}

	if (hasFatalError) {
		LOG_ERROR("[StoreCatalog::loadFromXML] Fatal catalog validation errors detected. Store will not load.");
		return nullptr;
	}

	// Build ID → pointer index and outfit lookType index.
	for (auto& cat : catalog->categories_) {
		for (const auto& offer : cat.offers) {
			catalog->offerById_[offer.id] = &offer;

			// Build outfit lookType map (replaces protocolgame.cpp duplicate parser).
			if (offer.type == StoreOfferType::Outfit && offer.id != 0) {
				uint8_t addons = offer.addon;
				if (addons == 0) {
					addons = 3; // default addon mask for outfit offers
				}

				// Map male lookType (from value or displayId).
				const auto maleLookType = static_cast<uint16_t>(
				    offer.value != 0 ? offer.value : offer.displayId);
				if (maleLookType != 0) {
					catalog->outfitByLookType_[maleLookType] = OutfitOfferInfo{offer.id, addons};
				}

				// Map female lookType.
				if (offer.femaleValue != 0) {
					const auto femaleLookType = static_cast<uint16_t>(offer.femaleValue);
					catalog->outfitByLookType_[femaleLookType] = OutfitOfferInfo{offer.id, addons};
				}
			}
		}
	}

	// Default banners (matching current Lua constants).
	catalog->banners_.push_back(StoreBanner{
	    .image = "/images/store/home/banner_exercisedummies",
	    .action = 0,
	    .target = 0,
	});
	catalog->bannerDelay_ = 10;

	LOG_INFO(fmt::format("[StoreCatalog] Loaded {} categories with {} total offers.",
	                     catalog->categories_.size(), catalog->offerById_.size()));

	return catalog;
}

// ─── StoreManager ────────────────────────────────────────────────────────────

StoreManager& StoreManager::getInstance()
{
	static StoreManager instance;
	return instance;
}

bool StoreManager::loadCatalog(std::string_view path)
{
	auto newCatalog = StoreCatalog::loadFromXML(path);
	if (!newCatalog) {
		return false;
	}
	catalog_ = std::move(newCatalog);
	return true;
}

std::shared_ptr<const StoreCatalog> StoreManager::catalogSnapshot() const noexcept
{
	return catalog_;
}
