-- Custom Cyclopedia PacketHandler
-- Client sends 0x39, 0x3A, 0x3B, 0x3E and 0x3F. Server responds through 0x39 with a response byte.

if not configManager.getBoolean(configKeys.BESTIARY_SYSTEM_ENABLED) or not CustomBestiary then
	return
end

local OPCODE_CYCLOPEDIA_INFO = 0x39
local OPCODE_CYCLOPEDIA_CATEGORY = 0x3A
local OPCODE_CYCLOPEDIA_MONSTER = 0x3B
local OPCODE_CYCLOPEDIA_CHARM = 0x3E
local OPCODE_CYCLOPEDIA_TRACKER = 0x3F
local OPCODE_CYCLOPEDIA_SEND = 0x39

local RESP_MESSAGE = 0x00
local RESP_BESTIARY_DATA = 0x01
local RESP_BESTIARY_OVERVIEW = 0x02
local RESP_BESTIARY_MONSTER = 0x03
local RESP_TRACKER = 0x05

local MAX_TRACKER_SLOTS = 5
local CHARM_REMOVE_COST = 10000
local BESTIARY_DETAIL_FULL = 4
local BESTIARY_SEARCH_PREFIX = "__search__:"
local MAX_CYCLOPEDIA_CLASS_LENGTH = 80

local killCache = {}
local charmCache = {}
local trackerCache = {}
local earnedPointsCache = {}
local charmByRaceCache = {}
local finishedCache = {}
local rebuildEarnedPoints

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function logError(message)
	if logger and logger.error then
		logger.error(message)
	else
		print(message)
	end
end

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

local function trimText(text)
	return tostring(text or ""):gsub("^%s*(.-)%s*$", "%1")
end

