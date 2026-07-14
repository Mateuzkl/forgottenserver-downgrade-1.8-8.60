function onUpdateDatabase()
	logMigration("Updating database to version 58 (repair Bestiary kill ownership)")

	if not db.query([[CREATE TABLE IF NOT EXISTS `player_bestiary_kills` (
		`player_id` INT NOT NULL,
		`raceid` SMALLINT UNSIGNED NOT NULL,
		`kills` INT UNSIGNED NOT NULL DEFAULT 0,
		PRIMARY KEY (`player_id`, `raceid`),
		CONSTRAINT `fk_player_bestiary_kills_player`
			FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
	) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4]]) then
		logMigration("Failed to create player_bestiary_kills")
		return false
	end

	local foreignKeyResult = db.storeQuery([[
		SELECT `k`.`CONSTRAINT_NAME`, `r`.`DELETE_RULE`
		FROM `information_schema`.`KEY_COLUMN_USAGE` AS `k`
		JOIN `information_schema`.`REFERENTIAL_CONSTRAINTS` AS `r`
			ON `r`.`CONSTRAINT_SCHEMA` = `k`.`CONSTRAINT_SCHEMA`
			AND `r`.`CONSTRAINT_NAME` = `k`.`CONSTRAINT_NAME`
		WHERE `k`.`TABLE_SCHEMA` = DATABASE()
			AND `k`.`TABLE_NAME` = 'player_bestiary_kills'
			AND `k`.`COLUMN_NAME` = 'player_id'
			AND `k`.`REFERENCED_TABLE_NAME` = 'players'
			AND `k`.`REFERENCED_COLUMN_NAME` = 'id'
		LIMIT 1
	]])

	local foreignKeyName
	local deleteRule
	if foreignKeyResult then
		foreignKeyName = result.getString(foreignKeyResult, "CONSTRAINT_NAME")
		deleteRule = result.getString(foreignKeyResult, "DELETE_RULE")
		result.free(foreignKeyResult)
	end

	if foreignKeyName and deleteRule ~= "CASCADE" then
		if not foreignKeyName:match("^[%w_]+$") or not db.query(
			"ALTER TABLE `player_bestiary_kills` DROP FOREIGN KEY `" .. foreignKeyName .. "`"
		) then
			logMigration("Failed to replace the player_bestiary_kills foreign key")
			return false
		end
		foreignKeyName = nil
	end

	if not foreignKeyName then
		if not db.query([[
			DELETE `kills` FROM `player_bestiary_kills` AS `kills`
			LEFT JOIN `players` AS `player` ON `player`.`id` = `kills`.`player_id`
			WHERE `player`.`id` IS NULL
		]]) or not db.query([[
			ALTER TABLE `player_bestiary_kills`
			ADD CONSTRAINT `fk_player_bestiary_kills_player`
			FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		]]) then
			logMigration("Failed to add the player_bestiary_kills ownership constraint")
			return false
		end
	end

	return true
end
