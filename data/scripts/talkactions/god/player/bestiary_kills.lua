local bestiaryKills = TalkAction("/bestiarykills")

local function normalizeName(value)
	return tostring(value or ""):lower():gsub("^%s*(.-)%s*$", "%1")
end

local function findBestiaryEntry(value)
	if not CustomBestiary or not CustomBestiary.monstersByRaceId then
		return nil
	end

	local raceId = tonumber(value)
	if raceId and CustomBestiary.getMonster then
		return CustomBestiary.getMonster(raceId)
	end

	local wantedName = normalizeName(value)
	for _, entry in pairs(CustomBestiary.monstersByRaceId) do
		if normalizeName(entry.name) == wantedName then
			return entry
		end
	end
	return nil
end

local function getStoredKills(playerGuid, raceId)
	local resultId = db.storeQuery("SELECT `kills` FROM `player_bestiary_kills` WHERE `player_id` = " ..
		playerGuid .. " AND `raceid` = " .. raceId)
	if resultId == false then
		return 0
	end

	local kills = result.getDataInt(resultId, "kills")
	result.free(resultId)
	return math.max(0, kills)
end

local function getCharmPoints(playerGuid)
	local resultId = db.storeQuery("SELECT `charmpoints` FROM `players` WHERE `id` = " .. playerGuid)
	if resultId == false then
		return 0
	end

	local points = result.getDataInt(resultId, "charmpoints")
	result.free(resultId)
	return math.max(0, points)
end

local function setCharmPoints(playerGuid, points)
	db.query("UPDATE `players` SET `charmpoints` = " .. math.max(0, math.floor(points)) .. " WHERE `id` = " .. playerGuid)
end

local function updateCharmPointsForCompletion(playerGuid, entry, oldKills, newKills)
	if not entry or (entry.charmPoints or 0) <= 0 then
		return 0
	end

	local requiredKills = tonumber(entry.toKill) or 0
	if requiredKills <= 0 then
		return 0
	end

	local delta = 0
	if oldKills < requiredKills and newKills >= requiredKills then
		delta = entry.charmPoints
	elseif oldKills >= requiredKills and newKills < requiredKills then
		delta = -entry.charmPoints
	end

	if delta ~= 0 then
		setCharmPoints(playerGuid, getCharmPoints(playerGuid) + delta)
	end
	return delta
end

function bestiaryKills.onSay(player, words, param)
	if not CustomBestiary then
		player:sendCancelMessage("Custom Bestiary is not loaded.")
		return false
	end

	local split = param:splitTrimmed(",")
	local target = player
	local monsterName = split[1]
	local killAmount = split[2]

	if split[3] then
		target = Player(split[1])
		monsterName = split[2]
		killAmount = split[3]
	end

	if not target then
		player:sendCancelMessage("A player with that name is not online.")
		return false
	end

	local entry = findBestiaryEntry(monsterName)
	if not entry then
		player:sendCancelMessage("Bestiary creature not found.")
		return false
	end

	local parsedKills = tonumber(killAmount)
	if not parsedKills or parsedKills < 0 then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Usage: /bestiarykills [player,] monster name or raceid, kills")
		return false
	end
	local kills = math.floor(parsedKills)

	local playerGuid = target:getGuid()
	local oldKills = getStoredKills(playerGuid, entry.raceId)
	db.query("INSERT INTO `player_bestiary_kills` (`player_id`, `raceid`, `kills`) VALUES (" ..
		playerGuid .. ", " .. entry.raceId .. ", " .. kills ..
		") ON DUPLICATE KEY UPDATE `kills` = VALUES(`kills`)")

	local charmPointDelta = updateCharmPointsForCompletion(playerGuid, entry, oldKills, kills)
	if CustomBestiary.invalidatePlayer then
		CustomBestiary.invalidatePlayer(playerGuid)
	end
	if CustomBestiary.preloadPlayer then
		CustomBestiary.preloadPlayer(playerGuid)
	end
	if CustomBestiary.refreshPlayerCharms then
		CustomBestiary.refreshPlayerCharms(target)
	end
	if CustomBestiary.sendProgress then
		CustomBestiary.sendProgress(target, entry.raceId, kills)
	end

	local stage = "discovered"
	if kills >= (entry.toKill or 0) then
		stage = "full"
	elseif kills >= (entry.secondUnlock or 0) then
		stage = "second unlock"
	elseif kills >= (entry.firstUnlock or 0) then
		stage = "first unlock"
	end

	local message = string.format("Set %s Bestiary kills for %s to %d/%d (%s).",
		entry.name, target:getName(), kills, entry.toKill or 0, stage)
	if charmPointDelta ~= 0 then
		message = message .. string.format(" Charm points %+d.", charmPointDelta)
	end
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, message)
	if target ~= player then
		target:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, message)
	end
	return false
end

bestiaryKills:separator(" ")
bestiaryKills:accountType(6)
bestiaryKills:access(true)
bestiaryKills:register()
