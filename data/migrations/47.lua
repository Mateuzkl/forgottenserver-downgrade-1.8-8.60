function onUpdateDatabase()
	logMigration("Updating database to version 48 (Expanded blessings 1-8)")

	db.query([[
		ALTER TABLE `players`
		ADD COLUMN `blessings1` tinyint unsigned NOT NULL DEFAULT 0,
		ADD COLUMN `blessings2` tinyint unsigned NOT NULL DEFAULT 0,
		ADD COLUMN `blessings3` tinyint unsigned NOT NULL DEFAULT 0,
		ADD COLUMN `blessings4` tinyint unsigned NOT NULL DEFAULT 0,
		ADD COLUMN `blessings5` tinyint unsigned NOT NULL DEFAULT 0,
		ADD COLUMN `blessings6` tinyint unsigned NOT NULL DEFAULT 0,
		ADD COLUMN `blessings7` tinyint unsigned NOT NULL DEFAULT 0,
		ADD COLUMN `blessings8` tinyint unsigned NOT NULL DEFAULT 0
	]])
	return true
end
