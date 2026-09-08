function onUpdateDatabase()
	logMigration("Updating database to version 65 (bestiary persistence schema completeness)")

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

	return true
end
