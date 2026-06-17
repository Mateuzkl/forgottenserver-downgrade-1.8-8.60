function onUpdateDatabase()
	logMigration("Updating database to version 54 (store XP boost)")

	local columns = {
		{ name = "xpboost_stamina", query = "ALTER TABLE `players` ADD `xpboost_stamina` SMALLINT UNSIGNED NOT NULL DEFAULT 0" },
		{ name = "xpboost_value", query = "ALTER TABLE `players` ADD `xpboost_value` TINYINT UNSIGNED NOT NULL DEFAULT 0" },
	}

	for _, column in ipairs(columns) do
		local resultId = db.storeQuery(
			"SELECT COUNT(*) AS `count` FROM `information_schema`.`COLUMNS`"
			.. " WHERE `TABLE_SCHEMA` = DATABASE()"
			.. " AND `TABLE_NAME` = 'players'"
			.. " AND `COLUMN_NAME` = " .. db.escapeString(column.name)
		)
		if not resultId then
			logMigration("Failed to check existence of column `" .. column.name .. "` in `players`")
			return false
		end

		local exists = result.getNumber(resultId, "count") > 0
		result.free(resultId)
		if not exists and not db.query(column.query) then
			logMigration("Failed to add column `" .. column.name .. "` to `players`")
			return false
		end
	end

	return true
end
