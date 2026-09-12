// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_STORE_DAILY_OFFERS_H
#define FS_STORE_DAILY_OFFERS_H

#include "store/store_types.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

class StoreCatalog;

struct StoreDailyOffer
{
	uint32_t offerId = 0;
	uint32_t price = 0;
	uint32_t validUntilTimestamp = 0;
	uint8_t discountPercent = 0;
	StoreHighlightState state = StoreHighlightState::None;
};

struct StoreDailyOffersSnapshot
{
	uint32_t validUntilTimestamp = 0;
	std::unordered_map<uint32_t, StoreDailyOffer> offers;

	[[nodiscard]] const StoreDailyOffer* find(uint32_t offerId) const noexcept;
};

/// Runtime overlay for automatic Daily Offers. It never copies or mutates the
/// catalog: entries retain their original IDs and delivery data, while this
/// class supplies only the effective price/highlight for the active rotation.
class StoreDailyOffers final
{
public:
	StoreDailyOffers();
	explicit StoreDailyOffers(uint32_t randomSeed);

	StoreDailyOffers(const StoreDailyOffers&) = delete;
	StoreDailyOffers& operator=(const StoreDailyOffers&) = delete;

	/// Configure from a newly loaded catalog and restore or create its rotation.
	void configure(std::shared_ptr<const StoreCatalog> catalog, uint32_t nowTimestamp);

	/// Return one internally consistent active-rotation snapshot. An expired
	/// rotation is replaced and persisted before the snapshot is returned.
	[[nodiscard]] StoreDailyOffersSnapshot snapshot(uint32_t nowTimestamp);

	[[nodiscard]] static uint32_t discountedPrice(uint32_t basePrice,
	                                              uint8_t discountPercent) noexcept;

private:
	[[nodiscard]] bool isEligible(const StoreOffer& offer) const noexcept;
	[[nodiscard]] bool loadState(uint32_t nowTimestamp);
	void rotate(uint32_t nowTimestamp);
	void saveState() const;
	void clear() noexcept;

	mutable std::mutex mutex_;
	std::mt19937 random_;
	std::shared_ptr<const StoreCatalog> catalog_;
	StoreDailyOffersConfig config_;
	StoreDailyOffersSnapshot active_;
	std::vector<uint32_t> previousOfferIds_;
};

#endif // FS_STORE_DAILY_OFFERS_H
