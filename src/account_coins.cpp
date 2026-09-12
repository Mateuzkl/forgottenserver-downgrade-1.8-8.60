// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "account_coins.h"

#include "database.h"

#include <algorithm>
#include <fmt/core.h>

namespace AccountCoins {

uint64_t get(uint32_t accountId)
{
	if (accountId == 0) {
		return 0;
	}
	auto result = Database::getInstance().storeQuery(
	    fmt::format("SELECT `tibia_coins` FROM `accounts` WHERE `id` = {:d}", accountId));
	return result ? result->getNumber<uint64_t>("tibia_coins") : 0;
}

bool debit(uint32_t accountId, uint64_t amount)
{
	if (accountId == 0) {
		return false;
	}
	if (amount == 0) {
		return true;
	}
	if (amount > MaxCoins) {
		return false;
	}
	Database& db = Database::getInstance();
	return db.executeQuery(fmt::format(
	           "UPDATE `accounts` SET `tibia_coins` = `tibia_coins` - {:d} WHERE `id` = {:d} AND `tibia_coins` >= {:d}",
	           amount, accountId, amount)) &&
	       db.getAffectedRows() == 1;
}

bool credit(uint32_t accountId, uint64_t amount)
{
	if (accountId == 0) {
		return false;
	}
	if (amount == 0) {
		return true;
	}
	if (amount > MaxCoins) {
		return false;
	}
	Database& db = Database::getInstance();
	return db.executeQuery(fmt::format(
	           "UPDATE `accounts` SET `tibia_coins` = `tibia_coins` + {:d} WHERE `id` = {:d} AND `tibia_coins` <= {:d}",
	           amount, accountId, MaxCoins - amount)) &&
	       db.getAffectedRows() == 1;
}

bool transfer(uint32_t sourceAccountId, uint32_t destAccountId, uint64_t amount,
              const std::optional<TransferHistoryDetails>& history)
{
	if (sourceAccountId == 0 || destAccountId == 0 || amount == 0) {
		return false;
	}
	if (sourceAccountId == destAccountId) {
		return false;
	}
	if (amount > MaxCoins) {
		return false;
	}

	return DBTransaction::executeWithinTransactionRollbackOnFailure([&]() {
		Database& db = Database::getInstance();

		// Deterministic lock ordering: lock lower account ID first to prevent deadlocks.
		const uint32_t firstLock = std::min(sourceAccountId, destAccountId);
		const uint32_t secondLock = std::max(sourceAccountId, destAccountId);

		if (!db.storeQuery(
		        fmt::format("SELECT `id` FROM `accounts` WHERE `id` = {:d} FOR UPDATE", firstLock))) {
			return false;
		}
		if (!db.storeQuery(
		        fmt::format("SELECT `id` FROM `accounts` WHERE `id` = {:d} FOR UPDATE", secondLock))) {
			return false;
		}

		// Atomic debit from source.
		if (!db.executeQuery(fmt::format(
		        "UPDATE `accounts` SET `tibia_coins` = `tibia_coins` - {:d} "
		        "WHERE `id` = {:d} AND `tibia_coins` >= {:d}",
		        amount, sourceAccountId, amount)) ||
		    db.getAffectedRows() != 1) {
			return false;
		}

		// Atomic credit to destination.
		if (!db.executeQuery(fmt::format(
		        "UPDATE `accounts` SET `tibia_coins` = `tibia_coins` + {:d} "
		        "WHERE `id` = {:d} AND `tibia_coins` <= {:d}",
		        amount, destAccountId, MaxCoins - amount)) ||
		    db.getAffectedRows() != 1) {
			return false;
		}

		// Atomic history logging within the same transaction.
		if (history) {
			const int64_t transferAmount = static_cast<int64_t>(amount);
			if (!db.executeQuery(fmt::format(
			        "INSERT INTO `shop_history` (`account`, `player`, `date`, `title`, `price`, `costSecond`, `count`, `target`) "
			        "VALUES ({:d}, {:d}, NOW(), {:s}, {:d}, 0, 1, {:s})",
			        sourceAccountId, history->sourcePlayerId,
			        db.escapeString("Coin Transfer to " + history->destPlayerName),
			        -transferAmount, db.escapeString(history->destPlayerName)))) {
				return false;
			}
			if (!db.executeQuery(fmt::format(
			        "INSERT INTO `shop_history` (`account`, `player`, `date`, `title`, `price`, `costSecond`, `count`, `target`) "
			        "VALUES ({:d}, {:d}, NOW(), {:s}, {:d}, 0, 1, {:s})",
			        destAccountId, history->destPlayerId,
			        db.escapeString("Coin Transfer from " + history->sourcePlayerName),
			        transferAmount, db.escapeString(history->sourcePlayerName)))) {
				return false;
			}
		}

		return true;
	});
}

std::optional<CharacterAccountInfo> findCharacterAccount(const std::string& playerName)
{
	if (playerName.empty()) {
		return std::nullopt;
	}
	Database& db = Database::getInstance();
	auto result = db.storeQuery(fmt::format(
	    "SELECT p.`id`, p.`account_id`, p.`name` FROM `players` p "
	    "WHERE p.`name` = {:s} LIMIT 1",
	    db.escapeString(playerName)));
	if (!result) {
		return std::nullopt;
	}
	CharacterAccountInfo info;
	info.playerId = result->getNumber<uint32_t>("id");
	info.accountId = result->getNumber<uint32_t>("account_id");
	info.playerName = std::string(result->getString("name"));
	return info;
}

} // namespace AccountCoins
