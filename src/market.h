// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_MARKET_H
#define FS_MARKET_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct MarketOfferRecord
{
	uint32_t id = 0;
	uint32_t playerId = 0;
	uint8_t sale = 0;
	uint16_t itemId = 0;
	uint32_t amount = 0;
	uint32_t created = 0;
	bool anonymous = false;
	uint32_t price = 0;
	uint8_t tier = 0;
	std::string attributes;
	std::string playerName;
	uint8_t state = 0;
};

struct MarketStatisticRecord
{
	uint32_t day = 0;
	uint32_t transactions = 0;
	uint64_t totalPrice = 0;
	uint32_t highestPrice = 0;
	uint32_t lowestPrice = 0;
};

class Market
{
public:
	static constexpr uint32_t MAX_PACKET_OFFERS = 250;
	static constexpr uint32_t MAX_EXPIRED_OFFERS = 100;
	static constexpr uint32_t MAX_STATISTIC_DAYS = 30;
	static constexpr uint8_t MAX_ITEM_TIER = 10;

	[[nodiscard]] static uint8_t normalizeTier(uint32_t tier);
	[[nodiscard]] static uint32_t getOfferCount(uint32_t playerId);
	[[nodiscard]] static std::optional<MarketOfferRecord> getOffer(uint32_t offerId);
	[[nodiscard]] static std::vector<MarketOfferRecord> getOwnOffers(uint32_t playerId, uint32_t limit);
	[[nodiscard]] static std::vector<MarketOfferRecord> getItemOffers(uint16_t itemId, uint8_t sale, uint32_t limit);
	[[nodiscard]] static std::vector<MarketOfferRecord> getExpiredOffers(uint32_t createdBefore, uint32_t limit);
	[[nodiscard]] static std::vector<MarketOfferRecord> getHistory(uint32_t playerId, uint32_t limit);
	[[nodiscard]] static std::vector<MarketStatisticRecord> getStatistics(uint16_t itemId, uint8_t sale,
	                                                                    uint32_t firstDay, uint32_t limit);

	[[nodiscard]] static bool createOffer(const MarketOfferRecord& offer);
	[[nodiscard]] static bool claimOffer(uint32_t offerId, uint32_t expectedAmount, uint32_t acceptedAmount,
	                                     uint32_t ownerId = 0);
	[[nodiscard]] static bool restoreOffer(const MarketOfferRecord& offer, uint32_t acceptedAmount);
	[[nodiscard]] static bool addHistory(uint32_t playerId, uint8_t sale, uint16_t itemId, uint32_t amount,
	                                     uint32_t price, uint8_t tier, uint32_t expiresAt, uint32_t inserted,
	                                     uint8_t state);
	[[nodiscard]] static bool refreshStatistics(uint32_t firstDay, uint8_t acceptedState);
	[[nodiscard]] static bool creditBank(uint32_t playerId, uint64_t amount);
	[[nodiscard]] static bool insertInboxItems(uint32_t playerId, uint16_t itemId, uint32_t amount,
	                                           const std::string& attributes);

private:
	[[nodiscard]] static std::vector<MarketOfferRecord> fetchOffers(const std::string& query, uint32_t limit);
};

#endif // FS_MARKET_H
