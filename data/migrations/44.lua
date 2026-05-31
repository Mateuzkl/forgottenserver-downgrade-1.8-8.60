function onUpdateDatabase()
	logMigration("Updating database to version 44 (weapon proficiency)")
	local success = db.query([[
		CREATE TABLE IF NOT EXISTS `player_weapon_proficiency` (
			`player_id` int NOT NULL,
			`item_id` smallint unsigned NOT NULL,
			`experience` int unsigned NOT NULL DEFAULT '0',
			`perks` varchar(64) NOT NULL DEFAULT '',
			PRIMARY KEY (`player_id`, `item_id`),
			FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;
	]])
	if not success then
		logMigration("Failed to create player_weapon_proficiency table")
		return false
	end
	return true
end
