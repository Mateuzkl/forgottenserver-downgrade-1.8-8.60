function onUpdateDatabase()
	logMigration("Updating database to version 60 (move Market persistence to C++)")

	local function columnExists(tableName, columnName)
		local query = "SELECT COUNT(*) AS `count` FROM `information_schema`.`COLUMNS`"
			.. " WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = '" .. tableName .. "'"
			.. " AND `COLUMN_NAME` = '" .. columnName .. "'"
		local queryResult = db.storeQuery(query)
		local exists = queryResult and result.getNumber(queryResult, "count") > 0
		if queryResult then
			result.free(queryResult)
		end
		return exists
	end

	local function indexSignature(tableName, indexName)
		local columns = {}
		local query = "SELECT `COLUMN_NAME` FROM `information_schema`.`STATISTICS`"
			.. " WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = '" .. tableName .. "'"
			.. " AND `INDEX_NAME` = '" .. indexName .. "' ORDER BY `SEQ_IN_INDEX`"
		local queryResult = db.storeQuery(query)
		if queryResult then
			repeat
				columns[#columns + 1] = result.getString(queryResult, "COLUMN_NAME")
			until not result.next(queryResult)
			result.free(queryResult)
		end
		return table.concat(columns, ",")
	end

	local function ensureColumn(tableName, columnName, definition)
		return columnExists(tableName, columnName)
			or db.query("ALTER TABLE `" .. tableName .. "` ADD COLUMN `" .. columnName .. "` " .. definition)
	end

	local function ensureIndex(tableName, indexName, columns, expectedSignature)
		local currentSignature = indexSignature(tableName, indexName)
		if currentSignature == expectedSignature then
			return true
		end

		local dropIndex = currentSignature ~= "" and "DROP INDEX `" .. indexName .. "`, " or ""
		return db.query(
			"ALTER TABLE `" .. tableName .. "` " .. dropIndex ..
			"ADD INDEX `" .. indexName .. "` (" .. columns .. ")"
		) and indexSignature(tableName, indexName) == expectedSignature
	end

	if not db.query([[CREATE TABLE IF NOT EXISTS `market_offers` (
		`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
		`player_id` INT NOT NULL,
		`sale` TINYINT NOT NULL DEFAULT 0,
		`itemtype` SMALLINT UNSIGNED NOT NULL,
		`amount` SMALLINT UNSIGNED NOT NULL,
		`created` INT UNSIGNED NOT NULL,
		`anonymous` TINYINT NOT NULL DEFAULT 0,
		`price` INT UNSIGNED NOT NULL DEFAULT 0,
		`tier` TINYINT UNSIGNED NOT NULL DEFAULT 0,
		`attributes` MEDIUMBLOB NULL,
		PRIMARY KEY (`id`),
		KEY `idx_market_offers_item_sale_price` (`itemtype`, `sale`, `price`, `created`),
		KEY `idx_market_offers_player_created` (`player_id`, `created`),
		KEY `idx_market_offers_created` (`created`),
		CONSTRAINT `fk_market_offers_player` FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
	) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8]]) then
		return false
	end

	if not db.query([[CREATE TABLE IF NOT EXISTS `market_history` (
		`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
		`player_id` INT NOT NULL,
		`sale` TINYINT NOT NULL DEFAULT 0,
		`itemtype` SMALLINT UNSIGNED NOT NULL,
		`amount` SMALLINT UNSIGNED NOT NULL,
		`price` INT UNSIGNED NOT NULL DEFAULT 0,
		`tier` TINYINT UNSIGNED NOT NULL DEFAULT 0,
		`expires_at` BIGINT UNSIGNED NOT NULL,
		`inserted` BIGINT UNSIGNED NOT NULL,
		`state` TINYINT UNSIGNED NOT NULL,
		PRIMARY KEY (`id`),
		KEY `idx_market_history_player_inserted` (`player_id`, `inserted`),
		CONSTRAINT `fk_market_history_player` FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
	) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8]]) then
		return false
	end

	if not db.query([[CREATE TABLE IF NOT EXISTS `market_statistics` (
		`itemtype` SMALLINT UNSIGNED NOT NULL,
		`sale` TINYINT NOT NULL DEFAULT 0,
		`day` INT UNSIGNED NOT NULL,
		`transactions` INT UNSIGNED NOT NULL DEFAULT 0,
		`total_price` BIGINT UNSIGNED NOT NULL DEFAULT 0,
		`highest_price` INT UNSIGNED NOT NULL DEFAULT 0,
		`lowest_price` INT UNSIGNED NOT NULL DEFAULT 0,
		PRIMARY KEY (`itemtype`, `sale`, `day`)
	) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8]]) then
		return false
	end

	return ensureColumn("market_offers", "tier", "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `price`")
		and ensureColumn("market_offers", "attributes", "MEDIUMBLOB NULL AFTER `tier`")
		and ensureColumn("market_history", "tier", "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `price`")
		and ensureIndex("market_offers", "idx_market_offers_item_sale_price", "`itemtype`, `sale`, `price`, `created`", "itemtype,sale,price,created")
		and ensureIndex("market_offers", "idx_market_offers_player_created", "`player_id`, `created`", "player_id,created")
		and ensureIndex("market_offers", "idx_market_offers_created", "`created`", "created")
		and ensureIndex("market_history", "idx_market_history_player_inserted", "`player_id`, `inserted`", "player_id,inserted")
end
