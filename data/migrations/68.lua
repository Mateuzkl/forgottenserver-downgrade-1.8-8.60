function onUpdateDatabase()
	logMigration("Updating database to version 69 (durable Market deliveries)")
	if not db.query([[
		CREATE TABLE IF NOT EXISTS `market_delivery_pending` (
			`id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
			`event_key` VARCHAR(128) NOT NULL,
			`offer_id` INT UNSIGNED NOT NULL,
			`player_id` INT NOT NULL,
			`depot_id` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
			`itemtype` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
			`amount` INT UNSIGNED NOT NULL DEFAULT 0,
			`attributes` LONGBLOB NOT NULL,
			`bank_credit` BIGINT UNSIGNED NOT NULL DEFAULT 0,
			`state` ENUM('pending', 'applied') NOT NULL DEFAULT 'pending',
			`created_at` BIGINT UNSIGNED NOT NULL,
			`applied_at` BIGINT UNSIGNED NULL,
			PRIMARY KEY (`id`),
			UNIQUE KEY `uq_market_delivery_event` (`event_key`),
			KEY `idx_market_delivery_player_state` (`player_id`, `state`, `id`),
			CONSTRAINT `fk_market_delivery_player`
				FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8mb4 COLLATE=utf8mb4_bin
	]]) then
		logMigration("Failed to create market_delivery_pending")
		return false
	end
	return true
end
