function onUpdateDatabase()
	logMigration("Updating database to version 64 (boss difficulty progression)")
	return db.query([[
		CREATE TABLE IF NOT EXISTS `player_boss_difficulty` (
			`player_id` INT NOT NULL,
			`boss_race_id` SMALLINT UNSIGNED NOT NULL,
			`unlocked_difficulty` INT UNSIGNED NOT NULL DEFAULT 1,
			`highest_defeated` INT UNSIGNED NOT NULL DEFAULT 0,
			`selected_difficulty` INT UNSIGNED NOT NULL DEFAULT 1,
			`bad_luck` INT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `boss_race_id`),
			CONSTRAINT `player_boss_difficulty_player_fk`
				FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8mb4 COLLATE=utf8mb4_unicode_ci
	]])
end
