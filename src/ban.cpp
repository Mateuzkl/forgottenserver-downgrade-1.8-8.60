// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "ban.h"

#include "connection.h"
#include "database.h"
#include "databasetasks.h"
#include "tools.h"

bool IOBan::isAccountBanned(uint32_t accountId, BanInfo& banInfo)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format(
	    "SELECT `reason`, `expires_at`, `banned_at`, `banned_by`, (SELECT `name` FROM `players` WHERE `id` = `banned_by`) AS `name` FROM `account_bans` WHERE `account_id` = {:d}",
	    accountId));
	if (!result) {
		return false;
	}

	time_t expiresAt = result->getNumber<time_t>("expires_at");
	if (expiresAt != 0 && std::chrono::system_clock::now() > std::chrono::system_clock::from_time_t(expiresAt)) {
		// Move the ban to history if it has expired
		g_databaseTasks.addTask(fmt::format(
		    "INSERT INTO `account_ban_history` (`account_id`, `reason`, `banned_at`, `expired_at`, `banned_by`) VALUES ({:d}, {:s}, {:d}, {:d}, {:d})",
		    accountId, db.escapeString(result->getString("reason")), result->getNumber<time_t>("banned_at"), expiresAt,
		    result->getNumber<uint32_t>("banned_by")));
		g_databaseTasks.addTask(fmt::format("DELETE FROM `account_bans` WHERE `account_id` = {:d}", accountId));
		return false;
	}

	banInfo.expiresAt = expiresAt;
	banInfo.reason = result->getString("reason");
	banInfo.bannedBy = result->getString("name");
	return true;
}

bool IOBan::isIpBanned(const uint32_t clientIP, BanInfo& banInfo)
{
	if (clientIP == 0) {
		return false;
	}

	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format(
	    "SELECT `reason`, `expires_at`, (SELECT `name` FROM `players` WHERE `id` = `banned_by`) AS `name` FROM `ip_bans` WHERE `ip` = {:d}",
	    clientIP));
	if (!result) {
		return false;
	}

	time_t expiresAt = result->getNumber<time_t>("expires_at");
	if (expiresAt != 0 && std::chrono::system_clock::now() > std::chrono::system_clock::from_time_t(expiresAt)) {
		g_databaseTasks.addTask(fmt::format("DELETE FROM `ip_bans` WHERE `ip` = {:d}", clientIP));
		return false;
	}

	banInfo.expiresAt = expiresAt;
	banInfo.reason = result->getString("reason");
	banInfo.bannedBy = result->getString("name");
	return true;
}

bool IOBan::isPlayerNamelocked(uint32_t playerId)
{
	return Database::getInstance()
	    .storeQuery(fmt::format("SELECT 1 FROM `player_namelocks` WHERE `player_id` = {:d}", playerId))
	    .get();
}

bool IOBan::accountHasNamelockedPlayer(uint32_t accountId)
{
	Database& db = Database::getInstance();
	DBResult_ptr result =
	    db.storeQuery(fmt::format("SELECT 1 FROM `player_namelocks` pn "
	                              "JOIN `players` p ON p.`id` = pn.`player_id` "
	                              "WHERE p.`account_id` = {:d} LIMIT 1",
	                              accountId));
	return result != nullptr;
}

uint32_t IOBan::getNamelockedPlayerByAccount(uint32_t accountId)
{
	Database& db = Database::getInstance();
	DBResult_ptr result =
	    db.storeQuery(fmt::format("SELECT pn.`player_id` FROM `player_namelocks` pn "
	                              "JOIN `players` p ON p.`id` = pn.`player_id` "
	                              "WHERE p.`account_id` = {:d} LIMIT 1",
	                              accountId));
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("player_id");
}
