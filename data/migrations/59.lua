function onUpdateDatabase()
	logMigration("Updating database to version 60 (ensure expanded blessings columns)")

	local blessingColumns = {
		"blessings1", "blessings2", "blessings3", "blessings4",
		"blessings5", "blessings6", "blessings7", "blessings8"
	}
	local previousColumn = "blessings"

	for _, columnName in ipairs(blessingColumns) do
		local columnResult = db.storeQuery(
			"SELECT 1 FROM `information_schema`.`COLUMNS`"
				.. " WHERE `TABLE_SCHEMA` = DATABASE()"
				.. " AND `TABLE_NAME` = 'players'"
				.. " AND `COLUMN_NAME` = '" .. columnName .. "' LIMIT 1"
		)

		if columnResult then
			result.free(columnResult)
		elseif not db.query(
			"ALTER TABLE `players` ADD COLUMN `" .. columnName
				.. "` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `" .. previousColumn .. "`"
		) then
			logMigration("Failed to add players." .. columnName)
			return false
		end

		previousColumn = columnName
	end

	return true
end
