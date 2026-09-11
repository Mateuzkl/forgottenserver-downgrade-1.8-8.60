// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_STORE_TYPES_H
#define FS_STORE_TYPES_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// All offer types discovered from data/store/gamestore.xml.
/// If the XML contains a type= value not in this enum, catalog loading will fail at startup.
enum class StoreOfferType : uint8_t
{
	Item,
	House,
	Outfit,
	Mount,
	Premium,
	BattlePass,
	ExpBoost,
	Blessing,
	PreyWildcard,
	ChangeName,
	SexChange,
	Hireling,
	HirelingSkill,
	HirelingOutfit,
	BountyKillBoost,
	WeeklyKillBoost,
	WeeklyReducedItems,
	WeeklyTaskExpansion,
};

/// Parse a type= string from XML to StoreOfferType.
/// Returns std::nullopt on unknown type.
[[nodiscard]] std::optional<StoreOfferType> parseStoreOfferType(std::string_view typeStr);

/// Convert StoreOfferType back to its canonical string form.
[[nodiscard]] std::string_view storeOfferTypeToString(StoreOfferType type) noexcept;

/// Whether this offer type requires Task Board systems to be enabled.
[[nodiscard]] constexpr bool isTaskBoardOfferType(StoreOfferType type) noexcept
{
	return type == StoreOfferType::BountyKillBoost || type == StoreOfferType::WeeklyKillBoost ||
	       type == StoreOfferType::WeeklyReducedItems || type == StoreOfferType::WeeklyTaskExpansion;
}

/// Whether this offer type is hireling-related.
[[nodiscard]] constexpr bool isHirelingOfferType(StoreOfferType type) noexcept
{
	return type == StoreOfferType::Hireling || type == StoreOfferType::HirelingSkill ||
	       type == StoreOfferType::HirelingOutfit;
}

/// Whether this offer type is an XP boost.
[[nodiscard]] constexpr bool isXpBoostOfferType(StoreOfferType type) noexcept
{
	return type == StoreOfferType::ExpBoost;
}

/// A single purchasable offer in the store catalog.
struct StoreOffer
{
	uint32_t id = 0;
	std::string name;
	std::string icon;
	uint32_t price = 0;

	uint16_t displayId = 0; ///< eid in XML (lookType or display item for client)
	uint16_t itemId = 0;    ///< itemid for delivery

	std::vector<uint16_t> items; ///< multi-item house offers
	uint16_t count = 1;

	std::string description;
	StoreOfferType type = StoreOfferType::Item;

	int64_t value = 0;       ///< type-specific value (days, seconds, blessing index, lookType, etc.)
	int64_t femaleValue = 0; ///< female lookType for outfit offers
	uint8_t addon = 0;       ///< outfit addon mask (0-3)
};

/// A category grouping of offers.
struct StoreCategory
{
	std::string name;
	std::string icon;
	std::string parent;
	std::string description;
	std::vector<StoreOffer> offers;
};

/// A home banner entry.
struct StoreBanner
{
	std::string image;
	uint8_t action = 0;
	uint32_t target = 0;
};

/// A purchase history entry matching the shop_history DB schema.
struct StoreHistoryEntry
{
	std::string date;
	int32_t price = 0;        ///< negative = debit, positive = credit
	int32_t costSecond = 0;   ///< secondary cost flag
	std::string title;
	uint16_t count = 0;
};

#endif // FS_STORE_TYPES_H
