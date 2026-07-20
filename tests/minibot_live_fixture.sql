-- Disposable fixture for AstraClient's --minibot-live-smoke mode.
-- Run only against the isolated database selected by minibot_boot_config.lua.

INSERT INTO `accounts` (`name`, `password`, `type`, `premium_ends_at`)
VALUES ('codex_smoke', SHA1('CodexSmoke860'), 6, UNIX_TIMESTAMP() + 86400)
ON DUPLICATE KEY UPDATE
  `password` = VALUES(`password`),
  `type` = VALUES(`type`),
  `premium_ends_at` = VALUES(`premium_ends_at`);

SET @minibot_smoke_account_id = (
  SELECT `id` FROM `accounts` WHERE `name` = 'codex_smoke' LIMIT 1
);

INSERT INTO `players` (
  `name`, `group_id`, `account_id`, `level`, `vocation`,
  `health`, `healthmax`, `experience`, `looktype`, `mana`, `manamax`,
  `town_id`, `posx`, `posy`, `posz`, `balance`
)
VALUES (
  'Codex Smoke', 6, @minibot_smoke_account_id, 1, 1,
  150, 150, 0, 128, 100, 100,
  1, 50, 50, 7, 20000000
)
ON DUPLICATE KEY UPDATE
  `group_id` = VALUES(`group_id`),
  `account_id` = VALUES(`account_id`),
  `vocation` = VALUES(`vocation`),
  `health` = VALUES(`health`),
  `healthmax` = VALUES(`healthmax`),
  `mana` = VALUES(`mana`),
  `manamax` = VALUES(`manamax`),
  `town_id` = VALUES(`town_id`),
  `posx` = VALUES(`posx`),
  `posy` = VALUES(`posy`),
  `posz` = VALUES(`posz`),
  `balance` = VALUES(`balance`);
