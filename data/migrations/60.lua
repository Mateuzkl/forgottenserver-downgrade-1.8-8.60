function onUpdateDatabase()
	logMigration("Updating database to version 61 (move Supply Stash persistence to C++)")

	local function columnExists(columnName)
		local queryResult = db.storeQuery(
			"SELECT COUNT(*) AS `count` FROM `information_schema`.`COLUMNS`" ..
			" WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'player_supplystash'" ..
			" AND `COLUMN_NAME` = '" .. columnName .. "'"
		)
		local exists = queryResult and result.getNumber(queryResult, "count") > 0
		if queryResult then
			result.free(queryResult)
		end
		return exists
	end

	local function primaryKeySignature()
		local columns = {}
		local queryResult = db.storeQuery(
			"SELECT `COLUMN_NAME` FROM `information_schema`.`STATISTICS`" ..
			" WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'player_supplystash'" ..
			" AND `INDEX_NAME` = 'PRIMARY' ORDER BY `SEQ_IN_INDEX`"
		)
		if queryResult then
			repeat
				columns[#columns + 1] = result.getString(queryResult, "COLUMN_NAME")
			until not result.next(queryResult)
			result.free(queryResult)
		end
		return table.concat(columns, ",")
	end

	-- Returns "cascade" when the FK exists with ON DELETE CASCADE,
	-- "wrong_rule" and the constraint name when it exists with a different rule,
	-- or false when no matching FK exists.
	local function checkPlayerForeignKey()
		local queryResult = db.storeQuery([[
			SELECT `rc`.`CONSTRAINT_NAME`, `rc`.`DELETE_RULE`
			FROM `information_schema`.`KEY_COLUMN_USAGE` AS `kcu`
			JOIN `information_schema`.`REFERENTIAL_CONSTRAINTS` AS `rc`
			  ON `rc`.`CONSTRAINT_SCHEMA` = `kcu`.`CONSTRAINT_SCHEMA`
			  AND `rc`.`CONSTRAINT_NAME` = `kcu`.`CONSTRAINT_NAME`
			WHERE `kcu`.`CONSTRAINT_SCHEMA` = DATABASE()
			  AND `kcu`.`TABLE_NAME` = 'player_supplystash'
			  AND `kcu`.`COLUMN_NAME` = 'player_id'
			  AND `kcu`.`REFERENCED_TABLE_NAME` = 'players'
			  AND `kcu`.`REFERENCED_COLUMN_NAME` = 'id'
			LIMIT 1
		]])
		if not queryResult then
			return false
		end
		local constraintName = result.getString(queryResult, "CONSTRAINT_NAME")
		local deleteRule = result.getString(queryResult, "DELETE_RULE")
		result.free(queryResult)
		if deleteRule == "CASCADE" then
			return "cascade"
		end
		return "wrong_rule", constraintName
	end

	if not db.query([[
		CREATE TABLE IF NOT EXISTS `player_supplystash` (
			`player_id` INT NOT NULL,
			`itemtype` SMALLINT UNSIGNED NOT NULL,
			`tier` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`amount` INT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `itemtype`, `tier`),
			CONSTRAINT `player_supplystash_player_fk`
				FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]]) then
		return false
	end

	if not columnExists("tier") and not db.query(
		"ALTER TABLE `player_supplystash` ADD COLUMN `tier` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `itemtype`"
	) then
		return false
	end

	local primaryKey = primaryKeySignature()
	if primaryKey ~= "player_id,itemtype,tier" then
		local query
		if primaryKey == "" then
			query = "ALTER TABLE `player_supplystash` ADD PRIMARY KEY (`player_id`, `itemtype`, `tier`)"
		else
			query = "ALTER TABLE `player_supplystash` DROP PRIMARY KEY, ADD PRIMARY KEY (`player_id`, `itemtype`, `tier`)"
		end
		if not db.query(query) or primaryKeySignature() ~= "player_id,itemtype,tier" then
			logMigration("Failed to enforce the Supply Stash primary key; existing data was left untouched")
			return false
		end
	end

	local fkStatus, constraintName = checkPlayerForeignKey()
	if fkStatus == "wrong_rule" then
		if not db.query(
			"ALTER TABLE `player_supplystash` DROP FOREIGN KEY `" .. constraintName .. "`"
		) then
			return false
		end
		fkStatus = false
	end

	if not fkStatus then
		if not db.query([[
			DELETE `stash` FROM `player_supplystash` AS `stash`
			LEFT JOIN `players` AS `player` ON `player`.`id` = `stash`.`player_id`
			WHERE `player`.`id` IS NULL
		]]) or not db.query([[
			ALTER TABLE `player_supplystash`
			ADD CONSTRAINT `player_supplystash_player_fk`
			FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		]]) then
			return false
		end
	end

	return true
end
