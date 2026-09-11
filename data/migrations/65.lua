function onUpdateDatabase()
	logMigration("Updating database to version 66 (player deaths search indexes for character store rename)")

	local indexes = {
		{tableName = "player_deaths", indexName = "idx_pd_killed_by", columnName = "killed_by"},
		{tableName = "player_deaths", indexName = "idx_pd_mostdamage_by", columnName = "mostdamage_by"},
		{tableName = "player_deaths_backup", indexName = "idx_pdb_killed_by", columnName = "killed_by"},
		{tableName = "player_deaths_backup", indexName = "idx_pdb_mostdamage_by", columnName = "mostdamage_by"}
	}

	local function indexExists(tableName, indexName)
		local resultId = db.storeQuery(
			"SELECT 1 FROM `information_schema`.`STATISTICS` WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = " ..
				db.escapeString(tableName) ..
				" AND `INDEX_NAME` = " ..
				db.escapeString(indexName) ..
				" LIMIT 1"
		)
		if resultId ~= false then
			result.free(resultId)
			return true
		end
		return false
	end

	for _, idx in ipairs(indexes) do
		local tableExists = (not db.tableExists or db.tableExists(idx.tableName))
		if tableExists and not indexExists(idx.tableName, idx.indexName) then
			db.query(string.format("ALTER TABLE `%s` ADD INDEX `%s` (`%s`(64))", idx.tableName, idx.indexName, idx.columnName))
		end
	end

	return true
end
