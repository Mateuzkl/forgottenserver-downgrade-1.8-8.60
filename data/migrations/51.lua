function onUpdateDatabase()
	logMigration("Updating database to version 52 (task hunting/bounty/soulseals columns in players)")

	-- Add task hunting / bounty / soulseals point columns to the players table
	local columns = {
		{ name = "task_hunting_points", query = "ALTER TABLE `players` ADD `task_hunting_points` BIGINT UNSIGNED NOT NULL DEFAULT 0" },
		{ name = "bounty_points", query = "ALTER TABLE `players` ADD `bounty_points` BIGINT UNSIGNED NOT NULL DEFAULT 0" },
		{ name = "soulseals_points", query = "ALTER TABLE `players` ADD `soulseals_points` BIGINT UNSIGNED NOT NULL DEFAULT 0" },
		{ name = "has_weekly_expansion", query = "ALTER TABLE `players` ADD `has_weekly_expansion` TINYINT UNSIGNED NOT NULL DEFAULT 0" },
	}

	for _, col in ipairs(columns) do
		local resultId = db.storeQuery(
			"SELECT COUNT(*) AS `count` FROM `information_schema`.`COLUMNS`"
			.. " WHERE `TABLE_SCHEMA` = DATABASE()"
			.. " AND `TABLE_NAME` = 'players'"
			.. " AND `COLUMN_NAME` = " .. db.escapeString(col.name)
		)
		if not resultId then
			logMigration("Failed to check existence of column `" .. col.name .. "` in `players`")
			return false
		end

		local exists = result.getNumber(resultId, "count") > 0
		result.free(resultId)
		if not exists then
			if not db.query(col.query) then
				logMigration("Failed to add column `" .. col.name .. "` to `players`")
				return false
			end
		end
	end

	return true
end
