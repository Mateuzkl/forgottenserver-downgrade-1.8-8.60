// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "market.h"

#include "database.h"
#include "item.h"
#include "game.h"
#include "inbox.h"
#include "player.h"
#include "save_manager.h"
#include "tasks.h"
#include "depotlocker.h"
#include "tile.h"

#include <tuple>
#include <unordered_set>

extern Game g_game;

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

namespace {

std::optional<bool> marketResult(SaveManager::AtomicSaveResult result, Player* player)
{
	if (result == SaveManager::AtomicSaveResult::Indeterminate) {
		// No further packet or logout save may overwrite an uncertain commit.
		if (player) {
			g_game.kickPlayer(player->getID(), false);
		}
		return std::nullopt;
	}
	return result == SaveManager::AtomicSaveResult::Committed;
}

bool enqueueDelivery(const std::string& eventKey, uint32_t offerId, uint32_t playerId,
                     uint32_t depotId, uint16_t itemId, uint32_t amount, const std::string& attributes, uint64_t bankCredit)
{
	if (depotId > Market::MAX_INBOX_DEPOT_ID) {
		LOG_ERROR(fmt::format("[Market] Invalid inbox depot={} guid={}; offer unchanged", depotId, playerId));
		return false;
	}
	Database& db = Database::getInstance();
	const auto query = fmt::format(
	    "INSERT INTO `market_delivery_pending` (`event_key`, `offer_id`, `player_id`, `depot_id`, "
	    "`itemtype`, `amount`, `attributes`, `bank_credit`, `created_at`) "
	    "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, UNIX_TIMESTAMP())",
	    db.escapeString(eventKey), offerId, playerId, depotId, itemId, amount,
	    escapedAttributes(db, attributes), bankCredit);
	// Include the protocol command byte and leave space for packet framing.
	return query.size() + 1024 < db.getMaxPacketSize() && db.executeQuery(query);
}

} // namespace

bool Market::isPlayerReady(Player* player)
{
	return player && g_saveManager.canSavePlayersSync({player});
}

uint32_t Market::getInboxDepotId(const Player& player)
{
	// The home town is not the player's current city. Read the actual nearby
	// map locker, even when the player moved cities without opening it first.
	const Position& position = player.getPosition();
	std::optional<std::tuple<int, int, uint32_t>> best;
	for (int dx = -1; dx <= 1; ++dx) {
		for (int dy = -1; dy <= 1; ++dy) {
			const int x = position.x + dx;
			const int y = position.y + dy;
			if (x < 0 || y < 0 || x > UINT16_MAX || y > UINT16_MAX) {
				continue;
			}
			const Tile* tile = player.getTile() ?
			    g_game.map.getTile(static_cast<uint16_t>(x), static_cast<uint16_t>(y), position.z) : nullptr;
			const auto* items = tile ? tile->getItemList() : nullptr;
			if (!items) {
				continue;
			}
			for (const auto& item : *items) {
				const Container* container = item->getContainer();
				const DepotLocker* depot = container ? container->getDepotLocker() : nullptr;
				if (!depot) {
					continue;
				}
				const uint32_t id = depot->getDepotId();
				const auto candidate = std::tuple{std::abs(dx) + std::abs(dy),
				    player.getLastDepotId() == static_cast<int32_t>(id) ? 0 : 1, id};
				if (!best || candidate < *best) {
					best = candidate;
				}
			}
		}
	}
	if (best) {
		return std::get<2>(*best);
	}
	if (player.getLastDepotId() >= 0) {
		return static_cast<uint32_t>(player.getLastDepotId());
	}
	return player.getTown() ? player.getTown()->getID() : 0;
}

std::optional<MarketDeliveryStatus> Market::getDeliveryStatus(uint32_t playerId, uint32_t offerId)
{
	auto row = Database::getInstance().storeQuery(fmt::format(
	    "SELECT `id`, `depot_id`, `itemtype`, `amount`, `bank_credit`, `state` FROM `market_delivery_pending` "
	    "WHERE `player_id` = {} AND `offer_id` = {} ORDER BY `id` DESC LIMIT 1", playerId, offerId));
	if (!row) {
		return std::nullopt;
	}
	return MarketDeliveryStatus{row->getNumber<uint64_t>("id"), row->getNumber<uint32_t>("depot_id"),
	    row->getNumber<uint16_t>("itemtype"), row->getNumber<uint32_t>("amount"),
	    row->getNumber<uint64_t>("bank_credit"), row->getString("state") == "applied"};
}

