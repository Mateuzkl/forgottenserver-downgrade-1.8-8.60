// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "market.h"

#include "database.h"
#include "item.h"

namespace {

constexpr std::string_view OFFER_COLUMNS =
	"mo.`id`, mo.`player_id`, mo.`sale`, mo.`itemtype`, mo.`amount`, mo.`created`, mo.`anonymous`, "
	"mo.`price`, mo.`tier`, mo.`attributes`, p.`name` AS `player_name`";

uint32_t clampLimit(uint32_t limit, uint32_t maximum)
{
	return std::clamp<uint32_t>(limit, 1, maximum);
}

MarketOfferRecord readOffer(const DBResult_ptr& result)
{
	MarketOfferRecord offer;
	offer.id = result->getNumber<uint32_t>("id");
	offer.playerId = result->getNumber<uint32_t>("player_id");
	offer.sale = result->getNumber<uint8_t>("sale");
	offer.itemId = result->getNumber<uint16_t>("itemtype");
	offer.amount = result->getNumber<uint32_t>("amount");
	offer.created = result->getNumber<uint32_t>("created");
	offer.anonymous = result->getNumber<uint8_t>("anonymous") != 0;
	offer.price = result->getNumber<uint32_t>("price");
	offer.tier = Market::normalizeTier(result->getNumber<uint32_t>("tier"));
	offer.attributes = std::string(result->getString("attributes"));
	offer.playerName = std::string(result->getString("player_name"));
	return offer;
}

std::string escapedAttributes(Database& db, const std::string& attributes)
{
	return db.escapeBlob(attributes.data(), static_cast<uint32_t>(attributes.size()));
}

bool affectedExactlyOne(Database& db, std::string query)
{
	return db.executeQuery(query) && db.getAffectedRows() == 1;
}

} // namespace

uint8_t Market::normalizeTier(uint32_t tier)
{
	return static_cast<uint8_t>(std::min<uint32_t>(tier, MAX_ITEM_TIER));
}

uint32_t Market::getOfferCount(uint32_t playerId)
{
	const DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
		"SELECT COUNT(*) AS `total` FROM `market_offers` WHERE `player_id` = {:d}", playerId));
	return result ? result->getNumber<uint32_t>("total") : 0;
}

std::optional<MarketOfferRecord> Market::getOffer(uint32_t offerId)
{
	const DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
		"SELECT {:s} FROM `market_offers` AS mo INNER JOIN `players` AS p ON p.`id` = mo.`player_id` "
		"WHERE mo.`id` = {:d} LIMIT 1",
		OFFER_COLUMNS, offerId));
	if (!result) {
		return std::nullopt;
	}
	return readOffer(result);
}

std::vector<MarketOfferRecord> Market::fetchOffers(const std::string& query, uint32_t limit)
{
	std::vector<MarketOfferRecord> offers;
	const DBResult_ptr result = Database::getInstance().storeQuery(query);
	if (!result) {
		return offers;
	}

	offers.reserve(limit);
	do {
		offers.emplace_back(readOffer(result));
	} while (offers.size() < limit && result->next());
	return offers;
}

std::vector<MarketOfferRecord> Market::getOwnOffers(uint32_t playerId, uint32_t limit)
{
	limit = clampLimit(limit, MAX_PACKET_OFFERS);
	return fetchOffers(fmt::format(
		"SELECT {:s} FROM `market_offers` AS mo INNER JOIN `players` AS p ON p.`id` = mo.`player_id` "
		"WHERE mo.`player_id` = {:d} ORDER BY mo.`created` DESC LIMIT {:d}",
		OFFER_COLUMNS, playerId, limit), limit);
}

std::vector<MarketOfferRecord> Market::getItemOffers(uint16_t itemId, uint8_t sale, uint32_t limit)
{
	limit = clampLimit(limit, MAX_PACKET_OFFERS);
	if (sale > 1) {
		return {};
	}

	const std::string_view priceOrder = sale == 0 ? "DESC" : "ASC";
	return fetchOffers(fmt::format(
		"SELECT {:s} FROM `market_offers` AS mo INNER JOIN `players` AS p ON p.`id` = mo.`player_id` "
		"WHERE mo.`itemtype` = {:d} AND mo.`sale` = {:d} ORDER BY mo.`price` {:s}, mo.`created` ASC LIMIT {:d}",
		OFFER_COLUMNS, itemId, sale, priceOrder, limit), limit);
}

std::vector<MarketOfferRecord> Market::getExpiredOffers(uint32_t createdBefore, uint32_t limit)
{
	limit = clampLimit(limit, MAX_EXPIRED_OFFERS);
	return fetchOffers(fmt::format(
		"SELECT {:s} FROM `market_offers` AS mo INNER JOIN `players` AS p ON p.`id` = mo.`player_id` "
		"WHERE mo.`created` <= {:d} ORDER BY mo.`created` ASC LIMIT {:d}",
		OFFER_COLUMNS, createdBefore, limit), limit);
}

