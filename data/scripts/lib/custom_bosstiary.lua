if not configManager.getBoolean(configKeys.BESTIARY_SYSTEM_ENABLED) then
	CustomBosstiary = nil
	return
end

CustomBosstiary = CustomBosstiary or {}
CustomBosstiary.monstersByRaceId = CustomBosstiary.monstersByRaceId or {}
CustomBosstiary.monstersByName = CustomBosstiary.monstersByName or {}
CustomBosstiary.boostedBoss = CustomBosstiary.boostedBoss or nil

local thresholds = {
	[1] = {25, 100, 300},
	[2] = {5, 20, 60},
	[3] = {1, 3, 5},
}

local rewards = {
	[1] = {5, 15, 30},
	[2] = {10, 30, 60},
	[3] = {10, 30, 60},
}

local boostedBossCategory = RARITY_ARCHFOE or 2

local function clamp(value, minValue, maxValue)
	value = tonumber(value) or minValue
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function getConfigNumber(key, fallback)
	if configManager and configManager.getNumber and configKeys and configKeys[key] ~= nil then
		local value = tonumber(configManager.getNumber(configKeys[key]))
		if value then
			return value
		end
	end
	return fallback
end

local function isArchfoeBoss(entry)
	return entry and entry.category == boostedBossCategory
end

local function getBoostedBossDateKey()
	return os.date("%Y-%m-%d")
end

local function normalizeOutfit(outfit)
	if type(outfit) ~= "table" then
		return {type = 0, typeEx = 0, head = 0, body = 0, legs = 0, feet = 0, addons = 0}
	end

	return {
		type = clamp(outfit.lookType or outfit.type or 0, 0, 0xFFFF),
		typeEx = clamp(outfit.lookTypeEx or outfit.typeEx or 0, 0, 0xFFFF),
		head = clamp(outfit.lookHead or outfit.head or 0, 0, 0xFF),
		body = clamp(outfit.lookBody or outfit.body or 0, 0, 0xFF),
		legs = clamp(outfit.lookLegs or outfit.legs or 0, 0, 0xFF),
		feet = clamp(outfit.lookFeet or outfit.feet or 0, 0, 0xFF),
		addons = clamp(outfit.lookAddons or outfit.addons or 0, 0, 0xFF),
	}
end

