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

enum class StoreHighlightState : uint8_t
{
	None = 0,
	New = 1,
	Sale = 2,
	Timed = 3,
};

enum class StoreDailyHighlightMode : uint8_t
{
	Sale,
	Timed,
	Mixed,
};

/// Rotation settings parsed from the optional <dailyOffers> catalog node.
struct StoreDailyOffersConfig
{
	bool enabled = false;
	bool rotateOnStartup = false;
	uint16_t offerCount = 2;
	uint32_t rotationSeconds = 24 * 60 * 60;
	uint8_t minimumDiscountPercent = 10;
	uint8_t maximumDiscountPercent = 25;
	StoreDailyHighlightMode highlightMode = StoreDailyHighlightMode::Mixed;
	std::string stateFile = "data/store/daily_offers_state.xml";
};

[[nodiscard]] std::optional<StoreHighlightState> parseStoreHighlightState(std::string_view stateStr);

[[nodiscard]] constexpr bool storeHighlightHasExpiration(StoreHighlightState state) noexcept
{
	return state == StoreHighlightState::Sale || state == StoreHighlightState::Timed;
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
	StoreHighlightState state = StoreHighlightState::None;
	uint32_t saleValidUntilTimestamp = 0;
	bool dailyEligible = true;

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
	StoreHighlightState state = StoreHighlightState::None;
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
	int64_t price = 0;        ///< negative = debit, positive = credit
	int32_t costSecond = 0;   ///< secondary cost flag
	std::string title;
	uint16_t count = 0;
};

#endif // FS_STORE_TYPES_H
