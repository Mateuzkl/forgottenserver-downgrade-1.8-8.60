// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_STORE_CATALOG_H
#define FS_STORE_CATALOG_H

#include "store/store_types.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// Immutable, server-global store catalog parsed from data/store/gamestore.xml.
/// This is the single source of truth for all offer data.
///
/// The catalog is loaded once at startup (or on explicit reload).
/// All lookups are read-only and safe for concurrent access.
class StoreCatalog final
{
public:
	StoreCatalog(const StoreCatalog&) = delete;
	StoreCatalog& operator=(const StoreCatalog&) = delete;
	StoreCatalog(StoreCatalog&&) = delete;
	StoreCatalog& operator=(StoreCatalog&&) = delete;

	/// Load catalog from an XML file. Returns nullptr on fatal validation error.
	/// Warnings are logged but do not prevent loading.
	[[nodiscard]] static std::shared_ptr<const StoreCatalog> loadFromXML(std::string_view path);

	/// Find an offer by its unique ID. Returns nullptr if not found.
	[[nodiscard]] const StoreOffer* findOffer(uint32_t id) const noexcept;

	/// All categories in catalog order.
	[[nodiscard]] std::span<const StoreCategory> categories() const noexcept;

	/// Home banners.
	[[nodiscard]] std::span<const StoreBanner> banners() const noexcept;

	/// Banner rotation delay in seconds.
	[[nodiscard]] uint8_t bannerDelay() const noexcept { return bannerDelay_; }

	/// Find the outfit offer data for a given lookType (male or female).
	/// Used by ProtocolGame to check store outfit ownership without a separate XML parser.
	struct OutfitOfferInfo
	{
		uint32_t offerId = 0;
		uint8_t addons = 3;
	};
	[[nodiscard]] const OutfitOfferInfo* findOutfitByLookType(uint16_t lookType) const noexcept;

private:
	StoreCatalog() = default;

	std::vector<StoreCategory> categories_;
	std::vector<StoreBanner> banners_;
	uint8_t bannerDelay_ = 10;

	/// O(1) offer ID → pointer (valid as long as this catalog is alive).
	std::unordered_map<uint32_t, const StoreOffer*> offerById_;

	/// lookType → outfit offer info, for replacing the duplicate parser in protocolgame.cpp.
	std::unordered_map<uint16_t, OutfitOfferInfo> outfitByLookType_;
};

/// Global catalog manager — holds the current active catalog snapshot.
class StoreManager final
{
public:
	static StoreManager& getInstance();

	/// Load the catalog from XML. Called during server startup.
	/// Returns false if loading fails fatally.
	[[nodiscard]] bool loadCatalog(std::string_view path = "data/store/gamestore.xml");

	/// Get the current immutable catalog snapshot.
	[[nodiscard]] std::shared_ptr<const StoreCatalog> catalogSnapshot() const noexcept;

private:
	StoreManager() = default;
	std::shared_ptr<const StoreCatalog> catalog_;
};

#endif // FS_STORE_CATALOG_H
