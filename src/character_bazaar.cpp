// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "character_bazaar.h"

#include "configmanager.h"
#include "database.h"
#include "game.h"
#include "player.h"
#include "scheduler.h"
#include "tile.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <limits>

namespace {

using CharacterBazaar::AUCTION_STATUS_ACTIVE;

constexpr uint8_t AUCTION_STATUS_FINISHED = 2;
constexpr uint8_t AUCTION_STATUS_CANCELLED = 3;
constexpr uint32_t FINALIZATION_INTERVAL_MS = 60 * 1000;

uint32_t asUnsignedConfig(ConfigManager::Integer config, uint32_t fallback, uint32_t maximum = UINT32_MAX)
{
	const int64_t value = ConfigManager::getInteger(config);
	if (value < 0) {
		return fallback;
	}
	return static_cast<uint32_t>(std::min<int64_t>(value, maximum));
}

uint32_t getMinimumLevel()
{
	return std::max<uint32_t>(1, asUnsignedConfig(ConfigManager::CHARACTER_BAZAAR_MIN_LEVEL, 50));
}

uint32_t getMinimumPrice()
{
	return std::max<uint32_t>(1, asUnsignedConfig(ConfigManager::CHARACTER_BAZAAR_MIN_PRICE, 100));
}

uint32_t getAuctionFee()
{
	return asUnsignedConfig(ConfigManager::CHARACTER_BAZAAR_AUCTION_FEE, 50);
}

uint32_t getCommissionPercent()
{
	return std::min<uint32_t>(100, asUnsignedConfig(ConfigManager::CHARACTER_BAZAAR_COMMISSION_PERCENT, 10, 100));
}

uint32_t getMinimumDuration()
{
	const uint64_t seconds =
	    static_cast<uint64_t>(asUnsignedConfig(ConfigManager::CHARACTER_BAZAAR_MIN_DURATION_HOURS, 24)) * 60 * 60;
	return std::max<uint32_t>(60, static_cast<uint32_t>(std::min<uint64_t>(seconds, UINT32_MAX)));
}

uint32_t getMaximumDuration()
{
	const uint64_t seconds = static_cast<uint64_t>(asUnsignedConfig(ConfigManager::CHARACTER_BAZAAR_MAX_DURATION_DAYS, 7)) *
	                         24 * 60 * 60;
	return static_cast<uint32_t>(std::min<uint64_t>(seconds, UINT32_MAX));
}

bool isEnabled()
{
	return ConfigManager::getBoolean(ConfigManager::CHARACTER_BAZAAR_ENABLED);
}

bool queryHasRows(const std::string& query)
{
	return static_cast<bool>(Database::getInstance().storeQuery(query));
}

bool hasActiveAuctionForAccount(uint32_t accountId)
{
	return queryHasRows(fmt::format(
	    "SELECT `id` FROM `character_auctions` WHERE `seller_account_id` = {:d} AND `status` = {:d} LIMIT 1", accountId,
	    AUCTION_STATUS_ACTIVE));
}

bool lockAccountForAuction(uint32_t accountId)
{
	return static_cast<bool>(Database::getInstance().storeQuery(
	    fmt::format("SELECT `id` FROM `accounts` WHERE `id` = {:d} FOR UPDATE", accountId)));
}

bool addHistoryInternal(uint32_t auctionId, const std::string& action, uint32_t accountId, uint32_t playerId,
                        uint64_t amount, const std::string& message)
{
	Database& db = Database::getInstance();
	return db.executeQuery(fmt::format(
	    "INSERT INTO `character_auction_history` (`auction_id`, `action`, `account_id`, `player_id`, `amount`, `message`, "
	    "`created_at`) VALUES ({:d}, {:s}, {:d}, {:d}, {:d}, {:s}, {:d})",
	    auctionId, db.escapeString(action), accountId, playerId, amount, db.escapeString(message), time(nullptr)));
}

std::string sanitizeDescription(std::string description)
{
	for (char& character : description) {
		if (std::iscntrl(static_cast<unsigned char>(character))) {
			character = ' ';
		}
	}
	return description;
}

bool finalizeAuction(uint32_t auctionId)
{
	const time_t currentTime = time(nullptr);
	return DBTransaction::executeWithinTransactionRollbackOnFailure([auctionId, currentTime]() {
		Database& db = Database::getInstance();
		auto result = db.storeQuery(fmt::format(
		    "SELECT `player_id`, `seller_account_id`, COALESCE(`current_bidder_account_id`, 0) AS `bidder_account_id`, "
		    "`current_bid`, `commission_percent` FROM `character_auctions` WHERE `id` = {:d} AND `status` = {:d} "
		    "AND `end_at` <= {:d} FOR UPDATE",
		    auctionId, AUCTION_STATUS_ACTIVE, currentTime));
		if (!result) {
			return true; // already finalized by another scheduler run
		}

		const uint32_t playerId = result->getNumber<uint32_t>("player_id");
		const uint32_t sellerAccountId = result->getNumber<uint32_t>("seller_account_id");
		const uint32_t bidderAccountId = result->getNumber<uint32_t>("bidder_account_id");
		const uint64_t bid = result->getNumber<uint64_t>("current_bid");
		const uint32_t commissionPercent = std::min<uint32_t>(100, result->getNumber<uint32_t>("commission_percent"));

		if (bidderAccountId == 0 || bid == 0) {
			if (!db.executeQuery(fmt::format(
			        "UPDATE `character_auctions` SET `status` = {:d}, `finished_at` = {:d} WHERE `id` = {:d} "
			        "AND `status` = {:d}",
			        AUCTION_STATUS_CANCELLED, currentTime, auctionId, AUCTION_STATUS_ACTIVE))) {
				return false;
			}
			return addHistoryInternal(auctionId, "expired_no_bid", sellerAccountId, playerId, 0,
			                          "Auction expired without a bid.");
		}

		const uint64_t commission = bid * commissionPercent / 100;
		const uint64_t sellerPayout = bid - commission;
		if (!db.executeQuery(fmt::format(
		        "UPDATE `players` SET `account_id` = {:d} WHERE `id` = {:d} AND `account_id` = {:d}", bidderAccountId,
		        playerId, sellerAccountId)) ||
		    db.getAffectedRows() != 1 || !CharacterBazaar::creditTransferableCoins(sellerAccountId, sellerPayout)) {
			return false;
		}

		if (!db.executeQuery(fmt::format(
		        "UPDATE `character_auctions` SET `status` = {:d}, `winner_account_id` = {:d}, `final_price` = {:d}, "
		        "`finished_at` = {:d} WHERE `id` = {:d} AND `status` = {:d}",
		        AUCTION_STATUS_FINISHED, bidderAccountId, bid, currentTime, auctionId, AUCTION_STATUS_ACTIVE))) {
			return false;
		}

		return addHistoryInternal(auctionId, "finished", bidderAccountId, playerId, sellerPayout,
		                          fmt::format("Auction finished for {:d} coins (commission: {:d}).", bid, commission));
	});
}

} // namespace

