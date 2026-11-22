function onUpdateDatabase()
	logMigration("Updating database to version 71 (Account Manager recovery keys)")
	local column = db.storeQuery([[SELECT 1 FROM information_schema.COLUMNS
		WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'accounts' AND COLUMN_NAME = 'recovery_key_hash']])
	if column then
		result.free(column)
	elseif not db.query("ALTER TABLE `accounts` ADD COLUMN `recovery_key_hash` CHAR(40) DEFAULT NULL") then
		return false
	end
	-- Some installations widened secret and stored complete legacy hashes.
	-- A truncated hash cannot verify a key. Leave the authenticator field intact.
	return db.query([[UPDATE `accounts` SET `recovery_key_hash` = LOWER(`secret`)
		WHERE `id` > 1 AND `recovery_key_hash` IS NULL AND `secret` REGEXP '^[0-9a-fA-F]{40}$']])
end