bool Market::buildDeliveryItems(uint16_t itemId, uint32_t amount, const std::string& attributes,
                                std::vector<std::shared_ptr<Item>>& items)
{
	items.clear();
	if (Item::items[itemId].id == 0 || amount == 0 || amount > MAX_DELIVERY_AMOUNT ||
	    attributes.size() > MAX_DELIVERY_ATTRIBUTES || (!attributes.empty() && amount != 1)) {
		return false;
	}
	std::vector<std::shared_ptr<Item>> parsed;
	const ItemType& type = Item::items[itemId];
	const uint32_t stackSize = type.stackable ? std::max<uint32_t>(1, type.stackSize) : 1;
	for (uint32_t remaining = amount; remaining > 0;) {
		const uint16_t count = static_cast<uint16_t>(std::min(remaining, stackSize));
		auto item = Item::CreateItem(itemId, count);
		if (!item) {
			return false;
		}
		if (!attributes.empty()) {
			PropStream stream;
			stream.init(attributes.data(), attributes.size());
			if (!item->unserializeAttr(stream) || stream.size() != 0 ||
			    (type.stackable && item->getItemCount() != count)) {
				return false;
			}
		}
		parsed.push_back(std::move(item));
		remaining -= count;
	}
	items = std::move(parsed);
	return true;
}

std::optional<bool> Market::createOfferAndSave(Player* player, const MarketOfferRecord& offer)
{
	if (!player || player->getGUID() != offer.playerId || !isPlayerReady(player)) {
		return false;
	}
	std::vector<std::shared_ptr<Item>> items;
	if (offer.price == 0 || uint64_t{offer.price} * offer.amount > 999999999 ||
	    !buildDeliveryItems(offer.itemId, offer.amount, offer.attributes, items)) {
		return false;
	}
	return marketResult(g_saveManager.savePlayersAtomic({player}, [&] { return createOffer(offer); }), player);
}

std::optional<bool> Market::settleOffer(Player* actor, uint32_t offerId, uint32_t expectedAmount,
    uint32_t amount, uint8_t state, uint32_t duration, uint8_t tier, const std::string& attributes)
{
	// State values are the custom Market wire/history values: cancel=1, expire=2, accept=3.
	if (!g_dispatcher.isDispatcherThread() || state < 1 || state > 3 || offerId == 0 ||
	    amount == 0 || amount > expectedAmount || (state != 3 && amount != expectedAmount) ||
	    (state != 2 && !actor) || (state == 3 && !isPlayerReady(actor))) {
		return false;
	}
	const uint32_t actorId = actor ? actor->getGUID() : 0;
	const auto key = fmt::format("offer:{}:remaining:{}:amount:{}:actor:{}:state:{}",
	    offerId, expectedAmount, amount, actorId, state);
	std::optional<uint32_t> destination;
	const std::vector<Player*> players = state == 3 ? std::vector<Player*>{actor} : std::vector<Player*>{};
	const auto result = g_saveManager.savePlayersAtomic(players, [&] {
		Database& db = Database::getInstance();
		auto row = db.storeQuery(fmt::format(
		    "SELECT {} FROM `market_offers` AS mo INNER JOIN `players` AS p ON p.`id` = mo.`player_id` "
		    "WHERE mo.`id` = {} FOR UPDATE", OFFER_COLUMNS, offerId));
		if (!row) {
			// Repeating a completed return is harmless. Acceptors have already reserved
			// resources in Lua, so a stale accept must fail and undo that new reservation.
			if (state == 3 || db.getLastErrno() != 0) {
				return false;
			}
			auto prior = db.storeQuery(fmt::format(
			    "SELECT `id`, `depot_id` FROM `market_delivery_pending` WHERE `event_key` = {}", db.escapeString(key)));
			if (prior) {
				destination = prior->getNumber<uint32_t>("depot_id");
			}
			return prior != nullptr;
		}
		const MarketOfferRecord offer = readOffer(row);
		if (offer.playerId == actorId) {
			destination = getInboxDepotId(*actor);
		} else if (auto owner = g_game.getPlayerByGUID(offer.playerId)) {
			destination = getInboxDepotId(*owner);
		} else {
			// An offline character has no current position/depot session. Persist
			// its home town now so subsequent login/movement cannot change delivery.
			auto ownerRow = db.storeQuery(fmt::format("SELECT `town_id` FROM `players` WHERE `id` = {}", offer.playerId));
			if (!ownerRow) {
				return false;
			}
			destination = ownerRow->getNumber<uint32_t>("town_id");
		}
		if (offer.amount != expectedAmount || offer.sale > 1 ||
		    (state == 1 && offer.playerId != actorId) || (state == 3 && offer.playerId == actorId) ||
		    (state == 2 && uint64_t{offer.created} + duration > static_cast<uint64_t>(time(nullptr)))) {
			return false;
		}
		const uint64_t total = uint64_t{offer.price} * amount;
		if (total == 0 || total > 999999999) {
			return false;
		}
		const bool deliversItem = state == 3 || offer.sale == 1;
		const std::string& deliveryAttributes = state == 3 && offer.sale == 0 ? attributes : offer.attributes;
		std::vector<std::shared_ptr<Item>> items;
		if (deliversItem && !buildDeliveryItems(offer.itemId, amount, deliveryAttributes, items)) {
			return false;
		}
		if (!claimOffer(offer.id, expectedAmount, amount, state == 1 ? actorId : 0)) {
			return false;
		}
		bool queued = false;
		if (state != 3) {
			queued = enqueueDelivery(key, offer.id, offer.playerId, *destination,
			    offer.sale == 1 ? offer.itemId : 0, offer.sale == 1 ? amount : 0,
			    offer.sale == 1 ? offer.attributes : std::string{}, offer.sale == 0 ? total : 0);
		} else if (offer.sale == 1) {
			queued = enqueueDelivery(key + ":item", offer.id, actorId, getInboxDepotId(*actor), offer.itemId, amount, offer.attributes, 0) &&
			         enqueueDelivery(key + ":bank", offer.id, offer.playerId, *destination, 0, 0, {}, total);
		} else {
			queued = enqueueDelivery(key + ":item", offer.id, offer.playerId, *destination, offer.itemId, amount, attributes, 0);
		}
		return queued && addHistory(offer.playerId, offer.sale, offer.itemId, amount, offer.price,
		    state == 3 ? normalizeTier(tier) : offer.tier,
		    static_cast<uint32_t>(std::min<uint64_t>(uint64_t{offer.created} + duration, UINT32_MAX)),
		    static_cast<uint32_t>(time(nullptr)), state);
	});
	LOG_INFO(fmt::format("[Market] event={} offer={} actor={} owner_depot={} result={}",
	    key, offerId, actorId, destination.value_or(0), static_cast<int>(result)));
	return marketResult(result, state == 3 ? actor : nullptr);
}

