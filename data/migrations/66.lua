function onUpdateDatabase()
	logMigration("Updating database to version 67 (migrate shop_history price column to BIGINT)")

	local tableExists = (not db.tableExists or db.tableExists("shop_history"))
	if tableExists then
		if not db.query("ALTER TABLE `shop_history` MODIFY COLUMN `price` BIGINT NOT NULL DEFAULT '0'") then
			print("[Migration 66] Failed to modify `shop_history`.`price` to BIGINT")
			return false
		end
	end

	return true
end
