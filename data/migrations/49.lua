function onUpdateDatabase()
	print("[DB] Updating database to version 49 (Hireling)")

	db.query([[
		CREATE TABLE IF NOT EXISTS `player_hirelings` (
			`id` INT NOT NULL PRIMARY KEY AUTO_INCREMENT,
			`player_id` INT NOT NULL,
			`name` VARCHAR(255),
			`active` TINYINT UNSIGNED NOT NULL DEFAULT '0',
			`sex` TINYINT UNSIGNED NOT NULL DEFAULT '0',
			`posx` INT(11) NOT NULL DEFAULT '0',
			`posy` INT(11) NOT NULL DEFAULT '0',
			`posz` INT(11) NOT NULL DEFAULT '0',
			`lookbody` INT(11) NOT NULL DEFAULT '0',
			`lookfeet` INT(11) NOT NULL DEFAULT '0',
			`lookhead` INT(11) NOT NULL DEFAULT '0',
			`looklegs` INT(11) NOT NULL DEFAULT '0',
			`looktype` INT(11) NOT NULL DEFAULT '136',

			FOREIGN KEY(`player_id`) REFERENCES `players`(`id`)
				ON DELETE CASCADE
		)
	]])

	return true
end
