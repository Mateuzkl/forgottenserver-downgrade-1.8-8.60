if not configManager.getBoolean(configKeys.BESTIARY_SYSTEM_ENABLED) then
	CustomBosstiary = nil
	return
end

CustomBosstiary = CustomBosstiary or {}
CustomBosstiary.monstersByRaceId = CustomBosstiary.monstersByRaceId or {}
CustomBosstiary.monstersByName = CustomBosstiary.monstersByName or {}

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

	local boosted = CustomBosstiary.getBoostedMonster()
	local increment = boosted and boosted.raceId == entry.raceId and 2 or 1
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
