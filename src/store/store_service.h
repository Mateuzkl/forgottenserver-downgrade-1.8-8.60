// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_STORE_SERVICE_H
#define FS_STORE_SERVICE_H

#include "store/store_types.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Player;
class StoreCatalog;

/// Extra data sent by the client alongside a purchase request.
struct StorePurchaseExtra
{
	std::string name;  ///< For changename / hireling
	uint8_t sex = 0;   ///< For hireling
};

/// Result of a purchase or delivery attempt.
struct StoreResult
{
	bool success = false;
	std::string message;
};

/// Per-player rate-limiting state. Lifetime follows the player session.
struct StoreRateLimit
{
	std::chrono::steady_clock::time_point lastPurchase{};
	std::chrono::steady_clock::time_point lastTransfer{};
};

/// Core store business logic — purchase state machine, delivery dispatch,
/// feature gating, coin transfer, and rate limiting.
///
/// Dependency graph:
///   StoreService → StoreCatalog (immutable snapshot)
///   StoreService → AccountCoins (atomic SQL)
///   StoreService → StoreRepository (history, rename)
///   StoreService → Player (non-owning, resolved by ID for async safety)
class StoreService final
{
public:
	static StoreService& getInstance();

	/// Attempt to purchase an offer for a player.
	/// Implements the full state machine: validate → debit → deliver → history → success.
	[[nodiscard]] StoreResult purchase(Player& player, uint32_t offerId,
	                                   const StorePurchaseExtra& extra);

	/// Transfer coins from one player's account to a target player's account.
	[[nodiscard]] StoreResult transferCoins(Player& player, std::string_view targetName,
	                                        uint32_t amount);

	/// Get or create rate-limit state for a player.
	StoreRateLimit& getRateLimit(uint32_t playerId);

	/// Clean up rate-limit state on player logout.
	void clearRateLimit(uint32_t playerId);

	/// Purchase cooldown duration.
	static constexpr auto PurchaseCooldown = std::chrono::seconds{2};
	static constexpr auto TransferCooldown = std::chrono::seconds{2};

private:
	StoreService() = default;

	/// Check if a player can see/purchase a specific offer type.
	[[nodiscard]] bool isOfferAvailable(const Player& player, const StoreOffer& offer) const;

	/// Deliver a purchased offer. Returns error message or empty on success.
	[[nodiscard]] std::string deliverOffer(Player& player, const StoreOffer& offer,
	                                       const StorePurchaseExtra& extra);

	/// Per-type delivery implementations.
	[[nodiscard]] std::string deliverPremium(Player& player, const StoreOffer& offer);
	[[nodiscard]] std::string deliverBlessing(Player& player, const StoreOffer& offer);
	[[nodiscard]] std::string deliverOutfit(Player& player, const StoreOffer& offer);
	[[nodiscard]] std::string deliverMount(Player& player, const StoreOffer& offer);
	[[nodiscard]] std::string deliverXpBoost(Player& player, const StoreOffer& offer);
	[[nodiscard]] std::string deliverItem(Player& player, const StoreOffer& offer);
	[[nodiscard]] std::string deliverHouseItem(Player& player, const StoreOffer& offer);
	[[nodiscard]] std::string deliverNameChange(Player& player, const StoreOffer& offer,
	                                             const StorePurchaseExtra& extra);
	[[nodiscard]] std::string deliverSexChange(Player& player, const StoreOffer& offer);
	[[nodiscard]] std::string deliverHireling(Player& player, const StoreOffer& offer,
	                                           const StorePurchaseExtra& extra);
	[[nodiscard]] std::string deliverHirelingSkill(Player& player, const StoreOffer& offer);
	[[nodiscard]] std::string deliverHirelingOutfit(Player& player, const StoreOffer& offer);

	/// Lua callback bridge for subsystems that only exist in Lua.
	[[nodiscard]] std::string deliverViaLuaCallback(Player& player, const StoreOffer& offer,
	                                                 const StorePurchaseExtra& extra);

	std::unordered_map<uint32_t, StoreRateLimit> rateLimits_;
};

#endif // FS_STORE_SERVICE_H
