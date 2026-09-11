// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "store/store_catalog.h"

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

std::vector<uint16_t> parseItemList(std::string_view value)
{
	std::vector<uint16_t> items;
	if (value.empty()) {
		return items;
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
			auto [ptr, ec] = std::from_chars(sub.data(), sub.data() + sub.size(), id);
			if (ec == std::errc{} && id != 0) {
				items.push_back(id);
			}
		}
	}
	return items;
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

		if (category.name.empty()) {
			LOG_WARN("[StoreCatalog::loadFromXML] Category with empty name, skipping.");
			continue;
		}

		for (auto offerNode : categoryNode.children("offer")) {
			StoreOffer offer;
			offer.id = offerNode.attribute("id").as_uint(0);
			if (offer.id == 0) {
				LOG_WARN(fmt::format("[StoreCatalog::loadFromXML] Offer with id=0 in category '{}', skipping.",
				                     category.name));
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
			offer.price = offerNode.attribute("price").as_uint(0);

			if (offer.price == 0) {
				LOG_WARN(fmt::format(
				    "[StoreCatalog::loadFromXML] Offer id={} '{}' has price=0.", offer.id, offer.name));
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

			offer.displayId = static_cast<uint16_t>(offerNode.attribute("eid").as_uint(0));
			offer.itemId = static_cast<uint16_t>(offerNode.attribute("itemid").as_uint(0));
			offer.count = static_cast<uint16_t>(std::max(1u, offerNode.attribute("count").as_uint(1)));
			offer.description = offerNode.attribute("description").as_string("");
			offer.value = offerNode.attribute("value").as_llong(0);
			offer.femaleValue = offerNode.attribute("femalevalue").as_llong(0);

			uint32_t addonValue = offerNode.attribute("addon").as_uint(0);
			if (addonValue > 3) {
				LOG_WARN(fmt::format(
				    "[StoreCatalog::loadFromXML] Offer id={} has addon={} > 3, clamping to 3.",
				    offer.id, addonValue));
				addonValue = 3;
			}
			offer.addon = static_cast<uint8_t>(addonValue);

			// Parse multi-item list for house offers.
			const std::string_view itemsStr = offerNode.attribute("items").as_string("");
			if (!itemsStr.empty()) {
				offer.items = parseItemList(itemsStr);
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