local function ensureTables()
	db.query([[
		CREATE TABLE IF NOT EXISTS `player_bestiary_kills` (
			`player_id` INT NOT NULL,
			`raceid` SMALLINT UNSIGNED NOT NULL,
			`kills` INT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `raceid`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])

	db.query([[
		CREATE TABLE IF NOT EXISTS `player_bestiary_charms` (
			`player_id` INT NOT NULL,
			`charm_id` TINYINT UNSIGNED NOT NULL,
			`unlocked` TINYINT(1) NOT NULL DEFAULT 0,
			`raceid` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `charm_id`),
			KEY `idx_player_bestiary_charms_race` (`player_id`, `raceid`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])

	db.query([[
		CREATE TABLE IF NOT EXISTS `player_bestiary_tracker` (
			`player_id` INT NOT NULL,
			`raceid` SMALLINT UNSIGNED NOT NULL,
			`slot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `raceid`),
			KEY `idx_player_bestiary_tracker_slot` (`player_id`, `slot`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])
end

local function getPlayerGuid(player)
	return player:getGuid()
end

local function getPlayerCharmPoints(playerGuid)
	local resultId = db.storeQuery("SELECT `charmpoints` FROM `players` WHERE `id` = " .. playerGuid)
	if resultId == false then
		return 0
	end

	local points = math.max(0, result.getDataInt(resultId, "charmpoints"))
	result.free(resultId)
	return points
end

local function setPlayerCharmPoints(playerGuid, points)
	points = math.max(0, tonumber(points) or 0)
	db.query("UPDATE `players` SET `charmpoints` = " .. points .. " WHERE `id` = " .. playerGuid)
	return points
end

local function invalidatePlayer(playerGuid)
	killCache[playerGuid] = nil
	charmCache[playerGuid] = nil
	trackerCache[playerGuid] = nil
	earnedPointsCache[playerGuid] = nil
	charmByRaceCache[playerGuid] = nil
	finishedCache[playerGuid] = nil
end

if CustomBestiary then
	CustomBestiary.invalidatePlayer = invalidatePlayer
end

local function loadKillMap(playerGuid)
	local cached = killCache[playerGuid]
	if cached then
		return cached
	end

	local kills = {}
	local resultId = db.storeQuery("SELECT `raceid`, `kills` FROM `player_bestiary_kills` WHERE `player_id` = " .. playerGuid)
	if resultId ~= false then
		repeat
			kills[result.getDataInt(resultId, "raceid")] = result.getDataInt(resultId, "kills")
		until not result.next(resultId)
		result.free(resultId)
	end

	killCache[playerGuid] = kills
	rebuildEarnedPoints(playerGuid, kills)
	return kills
end

local function loadCharmMap(playerGuid)
	local cached = charmCache[playerGuid]
	if cached then
		return cached
	end

	local charms = {}
	local resultId = db.storeQuery("SELECT `charm_id`, `unlocked`, `raceid` FROM `player_bestiary_charms` WHERE `player_id` = " .. playerGuid)
	if resultId ~= false then
		repeat
			local charmId = result.getDataInt(resultId, "charm_id")
			charms[charmId] = {
				unlocked = result.getDataInt(resultId, "unlocked") ~= 0,
				raceId = result.getDataInt(resultId, "raceid")
			}
		until not result.next(resultId)
		result.free(resultId)
	end

	charmCache[playerGuid] = charms
	charmByRaceCache[playerGuid] = {}
	for charmId, state in pairs(charms) do
		if state.unlocked and state.raceId > 0 then
			charmByRaceCache[playerGuid][state.raceId] = charmId
		end
	end
	return charms
end

local function loadTrackerList(playerGuid)
	local cached = trackerCache[playerGuid]
	if cached then
		return cached
	end

	local tracker = {}
	local resultId = db.storeQuery("SELECT `raceid` FROM `player_bestiary_tracker` WHERE `player_id` = " .. playerGuid .. " ORDER BY `slot` ASC, `raceid` ASC")
	if resultId ~= false then
		repeat
			tracker[#tracker + 1] = result.getDataInt(resultId, "raceid")
		until not result.next(resultId)
		result.free(resultId)
	end

	trackerCache[playerGuid] = tracker
	return tracker
end

local function getEarnedCharmPoints(kills)
	local earned = 0
	for raceId, entry in pairs(CustomBestiary.monstersByRaceId) do
		if (kills[raceId] or 0) >= entry.toKill then
			earned = earned + entry.charmPoints
		end
	end
	return earned
end

rebuildEarnedPoints = function(playerGuid, kills)
	earnedPointsCache[playerGuid] = getEarnedCharmPoints(kills)

	local finished = {}
	for raceId, entry in pairs(CustomBestiary.monstersByRaceId) do
		if (kills[raceId] or 0) >= entry.toKill then
			finished[#finished + 1] = raceId
		end
	end
	table.sort(finished)
	finishedCache[playerGuid] = finished
end

local function updateKillCache(playerGuid, raceId, amount)
	playerGuid = tonumber(playerGuid) or 0
	raceId = tonumber(raceId) or 0
	amount = tonumber(amount) or 0
	if playerGuid <= 0 or raceId <= 0 or amount == 0 then
		return false
	end

	local kills = killCache[playerGuid]
	if not kills then
		earnedPointsCache[playerGuid] = nil
		finishedCache[playerGuid] = nil
		return false
	end

	kills[raceId] = math.max(0, (kills[raceId] or 0) + amount)
	rebuildEarnedPoints(playerGuid, kills)
	return true
end

local function getKillCount(playerGuid, raceId)
	playerGuid = tonumber(playerGuid) or 0
	raceId = tonumber(raceId) or 0
	if playerGuid <= 0 or raceId <= 0 then
		return 0
	end

	local kills = loadKillMap(playerGuid)
	return kills[raceId] or 0
end

local function preloadPlayer(playerGuid)
	playerGuid = tonumber(playerGuid) or 0
	if playerGuid <= 0 then
		return false
	end

	loadKillMap(playerGuid)
	loadTrackerList(playerGuid)
	return true
end

if CustomBestiary then
	CustomBestiary.updateKillCache = updateKillCache
	CustomBestiary.getKillCount = getKillCount
	CustomBestiary.preloadPlayer = preloadPlayer
end

local function getSpentCharmPoints(charms)
	local spent = 0
	for charmId, state in pairs(charms) do
		local charm = CustomBestiary.charmById[charmId]
		if charm and state.unlocked then
			spent = spent + charm.price
		end
	end
	return spent
end

local function getCharmBalance(playerGuid, kills, charms)
	if not earnedPointsCache[playerGuid] then
		rebuildEarnedPoints(playerGuid, kills)
	end
	return math.max(0, (earnedPointsCache[playerGuid] or 0) - getSpentCharmPoints(charms))
end

local function getStoredCharmBalance(playerGuid, kills, charms)
	local points = getPlayerCharmPoints(playerGuid)
	if points > 0 then
		return points
	end

	local legacyBalance = getCharmBalance(playerGuid, kills, charms)
	if legacyBalance > 0 then
		return setPlayerCharmPoints(playerGuid, legacyBalance)
	end
	return 0
end

local function getGoldBalance(player)
	local inventoryMoney = math.max(0, tonumber(player:getMoney()) or 0)
	local bankBalance = math.max(0, tonumber(player:getBankBalance()) or 0)
	return inventoryMoney + bankBalance
end

local function removePlayerGold(player, amount)
	amount = tonumber(amount) or 0
	if amount <= 0 then
		return true
	end

	local inventoryMoney = math.max(0, tonumber(player:getMoney()) or 0)
	local bankBalance = math.max(0, tonumber(player:getBankBalance()) or 0)
	if inventoryMoney + bankBalance < amount then
		return false
	end

	local fromInventory = math.min(inventoryMoney, amount)
	if fromInventory > 0 and not player:removeMoney(fromInventory) then
		return false
	end

	local fromBank = amount - fromInventory
	if fromBank > 0 then
		player:setBankBalance(bankBalance - fromBank)
	end
	return true
end

local function sendMessage(player, message)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_CYCLOPEDIA_SEND)
	out:addByte(RESP_MESSAGE)
	out:addString(message)
	return out:sendToPlayer(player)
end

local function writeCreatureInfo(out, entry)
	local outfit = entry and entry.outfit or {}
	out:addString(entry and entry.name or "?")
	out:addU16(clamp(outfit.type or 0, 0, 0xFFFF))
	out:addByte(clamp(outfit.head or 0, 0, 0xFF))
	out:addByte(clamp(outfit.body or 0, 0, 0xFF))
	out:addByte(clamp(outfit.legs or 0, 0, 0xFF))
	out:addByte(clamp(outfit.feet or 0, 0, 0xFF))
	out:addByte(clamp(outfit.addons or 0, 0, 0xFF))
end

local function writeCharms(out, player, kills, charms)
	local playerGuid = getPlayerGuid(player)
	out:addU32(clamp(getStoredCharmBalance(playerGuid, kills, charms), 0, 0xFFFFFFFF))
	out:addU64(getGoldBalance(player))
	out:addByte(math.min(#CustomBestiary.charmRunes, 0xFF))

	for _, charm in ipairs(CustomBestiary.charmRunes) do
		local state = charms[charm.id]
		local unlocked = state and state.unlocked
		local assignedRaceId = unlocked and state.raceId or 0

		out:addByte(charm.id)
		out:addString(charm.name)
		out:addString(charm.description)
		out:addByte(0)
		out:addU16(clamp(charm.price, 0, 0xFFFF))
		out:addByte(unlocked and 1 or 0)
		if unlocked then
			out:addByte(assignedRaceId > 0 and 1 or 0)
			if assignedRaceId > 0 then
				out:addU16(assignedRaceId)
				out:addU32(CHARM_REMOVE_COST)
				writeCreatureInfo(out, CustomBestiary.getMonster(assignedRaceId))
			end
		else
			out:addByte(0)
		end
	end

	out:addByte(0)

	local finished = finishedCache[playerGuid] or {}

	out:addU16(math.min(#finished, 0xFFFF))
	for i = 1, math.min(#finished, 0xFFFF) do
		out:addU16(finished[i])
		writeCreatureInfo(out, CustomBestiary.getMonster(finished[i]))
	end
end

local function sendBestiaryData(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local playerGuid = getPlayerGuid(player)
	local kills = loadKillMap(playerGuid)
	local charms = loadCharmMap(playerGuid)
	local classOrder, classes = CustomBestiary.getClasses()

	local out = NetworkMessage(player)
	out:addByte(OPCODE_CYCLOPEDIA_SEND)
	out:addByte(RESP_BESTIARY_DATA)
	out:addU16(math.min(#classOrder, 0xFFFF))

	for i = 1, math.min(#classOrder, 0xFFFF) do
		local className = classOrder[i]
		local entries = classes[className]
		local discovered = 0
		for _, entry in ipairs(entries) do
			if (kills[entry.raceId] or 0) > 0 then
				discovered = discovered + 1
			end
		end

		out:addString(className)
		out:addU16(math.min(#entries, 0xFFFF))
		out:addU16(math.min(discovered, 0xFFFF))
	end

	out:addByte(0)
	writeCharms(out, player, kills, charms)
	return out:sendToPlayer(player)
end

local function sendBestiaryOverviewEntries(player, title, entries)
	if not supportsCustomNetwork(player) then
		return false
	end

	local playerGuid = getPlayerGuid(player)
	local kills = loadKillMap(playerGuid)

	local out = NetworkMessage(player)
	out:addByte(OPCODE_CYCLOPEDIA_SEND)
	out:addByte(RESP_BESTIARY_OVERVIEW)
	out:addString(title)
	out:addU16(math.min(#entries, 0xFFFF))

	for i = 1, math.min(#entries, 0xFFFF) do
		local entry = entries[i]
		local progress = CustomBestiary.getProgress(entry, kills[entry.raceId] or 0)
		out:addU16(entry.raceId)
		if progress <= 0 then
			-- Astra paints undiscovered creatures with the black outfit shader.
			out:addByte(1)
			out:addByte(0)
			local masked = { name = "?", outfit = entry.outfit }
			writeCreatureInfo(out, masked)
		else
			out:addByte(math.min(progress + 1, 0xFF))
			out:addByte(math.min(progress, 0xFF))
			writeCreatureInfo(out, entry)
		end
	end

	return out:sendToPlayer(player)
end

local function sendBestiaryOverview(player, className)
	return sendBestiaryOverviewEntries(player, className, CustomBestiary.classes[className] or {})
end

local function sendBestiarySearch(player, query)
	if not supportsCustomNetwork(player) then
		return false
	end

	query = trimText(query):lower()
	local entries = {}
	if query ~= "" then
		for _, entry in pairs(CustomBestiary.monstersByRaceId) do
			local name = tostring(entry.name or ""):lower()
			if name:find(query, 1, true) then
				entries[#entries + 1] = entry
			end
		end
	end

	table.sort(entries, function(a, b)
		return tostring(a.name or "") < tostring(b.name or "")
	end)
	return sendBestiaryOverviewEntries(player, "Search", entries)
end

local function sendBestiaryMonster(player, raceId)
	if not supportsCustomNetwork(player) then
		return false
	end

	local entry = CustomBestiary.getMonster(raceId)
	if not entry then
		sendMessage(player, "Creature not found.")
		return false
	end

	local playerGuid = getPlayerGuid(player)
	local kills = loadKillMap(playerGuid)
	loadCharmMap(playerGuid) -- populates charmByRaceCache[playerGuid] as a side effect
	local killCount = kills[entry.raceId] or 0
	local detailLevel = BESTIARY_DETAIL_FULL

	local out = NetworkMessage(player)
	out:addByte(OPCODE_CYCLOPEDIA_SEND)
	out:addByte(RESP_BESTIARY_MONSTER)
	out:addU16(entry.raceId)
	out:addString(entry.class)
	writeCreatureInfo(out, entry)
	out:addByte(detailLevel)
	out:addU32(clamp(killCount, 0, 0xFFFFFFFF))
	out:addU16(entry.firstUnlock)
	out:addU16(entry.secondUnlock)
	out:addU16(entry.toKill)
	out:addByte(entry.stars)
	out:addByte(entry.occurrence)

	out:addByte(math.min(#entry.loot, 0xFF))
	for i = 1, math.min(#entry.loot, 0xFF) do
		local loot = entry.loot[i]
		out:addU16(loot.itemId)
		out:addByte(CustomBestiary.getLootTier(loot.chance))
		out:addByte(0)
		out:addString(loot.name)
		out:addByte(loot.maxCount)
	end

	out:addU16(entry.charmPoints)
	out:addByte(0)
	out:addByte(0)
	out:addU32(entry.health)
	out:addU32(entry.experience)
	out:addU16(entry.baseSpeed)
	out:addU16(entry.armor)

	out:addByte(math.min(#entry.elements, 0xFF))
	for i = 1, math.min(#entry.elements, 0xFF) do
		local element = entry.elements[i]
		out:addByte(element.id)
		out:addU16(element.percent)
	end

	out:addU16(math.min(#entry.locations, 0xFFFF))
	for i = 1, math.min(#entry.locations, 0xFFFF) do
		out:addString(entry.locations[i])
	end

	local assignedCharmId = charmByRaceCache[playerGuid] and charmByRaceCache[playerGuid][entry.raceId]
	out:addByte(assignedCharmId and 1 or 0)
	if assignedCharmId then
		out:addByte(assignedCharmId)
		out:addU32(CHARM_REMOVE_COST)
	else
		out:addByte(0)
	end

	return out:sendToPlayer(player)
end

local function sendBestiaryProgress(player, raceId, killCount)
	local entry = CustomBestiary.getMonster(raceId)
	if not entry then
		return
	end

	sendBestiaryOverview(player, entry.class)
end

local function sendTracker(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	if not CustomBestiary then
		return false
	end

	local playerGuid = getPlayerGuid(player)
	local kills = loadKillMap(playerGuid)
	local tracker = loadTrackerList(playerGuid)

	local out = NetworkMessage(player)
	out:addByte(OPCODE_CYCLOPEDIA_SEND)
	out:addByte(RESP_TRACKER)
	out:addByte(math.min(#tracker, 0xFF))
	for i = 1, math.min(#tracker, 0xFF) do
		local raceId = tracker[i]
		local entry = CustomBestiary.getMonster(raceId)
		if entry then
			local killCount = kills[raceId] or 0
			out:addU16(raceId)
			writeCreatureInfo(out, entry)
			out:addU32(clamp(killCount, 0, 0xFFFFFFFF))
			out:addU16(entry.firstUnlock)
			out:addU16(entry.secondUnlock)
			out:addU16(entry.toKill)
			out:addByte(CustomBestiary.getProgress(entry, killCount))
		else
			out:addU16(raceId)
			writeCreatureInfo(out, nil)
			out:addU32(0)
			out:addU16(1)
			out:addU16(1)
			out:addU16(1)
			out:addByte(0)
		end
	end
	return out:sendToPlayer(player)
end

local function sendTrackerIfTracked(player, raceId)
	if not supportsCustomNetwork(player) then
		return false
	end

	raceId = tonumber(raceId) or 0
	if raceId <= 0 then
		return false
	end

	local tracker = loadTrackerList(getPlayerGuid(player))
	for _, trackedRaceId in ipairs(tracker) do
		if trackedRaceId == raceId then
			return sendTracker(player)
		end
	end

	return false
end

CustomBestiary.sendTracker = sendTracker
CustomBestiary.sendTrackerIfTracked = sendTrackerIfTracked
CustomBestiary.sendProgress = sendBestiaryProgress

local function toggleTracker(player, raceId)
	local playerGuid = getPlayerGuid(player)
	if raceId <= 0 then
		sendTracker(player)
		return
	end

	local entry = CustomBestiary.getMonster(raceId)
	if not entry then
		sendMessage(player, "Creature not found.")
		return
	end

	local tracker = loadTrackerList(playerGuid)
	for slot, trackedRaceId in ipairs(tracker) do
		if trackedRaceId == raceId then
			db.query("DELETE FROM `player_bestiary_tracker` WHERE `player_id` = " .. playerGuid .. " AND `raceid` = " .. raceId)
			trackerCache[playerGuid] = nil
			sendTracker(player)
			return
		end
	end

	if #tracker >= MAX_TRACKER_SLOTS then
		sendMessage(player, "Your bestiary tracker is full.")
		return
	end

	db.query("INSERT INTO `player_bestiary_tracker` (`player_id`, `raceid`, `slot`) VALUES (" ..
		playerGuid .. ", " .. raceId .. ", " .. (#tracker + 1) .. ") ON DUPLICATE KEY UPDATE `slot` = VALUES(`slot`)")
	trackerCache[playerGuid] = nil
	sendTracker(player)
end

local function handleCharmAction(player, charmId, action, raceId)
	local playerGuid = getPlayerGuid(player)
	local charm = CustomBestiary.charmById[charmId]
	if not charm then
		sendMessage(player, "Charm not found.")
		return
	end

	local kills = loadKillMap(playerGuid)
	local charms = loadCharmMap(playerGuid)
	local state = charms[charmId]

	if action == 0 then
		if state and state.unlocked then
			sendMessage(player, "This charm is already unlocked.")
			return
		end

		local charmPoints = getStoredCharmBalance(playerGuid, kills, charms)
		if charmPoints < charm.price then
			sendMessage(player, "You do not have enough charm points.")
			return
		end

		db.query("INSERT INTO `player_bestiary_charms` (`player_id`, `charm_id`, `unlocked`, `raceid`) VALUES (" ..
			playerGuid .. ", " .. charmId .. ", 1, 0) ON DUPLICATE KEY UPDATE `unlocked` = 1")
		setPlayerCharmPoints(playerGuid, charmPoints - charm.price)
		invalidatePlayer(playerGuid)
		if CustomBestiary.refreshPlayerCharms then
			CustomBestiary.refreshPlayerCharms(player)
		end
		sendMessage(player, "Charm unlocked.")
		sendBestiaryData(player)
		return
	end

	if not state or not state.unlocked then
		sendMessage(player, "This charm is not unlocked.")
		return
	end

	if action == 1 then
		local entry = CustomBestiary.getMonster(raceId)
		if not entry or CustomBestiary.getProgress(entry, kills[raceId] or 0) < 4 then
			sendMessage(player, "This creature is not fully unlocked.")
			return
		end

		db.query("UPDATE `player_bestiary_charms` SET `raceid` = 0 WHERE `player_id` = " .. playerGuid .. " AND `raceid` = " .. raceId)
		db.query("UPDATE `player_bestiary_charms` SET `raceid` = " .. raceId .. " WHERE `player_id` = " .. playerGuid .. " AND `charm_id` = " .. charmId)
		invalidatePlayer(playerGuid)
		if CustomBestiary.refreshPlayerCharms then
			CustomBestiary.refreshPlayerCharms(player)
		end
		sendMessage(player, "Charm assigned.")
		sendBestiaryData(player)
		return
	elseif action == 2 then
		if (state.raceId or 0) <= 0 then
			sendMessage(player, "This charm is not assigned.")
			return
		end
		if not removePlayerGold(player, CHARM_REMOVE_COST) then
			sendMessage(player, "You do not have enough gold.")
			return
		end

		db.query("UPDATE `player_bestiary_charms` SET `raceid` = 0 WHERE `player_id` = " .. playerGuid .. " AND `charm_id` = " .. charmId)
		invalidatePlayer(playerGuid)
		if CustomBestiary.refreshPlayerCharms then
			CustomBestiary.refreshPlayerCharms(player)
		end
		sendMessage(player, "Charm removed.")
		sendBestiaryData(player)
		return
	end

	sendMessage(player, "Invalid charm action.")
end

local infoHandler = PacketHandler(OPCODE_CYCLOPEDIA_INFO)
function infoHandler.onReceive(player, msg)
	if not CustomBestiary then
		logError("[CustomBestiary] CustomBestiary lib was not loaded.")
		return
	end

	sendBestiaryData(player)
	sendTracker(player)
end
infoHandler:register()

local categoryHandler = PacketHandler(OPCODE_CYCLOPEDIA_CATEGORY)
function categoryHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 3 then
		return
	end

	if NetworkGuard.readByte(msg) == nil then
		return
	end

	local className = NetworkGuard.readString(msg, MAX_CYCLOPEDIA_CLASS_LENGTH)
	if not className then
		return
	end

	if className:sub(1, #BESTIARY_SEARCH_PREFIX) == BESTIARY_SEARCH_PREFIX then
		sendBestiarySearch(player, className:sub(#BESTIARY_SEARCH_PREFIX + 1))
	else
		sendBestiaryOverview(player, className)
	end
end
categoryHandler:register()

local monsterHandler = PacketHandler(OPCODE_CYCLOPEDIA_MONSTER)
function monsterHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 2 then
		return
	end

	local raceId = NetworkGuard.readU16(msg)
	if not raceId then
		return
	end

	sendBestiaryMonster(player, raceId)
end
monsterHandler:register()

local charmHandler = PacketHandler(OPCODE_CYCLOPEDIA_CHARM)
function charmHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 4 then
		return
	end

	local charmId = NetworkGuard.readByte(msg)
	local action = NetworkGuard.readByte(msg)
	local raceId = NetworkGuard.readU16(msg)
	if charmId == nil or action == nil or not raceId then
		return
	end

	handleCharmAction(player, charmId, action, raceId)
end
charmHandler:register()

local trackerHandler = PacketHandler(OPCODE_CYCLOPEDIA_TRACKER)
function trackerHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 2 then
		return
	end

	local raceId = NetworkGuard.readU16(msg)
	if not raceId then
		return
	end

	toggleTracker(player, raceId)
end
trackerHandler:register()

local bestiaryLogout = CreatureEvent("CustomBestiaryLogout")
function bestiaryLogout.onLogout(player)
	invalidatePlayer(player:getGuid())
	return true
end
bestiaryLogout:register()

local bestiaryLogin = CreatureEvent("CustomBestiaryLogin")
function bestiaryLogin.onLogin(player)
	if CustomBestiary and CustomBestiary.preloadPlayer then
		CustomBestiary.preloadPlayer(player:getGuid())
	end
	player:registerEvent("CustomBestiaryLogout")
	return true
end
bestiaryLogin:register()

ensureTables()
