// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "store/store_daily_offers.h"

#include "logger.h"
#include "store/store_catalog.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <pugixml.hpp>
#include <unordered_set>

namespace {

bool isConfiguredState(StoreHighlightState state, StoreDailyHighlightMode mode) noexcept
{
	if (state != StoreHighlightState::Sale && state != StoreHighlightState::Timed) {
		return false;
	}
	return mode == StoreDailyHighlightMode::Mixed ||
	       (mode == StoreDailyHighlightMode::Sale && state == StoreHighlightState::Sale) ||
	       (mode == StoreDailyHighlightMode::Timed && state == StoreHighlightState::Timed);
}

std::vector<uint32_t> sortedOfferIds(const StoreDailyOffersSnapshot& snapshot)
{
	std::vector<uint32_t> ids;
	ids.reserve(snapshot.offers.size());
	for (const auto& [offerId, unused] : snapshot.offers) {
		(void)unused;
		ids.push_back(offerId);
	}
	std::ranges::sort(ids);
	return ids;
}

} // namespace

const StoreDailyOffer* StoreDailyOffersSnapshot::find(uint32_t offerId) const noexcept
{
	const auto it = offers.find(offerId);
	return it != offers.end() ? &it->second : nullptr;
}

StoreDailyOffers::StoreDailyOffers() : StoreDailyOffers(std::random_device{}())
{
}

StoreDailyOffers::StoreDailyOffers(uint32_t randomSeed) : random_(randomSeed)
{
}

uint32_t StoreDailyOffers::discountedPrice(uint32_t basePrice,
	                                        uint8_t discountPercent) noexcept
{
	if (basePrice == 0 || discountPercent == 0) {
		return basePrice;
	}

	const uint64_t percentage = 100U - std::min<uint8_t>(discountPercent, 99);
	const uint64_t discounted = (static_cast<uint64_t>(basePrice) * percentage) / 100U;
	return static_cast<uint32_t>(std::max<uint64_t>(discounted, 1));
}

bool StoreDailyOffers::isEligible(const StoreOffer& offer) const noexcept
{
	if (!offer.dailyEligible || offer.price == 0 || offer.state != StoreHighlightState::None) {
		return false;
	}

	// Daily purchase packets carry only the existing offer ID. Offers requiring
	// extra client input, and Astra-only systems that can be hidden by config,
	// must stay in their regular category purchase flow.
	return offer.type != StoreOfferType::ChangeName &&
	       !isHirelingOfferType(offer.type) &&
	       !isTaskBoardOfferType(offer.type) &&
	       offer.type != StoreOfferType::BattlePass;
}

void StoreDailyOffers::clear() noexcept
{
	active_ = {};
	previousOfferIds_.clear();
}

void StoreDailyOffers::configure(std::shared_ptr<const StoreCatalog> catalog,
	                              uint32_t nowTimestamp)
{
	std::scoped_lock lock(mutex_);
	clear();
	catalog_ = std::move(catalog);
	if (!catalog_) {
		config_ = {};
		return;
	}

	config_ = catalog_->dailyOffersConfig();
	if (!config_.enabled) {
		return;
	}

	const bool loaded = loadState(nowTimestamp);
	const bool canReuse = loaded && !config_.rotateOnStartup &&
	                      active_.validUntilTimestamp > nowTimestamp;
	if (!canReuse) {
		if (!active_.offers.empty()) {
			previousOfferIds_ = sortedOfferIds(active_);
		}
		rotate(nowTimestamp);
	}

	LOG_INFO(fmt::format(
	    "[StoreDailyOffers] Active rotation has {} offer(s) and expires at {}.",
	    active_.offers.size(), active_.validUntilTimestamp));
}

StoreDailyOffersSnapshot StoreDailyOffers::snapshot(uint32_t nowTimestamp)
{
	std::scoped_lock lock(mutex_);
	if (!config_.enabled || !catalog_) {
		return {};
	}

	if (active_.validUntilTimestamp <= nowTimestamp) {
		previousOfferIds_ = sortedOfferIds(active_);
		rotate(nowTimestamp);
	}
	return active_;
}

