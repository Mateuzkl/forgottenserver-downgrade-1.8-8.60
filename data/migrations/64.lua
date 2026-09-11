function onUpdateDatabase()
	logMigration("Updating database to version 65 (bestiary persistence schema completeness)")

	local function ensureIndex(tableName, indexName, definition)
		local indexResult = db.storeQuery(
			"SELECT 1 FROM `information_schema`.`STATISTICS`"
				.. " WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = " .. db.escapeString(tableName)
				.. " AND `INDEX_NAME` = " .. db.escapeString(indexName) .. " LIMIT 1"
		)
		if indexResult then
			result.free(indexResult)
			return true
		end
		return db.query("ALTER TABLE `" .. tableName .. "` ADD " .. definition)
	end

	local function reconcilePlayerForeignKey(tableName, constraintName)
		local foreignKeyResult = db.storeQuery([[
			SELECT `k`.`CONSTRAINT_NAME`, `r`.`DELETE_RULE`
			FROM `information_schema`.`KEY_COLUMN_USAGE` AS `k`
			JOIN `information_schema`.`REFERENTIAL_CONSTRAINTS` AS `r`
				ON `r`.`CONSTRAINT_SCHEMA` = `k`.`CONSTRAINT_SCHEMA`
				AND `r`.`CONSTRAINT_NAME` = `k`.`CONSTRAINT_NAME`
			WHERE `k`.`TABLE_SCHEMA` = DATABASE()
				AND `k`.`TABLE_NAME` = ]] .. db.escapeString(tableName) .. [[
				AND `k`.`COLUMN_NAME` = 'player_id'
				AND `k`.`REFERENCED_TABLE_NAME` = 'players'
				AND `k`.`REFERENCED_COLUMN_NAME` = 'id'
			LIMIT 1
		]])

		local existingName
		local deleteRule
		if foreignKeyResult then
			existingName = result.getString(foreignKeyResult, "CONSTRAINT_NAME")
			deleteRule = result.getString(foreignKeyResult, "DELETE_RULE")
			result.free(foreignKeyResult)
		end

		if existingName and deleteRule ~= "CASCADE" then
			if not existingName:match("^[%w_]+$") or not db.query(
				"ALTER TABLE `" .. tableName .. "` DROP FOREIGN KEY `" .. existingName .. "`"
			) then
				return false
			end
			existingName = nil
		end

		if existingName then
			return true
		end

		if not db.query(
			"DELETE `child` FROM `" .. tableName .. "` AS `child`"
				.. " LEFT JOIN `players` AS `player` ON `player`.`id` = `child`.`player_id`"
				.. " WHERE `player`.`id` IS NULL"
		) then
			return false
		end

		return db.query(
			"ALTER TABLE `" .. tableName .. "` ADD CONSTRAINT `" .. constraintName .. "`"
				.. " FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE"
		)
	end

	if not db.query([[
		CREATE TABLE IF NOT EXISTS `player_bestiary_charms` (
			`player_id` INT NOT NULL,
			`charm_id` TINYINT UNSIGNED NOT NULL,
			`unlocked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`raceid` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `charm_id`),
			KEY `idx_player_bestiary_charms_race` (`player_id`, `raceid`),
			CONSTRAINT `fk_player_bestiary_charms_player`
				FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8mb4
	]]) then
		logMigration("Failed to create player_bestiary_charms")
		return false
	end

	-- These tables may already have been created by the former Lua fallback.
	-- CREATE TABLE IF NOT EXISTS does not repair that legacy shape, so normalize
	-- it before marking the migration complete while preserving all valid rows.
	if not db.query([[UPDATE `player_bestiary_charms` SET `unlocked` = 0 WHERE `unlocked` < 0]])
		or not db.query([[ALTER TABLE `player_bestiary_charms` MODIFY `unlocked` TINYINT UNSIGNED NOT NULL DEFAULT 0]])
		or not ensureIndex("player_bestiary_charms", "idx_player_bestiary_charms_race",
			"KEY `idx_player_bestiary_charms_race` (`player_id`, `raceid`)")
		or not reconcilePlayerForeignKey("player_bestiary_charms", "fk_player_bestiary_charms_player") then
		logMigration("Failed to reconcile player_bestiary_charms")
		return false
	end

	if not db.query([[
		CREATE TABLE IF NOT EXISTS `player_bestiary_resources` (
			`player_id` INT NOT NULL,
			`minor_charm_echoes` INT UNSIGNED NOT NULL DEFAULT 0,
			`max_minor_charm_echoes` INT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`),
			CONSTRAINT `fk_player_bestiary_resources_player`
				FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8mb4
	]]) then
		logMigration("Failed to create player_bestiary_resources")
		return false
	end

	if not reconcilePlayerForeignKey("player_bestiary_resources", "fk_player_bestiary_resources_player") then
		logMigration("Failed to reconcile player_bestiary_resources")
		return false
	end

	if not db.query([[
		CREATE TABLE IF NOT EXISTS `player_bestiary_tracker` (
			`player_id` INT NOT NULL,
			`raceid` SMALLINT UNSIGNED NOT NULL,
			`slot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `raceid`),
			KEY `idx_player_bestiary_tracker_slot` (`player_id`, `slot`),
			CONSTRAINT `fk_player_bestiary_tracker_player`
				FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8mb4
	]]) then
		logMigration("Failed to create player_bestiary_tracker")
		return false
	end

	if not ensureIndex("player_bestiary_tracker", "idx_player_bestiary_tracker_slot",
		"KEY `idx_player_bestiary_tracker_slot` (`player_id`, `slot`)")
		or not reconcilePlayerForeignKey("player_bestiary_tracker", "fk_player_bestiary_tracker_player") then
		logMigration("Failed to reconcile player_bestiary_tracker")
		return false
	end

	for _, tableName in ipairs({
		"player_bestiary_charms",
		"player_bestiary_resources",
		"player_bestiary_tracker",
	}) do
		if not db.query("ALTER TABLE `" .. tableName .. "` DEFAULT CHARACTER SET=utf8mb4") then
			logMigration("Failed to convert " .. tableName .. " to utf8mb4")
			return false
		end
	end

	return true
end
