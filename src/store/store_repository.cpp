// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "store/store_repository.h"

#include "database.h"
#include "logger.h"

#include <fmt/core.h>

StoreRepository& StoreRepository::getInstance()
{
	static StoreRepository instance;
	return instance;
}

std::vector<StoreHistoryEntry> StoreRepository::loadHistory(uint32_t accountId, size_t limit)
{
	std::vector<StoreHistoryEntry> history;
	if (accountId == 0 || limit == 0) {
		return history;
	}

	auto result = Database::getInstance().storeQuery(fmt::format(
	    "SELECT `date`, `price`, `costSecond`, `title`, `count` FROM `shop_history` "
	    "WHERE `account` = {:d} ORDER BY `id` DESC LIMIT {:d}",
	    accountId, limit));
	if (!result) {
		return history;
	}

	do {
		StoreHistoryEntry entry;
		entry.date = result->getString("date");
		entry.price = result->getNumber<int32_t>("price");
		entry.costSecond = result->getNumber<int32_t>("costSecond");
		entry.title = result->getString("title");
		entry.count = static_cast<uint16_t>(result->getNumber<int32_t>("count"));
		history.push_back(std::move(entry));
	} while (result->next());

	return history;
}

bool StoreRepository::addHistory(uint32_t accountId, uint32_t playerGuid,
                                 std::string_view title, int32_t price,
                                 uint16_t count, std::string_view target)
{
	if (accountId == 0) {
		return false;
	}

	Database& db = Database::getInstance();
	const std::string safeTitle = std::string(title).substr(0, 100);

	std::string targetSql = "NULL";
	if (!target.empty()) {
		targetSql = db.escapeString(std::string(target));
	}

	return db.executeQuery(fmt::format(
	    "INSERT INTO `shop_history` (`account`, `player`, `date`, `title`, `price`, `costSecond`, `count`, `target`) "
	    "VALUES ({:d}, {:d}, NOW(), {:s}, {:d}, 0, {:d}, {:s})",
	    accountId, playerGuid, db.escapeString(safeTitle), price, count, targetSql));
}

bool StoreRepository::renameCharacter(uint32_t playerId, std::string_view oldName,
                                       std::string_view newName, std::string& reason)
{
	if (playerId == 0 || oldName.empty() || newName.empty()) {
		reason = "Invalid rename parameters.";
		return false;
	}

	Database& db = Database::getInstance();

	// Attempt the rename in a single query — the UNIQUE constraint on players.name
	// will reject duplicates at the DB level, which is stronger than SELECT-then-UPDATE.
	if (!db.executeQuery(fmt::format(
	        "UPDATE `players` SET `name` = {:s} WHERE `id` = {:d}",
	        db.escapeString(std::string(newName)), playerId)) ||
	    db.getAffectedRows() != 1) {
		reason = "Character name already taken or character not found.";
		return false;
	}

	// Update death-history tables to reflect the new name.
	// These are best-effort — failure here does not roll back the rename.
	const std::string escapedOld = db.escapeString(std::string(oldName));
	const std::string escapedNew = db.escapeString(std::string(newName));

	db.executeQuery(fmt::format(
	    "UPDATE `player_deaths` SET `killed_by` = {:s}, `mostdamage_by` = {:s} "
	    "WHERE `killed_by` = {:s} OR `mostdamage_by` = {:s}",
	    escapedNew, escapedNew, escapedOld, escapedOld));

	db.executeQuery(fmt::format(
	    "UPDATE `player_deaths_backup` SET `killed_by` = {:s}, `mostdamage_by` = {:s} "
	    "WHERE `killed_by` = {:s} OR `mostdamage_by` = {:s}",
	    escapedNew, escapedNew, escapedOld, escapedOld));

	reason.clear();
	return true;
}

void StoreRepository::recordNameChange(uint32_t playerId, std::string_view oldName, std::string_view newName)
{
	Database& db = Database::getInstance();

	// Check if table exists by attempting a lightweight query.
	auto tableCheck = db.storeQuery(
	    "SELECT 1 FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA` = DATABASE() "
	    "AND `TABLE_NAME` = 'change_name_history' LIMIT 1");
	if (!tableCheck) {
		return;
	}

	db.executeQuery(fmt::format(
	    "INSERT INTO `change_name_history` (`player_id`, `last_name`, `current_name`, `changed_name_in`) "
	    "VALUES ({:d}, {:s}, {:s}, {:d})",
	    playerId, db.escapeString(std::string(oldName)), db.escapeString(std::string(newName)),
	    static_cast<uint32_t>(time(nullptr))));
}