namespace CharacterBazaar {

bool isPlayerOnActiveAuction(uint32_t playerId)
{
	if (playerId == 0) {
		return false;
	}
	return queryHasRows(fmt::format(
	    "SELECT `id` FROM `character_auctions` WHERE `player_id` = {:d} AND `status` = {:d} LIMIT 1", playerId,
	    AUCTION_STATUS_ACTIVE));
}

uint64_t getTransferableCoins(uint32_t accountId)
{
	if (accountId == 0) {
		return 0;
	}
	auto result = Database::getInstance().storeQuery(
	    fmt::format("SELECT `tibia_coins` FROM `accounts` WHERE `id` = {:d}", accountId));
	return result ? result->getNumber<uint64_t>("tibia_coins") : 0;
}

bool debitTransferableCoins(uint32_t accountId, uint64_t amount)
{
	if (accountId == 0) {
		return false;
	}
	if (amount == 0) {
		return true;
	}
	Database& db = Database::getInstance();
	return db.executeQuery(fmt::format(
	           "UPDATE `accounts` SET `tibia_coins` = `tibia_coins` - {:d} WHERE `id` = {:d} AND `tibia_coins` >= {:d}",
	           amount, accountId, amount)) &&
	       db.getAffectedRows() == 1;
}

bool creditTransferableCoins(uint32_t accountId, uint64_t amount)
{
	if (accountId == 0) {
		return false;
	}
	if (amount == 0) {
		return true;
	}
	if (amount > MAX_TIBIA_COINS) {
		return false;
	}
	Database& db = Database::getInstance();
	return db.executeQuery(fmt::format(
	           "UPDATE `accounts` SET `tibia_coins` = `tibia_coins` + {:d} WHERE `id` = {:d} "
	           "AND `tibia_coins` <= {:d}",
	           amount, accountId, MAX_TIBIA_COINS - amount)) &&
	       db.getAffectedRows() == 1;
}

bool addHistory(uint32_t auctionId, const std::string& action, uint32_t accountId, uint32_t playerId, uint64_t amount,
	            const std::string& message)
{
	return addHistoryInternal(auctionId, action, accountId, playerId, amount, message);
}

bool canCreateAuction(Player* player, std::string& reason)
{
	if (!isEnabled()) {
		reason = "Character Bazaar is disabled.";
		return false;
	}
	if (!player || player->isRemoved()) {
		reason = "The character is not available.";
		return false;
	}
	if (player->isDead()) {
		reason = "Dead characters cannot be listed.";
		return false;
	}
	if (player->getAccountType() > ACCOUNT_TYPE_NORMAL) {
		reason = "Staff characters cannot be listed.";
		return false;
	}
	if (player->getLevel() < getMinimumLevel()) {
		reason = fmt::format("Your character must be at least level {:d}.", getMinimumLevel());
		return false;
	}
	if (!player->getTile() || !player->getTile()->hasFlag(TILESTATE_PROTECTIONZONE)) {
		reason = "You must be in a protection zone.";
		return false;
	}
	if (player->isPzLocked() || player->hasCondition(CONDITION_INFIGHT)) {
		reason = "You cannot list a character while in a fight.";
		return false;
	}
	if (isPlayerOnActiveAuction(player->getGUID()) || hasActiveAuctionForAccount(player->getAccount())) {
		reason = "This account already has an active character auction.";
		return false;
	}
	if (queryHasRows(fmt::format("SELECT `player_id` FROM `guild_membership` WHERE `player_id` = {:d} LIMIT 1",
	                             player->getGUID()))) {
		reason = "Leave your guild before listing this character.";
		return false;
	}
	if (queryHasRows(fmt::format(
	        "SELECT `id` FROM `houses` WHERE `owner` = {:d} OR `highest_bidder` = {:d} LIMIT 1", player->getGUID(),
	        player->getGUID()))) {
		reason = "Characters involved with houses cannot be listed.";
		return false;
	}
	if (ConfigManager::getBoolean(ConfigManager::MARKET_SYSTEM_ENABLED) &&
	    queryHasRows(fmt::format("SELECT `id` FROM `market_offers` WHERE `player_id` = {:d} LIMIT 1", player->getGUID()))) {
		reason = "Cancel your market offers before listing this character.";
		return false;
	}
	if (getTransferableCoins(player->getAccount()) < getAuctionFee()) {
		reason = "You do not have enough transferable Tibia Coins for the auction fee.";
		return false;
	}

	reason.clear();
	return true;
}

bool createAuction(Player* player, uint32_t startPrice, uint32_t durationSeconds, const std::string& description,
	               std::string& reason)
{
	if (!canCreateAuction(player, reason)) {
		return false;
	}
	if (startPrice < getMinimumPrice()) {
		reason = fmt::format("The starting price must be at least {:d} coins.", getMinimumPrice());
		return false;
	}
	const uint32_t minDuration = getMinimumDuration();
	const uint32_t maxDuration = std::max(minDuration, getMaximumDuration());
	if (durationSeconds < minDuration || durationSeconds > maxDuration) {
		reason = fmt::format("The auction duration must be between {:d} and {:d} seconds.", minDuration, maxDuration);
		return false;
	}
	if (description.size() > MAX_DESCRIPTION_LENGTH) {
		reason = fmt::format("The description may contain at most {:d} characters.", MAX_DESCRIPTION_LENGTH);
		return false;
	}

	const std::string safeDescription = sanitizeDescription(description);
	const uint32_t playerId = player->getGUID();
	const uint32_t accountId = player->getAccount();
	const time_t currentTime = time(nullptr);
	uint32_t auctionId = 0;

	const bool success = DBTransaction::executeWithinTransactionRollbackOnFailure([&]() {
		Database& db = Database::getInstance();
		if (!lockAccountForAuction(accountId)) {
			reason = "The account is no longer available.";
			return false;
		}
		if (isPlayerOnActiveAuction(playerId) || hasActiveAuctionForAccount(accountId)) {
			reason = "This account already has an active character auction.";
			return false;
		}
		if (!debitTransferableCoins(accountId, getAuctionFee())) {
			reason = "You do not have enough transferable Tibia Coins for the auction fee.";
			return false;
		}
		const Outfit_t outfit = player->getCurrentOutfit();
		if (!db.executeQuery(fmt::format(
		        "INSERT INTO `character_auctions` (`player_id`, `player_name`, `seller_account_id`, `start_price`, "
		        "`current_bid`, `auction_fee`, `commission_percent`, `status`, `created_at`, `end_at`, `description`, "
		        "`snapshot_level`, `snapshot_vocation`, `vocation`, `level`, `looktype`, `lookaddons`, `lookhead`, "
		        "`lookbody`, `looklegs`, `lookfeet`) VALUES ({:d}, {:s}, {:d}, {:d}, 0, {:d}, {:d}, {:d}, {:d}, {:d}, {:s}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d})",
		        playerId, db.escapeString(player->getName()), accountId, startPrice, getAuctionFee(), getCommissionPercent(),
		        AUCTION_STATUS_ACTIVE, currentTime, currentTime + durationSeconds, db.escapeString(safeDescription),
		        player->getLevel(), player->getVocationId(), player->getVocationId(), player->getLevel(),
		        outfit.lookType, outfit.lookAddons, outfit.lookHead, outfit.lookBody, outfit.lookLegs, outfit.lookFeet))) {
			reason = "The auction could not be created.";
			return false;
		}
		auctionId = static_cast<uint32_t>(db.getLastInsertId());
		if (auctionId == 0 || !addHistoryInternal(auctionId, "created", accountId, playerId, getAuctionFee(),
		                                          "Character auction created.")) {
			reason = "The auction history could not be created.";
			return false;
		}
		return true;
	});

	if (!success) {
		if (reason.empty()) {
			reason = "The auction could not be created. Please try again.";
		}
		return false;
	}

	reason = fmt::format("Auction #{:d} created. You will now be logged out.", auctionId);
	const uint32_t creatureId = player->getID();
	g_dispatcher.addTask([creatureId]() { g_game.kickPlayer(creatureId, true); });
	return true;
}

void sendRequirements(Player* player)
{
	if (!player) {
		return;
	}
	std::string reason;
	const bool canAuction = canCreateAuction(player, reason);
	NetworkMessage message;
	message.addByte(SERVER_PACKET);
	message.addByte(ACTION_REQUEST_REQUIREMENTS);
	message.addByte(canAuction ? 1 : 0);
	message.add<uint32_t>(getMinimumLevel());
	message.add<uint32_t>(getMinimumPrice());
	message.add<uint32_t>(getMinimumDuration());
	message.add<uint32_t>(std::max(getMinimumDuration(), getMaximumDuration()));
	message.add<uint32_t>(getAuctionFee());
	message.addByte(static_cast<uint8_t>(getCommissionPercent()));
	message.add<uint32_t>(static_cast<uint32_t>(std::min<uint64_t>(getTransferableCoins(player->getAccount()), UINT32_MAX)));
	message.addString(reason);
	player->sendNetworkMessage(message);
}

void sendCreateResult(Player* player, bool success, const std::string& result)
{
	if (!player) {
		return;
	}
	NetworkMessage message;
	message.addByte(SERVER_PACKET);
	message.addByte(ACTION_CREATE_AUCTION);
	message.addByte(success ? 1 : 0);
	message.addString(result);
	player->sendNetworkMessage(message);
}

void finalizeExpiredAuctions()
{
	if (!isEnabled()) {
		return;
	}
	const time_t currentTime = time(nullptr);
	auto result = Database::getInstance().storeQuery(fmt::format(
	    "SELECT `id` FROM `character_auctions` WHERE `status` = {:d} AND `end_at` <= {:d}", AUCTION_STATUS_ACTIVE,
	    currentTime));
	if (!result) {
		return;
	}

	do {
		const uint32_t auctionId = result->getNumber<uint32_t>("id");
		if (!finalizeAuction(auctionId)) {
			LOG_ERROR(fmt::format("[CharacterBazaar] Failed to finalize auction #{}.", auctionId));
		}
	} while (result->next());
}

void scheduleFinalization()
{
	if (!isEnabled()) {
		return;
	}
	g_scheduler.addEvent(FINALIZATION_INTERVAL_MS, []() {
		try {
			finalizeExpiredAuctions();
		} catch (const std::exception& exception) {
			LOG_ERROR(fmt::format("[CharacterBazaar] Exception during finalization: {}", exception.what()));
		} catch (...) {
			LOG_ERROR("[CharacterBazaar] Unknown exception during finalization.");
		}
		scheduleFinalization();
	});
}

} // namespace CharacterBazaar
