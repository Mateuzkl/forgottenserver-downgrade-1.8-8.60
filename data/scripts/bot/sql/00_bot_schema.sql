-- Bot player registry for TFS 1.8.
-- Safe to run more than once.

CREATE TABLE IF NOT EXISTS `bot_players` (
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
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;
