function onUpdateDatabase()
	logMigration("Updating database to version 56 (bot players)")

	local queries = {
		[[CREATE TABLE IF NOT EXISTS `bot_players` (
			`player_id` int NOT NULL,
			`enabled` tinyint NOT NULL DEFAULT '1',
			`auto_spawn` tinyint NOT NULL DEFAULT '0',
			`last_spawn` bigint unsigned NOT NULL DEFAULT '0',
			`last_despawn` bigint unsigned NOT NULL DEFAULT '0',
			`created_at` bigint unsigned NOT NULL DEFAULT '0',
			`updated_at` bigint unsigned NOT NULL DEFAULT '0',
			PRIMARY KEY (`player_id`),
			KEY `idx_bot_players_auto_spawn` (`enabled`, `auto_spawn`),
			CONSTRAINT `fk_bot_players_player` FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8]]
	}

	for _, query in ipairs(queries) do
		if not db.query(query) then
			logMigration("Failed to create bot player tables")
			return false
		end
	end

	return true
end
