function onUpdateDatabase()
	logMigration("> Updating database to version 56 (optional multi-world isolation)")

	local function tableExists(tableName)
		local resultId = db.storeQuery(
			"SELECT COUNT(*) AS `count` FROM `information_schema`.`TABLES`"
			.. " WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = " .. db.escapeString(tableName)
		)
		if not resultId then
			return false
		end
		local exists = result.getNumber(resultId, "count") > 0
		result.free(resultId)
		return exists
	end

	local function columnExists(tableName, columnName)
		local resultId = db.storeQuery(
			"SELECT COUNT(*) AS `count` FROM `information_schema`.`COLUMNS`"
			.. " WHERE `TABLE_SCHEMA` = DATABASE()"
			.. " AND `TABLE_NAME` = " .. db.escapeString(tableName)
			.. " AND `COLUMN_NAME` = " .. db.escapeString(columnName)
		)
		if not resultId then
			return false
		end
		local exists = result.getNumber(resultId, "count") > 0
		result.free(resultId)
		return exists
	end

	local function indexExists(tableName, indexName)
		local resultId = db.storeQuery(
			"SELECT COUNT(*) AS `count` FROM `information_schema`.`STATISTICS`"
			.. " WHERE `TABLE_SCHEMA` = DATABASE()"
			.. " AND `TABLE_NAME` = " .. db.escapeString(tableName)
			.. " AND `INDEX_NAME` = " .. db.escapeString(indexName)
		)
		if not resultId then
			return false
		end
		local exists = result.getNumber(resultId, "count") > 0
		result.free(resultId)
		return exists
	end

	local function foreignKeyExists(tableName, constraintName)
		local resultId = db.storeQuery(
			"SELECT COUNT(*) AS `count` FROM `information_schema`.`TABLE_CONSTRAINTS`"
			.. " WHERE `TABLE_SCHEMA` = DATABASE()"
			.. " AND `TABLE_NAME` = " .. db.escapeString(tableName)
			.. " AND `CONSTRAINT_NAME` = " .. db.escapeString(constraintName)
			.. " AND `CONSTRAINT_TYPE` = 'FOREIGN KEY'"
		)
		if not resultId then
			return false
		end
		local exists = result.getNumber(resultId, "count") > 0
		result.free(resultId)
		return exists
	end

	local function ensureColumn(tableName, columnName, definition)
		if not tableExists(tableName) then
			return true
		end
		if columnExists(tableName, columnName) then
			return true
		end
		if not db.query("ALTER TABLE `" .. tableName .. "` ADD COLUMN `" .. columnName .. "` " .. definition) then
			logMigration("Failed to add `" .. columnName .. "` to `" .. tableName .. "`")
			return false
		end
		return true
	end

	local function ensureIndex(tableName, indexName, definition)
		if not tableExists(tableName) or indexExists(tableName, indexName) then
			return true
		end
		if not db.query("ALTER TABLE `" .. tableName .. "` ADD " .. definition) then
			logMigration("Failed to add index `" .. indexName .. "` to `" .. tableName .. "`")
			return false
		end
		return true
	end

	local function ensureWorldForeignKey(tableName, constraintName)
		if not tableExists(tableName) or foreignKeyExists(tableName, constraintName) then
			return true
		end
		if not db.query("ALTER TABLE `" .. tableName .. "` ADD CONSTRAINT `" .. constraintName
			.. "` FOREIGN KEY (`world_id`) REFERENCES `worlds` (`id`) ON DELETE RESTRICT ON UPDATE CASCADE") then
			logMigration("Failed to add world foreign key `" .. constraintName .. "` to `" .. tableName .. "`")
			return false
		end
		return true
	end

	local function hasCompositeHousePrimaryKey()
		local resultId = db.storeQuery(
			"SELECT COUNT(*) AS `count` FROM `information_schema`.`STATISTICS`"
			.. " WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'houses'"
			.. " AND `INDEX_NAME` = 'PRIMARY' AND `COLUMN_NAME` IN ('id', 'world_id')"
		)
		if not resultId then
			return false
		end
		local isComposite = result.getNumber(resultId, "count") == 2
		result.free(resultId)
		return isComposite
	end

	local function dropForeignKeysReferencingHouses()
		local resultId = db.storeQuery(
			"SELECT DISTINCT `TABLE_NAME`, `CONSTRAINT_NAME` FROM `information_schema`.`KEY_COLUMN_USAGE`"
			.. " WHERE `TABLE_SCHEMA` = DATABASE() AND `REFERENCED_TABLE_NAME` = 'houses'"
		)
		if not resultId then
			return true
		end

		repeat
			local tableName = result.getString(resultId, "TABLE_NAME")
			local constraintName = result.getString(resultId, "CONSTRAINT_NAME")
			if not db.query("ALTER TABLE `" .. tableName .. "` DROP FOREIGN KEY `" .. constraintName .. "`") then
				result.free(resultId)
				logMigration("Failed to drop old house foreign key `" .. constraintName .. "`")
				return false
			end
		until not result.next(resultId)
		result.free(resultId)
		return true
	end

	if not db.query([[
		CREATE TABLE IF NOT EXISTS `worlds` (
			`id` SMALLINT UNSIGNED NOT NULL AUTO_INCREMENT,
			`name` VARCHAR(80) NOT NULL,
			`type` ENUM('no-pvp', 'pvp', 'pvp-enforced', 'retro-pvp', 'retro-hardcore') NOT NULL DEFAULT 'pvp',
			`motd` VARCHAR(255) NOT NULL DEFAULT '',
			`location` VARCHAR(32) NOT NULL DEFAULT 'South America',
			`ip` VARCHAR(45) NOT NULL DEFAULT '127.0.0.1',
			`port` SMALLINT UNSIGNED NOT NULL DEFAULT 7172,
			`port_status` SMALLINT UNSIGNED NOT NULL DEFAULT 7171,
			`creation` INT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`id`),
			UNIQUE KEY `worlds_name_unique` (`name`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8
	]]) then
		return false
	end

	if not db.query([[INSERT IGNORE INTO `worlds` (`id`, `name`, `type`, `motd`, `location`, `ip`, `port`, `port_status`, `creation`)
		VALUES (1, 'Forgotten', 'pvp', '', 'South America', '127.0.0.1', 7172, 7171, UNIX_TIMESTAMP())]]) then
		return false
	end

	local worldTables = {
		"players", "players_online", "account_viplist", "guilds", "guild_wars", "guild_war_kills",
		"houses", "house_lists", "house_guests", "market_offers", "market_history", "player_deaths", "player_deaths_backup"
	}
	for _, tableName in ipairs(worldTables) do
		if not ensureColumn(tableName, "world_id", "SMALLINT UNSIGNED NOT NULL DEFAULT 1") then
			return false
		end
	end

	-- House ids come from the map and can repeat in another world. Replace the
	-- legacy single-column key only after removing its dependent foreign keys.
	if tableExists("houses") and not hasCompositeHousePrimaryKey() then
		if not dropForeignKeysReferencingHouses() then
			return false
		end
		if not db.query("ALTER TABLE `houses` DROP PRIMARY KEY, ADD PRIMARY KEY (`id`, `world_id`)") then
			logMigration("Failed to convert houses primary key to (`id`, `world_id`)")
			return false
		end
		if tableExists("house_guests") and not db.query("ALTER TABLE `house_guests` DROP PRIMARY KEY, ADD PRIMARY KEY (`house_id`, `player_id`, `world_id`)") then
			logMigration("Failed to convert house_guests primary key to include world_id")
			return false
		end
	end

	if tableExists("house_lists") and not foreignKeyExists("house_lists", "fk_house_lists_house_world") then
		if not db.query("ALTER TABLE `house_lists` ADD CONSTRAINT `fk_house_lists_house_world` FOREIGN KEY (`house_id`, `world_id`) REFERENCES `houses` (`id`, `world_id`) ON DELETE CASCADE") then
			return false
		end
	end
	if tableExists("house_guests") and not foreignKeyExists("house_guests", "fk_house_guests_house_world") then
		if not db.query("ALTER TABLE `house_guests` ADD CONSTRAINT `fk_house_guests_house_world` FOREIGN KEY (`house_id`, `world_id`) REFERENCES `houses` (`id`, `world_id`) ON DELETE CASCADE") then
			return false
		end
	end

	local indexes = {
		{ "players", "idx_players_world_id", "INDEX `idx_players_world_id` (`world_id`)" },
		{ "players", "idx_players_account_world", "INDEX `idx_players_account_world` (`account_id`, `world_id`)" },
		{ "players_online", "idx_players_online_world_id", "INDEX `idx_players_online_world_id` (`world_id`)" },
		{ "guilds", "idx_guilds_world_id", "INDEX `idx_guilds_world_id` (`world_id`)" },
		{ "houses", "idx_houses_world_id", "INDEX `idx_houses_world_id` (`world_id`)" },
		{ "house_lists", "idx_house_lists_world_id", "INDEX `idx_house_lists_world_id` (`world_id`)" },
		{ "house_guests", "idx_house_guests_world_id", "INDEX `idx_house_guests_world_id` (`world_id`)" },
		{ "account_viplist", "idx_account_viplist_world_id", "INDEX `idx_account_viplist_world_id` (`world_id`)" },
		{ "market_offers", "idx_market_offers_world_sale_item", "INDEX `idx_market_offers_world_sale_item` (`world_id`, `sale`, `itemtype`)" },
		{ "market_history", "idx_market_history_world_player", "INDEX `idx_market_history_world_player` (`world_id`, `player_id`, `sale`)" },
		{ "guild_wars", "idx_guild_wars_world_id", "INDEX `idx_guild_wars_world_id` (`world_id`)" },
		{ "guild_war_kills", "idx_guild_war_kills_world_id", "INDEX `idx_guild_war_kills_world_id` (`world_id`)" },
	}
	for _, index in ipairs(indexes) do
		if not ensureIndex(index[1], index[2], index[3]) then
			return false
		end
	end

	-- Existing installations use these single-column unique keys. They must be
	-- replaced before equal character/guild names can exist in separate worlds.
	if indexExists("players", "name") then
		if not db.query("ALTER TABLE `players` DROP INDEX `name`") then return false end
	end
	if indexExists("guilds", "name") then
		if not db.query("ALTER TABLE `guilds` DROP INDEX `name`") then return false end
	end
	if indexExists("account_viplist", "account_player_index") then
		if not db.query("ALTER TABLE `account_viplist` DROP INDEX `account_player_index`") then return false end
	end

	local uniqueIndexes = {
		{ "players", "players_name_world_unique", "UNIQUE KEY `players_name_world_unique` (`name`, `world_id`)" },
		{ "guilds", "guilds_name_world_unique", "UNIQUE KEY `guilds_name_world_unique` (`name`, `world_id`)" },
		{ "account_viplist", "account_player_world_unique", "UNIQUE KEY `account_player_world_unique` (`account_id`, `player_id`, `world_id`)" },
	}
	for _, index in ipairs(uniqueIndexes) do
		if not ensureIndex(index[1], index[2], index[3]) then
			return false
		end
	end

	-- Do not add a FK to the MEMORY players_online table: MariaDB/MySQL do not support it.
	local foreignKeys = {
		{ "players", "fk_players_world" }, { "account_viplist", "fk_account_viplist_world" },
		{ "guilds", "fk_guilds_world" }, { "guild_wars", "fk_guild_wars_world" },
		{ "guild_war_kills", "fk_guild_war_kills_world" }, { "houses", "fk_houses_world" },
		{ "house_lists", "fk_house_lists_world" }, { "house_guests", "fk_house_guests_world" },
		{ "market_offers", "fk_market_offers_world" }, { "market_history", "fk_market_history_world" },
		{ "player_deaths", "fk_player_deaths_world" }, { "player_deaths_backup", "fk_player_deaths_backup_world" },
	}
	for _, foreignKey in ipairs(foreignKeys) do
		if not ensureWorldForeignKey(foreignKey[1], foreignKey[2]) then
			return false
		end
	end

	return true
end
