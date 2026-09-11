function onUpdateDatabase()
	logMigration("Updating database to version 66 (player deaths search indexes for character store rename)")

	local indexes = {
		{tableName = "player_deaths", indexName = "idx_pd_killed_by", columnName = "killed_by"},
		{tableName = "player_deaths", indexName = "idx_pd_mostdamage_by", columnName = "mostdamage_by"},
		{tableName = "player_deaths_backup", indexName = "idx_pdb_killed_by", columnName = "killed_by"},
		{tableName = "player_deaths_backup", indexName = "idx_pdb_mostdamage_by", columnName = "mostdamage_by"}
	}

	local function hasFirstColumnIndex(tableName, columnName)
		local query = string.format(
			"SELECT 1 FROM `information_schema`.`STATISTICS` " ..
			"WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = %s AND `COLUMN_NAME` = %s AND `SEQ_IN_INDEX` = 1 LIMIT 1",
			db.escapeString(tableName),
			db.escapeString(columnName)
		)
		local resultId = db.storeQuery(query)
		if resultId ~= false then
			result.free(resultId)
			return true
		end
		return false
	end

	for _, idx in ipairs(indexes) do
		local tableExists = (not db.tableExists or db.tableExists(idx.tableName))
		if tableExists and not hasFirstColumnIndex(idx.tableName, idx.columnName) then
			local query = string.format("ALTER TABLE `%s` ADD INDEX `%s` (`%s`(64))", idx.tableName, idx.indexName, idx.columnName)
			if not db.query(query) then
				print(string.format("[Migration 65] Failed to add index '%s' on `%s`.`%s`", idx.indexName, idx.tableName, idx.columnName))
				return false
			end
		end
	end

	return true
end
