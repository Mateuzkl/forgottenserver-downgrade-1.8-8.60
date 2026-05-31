function onUpdateDatabase()
	logMigration("> Updating database to version 43 (stats performance indexes)")

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

	local indexes = {
		{
			tableName = "players",
			indexName = "idx_players_deletion",
			query = "ALTER TABLE `players` ADD INDEX `idx_players_deletion` (`deletion`)"
		},
		{
			tableName = "ip_bans",
			indexName = "idx_ip_bans_expires_at",
			query = "ALTER TABLE `ip_bans` ADD INDEX `idx_ip_bans_expires_at` (`expires_at`)"
		},
		{
			tableName = "player_deaths",
			indexName = "idx_player_deaths_unjustified_kills",
			query = "ALTER TABLE `player_deaths` ADD INDEX `idx_player_deaths_unjustified_kills` (`killed_by`(64), `is_player`, `unjustified`, `time`)"
		}
	}

	for _, index in ipairs(indexes) do
		if not indexExists(index.tableName, index.indexName) and not db.query(index.query) then
			logMigration("Failed to add index `" .. index.indexName .. "` on `" .. index.tableName .. "`")
			return false
		end
	end

	return true
end
