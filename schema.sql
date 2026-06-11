CREATE TABLE IF NOT EXISTS `accounts` (
  `id` int NOT NULL AUTO_INCREMENT,
  `name` varchar(32) NOT NULL,
  `password` char(40) NOT NULL,
  `secret` char(16) DEFAULT NULL,
  `type` int NOT NULL DEFAULT '1',
  `premium_ends_at` int unsigned NOT NULL DEFAULT '0',
  `email` varchar(255) NOT NULL DEFAULT '',
  `creation` int NOT NULL DEFAULT '0',
  `tibia_coins` int unsigned NOT NULL DEFAULT '0',
  `points_second` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  UNIQUE KEY `name` (`name`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

INSERT INTO `accounts` (`id`, `name`, `password`, `secret`, `type`, `premium_ends_at`, `email`, `creation`, `tibia_coins`, `points_second`) VALUES
(1, '1', '356a192b7913b04c54574d18c28d46e6395428ab', NULL, 1, 0, '', 0, 0, 0);

CREATE TABLE IF NOT EXISTS `players` (
  `id` int NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `group_id` int NOT NULL DEFAULT '1',
  `account_id` int NOT NULL DEFAULT '0',
  `level` int NOT NULL DEFAULT '1',
  `reset` int(11) NOT NULL DEFAULT 0,
  `vocation` int NOT NULL DEFAULT '0',
  `health` int NOT NULL DEFAULT '150',
  `healthmax` int NOT NULL DEFAULT '150',
  `experience` bigint unsigned NOT NULL DEFAULT '0',
  `lookbody` int NOT NULL DEFAULT '0',
  `lookfeet` int NOT NULL DEFAULT '0',
  `lookhead` int NOT NULL DEFAULT '0',
  `looklegs` int NOT NULL DEFAULT '0',
  `looktype` int NOT NULL DEFAULT '136',
  `lookaddons` int NOT NULL DEFAULT '0',
  `lookmount` smallint UNSIGNED NOT NULL DEFAULT '0',
  `currentmount` smallint UNSIGNED NOT NULL DEFAULT '0',
  `randomizemount` tinyint NOT NULL DEFAULT '0',
  `direction` tinyint unsigned NOT NULL DEFAULT '2',
  `maglevel` int NOT NULL DEFAULT '0',
  `mana` int NOT NULL DEFAULT '0',
  `manamax` int NOT NULL DEFAULT '0',
  `manaspent` bigint unsigned NOT NULL DEFAULT '0',
  `soul` int unsigned NOT NULL DEFAULT '0',
  `town_id` int NOT NULL DEFAULT '1',
  `posx` int NOT NULL DEFAULT '0',
  `posy` int NOT NULL DEFAULT '0',
  `posz` int NOT NULL DEFAULT '0',
  `conditions` blob DEFAULT NULL,
  `cap` int NOT NULL DEFAULT '400',
  `sex` int NOT NULL DEFAULT '0',
  `lastlogin` bigint unsigned NOT NULL DEFAULT '0',
  `lastip` int unsigned NOT NULL DEFAULT '0',
  `save` tinyint NOT NULL DEFAULT '1',
  `skull` tinyint NOT NULL DEFAULT '0',
  `skulltime` bigint NOT NULL DEFAULT '0',
  `lastlogout` bigint unsigned NOT NULL DEFAULT '0',
  `blessings` tinyint NOT NULL DEFAULT '0',
  `onlinetime` bigint NOT NULL DEFAULT '0',
  `deletion` bigint NOT NULL DEFAULT '0',
  `balance` bigint unsigned NOT NULL DEFAULT '0',
  `bonus_rerolls` bigint unsigned NOT NULL DEFAULT '0',
  `charmpoints` int unsigned NOT NULL DEFAULT '0',
  `protection_time` bigint unsigned NOT NULL DEFAULT '0',
  `offlinetraining_time` smallint unsigned NOT NULL DEFAULT '43200',
  `offlinetraining_skill` int NOT NULL DEFAULT '-1',
  `stamina` smallint unsigned NOT NULL DEFAULT '2520',
  `skill_fist` int unsigned NOT NULL DEFAULT 10,
  `skill_fist_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_club` int unsigned NOT NULL DEFAULT 10,
  `skill_club_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_sword` int unsigned NOT NULL DEFAULT 10,
  `skill_sword_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_axe` int unsigned NOT NULL DEFAULT 10,
  `skill_axe_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_dist` int unsigned NOT NULL DEFAULT 10,
  `skill_dist_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_shielding` int unsigned NOT NULL DEFAULT 10,
  `skill_shielding_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_fishing` int unsigned NOT NULL DEFAULT 10,
  `skill_fishing_tries` bigint unsigned NOT NULL DEFAULT 0,
  `token_protected` tinyint NOT NULL DEFAULT '0',
  `token_hash` varchar(64) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  UNIQUE KEY `name` (`name`),
  FOREIGN KEY (`account_id`) REFERENCES `accounts` (`id`) ON DELETE CASCADE,
  KEY `vocation` (`vocation`),
  KEY `idx_players_deletion` (`deletion`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

INSERT INTO `players` (`id`, `name`, `group_id`, `account_id`, `level`, `vocation`, `health`, `healthmax`, `experience`, `lookbody`, `lookfeet`, `lookhead`, `looklegs`, `looktype`, `lookaddons`, `currentmount`, `randomizemount`, `direction`, `maglevel`, `mana`, `manamax`, `manaspent`, `soul`, `town_id`, `posx`, `posy`, `posz`, `conditions`, `cap`, `sex`, `lastlogin`, `lastip`, `save`, `skull`, `skulltime`, `lastlogout`, `blessings`, `onlinetime`, `deletion`, `balance`, `offlinetraining_time`, `offlinetraining_skill`, `stamina`, `skill_fist`, `skill_fist_tries`, `skill_club`, `skill_club_tries`, `skill_sword`, `skill_sword_tries`, `skill_axe`, `skill_axe_tries`, `skill_dist`, `skill_dist_tries`, `skill_shielding`, `skill_shielding_tries`, `skill_fishing`, `skill_fishing_tries`) VALUES
(1, 'Account Manager', 1, 1, 1, 0, 150, 150, 0, 0, 0, 0, 0, 110, 0, 0, 0, 2, 0, 0, 0, 0, 0, 1, 50, 50, 7, NULL, 400, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 43200, -1, 2520, 10, 0, 10, 0, 10, 0, 10, 0, 10, 0, 10, 0, 10, 0);

CREATE TABLE IF NOT EXISTS `player_autolootconfig` (
  `player_id` int(11) NOT NULL,
  `config` blob NOT NULL,
  PRIMARY KEY (`player_id`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `account_bans` (
  `account_id` int NOT NULL,
  `reason` varchar(255) NOT NULL,
  `banned_at` bigint NOT NULL,
  `expires_at` bigint NOT NULL,
  `banned_by` int NOT NULL,
  PRIMARY KEY (`account_id`),
  FOREIGN KEY (`account_id`) REFERENCES `accounts` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  FOREIGN KEY (`banned_by`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `account_ban_history` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `account_id` int NOT NULL,
  `reason` varchar(255) NOT NULL,
  `banned_at` bigint NOT NULL,
  `expired_at` bigint NOT NULL,
  `banned_by` int NOT NULL,
  PRIMARY KEY (`id`),
  FOREIGN KEY (`account_id`) REFERENCES `accounts` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  FOREIGN KEY (`banned_by`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `account_storage` (
  `account_id` int NOT NULL,
  `key` int unsigned NOT NULL,
  `value` int NOT NULL,
  PRIMARY KEY (`account_id`, `key`),
  FOREIGN KEY (`account_id`) REFERENCES `accounts`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `ip_bans` (
  `ip` int unsigned NOT NULL,
  `reason` varchar(255) NOT NULL,
  `banned_at` bigint NOT NULL,
  `expires_at` bigint NOT NULL,
  `banned_by` int NOT NULL,
  PRIMARY KEY (`ip`),
  KEY `idx_ip_bans_expires_at` (`expires_at`),
  FOREIGN KEY (`banned_by`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_namelocks` (
  `player_id` int NOT NULL,
  `reason` varchar(255) NOT NULL,
  `namelocked_at` bigint NOT NULL,
  `namelocked_by` int NOT NULL,
  PRIMARY KEY (`player_id`),
  FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  FOREIGN KEY (`namelocked_by`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `account_viplist` (
  `account_id` int NOT NULL COMMENT 'id of account whose viplist entry it is',
  `player_id` int NOT NULL COMMENT 'id of target player of viplist entry',
  `description` varchar(128) NOT NULL DEFAULT '',
  UNIQUE KEY `account_player_index` (`account_id`,`player_id`),
  FOREIGN KEY (`account_id`) REFERENCES `accounts` (`id`) ON DELETE CASCADE,
  FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `game_storage` (
  `key` int UNSIGNED NOT NULL DEFAULT '0',
  `value` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `guilds` (
  `id` int NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `ownerid` int NOT NULL,
  `creationdata` int NOT NULL,
  `motd` varchar(255) NOT NULL DEFAULT '',
  `balance` bigint(20) UNSIGNED NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  UNIQUE KEY (`name`),
  UNIQUE KEY (`ownerid`),
  FOREIGN KEY (`ownerid`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `guild_invites` (
  `player_id` int NOT NULL DEFAULT '0',
  `guild_id` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`player_id`,`guild_id`),
  FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE,
  FOREIGN KEY (`guild_id`) REFERENCES `guilds` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `guild_ranks` (
  `id` int NOT NULL AUTO_INCREMENT,
  `guild_id` int NOT NULL COMMENT 'guild',
  `name` varchar(255) NOT NULL COMMENT 'rank name',
  `level` int NOT NULL COMMENT 'rank level - leader, vice, member, maybe something else',
  PRIMARY KEY (`id`),
  FOREIGN KEY (`guild_id`) REFERENCES `guilds` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `guild_membership` (
  `player_id` int NOT NULL,
  `guild_id` int NOT NULL,
  `rank_id` int NOT NULL,
  `nick` varchar(15) NOT NULL DEFAULT '',
  PRIMARY KEY (`player_id`),
  FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  FOREIGN KEY (`guild_id`) REFERENCES `guilds` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  FOREIGN KEY (`rank_id`) REFERENCES `guild_ranks` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `guild_wars` (
  `id` int NOT NULL AUTO_INCREMENT,
  `guild1` int NOT NULL DEFAULT '0',
  `guild2` int NOT NULL DEFAULT '0',
  `name1` varchar(255) NOT NULL,
  `name2` varchar(255) NOT NULL,
  `status` tinyint NOT NULL DEFAULT '0',
  `started` bigint NOT NULL DEFAULT '0',
  `ended` bigint NOT NULL DEFAULT '0',
  `fraglimit` int NOT NULL DEFAULT '0',
  `payment` bigint NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `guild1` (`guild1`),
  KEY `guild2` (`guild2`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `guild_war_kills` (
  `id` int NOT NULL AUTO_INCREMENT,
  `war_id` int NOT NULL,
  `killer_guild` int NOT NULL,
  `killer` int NOT NULL,
  `victim` int NOT NULL,
  `time` bigint NOT NULL,
  PRIMARY KEY (`id`),
  KEY `war_id` (`war_id`),
  KEY `killer_guild` (`killer_guild`),
  FOREIGN KEY (`war_id`) REFERENCES `guild_wars` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `houses` (
  `id` int NOT NULL AUTO_INCREMENT,
  `owner` int NOT NULL,
  `type` varchar(32) NOT NULL DEFAULT 'House',
  `paid` int unsigned NOT NULL DEFAULT '0',
  `warnings` int NOT NULL DEFAULT '0',
  `is_protected` tinyint NOT NULL DEFAULT '0',
  `name` varchar(255) NOT NULL,
  `rent` int NOT NULL DEFAULT '0',
  `town_id` int NOT NULL DEFAULT '0',
  `bid` int NOT NULL DEFAULT '0',
  `bid_end` int NOT NULL DEFAULT '0',
  `last_bid` int NOT NULL DEFAULT '0',
  `highest_bidder` int NOT NULL DEFAULT '0',
  `size` int NOT NULL DEFAULT '0',
  `beds` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `owner` (`owner`),
  KEY `town_id` (`town_id`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `house_lists` (
  `house_id` int NOT NULL,
  `listid` int NOT NULL,
  `list` text NOT NULL,
  FOREIGN KEY (`house_id`) REFERENCES `houses` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `house_guests` (
  `house_id` int NOT NULL,
  `player_id` int NOT NULL,
  PRIMARY KEY (`house_id`, `player_id`),
  FOREIGN KEY (`house_id`) REFERENCES `houses` (`id`) ON DELETE CASCADE,
  FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `market_history` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `player_id` int NOT NULL,
  `sale` tinyint NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `amount` smallint unsigned NOT NULL,
  `price` int unsigned NOT NULL DEFAULT '0',
  `expires_at` bigint unsigned NOT NULL,
  `inserted` bigint unsigned NOT NULL,
  `state` tinyint unsigned NOT NULL,
  PRIMARY KEY (`id`),
  KEY `player_id` (`player_id`, `sale`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `market_offers` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `player_id` int NOT NULL,
  `sale` tinyint NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `amount` smallint unsigned NOT NULL,
  `created` bigint unsigned NOT NULL,
  `anonymous` tinyint NOT NULL DEFAULT '0',
  `price` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `sale` (`sale`,`itemtype`),
  KEY `created` (`created`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `players_online` (
 `player_id` int(11) NOT NULL,
  `broadcasting` tinyint(1) NOT NULL DEFAULT '0',
  `password` varchar(40) NOT NULL DEFAULT '0',
  `description` varchar(255) NOT NULL DEFAULT '',
  `spectators` int(11) NOT NULL DEFAULT '0',
  `protocol_version` int(4) NOT NULL DEFAULT '0'
) ENGINE=MEMORY DEFAULT CHARSET=latin1;

CREATE TABLE IF NOT EXISTS `player_deaths` (
  `player_id` int NOT NULL,
  `time` bigint unsigned NOT NULL DEFAULT '0',
  `level` int NOT NULL DEFAULT '1',
  `killed_by` varchar(255) NOT NULL,
  `is_player` tinyint NOT NULL DEFAULT '1',
  `mostdamage_by` varchar(100) NOT NULL,
  `mostdamage_is_player` tinyint NOT NULL DEFAULT '0',
  `unjustified` tinyint NOT NULL DEFAULT '0',
  `mostdamage_unjustified` tinyint NOT NULL DEFAULT '0',
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE,
  KEY `killed_by` (`killed_by`),
  KEY `mostdamage_by` (`mostdamage_by`),
  KEY `idx_player_deaths_unjustified_kills` (`killed_by`(64), `is_player`, `unjustified`, `time`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_deaths_backup` (
  `player_id` int NOT NULL,
  `time` bigint unsigned NOT NULL DEFAULT '0',
  `level` int NOT NULL DEFAULT '1',
  `killed_by` varchar(255) NOT NULL,
  `is_player` tinyint NOT NULL DEFAULT '1',
  `mostdamage_by` varchar(100) NOT NULL,
  `mostdamage_is_player` tinyint NOT NULL DEFAULT '0',
  `unjustified` tinyint NOT NULL DEFAULT '0',
  `mostdamage_unjustified` tinyint NOT NULL DEFAULT '0',
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE,
  KEY `killed_by` (`killed_by`),
  KEY `mostdamage_by` (`mostdamage_by`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `change_name_history` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `player_id` int(11) NOT NULL,
  `last_name` varchar(30) NOT NULL,
  `current_name` varchar(30) NOT NULL,
  `changed_name_in` int(11) NOT NULL,
  PRIMARY KEY (`id`),
  FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `shop_history` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `account` int(11) NOT NULL,
  `player` int(11) NOT NULL,
  `date` datetime NOT NULL,
  `title` varchar(100) NOT NULL,
  `price` int(11) NOT NULL,
  `costSecond` int(11) NOT NULL,
  `count` int(11) NOT NULL DEFAULT '0',
  `target` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`id`),
  FOREIGN KEY (`account`) REFERENCES `accounts` (`id`) ON DELETE CASCADE,
  FOREIGN KEY (`player`) REFERENCES `players` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_inboxitems` (
  `player_id` int NOT NULL,
  `sid` int NOT NULL,
  `pid` int NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `count` smallint NOT NULL DEFAULT '0',
  `attributes` blob NOT NULL,
  UNIQUE KEY `player_id_2` (`player_id`, `sid`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_storeinboxitems` (
  `player_id` int NOT NULL,
  `sid` int NOT NULL,
  `pid` int NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `count` smallint NOT NULL DEFAULT '0',
  `attributes` blob NOT NULL,
  UNIQUE KEY `player_id_2` (`player_id`, `sid`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_depotitems` (
  `player_id` int NOT NULL,
  `sid` int NOT NULL COMMENT 'any given range eg 0-100 will be reserved for depot lockers and all > 100 will be then normal items inside depots',
  `pid` int NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `count` smallint NOT NULL DEFAULT '0',
  `attributes` blob NOT NULL,
  UNIQUE KEY `player_id_2` (`player_id`, `sid`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_items` (
  `player_id` int NOT NULL DEFAULT '0',
  `pid` int NOT NULL DEFAULT '0',
  `sid` int NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `count` smallint NOT NULL DEFAULT '0',
  `attributes` blob NOT NULL,
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE,
  KEY `sid` (`sid`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_mounts` (
  `player_id` int NOT NULL DEFAULT '0',
  `mount_id` smallint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`player_id`, `mount_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `player_spells` (
  `player_id` int NOT NULL,
  `name` varchar(255) NOT NULL,
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_storage` (
  `player_id` int NOT NULL DEFAULT '0',
  `key` int unsigned NOT NULL DEFAULT '0',
  `value` bigint NOT NULL DEFAULT '0',
  PRIMARY KEY (`player_id`,`key`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_weapon_proficiency` (
  `player_id` int NOT NULL,
  `item_id` smallint unsigned NOT NULL,
  `experience` int unsigned NOT NULL DEFAULT '0',
  `perks` varchar(64) NOT NULL DEFAULT '',
  PRIMARY KEY (`player_id`,`item_id`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS player_bosstiary (
  player_id int NOT NULL,
  points int NOT NULL DEFAULT 0,
  slot_one int NOT NULL DEFAULT 0,
  slot_two int NOT NULL DEFAULT 0,
  remove_times int NOT NULL DEFAULT 0,
  PRIMARY KEY (player_id)
);

CREATE TABLE IF NOT EXISTS `player_rewarditems` (
  `player_id` int NOT NULL,
  `sid` int NOT NULL COMMENT 'range 0-100 will be reserved for adding items to player who are offline and all > 100 is for items saved from reward chest',
  `pid` int NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `count` smallint NOT NULL DEFAULT '0',
  `attributes` blob NOT NULL,
  UNIQUE KEY `player_id_2` (`player_id`, `sid`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `player_save_async_pending` (
  `guid` INT NOT NULL,
  `query_index` INT UNSIGNED NOT NULL,
  `query_text` LONGBLOB NOT NULL,
  `created_at` BIGINT NOT NULL,
  PRIMARY KEY (`guid`, `query_index`),
  FOREIGN KEY (`guid`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `player_prey` (
  `player_id` INT(11) NOT NULL,
  `slot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `monster_name` VARCHAR(255) NOT NULL DEFAULT '',
  `bonus_type` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `bonus_value` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `time_left` INT UNSIGNED NOT NULL DEFAULT 0,
  `list_monsters` VARCHAR(1024) NOT NULL DEFAULT '',
  `reroll_at` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `wildcards` INT UNSIGNED NOT NULL DEFAULT 0,
  `list_reroll_used` TINYINT(1) NOT NULL DEFAULT 0,
  PRIMARY KEY (`player_id`, `slot`),
  CONSTRAINT `fk_player_prey_player_id`
    FOREIGN KEY (`player_id`)
    REFERENCES `players` (`id`)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS player_bosstiary_tracker (
  player_id int NOT NULL,
  bossid int NOT NULL,
  slot tinyint NOT NULL DEFAULT 0,
  PRIMARY KEY (player_id, bossid),
  KEY idx_player_bosstiary_tracker_slot (player_id, slot)
);

CREATE TABLE IF NOT EXISTS player_hunting_tasks (
  player_id int NOT NULL,
  slot tinyint NOT NULL,
  state tinyint NOT NULL DEFAULT 2,
  raceid smallint NOT NULL DEFAULT 0,
  race_list text NOT NULL,
  rarity tinyint NOT NULL DEFAULT 1,
  upgraded tinyint NOT NULL DEFAULT 0,
  kills int NOT NULL DEFAULT 0,
  reroll_at bigint NOT NULL DEFAULT 0,
  disabled_until bigint NOT NULL DEFAULT 0,
  PRIMARY KEY (player_id, slot)
);

CREATE TABLE IF NOT EXISTS player_hunting_task_points (
  player_id int NOT NULL,
  points bigint NOT NULL DEFAULT 0,
  PRIMARY KEY (player_id)
);

CREATE TABLE IF NOT EXISTS `player_outfits` (
  `player_id` int NOT NULL DEFAULT '0',
  `outfit_id` smallint unsigned NOT NULL DEFAULT '0',
  `addons` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`player_id`,`outfit_id`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_debugasserts` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `player_id` int NOT NULL,
  `assert_line` varchar(255) NOT NULL,
  `date` varchar(255) NOT NULL,
  `description` text NOT NULL,
  `comment` text NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `player_id` (`player_id`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `kv_store` (
  `key_name` varchar(191) NOT NULL,
  `timestamp` bigint NOT NULL,
  `value` longblob NOT NULL,
  PRIMARY KEY (`key_name`),
  KEY `timestamp` (`timestamp`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8mb4 COLLATE=utf8mb4_bin;

CREATE TABLE IF NOT EXISTS `server_config` (
  `config` varchar(50) NOT NULL,
  `value` varchar(256) NOT NULL DEFAULT '',
  PRIMARY KEY `config` (`config`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `tile_store` (
  `house_id` int NOT NULL,
  `data` longblob NOT NULL,
  FOREIGN KEY (`house_id`) REFERENCES `houses` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `towns` (
  `id` int NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `posx` int NOT NULL DEFAULT '0',
  `posy` int NOT NULL DEFAULT '0',
  `posz` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  UNIQUE KEY `name` (`name`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

INSERT INTO server_config (config, value) VALUES ('db_version', '46'), ('motd_hash', ''), ('motd_num', '0'), ('players_record', '0');

CREATE TABLE IF NOT EXISTS guild_transactions (
  id SERIAL PRIMARY KEY,
  guild_id int NOT NULL,
  guild_associated int DEFAULT NULL,
  player_associated int DEFAULT NULL,
  type ENUM('DEPOSIT', 'WITHDRAW') NOT NULL,
  category ENUM ('OTHER', 'RENT', 'MATERIAL', 'SERVICES', 'REVENUE', 'CONTRIBUTION') NOT NULL DEFAULT 'OTHER',
  balance bigint NOT NULL DEFAULT 0,
  time bigint NOT NULL,
  FOREIGN KEY (guild_id) REFERENCES guilds(id) ON DELETE CASCADE,
  FOREIGN KEY (guild_associated) REFERENCES guilds(id) ON DELETE SET NULL,
  FOREIGN KEY (player_associated) REFERENCES players(id) ON DELETE SET NULL
);

/*!50003 CREATE TRIGGER ondelete_players BEFORE DELETE ON players
 FOR EACH ROW
 UPDATE houses SET owner = 0 WHERE owner = OLD.id */;

/*!50003 CREATE TRIGGER oncreate_guilds AFTER INSERT ON guilds
 FOR EACH ROW
 INSERT INTO guild_ranks (name, level, guild_id) VALUES 
   ('the Leader', 3, NEW.id),
   ('a Vice-Leader', 2, NEW.id),
   ('a Member', 1, NEW.id) */;