std::vector<MarketOfferRecord> Market::getHistory(uint32_t playerId, uint32_t limit)
{
	limit = clampLimit(limit, MAX_PACKET_OFFERS);
	std::vector<MarketOfferRecord> offers;
	const DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
		"SELECT `id`, `player_id`, `sale`, `itemtype`, `amount`, `price`, `tier`, `inserted`, `state` "
		"FROM `market_history` WHERE `player_id` = {:d} ORDER BY `inserted` DESC LIMIT {:d}",
		playerId, limit));
	if (!result) {
		return offers;
	}

	offers.reserve(limit);
	do {
		MarketOfferRecord offer;
		offer.id = result->getNumber<uint32_t>("id");
		offer.playerId = result->getNumber<uint32_t>("player_id");
		offer.sale = result->getNumber<uint8_t>("sale");
		offer.itemId = result->getNumber<uint16_t>("itemtype");
		offer.amount = result->getNumber<uint32_t>("amount");
		offer.created = result->getNumber<uint32_t>("inserted");
		offer.price = result->getNumber<uint32_t>("price");
		offer.tier = normalizeTier(result->getNumber<uint32_t>("tier"));
		offer.state = result->getNumber<uint8_t>("state");
		offers.emplace_back(std::move(offer));
	} while (offers.size() < limit && result->next());
	return offers;
}

std::vector<MarketStatisticRecord> Market::getStatistics(uint16_t itemId, uint8_t sale,
	                                                                 uint32_t firstDay, uint32_t limit)
{
	limit = clampLimit(limit, MAX_STATISTIC_DAYS);
	if (sale > 1) {
		return {};
	}

	std::vector<MarketStatisticRecord> statistics;
	const DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
		"SELECT `day`, `transactions`, `total_price`, `highest_price`, `lowest_price` FROM `market_statistics` "
		"WHERE `itemtype` = {:d} AND `sale` = {:d} AND `day` >= {:d} ORDER BY `day` ASC LIMIT {:d}",
		itemId, sale, firstDay, limit));
	if (!result) {
		return statistics;
	}

	statistics.reserve(limit);
	do {
		statistics.emplace_back(MarketStatisticRecord{
			result->getNumber<uint32_t>("day"),
			result->getNumber<uint32_t>("transactions"),
			result->getNumber<uint64_t>("total_price"),
			result->getNumber<uint32_t>("highest_price"),
			result->getNumber<uint32_t>("lowest_price")
		});
	} while (statistics.size() < limit && result->next());
	return statistics;
}

bool Market::createOffer(const MarketOfferRecord& offer)
{
	if (offer.playerId == 0 || offer.itemId == 0 || offer.amount == 0 || offer.price == 0 || offer.sale > 1 ||
	    (!offer.attributes.empty() && offer.amount != 1)) {
		return false;
	}

	Database& db = Database::getInstance();
	return db.executeQuery(fmt::format(
		"INSERT INTO `market_offers` (`player_id`, `sale`, `itemtype`, `amount`, `created`, `anonymous`, "
		"`price`, `tier`, `attributes`) VALUES ({:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:s})",
		offer.playerId, offer.sale, offer.itemId, offer.amount, offer.created, offer.anonymous ? 1 : 0,
		offer.price, normalizeTier(offer.tier), escapedAttributes(db, offer.attributes)));
}

bool Market::claimOffer(uint32_t offerId, uint32_t expectedAmount, uint32_t acceptedAmount,
	                                uint32_t ownerId)
{
	if (offerId == 0 || expectedAmount == 0 || acceptedAmount == 0 || acceptedAmount > expectedAmount) {
		return false;
	}

	Database& db = Database::getInstance();
	const std::string ownerGuard = ownerId == 0 ? std::string() : fmt::format(" AND `player_id` = {:d}", ownerId);
	if (acceptedAmount == expectedAmount) {
		return affectedExactlyOne(db, fmt::format(
			"DELETE FROM `market_offers` WHERE `id` = {:d} AND `amount` = {:d}{:s}",
			offerId, expectedAmount, ownerGuard));
	}

	return affectedExactlyOne(db, fmt::format(
		"UPDATE `market_offers` SET `amount` = `amount` - {:d} "
		"WHERE `id` = {:d} AND `amount` = {:d}{:s}",
		acceptedAmount, offerId, expectedAmount, ownerGuard));
}

