-- Standalone schema repair for the durable Market inbox (migrations 68 and 69).
-- Select the game database first (for the reported error: database `860`).
-- Stop the server, back up the database, run this file, then restart with updated code/data.
-- CREATE TABLE IF NOT EXISTS preserves an existing table and its deliveries.
-- Do not change server_config.db_version: other migrations may still be required.

CREATE TABLE IF NOT EXISTS `market_delivery_pending` (
	`id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
	`event_key` VARCHAR(128) NOT NULL,
	`offer_id` INT UNSIGNED NOT NULL,
	`player_id` INT NOT NULL,
	`depot_id` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
	`itemtype` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
	`amount` INT UNSIGNED NOT NULL DEFAULT 0,
	`delivered_amount` INT UNSIGNED NOT NULL DEFAULT 0,
	`retry_after` BIGINT UNSIGNED NOT NULL DEFAULT 0,
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
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8mb4 COLLATE=utf8mb4_bin;

-- CREATE TABLE IF NOT EXISTS alone does not upgrade an existing table.
-- Check each column separately so interrupted/partial upgrades can be resumed.
SET @market_schema_sql = (
	SELECT IF(COUNT(*) = 0,
		'ALTER TABLE `market_offers` ADD COLUMN `delivery_depot_id` SMALLINT UNSIGNED NULL DEFAULT NULL',
		'SELECT 1')
	FROM information_schema.COLUMNS
	WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'market_offers' AND COLUMN_NAME = 'delivery_depot_id'
);
PREPARE market_schema_repair FROM @market_schema_sql;
EXECUTE market_schema_repair;
DEALLOCATE PREPARE market_schema_repair;

SET @market_schema_sql = (
	SELECT IF(COUNT(*) = 0,
		'ALTER TABLE `market_delivery_pending` ADD COLUMN `delivered_amount` INT UNSIGNED NOT NULL DEFAULT 0',
		'SELECT 1')
	FROM information_schema.COLUMNS
	WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'market_delivery_pending' AND COLUMN_NAME = 'delivered_amount'
);
PREPARE market_schema_repair FROM @market_schema_sql;
EXECUTE market_schema_repair;
DEALLOCATE PREPARE market_schema_repair;

SET @market_schema_sql = (
	SELECT IF(COUNT(*) = 0,
		'ALTER TABLE `market_delivery_pending` ADD COLUMN `retry_after` BIGINT UNSIGNED NOT NULL DEFAULT 0',
		'SELECT 1')
	FROM information_schema.COLUMNS
	WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'market_delivery_pending' AND COLUMN_NAME = 'retry_after'
);
PREPARE market_schema_repair FROM @market_schema_sql;
EXECUTE market_schema_repair;
DEALLOCATE PREPARE market_schema_repair;

SELECT TABLE_NAME, COLUMN_NAME FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA = DATABASE() AND
	((TABLE_NAME = 'market_offers' AND COLUMN_NAME = 'delivery_depot_id') OR
	(TABLE_NAME = 'market_delivery_pending' AND COLUMN_NAME IN ('delivered_amount', 'retry_after')));
