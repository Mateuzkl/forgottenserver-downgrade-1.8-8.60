function onUpdateDatabase()
	logMigration("Updating database to version 59 (Character Bazaar ownership constraints)")

	local foreignKeys = {
		{"character_auction_bids", "fk_character_auction_bids_auction"},
		{"character_auction_bids", "fk_character_auction_bids_bidder"},
		{"character_auction_history", "fk_character_auction_history_auction"},
		{"character_auction_history", "fk_character_auction_history_account"},
		{"character_auction_history", "fk_character_auction_history_player"},
		{"character_auctions", "fk_character_auctions_player"},
		{"character_auctions", "fk_character_auctions_seller"},
		{"character_auctions", "fk_character_auctions_bidder"},
		{"character_auctions", "fk_character_auctions_winner"},
	}
	for _, foreignKey in ipairs(foreignKeys) do
		local foreignKeyResult = db.storeQuery(
			"SELECT COUNT(*) AS `count` FROM `information_schema`.`TABLE_CONSTRAINTS`"
			.. " WHERE `CONSTRAINT_SCHEMA` = DATABASE() AND `TABLE_NAME` = '" .. foreignKey[1] .. "'"
			.. " AND `CONSTRAINT_NAME` = '" .. foreignKey[2] .. "' AND `CONSTRAINT_TYPE` = 'FOREIGN KEY'"
		)
		local exists = foreignKeyResult and result.getNumber(foreignKeyResult, "count") > 0
		if foreignKeyResult then
			result.free(foreignKeyResult)
		end
		if exists and not db.query(
			"ALTER TABLE `" .. foreignKey[1] .. "` DROP FOREIGN KEY `" .. foreignKey[2] .. "`"
		) then
			logMigration("Failed to replace Character Bazaar ownership constraints")
			return false
		end
	end

	local queries = {
		[[DELETE `auction` FROM `character_auctions` AS `auction`
			LEFT JOIN `players` AS `player` ON `player`.`id` = `auction`.`player_id`
			LEFT JOIN `accounts` AS `seller` ON `seller`.`id` = `auction`.`seller_account_id`
			WHERE `player`.`id` IS NULL OR `seller`.`id` IS NULL]],
		[[UPDATE `character_auctions` AS `auction`
			LEFT JOIN `accounts` AS `bidder` ON `bidder`.`id` = `auction`.`current_bidder_account_id`
			SET `auction`.`current_bidder_account_id` = NULL
			WHERE `auction`.`current_bidder_account_id` IS NOT NULL AND `bidder`.`id` IS NULL]],
		[[UPDATE `character_auctions` AS `auction`
			LEFT JOIN `accounts` AS `winner` ON `winner`.`id` = `auction`.`winner_account_id`
			SET `auction`.`winner_account_id` = NULL
			WHERE `auction`.`winner_account_id` IS NOT NULL AND `winner`.`id` IS NULL]],
		[[DELETE `bid` FROM `character_auction_bids` AS `bid`
			LEFT JOIN `character_auctions` AS `auction` ON `auction`.`id` = `bid`.`auction_id`
			LEFT JOIN `accounts` AS `bidder` ON `bidder`.`id` = `bid`.`bidder_account_id`
			WHERE `auction`.`id` IS NULL OR `bidder`.`id` IS NULL]],
		[[DELETE `history` FROM `character_auction_history` AS `history`
			LEFT JOIN `character_auctions` AS `auction` ON `auction`.`id` = `history`.`auction_id`
			WHERE `auction`.`id` IS NULL]],
		[[UPDATE `character_auction_history` AS `history`
			LEFT JOIN `accounts` AS `account` ON `account`.`id` = `history`.`account_id`
			SET `history`.`account_id` = NULL
			WHERE `history`.`account_id` IS NOT NULL AND `account`.`id` IS NULL]],
		[[UPDATE `character_auction_history` AS `history`
			LEFT JOIN `players` AS `player` ON `player`.`id` = `history`.`player_id`
			SET `history`.`player_id` = NULL
			WHERE `history`.`player_id` IS NOT NULL AND `player`.`id` IS NULL]],
		[[ALTER TABLE `character_auctions`
			MODIFY `player_id` INT NOT NULL,
			MODIFY `seller_account_id` INT NOT NULL,
			MODIFY `current_bidder_account_id` INT DEFAULT NULL,
			MODIFY `winner_account_id` INT DEFAULT NULL]],
		[[ALTER TABLE `character_auction_bids`
			MODIFY `bidder_account_id` INT NOT NULL]],
		[[ALTER TABLE `character_auction_history`
			MODIFY `account_id` INT DEFAULT NULL,
			MODIFY `player_id` INT DEFAULT NULL]],
		[[ALTER TABLE `character_auctions`
			ADD CONSTRAINT `fk_character_auctions_player` FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE,
			ADD CONSTRAINT `fk_character_auctions_seller` FOREIGN KEY (`seller_account_id`) REFERENCES `accounts` (`id`) ON DELETE CASCADE,
			ADD CONSTRAINT `fk_character_auctions_bidder` FOREIGN KEY (`current_bidder_account_id`) REFERENCES `accounts` (`id`) ON DELETE SET NULL,
			ADD CONSTRAINT `fk_character_auctions_winner` FOREIGN KEY (`winner_account_id`) REFERENCES `accounts` (`id`) ON DELETE SET NULL]],
		[[ALTER TABLE `character_auction_bids`
			ADD CONSTRAINT `fk_character_auction_bids_auction` FOREIGN KEY (`auction_id`) REFERENCES `character_auctions` (`id`) ON DELETE CASCADE,
			ADD CONSTRAINT `fk_character_auction_bids_bidder` FOREIGN KEY (`bidder_account_id`) REFERENCES `accounts` (`id`) ON DELETE CASCADE]],
		[[ALTER TABLE `character_auction_history`
			ADD CONSTRAINT `fk_character_auction_history_auction` FOREIGN KEY (`auction_id`) REFERENCES `character_auctions` (`id`) ON DELETE CASCADE,
			ADD CONSTRAINT `fk_character_auction_history_account` FOREIGN KEY (`account_id`) REFERENCES `accounts` (`id`) ON DELETE SET NULL,
			ADD CONSTRAINT `fk_character_auction_history_player` FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE SET NULL]],
	}

	for _, query in ipairs(queries) do
		if not db.query(query) then
			logMigration("Failed to repair Character Bazaar ownership constraints")
			return false
		end
	end

	return true
end
