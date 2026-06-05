function onUpdateDatabase()
	logMigration("Updating database to version 46 (Boosted Boss)")

	local query = [[
		CREATE TABLE IF NOT EXISTS `boosted_boss` (
			`boostname` TEXT,
			`date` varchar(250) NOT NULL DEFAULT '',
			`raceid` varchar(250) NOT NULL DEFAULT '',
			`looktype` int(11) NOT NULL DEFAULT "136",
			`lookfeet` int(11) NOT NULL DEFAULT "0",
			`looklegs` int(11) NOT NULL DEFAULT "0",
			`lookhead` int(11) NOT NULL DEFAULT "0",
			`lookbody` int(11) NOT NULL DEFAULT "0",
			`lookaddons` int(11) NOT NULL DEFAULT "0",
			`lookmount` int(11) DEFAULT "0",
			PRIMARY KEY (`date`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]]

	if not db.query(query) then
		return false
	end

	-- Insert default row
	local insertQuery = [[
		INSERT IGNORE INTO `boosted_boss` (`date`, `boostname`, `raceid`)
		VALUES ('0', 'default', '0')
	]]

	return db.query(insertQuery)
end
