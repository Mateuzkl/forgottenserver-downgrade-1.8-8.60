local function firstToUpper(text)
	return tostring(text or ""):gsub("^%l", string.upper)
end

if not CustomBestiary then
	return
end

local function getBestiaryEntryByName(name)
	local targetName = tostring(name or ""):lower()
	if targetName == "" or not CustomBestiary or not CustomBestiary.monstersByRaceId then
		return nil
	end

	for _, entry in pairs(CustomBestiary.monstersByRaceId) do
		if tostring(entry.name or ""):lower() == targetName then
			return entry
		end
	end
	return nil
end

local function getBestiaryEntryForCreature(creature)
	if not creature or not CustomBestiary then
		return nil, 0
	end

	local monsterType = creature:getType()
	local raceId = monsterType and monsterType:raceId() or 0
	local entry = raceId > 0 and CustomBestiary.getMonster(raceId) or nil
	if entry then
		return entry, raceId
	end

	entry = getBestiaryEntryByName(creature:getName())
	if entry then
		raceId = entry.raceId
		if monsterType and raceId > 0 then
			monsterType:raceId(raceId) -- patches monsterType at runtime so future kills resolve by raceId directly
		end
		return entry, raceId
	end
	return nil, raceId
end

local function getBestiaryKillCounts(playerGuids, raceId)
	local counts = {}
	local ids = {}
	for guid in pairs(playerGuids) do
		ids[#ids + 1] = guid
	end
	if #ids == 0 then
		return counts
	end

	local inClause = table.concat(ids, ",")
	local resultId = db.storeQuery("SELECT `player_id`, `kills` FROM `player_bestiary_kills` WHERE `player_id` IN (" ..
		inClause .. ") AND `raceid` = " .. raceId)
	if resultId ~= false then
		repeat
			counts[result.getDataInt(resultId, "player_id")] = result.getDataInt(resultId, "kills")
		until not result.next(resultId)
		result.free(resultId)
	end
	return counts
end

local function addPlayerCharmPoints(playerGuid, points)
	points = math.max(0, tonumber(points) or 0)
	if points <= 0 then
		return
	end

	db.query("UPDATE `players` SET `charmpoints` = `charmpoints` + " .. points .. " WHERE `id` = " .. playerGuid)
end

local function sendBestiaryUnlockMessage(player, entry, progress)
	if not player or not entry then
		return
	end

	local msgType = MESSAGE_EVENT_ADVANCE or MESSAGE_STATUS_CONSOLE_BLUE or 0
	local creatureName = firstToUpper(entry.name)
	if progress >= 4 then
		player:sendTextMessage(msgType, "You completed the Bestiary entry for " ..
			creatureName .. " and earned " .. entry.charmPoints .. " charm points.")
	elseif progress == 3 then
		player:sendTextMessage(msgType, "You unlocked the second Bestiary stage for " ..
			creatureName .. ".")
	elseif progress == 2 then
		player:sendTextMessage(msgType, "You unlocked the first Bestiary stage for " ..
			creatureName .. ".")
	elseif progress == 1 then
		player:sendTextMessage(msgType, "You unlocked the Bestiary entry for " ..
			creatureName .. ".")
	end
end

local function getPlayerFromKiller(killer)
	if not killer then
		return nil
	end
	if killer:isPlayer() then
		return killer
	end

	local master = killer:getMaster()
	if master and master:isPlayer() then
		return master
	end
	return nil
end

local function addKillPlayer(players, player)
	if player then
		players[player:getGuid()] = player
	end
end

local function getKillPlayers(creature, killer, mostDamageKiller)
	local players = {}
	addKillPlayer(players, getPlayerFromKiller(killer))
	addKillPlayer(players, getPlayerFromKiller(mostDamageKiller))

	if creature and creature.getDamageMap then
		for creatureId in pairs(creature:getDamageMap()) do
			addKillPlayer(players, Player(creatureId))
		end
	end
	return players
end

local bestiaryKill = CreatureEvent("CustomBestiaryKill")

function bestiaryKill.onDeath(creature, corpse, killer, mostDamageKiller, lastHitUnjustified, mostDamageUnjustified)
	if not CustomBestiary then
		return true
	end

	if creature and creature:getMaster() then
		return true
	end

	local players = getKillPlayers(creature, killer, mostDamageKiller)
	if next(players) == nil then
		return true
	end

	local entry, raceId = getBestiaryEntryForCreature(creature)
	if not entry then
		return true
	end

	local killCounts = getBestiaryKillCounts(players, raceId)
	for playerGuid, player in pairs(players) do
		local oldKills = killCounts[playerGuid] or 0
		local newKills = oldKills + 1
		local oldProgress = CustomBestiary.getProgress(entry, oldKills)
		local newProgress = CustomBestiary.getProgress(entry, newKills)

		db.query("INSERT INTO `player_bestiary_kills` (`player_id`, `raceid`, `kills`) VALUES (" ..
			playerGuid .. ", " .. raceId .. ", 1) ON DUPLICATE KEY UPDATE `kills` = `kills` + 1")

		if CustomBestiary.invalidatePlayer then
			CustomBestiary.invalidatePlayer(playerGuid)
		end
		if oldKills < entry.toKill and newKills >= entry.toKill then
			addPlayerCharmPoints(playerGuid, entry.charmPoints)
		end
		if newProgress > oldProgress then
			sendBestiaryUnlockMessage(player, entry, newProgress)
		end
	end
	return true
end

bestiaryKill:register()

local bestiarySpawn = MonsterEvent and MonsterEvent("CustomBestiarySpawn") or Event()
function bestiarySpawn.onSpawn(monster)
	if CustomBestiary and monster then
		local entry = getBestiaryEntryForCreature(monster)
		if entry then
			monster:registerEvent("CustomBestiaryKill")
		end
	end
	return true
end
bestiarySpawn:register()
