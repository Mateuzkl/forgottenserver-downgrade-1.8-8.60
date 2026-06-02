function onUpdateDatabase()
	logMigration("Updating database to version 46 (Bosstiary and Hunting Tasks)")

	local queries = {
		[[
			CREATE TABLE IF NOT EXISTS `player_bosstiary` (
				`player_id` INT NOT NULL,
				`points` INT UNSIGNED NOT NULL DEFAULT 0,
				`slot_one` INT UNSIGNED NOT NULL DEFAULT 0,
				`slot_two` INT UNSIGNED NOT NULL DEFAULT 0,
				`remove_times` INT UNSIGNED NOT NULL DEFAULT 0,
				PRIMARY KEY (`player_id`)
			) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
		]],
		[[
			CREATE TABLE IF NOT EXISTS `player_bosstiary_tracker` (
				`player_id` INT NOT NULL,
				`bossid` INT UNSIGNED NOT NULL,
				`slot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
				PRIMARY KEY (`player_id`, `bossid`),
				KEY `idx_player_bosstiary_tracker_slot` (`player_id`, `slot`)
			) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
		]],
		[[
			CREATE TABLE IF NOT EXISTS `player_hunting_tasks` (
				`player_id` INT NOT NULL,
				`slot` TINYINT UNSIGNED NOT NULL,
				`state` TINYINT UNSIGNED NOT NULL DEFAULT 2,
				`raceid` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
				`race_list` TEXT NOT NULL,
				`rarity` TINYINT UNSIGNED NOT NULL DEFAULT 1,
				`upgraded` TINYINT(1) NOT NULL DEFAULT 0,
				`kills` INT UNSIGNED NOT NULL DEFAULT 0,
				`reroll_at` BIGINT UNSIGNED NOT NULL DEFAULT 0,
				`disabled_until` BIGINT UNSIGNED NOT NULL DEFAULT 0,
				PRIMARY KEY (`player_id`, `slot`)
			) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
		]],
		[[
			CREATE TABLE IF NOT EXISTS `player_hunting_task_points` (
				`player_id` INT NOT NULL,
				`points` BIGINT UNSIGNED NOT NULL DEFAULT 0,
				PRIMARY KEY (`player_id`)
			) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
		]],
	}

	for _, query in ipairs(queries) do
		if not db.query(query) then
			return false
		end
	end
	return true
end