function CustomBosstiary.ensureTables()
	db.query([[
		CREATE TABLE IF NOT EXISTS `player_bestiary_kills` (
			`player_id` INT NOT NULL,
			`raceid` SMALLINT UNSIGNED NOT NULL,
			`kills` INT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `raceid`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])

	db.query([[
		CREATE TABLE IF NOT EXISTS `player_bosstiary` (
			`player_id` INT NOT NULL,
			`points` INT UNSIGNED NOT NULL DEFAULT 0,
			`slot_one` INT UNSIGNED NOT NULL DEFAULT 0,
			`slot_two` INT UNSIGNED NOT NULL DEFAULT 0,
			`remove_times` INT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])

	db.query([[
		CREATE TABLE IF NOT EXISTS `player_bosstiary_tracker` (
			`player_id` INT NOT NULL,
			`bossid` INT UNSIGNED NOT NULL,
			`slot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `bossid`),
			KEY `idx_player_bosstiary_tracker_slot` (`player_id`, `slot`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])

	db.query([[
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
	]])

	-- Insert default row if not exists
	db.query([[INSERT IGNORE INTO `boosted_boss` (`date`, `boostname`, `raceid`) VALUES ('0', 'default', '0')]])
end

function CustomBosstiary.registerMonster(monsterType, mask)
	if type(mask) ~= "table" or type(mask.bosstiary) ~= "table" then
		return false
	end

	local bossRaceId = tonumber(mask.bosstiary.bossRaceId) or 0
	local category = tonumber(mask.bosstiary.bossRace) or 0
	if bossRaceId <= 0 or not thresholds[category] then
		return false
	end

	local entry = {
		raceId = bossRaceId,
		category = category,
		name = tostring(mask.name or (monsterType and monsterType:name()) or "unknown"),
		outfit = normalizeOutfit(mask.outfit),
	}
	CustomBosstiary.monstersByRaceId[bossRaceId] = entry
	CustomBosstiary.monstersByName[entry.name:lower()] = entry
	return true
end

function CustomBosstiary.getMonster(raceId)
	return CustomBosstiary.monstersByRaceId[tonumber(raceId) or 0]
end

function CustomBosstiary.getMonsterForCreature(creature)
	if not creature then
		return nil
	end
	return CustomBosstiary.monstersByName[tostring(creature:getName() or ""):lower()]
end

function CustomBosstiary.getBoostedMonster()
	local entries = {}
	for _, entry in pairs(CustomBosstiary.monstersByRaceId) do
		entries[#entries + 1] = entry
	end
	table.sort(entries, function(a, b) return a.raceId < b.raceId end)
	if #entries == 0 then
		return nil
	end
	return entries[(math.floor(os.time() / 86400) % #entries) + 1]
end

function CustomBosstiary.getThresholds(category)
	return thresholds[tonumber(category) or 0]
end

function CustomBosstiary.getRewards(category)
	return rewards[tonumber(category) or 0]
end

function CustomBosstiary.getProgress(entry, kills)
	local categoryThresholds = entry and thresholds[entry.category]
	kills = tonumber(kills) or 0
	if not categoryThresholds or kills < categoryThresholds[1] then
		return 0
	end
	if kills < categoryThresholds[2] then
		return 1
	end
	if kills < categoryThresholds[3] then
		return 2
	end
	return 3
end

function CustomBosstiary.getAwardedPoints(entry, oldKills, newKills)
	local categoryRewards = entry and rewards[entry.category]
	if not categoryRewards then
		return 0
	end

	local oldProgress = CustomBosstiary.getProgress(entry, oldKills)
	local newProgress = CustomBosstiary.getProgress(entry, newKills)
	local points = 0
	for stage = oldProgress + 1, newProgress do
		points = points + categoryRewards[stage]
	end
	return points
end

function CustomBosstiary.addKill(players, entry)
	if not entry then
		return false
	end

	local boostedBoss = CustomBosstiary.getBoostedBoss()
	local isBoosted = boostedBoss and boostedBoss.raceId == entry.raceId
	local increment = isBoosted and math.max(CustomBosstiary.getBoostedBossKillBonus(), 1) or 1

	for playerGuid, player in pairs(players or {}) do
		local oldKills = 0
		local resultId = db.storeQuery("SELECT `kills` FROM `player_bestiary_kills` WHERE `player_id` = " ..
			playerGuid .. " AND `raceid` = " .. entry.raceId)
		if resultId ~= false then
			oldKills = result.getDataInt(resultId, "kills")
			result.free(resultId)
		end

		local newKills = oldKills + increment
		local awardedPoints = CustomBosstiary.getAwardedPoints(entry, oldKills, newKills)
		db.query("INSERT INTO `player_bestiary_kills` (`player_id`, `raceid`, `kills`) VALUES (" ..
			playerGuid .. ", " .. entry.raceId .. ", " .. increment .. ") ON DUPLICATE KEY UPDATE `kills` = `kills` + " .. increment)
		db.query("INSERT IGNORE INTO `player_bosstiary` (`player_id`) VALUES (" .. playerGuid .. ")")
		if awardedPoints > 0 then
			db.query("UPDATE `player_bosstiary` SET `points` = `points` + " .. awardedPoints ..
				" WHERE `player_id` = " .. playerGuid)
			if player then
				player:sendTextMessage(MESSAGE_EVENT_ADVANCE or MESSAGE_STATUS_CONSOLE_BLUE,
					"You advanced your Bosstiary entry for " .. entry.name .. " and earned " ..
					awardedPoints .. " boss points.")
			end
		end

	end
	return true
end

function CustomBosstiary.loadBoostedBoss()
	CustomBosstiary.ensureTables()

	local today = getBoostedBossDateKey()
	local resultId = db.storeQuery("SELECT `date`, `boostname`, `raceid`, `looktype`, `lookhead`, `lookbody`, `looklegs`, `lookfeet`, `lookaddons`, `lookmount` FROM `boosted_boss` WHERE `date` = " .. db.escapeString(today) .. " LIMIT 1")
	if not resultId then
		CustomBosstiary.pickNewBoostedBoss()
		return
	end

	local savedName = result.getDataString(resultId, "boostname")
	local savedRaceId = tonumber(result.getDataString(resultId, "raceid")) or 0
	local entry = CustomBosstiary.getMonster(savedRaceId)
	if isArchfoeBoss(entry) then
		CustomBosstiary.boostedBoss = {
			name = savedName ~= "" and savedName or entry.name,
			raceId = savedRaceId,
			category = entry.category,
			outfit = {
				type = tonumber(result.getDataInt(resultId, "looktype")) or entry.outfit.type or 0,
				typeEx = 0,
				head = tonumber(result.getDataInt(resultId, "lookhead")) or entry.outfit.head or 0,
				body = tonumber(result.getDataInt(resultId, "lookbody")) or entry.outfit.body or 0,
				legs = tonumber(result.getDataInt(resultId, "looklegs")) or entry.outfit.legs or 0,
				feet = tonumber(result.getDataInt(resultId, "lookfeet")) or entry.outfit.feet or 0,
				addons = tonumber(result.getDataInt(resultId, "lookaddons")) or entry.outfit.addons or 0,
				mount = tonumber(result.getDataInt(resultId, "lookmount")) or 0,
			}
		}
	else
		result.free(resultId)
		CustomBosstiary.pickNewBoostedBoss()
		return
	end
	result.free(resultId)
end

function CustomBosstiary.getBoostedBoss()
	return CustomBosstiary.boostedBoss
end

function CustomBosstiary.setBoostedBoss(entry)
	if not entry then
		return false
	end

	local today = getBoostedBossDateKey()
	local outfit = entry.outfit or {}
	db.query("DELETE FROM `boosted_boss` WHERE `date` <> " .. db.escapeString(today))

	local query = string.format(
		"INSERT INTO `boosted_boss` (`date`, `boostname`, `raceid`, `looktype`, `lookhead`, `lookbody`, `looklegs`, `lookfeet`, `lookaddons`, `lookmount`) " ..
		"VALUES (%s, %s, '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d') " ..
		"ON DUPLICATE KEY UPDATE `boostname` = VALUES(`boostname`), `raceid` = VALUES(`raceid`), `looktype` = VALUES(`looktype`), " ..
		"`lookhead` = VALUES(`lookhead`), `lookbody` = VALUES(`lookbody`), `looklegs` = VALUES(`looklegs`), " ..
		"`lookfeet` = VALUES(`lookfeet`), `lookaddons` = VALUES(`lookaddons`), `lookmount` = VALUES(`lookmount`)",
		db.escapeString(today), db.escapeString(entry.name), entry.raceId,
		outfit.type or 0, outfit.head or 0, outfit.body or 0, outfit.legs or 0,
		outfit.feet or 0, outfit.addons or 0, outfit.mount or 0
	)

	if not db.query(query) then
		return false
	end

	CustomBosstiary.boostedBoss = entry
	db.query("UPDATE `player_bosstiary` SET `slot_one` = 0 WHERE `slot_one` = " .. entry.raceId)
	db.query("UPDATE `player_bosstiary` SET `slot_two` = 0 WHERE `slot_two` = " .. entry.raceId)
	return true
end

function CustomBosstiary.pickNewBoostedBoss()
	local archfoeBosses = {}
	local registeredBossCount = 0
	for _, entry in pairs(CustomBosstiary.monstersByRaceId) do
		registeredBossCount = registeredBossCount + 1
		if isArchfoeBoss(entry) then
			archfoeBosses[#archfoeBosses + 1] = entry
		end
	end

	if registeredBossCount <= 1 or #archfoeBosses == 0 then
		CustomBosstiary.boostedBoss = nil
		return false
	end

	local selected = archfoeBosses[math.random(1, #archfoeBosses)]
	return CustomBosstiary.setBoostedBoss(selected)
end

function CustomBosstiary.isBoostedBoss(entryOrRaceId)
	local boostedBoss = CustomBosstiary.getBoostedBoss()
	if not boostedBoss then
		return false
	end

	local raceId = tonumber(entryOrRaceId)
	if type(entryOrRaceId) == "table" then
		raceId = entryOrRaceId.raceId
	end

	return boostedBoss.raceId == raceId
end

function CustomBosstiary.getBoostedBossLootBonus()
	return getConfigNumber("BOOSTED_BOSS_LOOT_BONUS", 250)
end

function CustomBosstiary.getBoostedBossKillBonus()
	return getConfigNumber("BOOSTED_BOSS_KILL_BONUS", 3)
end
