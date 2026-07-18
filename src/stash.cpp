// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "stash.h"

#include "database.h"

uint8_t Stash::normalizeTier(uint32_t tier)
{
	return static_cast<uint8_t>(std::min<uint32_t>(tier, MAX_ITEM_TIER));
}

std::vector<StashRecord> Stash::getRows(uint32_t playerId)
{
	std::vector<StashRecord> rows;
	if (playerId == 0) {
		return rows;
	}

	const DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
		"SELECT `itemtype`, `tier`, `amount` FROM `player_supplystash` "
		"WHERE `player_id` = {:d} AND `amount` > 0 ORDER BY `itemtype` ASC, `tier` ASC",
		playerId));
	if (!result) {
		return rows;
	}

	do {
		rows.emplace_back(StashRecord{
			result->getNumber<uint16_t>("itemtype"),
			normalizeTier(result->getNumber<uint32_t>("tier")),
			result->getNumber<uint32_t>("amount")
		});
	} while (result->next());
	return rows;
}

bool Stash::addAmount(uint32_t playerId, uint16_t itemId, uint32_t amount, uint8_t tier)
{
	if (playerId == 0 || itemId == 0 || amount == 0) {
		return false;
	}

	return Database::getInstance().executeQuery(fmt::format(
		"INSERT INTO `player_supplystash` (`player_id`, `itemtype`, `tier`, `amount`) "
		"VALUES ({:d}, {:d}, {:d}, {:d}) "
		"ON DUPLICATE KEY UPDATE `amount` = `amount` + VALUES(`amount`)",
		playerId, itemId, normalizeTier(tier), amount));
}

bool Stash::removeAmount(uint32_t playerId, uint16_t itemId, uint32_t amount, uint8_t tier)
{
	if (playerId == 0 || itemId == 0 || amount == 0) {
		return false;
	}

	Database& db = Database::getInstance();
	return db.executeQuery(fmt::format(
		"UPDATE `player_supplystash` SET `amount` = `amount` - {:d} "
		"WHERE `player_id` = {:d} AND `itemtype` = {:d} AND `tier` = {:d} AND `amount` >= {:d}",
		amount, playerId, itemId, normalizeTier(tier), amount)) && db.getAffectedRows() == 1;
}

bool Stash::cleanup(uint32_t playerId)
{
	if (playerId == 0) {
		return false;
	}

	return Database::getInstance().executeQuery(fmt::format(
		"DELETE FROM `player_supplystash` WHERE `player_id` = {:d} AND `amount` = 0", playerId));
}
