function onUpdateDatabase()
	logMigration("Updating database to version 55 (character bazaar)")

	local queries = {
		[[CREATE TABLE IF NOT EXISTS `character_auctions` (
			`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
			`player_id` INT UNSIGNED NOT NULL,
			`player_name` VARCHAR(255) NOT NULL,
			`seller_account_id` INT UNSIGNED NOT NULL,
			`current_bidder_account_id` INT UNSIGNED DEFAULT NULL,
			`winner_account_id` INT UNSIGNED DEFAULT NULL,
			`start_price` INT UNSIGNED NOT NULL DEFAULT 0,
			`current_bid` INT UNSIGNED NOT NULL DEFAULT 0,
			`final_price` INT UNSIGNED DEFAULT NULL,
			`auction_fee` INT UNSIGNED NOT NULL DEFAULT 0,
			`commission_percent` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`status` TINYINT UNSIGNED NOT NULL DEFAULT 1,
			`created_at` INT UNSIGNED NOT NULL,
			`end_at` INT UNSIGNED NOT NULL,
			`finished_at` INT UNSIGNED DEFAULT NULL,
			`description` TEXT DEFAULT NULL,
			`snapshot_level` INT UNSIGNED NOT NULL DEFAULT 0,
			`snapshot_vocation` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
			`vocation` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
			`level` INT UNSIGNED NOT NULL DEFAULT 0,
			`looktype` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
			`lookaddons` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`lookhead` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`lookbody` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`looklegs` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`lookfeet` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`id`),
			KEY `idx_character_auctions_player_status` (`player_id`, `status`),
			KEY `idx_character_auctions_status_end` (`status`, `end_at`),
			KEY `idx_character_auctions_status_finished` (`status`, `finished_at`),
			KEY `idx_character_auctions_seller` (`seller_account_id`),
			KEY `idx_character_auctions_bidder` (`current_bidder_account_id`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;]],
		[[CREATE TABLE IF NOT EXISTS `character_auction_bids` (
			`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
			`auction_id` INT UNSIGNED NOT NULL,
			`bidder_account_id` INT UNSIGNED NOT NULL,
			`bid_amount` INT UNSIGNED NOT NULL,
			`created_at` INT UNSIGNED NOT NULL,
			PRIMARY KEY (`id`),
			KEY `idx_character_auction_bids_auction` (`auction_id`, `created_at`),
			KEY `idx_character_auction_bids_bidder` (`bidder_account_id`, `created_at`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;]],
		[[CREATE TABLE IF NOT EXISTS `character_auction_history` (
			`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
			`auction_id` INT UNSIGNED NOT NULL,
			`action` VARCHAR(64) NOT NULL,
			`account_id` INT UNSIGNED DEFAULT NULL,
			`player_id` INT UNSIGNED DEFAULT NULL,
			`amount` INT UNSIGNED DEFAULT NULL,
			`message` TEXT DEFAULT NULL,
			`created_at` INT UNSIGNED NOT NULL,
			PRIMARY KEY (`id`),
			KEY `idx_character_auction_history_auction` (`auction_id`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;]],
	}

	for _, query in ipairs(queries) do
		if not db.query(query) then
			logMigration("Failed to create Character Bazaar tables")
			return false
		end
	end

	return true
end
