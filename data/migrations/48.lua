function onUpdateDatabase()
	logMigration("Updating database to version 49 (hirelings)")

	db.query([[
		CREATE TABLE IF NOT EXISTS `player_hirelings` (
			`id` INT NOT NULL AUTO_INCREMENT,
			`player_id` INT NOT NULL,
			`name` VARCHAR(32) NOT NULL,
			`active` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`sex` TINYINT UNSIGNED NOT NULL DEFAULT 1,
			`posx` INT NOT NULL DEFAULT 0,
			`posy` INT NOT NULL DEFAULT 0,
			`posz` INT NOT NULL DEFAULT 0,
			`lookbody` INT NOT NULL DEFAULT 34,
			`lookfeet` INT NOT NULL DEFAULT 116,
			`lookhead` INT NOT NULL DEFAULT 97,
			`looklegs` INT NOT NULL DEFAULT 3,
			`looktype` INT NOT NULL DEFAULT 1108,
			PRIMARY KEY (`id`),
			KEY `idx_player_hirelings_player_id` (`player_id`),
			CONSTRAINT `fk_player_hirelings_player_id`
				FOREIGN KEY (`player_id`) REFERENCES `players` (`id`)
				ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARSET=utf8;
	]])

	return true
end
