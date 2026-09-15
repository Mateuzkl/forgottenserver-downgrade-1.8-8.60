function onUpdateDatabase()
	logMigration("Updating database to version 68 (atomic Echo Warden first-kill rewards)")

	if not db.query([[
		CREATE TABLE IF NOT EXISTS `player_echo_warden_rewards` (
			`player_id` INT NOT NULL,
			`raceid` SMALLINT UNSIGNED NOT NULL,
			PRIMARY KEY (`player_id`, `raceid`),
			CONSTRAINT `fk_player_echo_warden_rewards_player`
				FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8mb4
	]]) then
		logMigration("Failed to create player_echo_warden_rewards")
		return false
	end

	return true
end