bool StoreDailyOffers::loadState(uint32_t nowTimestamp)
{
	(void)nowTimestamp;
	pugi::xml_document document;
	const auto result = document.load_file(config_.stateFile.c_str());
	if (!result) {
		return false;
	}

	const auto root = document.child("dailyOffers");
	const uint32_t expiresAt = root.attribute("expiresAt").as_uint(0);
	if (!root || expiresAt == 0) {
		return false;
	}
	if (root.attribute("offerCount").as_uint(0) != config_.offerCount ||
	    root.attribute("rotationSeconds").as_uint(0) != config_.rotationSeconds ||
	    root.attribute("minDiscountPercent").as_uint(100) != config_.minimumDiscountPercent ||
	    root.attribute("maxDiscountPercent").as_uint(100) != config_.maximumDiscountPercent ||
	    root.attribute("highlightMode").as_uint(255) != static_cast<uint8_t>(config_.highlightMode)) {
		return false;
	}

	std::size_t eligibleCount = 0;
	for (const auto& category : catalog_->categories()) {
		eligibleCount += std::ranges::count_if(category.offers, [this](const StoreOffer& offer) {
			return isEligible(offer);
		});
	}
	const std::size_t expectedCount = std::min<std::size_t>(config_.offerCount, eligibleCount);

	StoreDailyOffersSnapshot loadedState;
	loadedState.validUntilTimestamp = expiresAt;
	std::unordered_set<uint32_t> seenIds;
	for (const auto offerNode : root.children("offer")) {
		const uint32_t offerId = offerNode.attribute("id").as_uint(0);
		const uint32_t discount = offerNode.attribute("discountPercent").as_uint(100);
		const auto parsedState = parseStoreHighlightState(offerNode.attribute("state").as_string(""));
		const StoreOffer* offer = catalog_->findOffer(offerId);
		if (!offer || !isEligible(*offer) || !seenIds.insert(offerId).second ||
		    discount < config_.minimumDiscountPercent ||
		    discount > config_.maximumDiscountPercent || discount > 99 ||
		    !parsedState || !isConfiguredState(*parsedState, config_.highlightMode)) {
			LOG_WARN("[StoreDailyOffers] Ignoring invalid persisted rotation.");
			return false;
		}

		loadedState.offers.emplace(offerId, StoreDailyOffer{
		    .offerId = offerId,
		    .price = discountedPrice(offer->price, static_cast<uint8_t>(discount)),
		    .validUntilTimestamp = expiresAt,
		    .discountPercent = static_cast<uint8_t>(discount),
		    .state = *parsedState,
		});
	}

	if (loadedState.offers.size() != expectedCount) {
		return false;
	}

	std::unordered_set<uint32_t> previousIds;
	for (const auto offerNode : root.child("previous").children("offer")) {
		const uint32_t offerId = offerNode.attribute("id").as_uint(0);
		if (catalog_->findOffer(offerId) && previousIds.insert(offerId).second) {
			previousOfferIds_.push_back(offerId);
		}
	}

	active_ = std::move(loadedState);
	return true;
}