bool Market::restoreOffer(const MarketOfferRecord& offer, uint32_t acceptedAmount)
{
	if (offer.id == 0 || offer.playerId == 0 || offer.itemId == 0 || offer.amount == 0 || acceptedAmount == 0 ||
	    acceptedAmount > offer.amount || offer.sale > 1 || (!offer.attributes.empty() && offer.amount != 1)) {
		return false;
	}

	Database& db = Database::getInstance();
	if (acceptedAmount == offer.amount) {
		return affectedExactlyOne(db, fmt::format(
			"INSERT INTO `market_offers` (`id`, `player_id`, `sale`, `itemtype`, `amount`, `created`, `anonymous`, "
			"`price`, `tier`, `attributes`) VALUES ({:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:s})",
			offer.id, offer.playerId, offer.sale, offer.itemId, offer.amount, offer.created,
			offer.anonymous ? 1 : 0, offer.price, normalizeTier(offer.tier),
			escapedAttributes(db, offer.attributes)));
	}

	return affectedExactlyOne(db, fmt::format(
		"UPDATE `market_offers` SET `amount` = `amount` + {:d} WHERE `id` = {:d}",
		acceptedAmount, offer.id));
}

bool Market::addHistory(uint32_t playerId, uint8_t sale, uint16_t itemId, uint32_t amount,
	                                uint32_t price, uint8_t tier, uint32_t expiresAt, uint32_t inserted,
	                                uint8_t state)
{
	if (playerId == 0 || itemId == 0 || amount == 0 || price == 0 || sale > 1) {
		return false;
	}

	return Database::getInstance().executeQuery(fmt::format(
		"INSERT INTO `market_history` (`player_id`, `sale`, `itemtype`, `amount`, `price`, `tier`, "
		"`expires_at`, `inserted`, `state`) VALUES ({:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d})",
		playerId, sale, itemId, amount, price, normalizeTier(tier), expiresAt, inserted, state));
}

bool Market::refreshStatistics(uint32_t firstDay, uint8_t acceptedState)
{
	return DBTransaction::executeWithinTransactionRollbackOnFailure([=]() {
		Database& db = Database::getInstance();
		return db.executeQuery("DELETE FROM `market_statistics`") && db.executeQuery(fmt::format(
			"INSERT INTO `market_statistics` (`itemtype`, `sale`, `day`, `transactions`, `total_price`, "
			"`highest_price`, `lowest_price`) "
			"SELECT `itemtype`, `sale`, FLOOR(`inserted` / 86400) * 86400, COUNT(*), "
			"CASE WHEN SUM(`amount`) > 0 THEN FLOOR((SUM(`price` * `amount`) / SUM(`amount`)) * COUNT(*)) ELSE 0 END, "
			"MAX(`price`), MIN(`price`) FROM `market_history` WHERE `state` = {:d} AND `inserted` >= {:d} "
			"GROUP BY `itemtype`, `sale`, FLOOR(`inserted` / 86400) * 86400",
			acceptedState, firstDay));
	});
}

bool Market::creditBank(uint32_t playerId, uint64_t amount)
{
	if (amount == 0) {
		return true;
	}
	if (playerId == 0) {
		return false;
	}

	Database& db = Database::getInstance();
	return affectedExactlyOne(db, fmt::format(
		"UPDATE `players` SET `balance` = `balance` + {:d} WHERE `id` = {:d}", amount, playerId));
}

bool Market::insertInboxItems(uint32_t playerId, uint16_t itemId, uint32_t amount,
	                                      const std::string& attributes)
{
	const ItemType& itemType = Item::items[itemId];
	if (playerId == 0 || itemType.id == 0 || amount == 0 || (!attributes.empty() && amount != 1)) {
		return false;
	}

	return DBTransaction::executeWithinTransactionRollbackOnFailure([&]() {
		Database& db = Database::getInstance();
		if (!db.storeQuery(fmt::format("SELECT `id` FROM `players` WHERE `id` = {:d} FOR UPDATE", playerId))) {
			return false;
		}

		const DBResult_ptr result = db.storeQuery(fmt::format(
			"SELECT COALESCE(MAX(`sid`), 100) AS `sid` FROM `player_inboxitems` WHERE `player_id` = {:d}",
			playerId));
		if (!result) {
			return false;
		}

		uint64_t sid = result->getNumber<uint64_t>("sid") + 1;
		const uint32_t stackSize = itemType.stackable ? std::max<uint32_t>(1, itemType.stackSize) : 1;
		const uint32_t rowCount = attributes.empty() ? 1 + ((amount - 1) / stackSize) : 1;
		if (sid + rowCount > std::numeric_limits<uint32_t>::max()) {
			return false;
		}

		const std::string attributesSql = escapedAttributes(db, attributes);
		DBInsert insert(
			"INSERT INTO `player_inboxitems` (`player_id`, `sid`, `pid`, `itemtype`, `count`, `attributes`) VALUES ");
		uint32_t remaining = amount;
		while (remaining > 0) {
			const uint32_t count = std::min<uint32_t>(remaining, stackSize);
			if (!insert.addRow(fmt::format("{:d}, {:d}, 0, {:d}, {:d}, {:s}",
			                              playerId, sid++, itemId, count, attributesSql))) {
				return false;
			}
			remaining -= count;
		}
		return insert.execute();
	});
}