bool Market::processDeliveries(Player* player)
{
	if (!isPlayerReady(player)) {
		return false;
	}
	Database& db = Database::getInstance();
	const uint32_t guid = player->getGUID();
	auto rows = db.storeQuery(fmt::format(
	    "SELECT `id`, `event_key`, `offer_id`, `depot_id`, `itemtype`, `amount`, `attributes`, `bank_credit` "
	    "FROM `market_delivery_pending` WHERE `player_id` = {} AND `state` = 'pending' ORDER BY `id` LIMIT 100", guid));
	if (!rows) {
		return db.getLastErrno() == 0;
	}
	struct Delivery {
		uint64_t id;
		uint32_t offerId;
		uint32_t depotId;
		uint32_t amount;
		uint64_t bankCredit;
		Inbox* inbox;
		std::vector<std::shared_ptr<Item>> items;
	};
	std::vector<Delivery> deliveries;
	std::unordered_set<Inbox*> inboxes;
	std::string ids;
	uint64_t bankCredit = 0;
	size_t itemCount = 0;
	do {
		const uint16_t itemId = rows->getNumber<uint16_t>("itemtype");
		const uint32_t amount = rows->getNumber<uint32_t>("amount");
		const uint32_t depotId = rows->getNumber<uint32_t>("depot_id");
		const uint64_t credit = rows->getNumber<uint64_t>("bank_credit");
		const std::string attributes{rows->getString("attributes")};
		std::vector<std::shared_ptr<Item>> batch;
		if (depotId > MAX_INBOX_DEPOT_ID ||
		    (itemId == 0 ? (amount != 0 || credit == 0 || !attributes.empty()) :
		                    (credit != 0 || !buildDeliveryItems(itemId, amount, attributes, batch))) ||
		    credit > UINT64_MAX - bankCredit || bankCredit + credit > UINT64_MAX - player->getBankBalance()) {
			LOG_ERROR(fmt::format("[Market] Invalid pending delivery event={} guid={} depot={}; preserved for recovery",
			    rows->getString("event_key"), guid, depotId));
			return false;
		}
		if (!deliveries.empty() && itemCount + batch.size() > MAX_DELIVERY_AMOUNT) {
			break;
		}
		Inbox* inbox = batch.empty() ? nullptr : player->getInbox(depotId);
		if (!batch.empty() && !inbox) {
			LOG_ERROR(fmt::format("[Market] Inbox unavailable guid={} depot={}; delivery still pending", guid, depotId));
			return false;
		}
		const uint64_t id = rows->getNumber<uint64_t>("id");
		itemCount += batch.size();
		bankCredit += credit;
		if (!ids.empty()) {
			ids += ',';
		}
		ids += std::to_string(id);
		if (inbox) {
			inboxes.insert(inbox);
		}
		deliveries.push_back(Delivery{id, rows->getNumber<uint32_t>("offer_id"), depotId,
		    amount, credit, inbox, std::move(batch)});
	} while (rows->next());

	const uint64_t oldBalance = player->getBankBalance();
	const auto rollback = [&] {
		for (const auto& delivery : deliveries) {
			for (const auto& item : delivery.items) {
				if (item->getParent() == delivery.inbox && delivery.inbox->getThingIndex(item.get()) >= 0) {
					delivery.inbox->removeThing(item.get(), item->getItemCount());
				}
			}
		}
		player->setBankBalance(oldBalance);
		for (Inbox* inbox : inboxes) {
			player->onSendContainer(inbox);
		}
	};
	for (const auto& delivery : deliveries) {
		for (const auto& item : delivery.items) {
			// Container::addThing can silently refuse insertion when its configured
			// capacity is zero. Inbox delivery is unlimited: verify actual ownership
			// before allowing any receipt to become applied. Never merge old stacks.
			delivery.inbox->internalAddThing(item.get());
			if (item->getParent() != delivery.inbox || delivery.inbox->getThingIndex(item.get()) < 0) {
				rollback();
				LOG_ERROR(fmt::format("[Market] Item insertion failed guid={} delivery={} depot={}; still pending",
				    guid, delivery.id, delivery.depotId));
				return false;
			}
		}
	}
	player->setBankBalance(oldBalance + bankCredit);
	const auto result = g_saveManager.savePlayersAtomic({player}, [&] {
		return db.executeQuery(fmt::format(
		    "UPDATE `market_delivery_pending` SET `state` = 'applied', `applied_at` = UNIX_TIMESTAMP() "
		    "WHERE `player_id` = {} AND `state` = 'pending' AND `id` IN ({})", guid, ids)) &&
		    db.getAffectedRows() == deliveries.size();
	});
	if (result == SaveManager::AtomicSaveResult::Failed) {
		rollback();
		LOG_ERROR(fmt::format("[Market] Player save failed guid={} delivery_ids={}; items/payment still pending", guid, ids));
	}
	if (result == SaveManager::AtomicSaveResult::Committed) {
		// Refresh the owner's open inbox directly; its locker need not be parented
		// to a visible map tile for the client to receive the updated contents.
		for (Inbox* inbox : inboxes) {
			player->onSendContainer(inbox);
		}
		for (const auto& delivery : deliveries) {
			if (delivery.inbox) {
				const Town* town = g_game.map.towns.getTown(delivery.depotId);
				const std::string name = town ? town->getName() : fmt::format("town {}", delivery.depotId);
				player->sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, fmt::format(
				    "Market: {}x {} entregue(s) no Inbox de {} (town/depot ID {}). Oferta #{}.",
				    delivery.amount, delivery.items.front()->getName(), name, delivery.depotId, delivery.offerId));
			} else {
				player->sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, fmt::format(
				    "Market: {} gold creditado(s) no banco. Oferta #{}.", delivery.bankCredit, delivery.offerId));
			}
			LOG_INFO(fmt::format("[Market] delivery={} guid={} offer={} depot={} amount={} bank={} state=applied",
			    delivery.id, guid, delivery.offerId, delivery.depotId, delivery.amount, delivery.bankCredit));
		}
	}
	return marketResult(result, player).value_or(false);
}

void Market::processOnlineDeliveries()
{
	const auto players = g_game.getPlayers();
	// One indexed lookup per batch, instead of querying WAL and pending rights
	// for every online player when there are no Market deliveries at all.
	for (size_t first = 0; first < players.size(); first += 256) {
		std::string ids;
		for (size_t i = first; i < std::min(first + 256, players.size()); ++i) {
			if (!ids.empty()) {
				ids += ',';
			}
			ids += std::to_string(players[i]->getGUID());
		}
		auto rows = Database::getInstance().storeQuery(fmt::format(
		    "SELECT DISTINCT `player_id` FROM `market_delivery_pending` "
		    "WHERE `state` = 'pending' AND `player_id` IN ({})", ids));
		if (!rows) {
			continue;
		}
		do {
			auto player = g_game.getPlayerByGUID(rows->getNumber<uint32_t>("player_id"));
			(void)processDeliveries(player.get());
		} while (rows->next());
	}
}
