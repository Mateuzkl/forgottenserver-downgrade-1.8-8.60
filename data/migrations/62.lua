function onUpdateDatabase()
	logMigration("Updating database to version 63 (weapon proficiency shape modifiers)")
	local queryResult = db.storeQuery([[
		SELECT COUNT(*) AS `count`
		FROM `information_schema`.`COLUMNS`
		WHERE `TABLE_SCHEMA` = DATABASE()
		  AND `TABLE_NAME` = 'player_weapon_proficiency'
		  AND `COLUMN_NAME` = 'modifiers'
	]])
	local exists = queryResult and result.getNumber(queryResult, "count") > 0
	if queryResult then
		result.free(queryResult)
	end
	return exists or db.query([[
		ALTER TABLE `player_weapon_proficiency`
		ADD COLUMN `modifiers` VARCHAR(512) NOT NULL DEFAULT '' AFTER `perks`
	]])
end
