function onUpdateDatabase()
    logMigration("Updating database to version 69 (generation-safe player save journal)")
    local column = db.storeQuery([[
        SELECT COUNT(*) AS `count` FROM `information_schema`.`COLUMNS`
        WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'players'
          AND `COLUMN_NAME` = 'save_generation'
    ]])
    if not column then
        return false
    end
    local exists = result.getNumber(column, "count") > 0
    result.free(column)
    if not exists and not db.query([[
        ALTER TABLE `players` ADD COLUMN `save_generation` BIGINT UNSIGNED NOT NULL DEFAULT 0
    ]]) then
        return false
    end
    -- Do not discard/replay legacy WAL: it has no generation and may contain
    -- non-idempotent online-time increments. Startup blocks unresolved GUIDs.
    return db.query([[
        CREATE TABLE IF NOT EXISTS `player_save_journal` (
            `guid` INT NOT NULL,
            `generation` BIGINT UNSIGNED NOT NULL,
            `payload` LONGBLOB NOT NULL,
            `payload_hash` BINARY(32) NOT NULL,
            `created_at` BIGINT NOT NULL,
            PRIMARY KEY (`guid`),
            FOREIGN KEY (`guid`) REFERENCES `players` (`id`) ON DELETE CASCADE
        ) ENGINE=InnoDB
    ]])
end