void StoreDailyOffers::rotate(uint32_t nowTimestamp)
{
	active_ = {};
	if (!catalog_ || config_.offerCount == 0) {
		return;
	}

	std::unordered_set<uint32_t> previous(previousOfferIds_.begin(), previousOfferIds_.end());
	std::vector<const StoreOffer*> preferred;
	std::vector<const StoreOffer*> repeated;
	for (const auto& category : catalog_->categories()) {
		for (const auto& offer : category.offers) {
			if (!isEligible(offer)) {
				continue;
			}
			(previous.contains(offer.id) ? repeated : preferred).push_back(&offer);
		}
	}

	std::ranges::shuffle(preferred, random_);
	std::ranges::shuffle(repeated, random_);
	preferred.insert(preferred.end(), repeated.begin(), repeated.end());
	const std::size_t selectedCount = std::min<std::size_t>(config_.offerCount, preferred.size());
	if (selectedCount == 0) {
		LOG_WARN("[StoreDailyOffers] No eligible catalog offers are available for rotation.");
		return;
	}

	const uint64_t expiry = std::min<uint64_t>(
	    static_cast<uint64_t>(nowTimestamp) + config_.rotationSeconds,
	    std::numeric_limits<uint32_t>::max());
	active_.validUntilTimestamp = static_cast<uint32_t>(expiry);

	std::uniform_int_distribution<uint16_t> discountDistribution(
	    config_.minimumDiscountPercent, config_.maximumDiscountPercent);
	std::uniform_int_distribution<unsigned int> stateDistribution(0, 1);
	for (std::size_t index = 0; index < selectedCount; ++index) {
		const StoreOffer& offer = *preferred[index];
		const auto discount = static_cast<uint8_t>(discountDistribution(random_));
		StoreHighlightState state = StoreHighlightState::Sale;
		if (config_.highlightMode == StoreDailyHighlightMode::Timed ||
		    (config_.highlightMode == StoreDailyHighlightMode::Mixed && stateDistribution(random_) != 0)) {
			state = StoreHighlightState::Timed;
		}

		active_.offers.emplace(offer.id, StoreDailyOffer{
		    .offerId = offer.id,
		    .price = discountedPrice(offer.price, discount),
		    .validUntilTimestamp = active_.validUntilTimestamp,
		    .discountPercent = discount,
		    .state = state,
		});
	}

	saveState();
}

void StoreDailyOffers::saveState() const
{
	if (config_.stateFile.empty() || active_.offers.empty()) {
		return;
	}

	pugi::xml_document document;
	auto root = document.append_child("dailyOffers");
	root.append_attribute("expiresAt") = active_.validUntilTimestamp;
	root.append_attribute("offerCount") = config_.offerCount;
	root.append_attribute("rotationSeconds") = config_.rotationSeconds;
	root.append_attribute("minDiscountPercent") =
	    static_cast<uint32_t>(config_.minimumDiscountPercent);
	root.append_attribute("maxDiscountPercent") =
	    static_cast<uint32_t>(config_.maximumDiscountPercent);
	root.append_attribute("highlightMode") = static_cast<uint32_t>(config_.highlightMode);
	for (const uint32_t offerId : sortedOfferIds(active_)) {
		const auto& offer = active_.offers.at(offerId);
		auto node = root.append_child("offer");
		node.append_attribute("id") = offer.offerId;
		node.append_attribute("discountPercent") = offer.discountPercent;
		node.append_attribute("state") = offer.state == StoreHighlightState::Sale ? "sale" : "timed";
	}

	if (!previousOfferIds_.empty()) {
		auto previous = root.append_child("previous");
		for (const uint32_t offerId : previousOfferIds_) {
			previous.append_child("offer").append_attribute("id") = offerId;
		}
	}

	const std::filesystem::path statePath(config_.stateFile);
	std::error_code error;
	if (statePath.has_parent_path()) {
		std::filesystem::create_directories(statePath.parent_path(), error);
		if (error) {
			LOG_ERROR(fmt::format("[StoreDailyOffers] Failed to create state directory '{}': {}",
			                      statePath.parent_path().string(), error.message()));
			return;
		}
	}

	const std::filesystem::path temporaryPath = statePath.string() + ".tmp";
	if (!document.save_file(temporaryPath.string().c_str(), "  ", pugi::format_default,
	                        pugi::encoding_utf8)) {
		LOG_ERROR(fmt::format("[StoreDailyOffers] Failed to write state file '{}'.",
		                      temporaryPath.string()));
		return;
	}

	std::filesystem::rename(temporaryPath, statePath, error);
	if (error) {
		// Windows does not replace an existing destination during rename.
		error.clear();
		std::filesystem::remove(statePath, error);
		error.clear();
		std::filesystem::rename(temporaryPath, statePath, error);
	}
	if (error) {
		std::error_code cleanupError;
		std::filesystem::remove(temporaryPath, cleanupError);
		LOG_ERROR(fmt::format("[StoreDailyOffers] Failed to replace state file '{}': {}",
		                      statePath.string(), error.message()));
	}
}
