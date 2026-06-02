if not configManager.getBoolean(configKeys.PREY_SYSTEM_ENABLED) or not CustomBestiary then
	return
end

local OPCODE_TASK_BASE_DATA = 0xBA
local OPCODE_TASK_ACTION = 0xBA
local OPCODE_TASK_DATA = 0xBB
local OPCODE_RESOURCE_BALANCE = 0xEE
local RESOURCE_BANK = 0
local RESOURCE_INVENTORY = 1
local RESOURCE_PREY = 10
local RESOURCE_HUNTING_TASK = 50
local TASK_SLOTS = 3
local TASK_LIST_SIZE = 9
local TASK_FREE_REROLL_TIME = 20 * 3600
local TASK_EXHAUST_TIME = 20 * 3600
local TASK_REROLL_COST_PER_LEVEL = 200
local TASK_WILDCARD_LIST_COST = 5
local TASK_REWARD_REROLL_COST = 1

local STATE_LOCKED = 0
local STATE_EXHAUSTED = 1
local STATE_SELECTION = 2
local STATE_WILDCARD = 3
local STATE_ACTIVE = 4
local STATE_REDEEM = 5

local ACTION_LIST_REROLL = 0
local ACTION_REWARD_REROLL = 1
local ACTION_WILDCARD_LIST = 2
local ACTION_SELECT = 3
local ACTION_CANCEL = 4
local ACTION_CLAIM = 5

HuntingTasks = HuntingTasks or {}
local taskCache = {}

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function ensureTables()
	db.query([[
		CREATE TABLE IF NOT EXISTS `player_hunting_tasks` (
			`player_id` INT NOT NULL,
			`slot` TINYINT UNSIGNED NOT NULL,
			`state` TINYINT UNSIGNED NOT NULL DEFAULT 2,
			`raceid` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
			`race_list` TEXT NOT NULL,
			`rarity` TINYINT UNSIGNED NOT NULL DEFAULT 1,
			`upgraded` TINYINT(1) NOT NULL DEFAULT 0,
			`kills` INT UNSIGNED NOT NULL DEFAULT 0,
			`reroll_at` BIGINT UNSIGNED NOT NULL DEFAULT 0,
			`disabled_until` BIGINT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `slot`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])

	db.query([[
		CREATE TABLE IF NOT EXISTS `player_hunting_task_points` (
			`player_id` INT NOT NULL,
			`points` BIGINT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])
end

local function serializeList(list)
	return table.concat(list or {}, ";")
end

local function parseList(raw)
	local list = {}
	for value in tostring(raw or ""):gmatch("[^;]+") do
		local raceId = tonumber(value)
		if raceId and CustomBestiary.getMonster(raceId) then
			list[#list + 1] = raceId
		end
	end
	return list
end

local function defaultSlot()
	return {
		state = STATE_SELECTION,
		raceId = 0,
		list = {},
		rarity = 1,
		upgraded = false,
		kills = 0,
		rerollAt = 0,
		disabledUntil = 0,
	}
end

local function saveSlot(playerGuid, slot, data)
	db.query(string.format(
		"INSERT INTO `player_hunting_tasks` (`player_id`, `slot`, `state`, `raceid`, `race_list`, `rarity`, `upgraded`, `kills`, `reroll_at`, `disabled_until`) " ..
		"VALUES (%d, %d, %d, %d, %s, %d, %d, %d, %d, %d) ON DUPLICATE KEY UPDATE " ..
		"`state` = VALUES(`state`), `raceid` = VALUES(`raceid`), `race_list` = VALUES(`race_list`), `rarity` = VALUES(`rarity`), " ..
		"`upgraded` = VALUES(`upgraded`), `kills` = VALUES(`kills`), `reroll_at` = VALUES(`reroll_at`), `disabled_until` = VALUES(`disabled_until`)",
		playerGuid,
		slot,
		data.state,
		data.raceId,
		db.escapeString(serializeList(data.list)),
		data.rarity,
		data.upgraded and 1 or 0,
		data.kills,
		data.rerollAt,
		data.disabledUntil
	))
end

local function getSelectedRaceIds(tasks, excludedSlot)
	local selected = {}
	for slot = 0, TASK_SLOTS - 1 do
		if slot ~= excludedSlot and tasks[slot] and tasks[slot].raceId > 0 then
			selected[tasks[slot].raceId] = true
		end
	end
	return selected
end

local function shuffle(list)
	for index = #list, 2, -1 do
		local other = math.random(1, index)
		list[index], list[other] = list[other], list[index]
	end
end

local function generateList(tasks, excludedSlot, all)
	local excluded = getSelectedRaceIds(tasks, excludedSlot)
	local candidates = {}
	for raceId in pairs(CustomBestiary.monstersByRaceId) do
		if not excluded[raceId] then
			candidates[#candidates + 1] = raceId
		end
	end
	table.sort(candidates)
	if all then
		return candidates
	end
	shuffle(candidates)
	local list = {}
	for index = 1, math.min(TASK_LIST_SIZE, #candidates) do
		list[index] = candidates[index]
	end
	return list
end

local function loadTasks(playerGuid)
	ensureTables()
	local tasks = {}
	for slot = 0, TASK_SLOTS - 1 do
		tasks[slot] = defaultSlot()
	end

	local resultId = db.storeQuery("SELECT * FROM `player_hunting_tasks` WHERE `player_id` = " .. playerGuid)
	if resultId ~= false then
		repeat
			local slot = result.getDataInt(resultId, "slot")
			if slot >= 0 and slot < TASK_SLOTS then
				tasks[slot] = {
					state = result.getDataInt(resultId, "state"),
					raceId = result.getDataInt(resultId, "raceid"),
					list = parseList(result.getDataString(resultId, "race_list")),
					rarity = math.max(1, math.min(5, result.getDataInt(resultId, "rarity"))),
					upgraded = result.getDataInt(resultId, "upgraded") ~= 0,
					kills = result.getDataInt(resultId, "kills"),
					rerollAt = result.getDataLong(resultId, "reroll_at"),
					disabledUntil = result.getDataLong(resultId, "disabled_until"),
				}
			end
		until not result.next(resultId)
		result.free(resultId)
	end

	local now = os.time()
	for slot = 0, TASK_SLOTS - 1 do
		local data = tasks[slot]
		if data.state == STATE_EXHAUSTED and data.disabledUntil <= now then
			data.state = STATE_SELECTION
		end
		if data.state == STATE_SELECTION and #data.list == 0 then
			data.list = generateList(tasks, slot, false)
		end
		saveSlot(playerGuid, slot, data)
	end
	return tasks
end

local function getTasks(player)
	local playerId = player:getId()
	if not taskCache[playerId] then
		taskCache[playerId] = loadTasks(player:getGuid())
	end
	return taskCache[playerId]
end

local function getBestiaryEntry(raceId)
	return CustomBestiary.getMonster(raceId)
end

local function getDifficultyForRace(raceId)
	local entry = getBestiaryEntry(raceId)
	if not entry then
		return 1
	end
	return entry.stars <= 1 and 1 or (entry.stars <= 3 and 2 or 3)
end

local function getTaskOption(raceId, rarity)
	local difficulty = getDifficultyForRace(raceId)
	local kills = 25 * (4 ^ (difficulty - 1))
	local reward = 10 * (4 ^ (difficulty - 1))
	for grade = 1, math.max(1, rarity) - 1 do
		reward = math.floor((reward * (115 + (difficulty * 5)) + 50) / 100)
	end
	return {
		firstKills = kills,
		firstReward = reward,
		secondKills = kills * 2,
		secondReward = reward * 2,
	}
end

local function getKillMap(playerGuid)
	local kills = {}
	local resultId = db.storeQuery("SELECT `raceid`, `kills` FROM `player_bestiary_kills` WHERE `player_id` = " .. playerGuid)
	if resultId ~= false then
		repeat
			kills[result.getDataInt(resultId, "raceid")] = result.getDataInt(resultId, "kills")
		until not result.next(resultId)
		result.free(resultId)
	end
	return kills
end

local function isFullyUnlocked(killMap, raceId)
	local entry = getBestiaryEntry(raceId)
	return entry and CustomBestiary.getProgress(entry, killMap[raceId] or 0) >= 4
end

local function getPoints(playerGuid)
	ensureTables()
	db.query("INSERT IGNORE INTO `player_hunting_task_points` (`player_id`) VALUES (" .. playerGuid .. ")")
	local resultId = db.storeQuery("SELECT `points` FROM `player_hunting_task_points` WHERE `player_id` = " .. playerGuid)
	if resultId == false then
		return 0
	end
	local points = result.getDataLong(resultId, "points")
	result.free(resultId)
	return math.max(0, points)
end

local function addPoints(playerGuid, amount)
	amount = math.max(0, tonumber(amount) or 0)
	db.query("INSERT INTO `player_hunting_task_points` (`player_id`, `points`) VALUES (" ..
		playerGuid .. ", " .. amount .. ") ON DUPLICATE KEY UPDATE `points` = `points` + " .. amount)
	return getPoints(playerGuid)
end

local function getWildcards(player)
	local resultId = db.storeQuery("SELECT `bonus_rerolls` FROM `players` WHERE `id` = " .. player:getGuid())
	if resultId == false then
		return 0
	end
	local value = result.getDataLong(resultId, "bonus_rerolls")
	result.free(resultId)
	return math.max(0, value)
end

local function consumeWildcards(player, amount)
	amount = math.max(0, tonumber(amount) or 0)
	local wildcards = getWildcards(player)
	if wildcards < amount then
		return false
	end
	db.query("UPDATE `players` SET `bonus_rerolls` = `bonus_rerolls` - " .. amount .. " WHERE `id` = " .. player:getGuid())
	return true
end

local function removePlayerGold(player, amount)
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

local function sendResource(player, resourceType, value)
	local out = NetworkMessage(player)
	out:addByte(OPCODE_RESOURCE_BALANCE)
	out:addByte(resourceType)
	out:addU64(math.max(0, tonumber(value) or 0))
	return out:sendToPlayer(player)
end

local function sendBalances(player)
	sendResource(player, RESOURCE_BANK, player:getBankBalance())
	sendResource(player, RESOURCE_INVENTORY, player:getMoney())
	sendResource(player, RESOURCE_PREY, getWildcards(player))
	sendResource(player, RESOURCE_HUNTING_TASK, getPoints(player:getGuid()))
end

local function sendBaseData(player)
	local entries = {}
	for _, entry in pairs(CustomBestiary.monstersByRaceId) do
		entries[#entries + 1] = entry
	end
	table.sort(entries, function(a, b) return a.raceId < b.raceId end)

	local out = NetworkMessage(player)
	out:addByte(OPCODE_TASK_BASE_DATA)
	out:addU16(math.min(#entries, 0xFFFF))
	for index = 1, math.min(#entries, 0xFFFF) do
		local entry = entries[index]
		out:addU16(entry.raceId)
		out:addByte(entry.stars <= 1 and 1 or (entry.stars <= 3 and 2 or 3))
	end

	out:addByte(15)
	for difficulty = 1, 3 do
		local kills = 25 * (4 ^ (difficulty - 1))
		local reward = 10 * (4 ^ (difficulty - 1))
		for grade = 1, 5 do
			out:addByte(difficulty)
			out:addByte(grade)
			out:addU16(kills)
			out:addU16(reward)
			out:addU16(kills * 2)
			out:addU16(reward * 2)
			reward = math.floor((reward * (115 + (difficulty * 5)) + 50) / 100)
		end
	end
	out:addU32(math.max(0, player:getLevel() * TASK_REROLL_COST_PER_LEVEL))
	out:addU32(math.max(0, player:getLevel() * TASK_REROLL_COST_PER_LEVEL))
	out:addByte(TASK_WILDCARD_LIST_COST)
	out:addByte(TASK_REWARD_REROLL_COST)
	return out:sendToPlayer(player)
end

local function sendSlot(player, slot)
	local tasks = getTasks(player)
	local data = tasks[slot]
	if not data then
		return false
	end

	local killMap = getKillMap(player:getGuid())
	local out = NetworkMessage(player)
	out:addByte(OPCODE_TASK_DATA)
	out:addByte(slot)
	out:addByte(data.state)
	if data.state == STATE_LOCKED then
		out:addByte(0)
	elseif data.state == STATE_SELECTION or data.state == STATE_WILDCARD then
		out:addU16(math.min(#data.list, 0xFFFF))
		for index = 1, math.min(#data.list, 0xFFFF) do
			local raceId = data.list[index]
			out:addU16(raceId)
			out:addByte(isFullyUnlocked(killMap, raceId) and 1 or 0)
		end
	elseif data.state == STATE_ACTIVE or data.state == STATE_REDEEM then
		local option = getTaskOption(data.raceId, data.rarity)
		out:addU16(data.raceId)
		out:addByte(data.upgraded and 1 or 0)
		out:addU16(data.upgraded and option.secondKills or option.firstKills)
		out:addU16(data.kills)
		out:addByte(data.rarity)
	end
	local nextActionAt = data.state == STATE_EXHAUSTED and data.disabledUntil or data.rerollAt
	out:addU32(math.max(0, (nextActionAt or 0) - os.time()))
	return out:sendToPlayer(player)
end

function HuntingTasks.sendAll(player)
	if not supportsCustomNetwork(player) then
		return false
	end
	getTasks(player)
	sendBaseData(player)
	for slot = 0, TASK_SLOTS - 1 do
		sendSlot(player, slot)
	end
	sendBalances(player)
	return true
end

local function resetSelection(tasks, slot, data)
	data.state = STATE_SELECTION
	data.raceId = 0
	data.rarity = 1
	data.upgraded = false
	data.kills = 0
	data.disabledUntil = 0
	data.list = generateList(tasks, slot, false)
end

local function handleAction(player, slot, action, upgraded, raceId)
	if slot < 0 or slot >= TASK_SLOTS then
		return
	end

	local tasks = getTasks(player)
	local data = tasks[slot]
	local rerollCost = math.max(0, player:getLevel() * TASK_REROLL_COST_PER_LEVEL)
	if action == ACTION_LIST_REROLL then
		if data.disabledUntil > os.time() then
			return
		end
		if data.rerollAt > os.time() then
			if not removePlayerGold(player, rerollCost) then
				return player:sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have enough gold to reroll this hunting task.")
			end
		else
			data.rerollAt = os.time() + TASK_FREE_REROLL_TIME
		end
		resetSelection(tasks, slot, data)
	elseif action == ACTION_REWARD_REROLL then
		if not consumeWildcards(player, TASK_REWARD_REROLL_COST) then
			return player:sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have enough Prey Wildcards.")
		end
		data.rarity = math.random(1, 5)
	elseif action == ACTION_WILDCARD_LIST then
		if not consumeWildcards(player, TASK_WILDCARD_LIST_COST) then
			return player:sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have enough Prey Wildcards.")
		end
		data.state = STATE_WILDCARD
		data.list = generateList(tasks, slot, true)
	elseif action == ACTION_SELECT then
		if data.state ~= STATE_SELECTION and data.state ~= STATE_WILDCARD then
			return
		end
		local found = false
		for _, listedRaceId in ipairs(data.list) do
			if listedRaceId == raceId then
				found = true
				break
			end
		end
		if not found or not getBestiaryEntry(raceId) then
			return
		end
		data.state = STATE_ACTIVE
		data.raceId = raceId
		data.upgraded = upgraded and isFullyUnlocked(getKillMap(player:getGuid()), raceId)
		data.kills = 0
	elseif action == ACTION_CANCEL then
		if not removePlayerGold(player, rerollCost) then
			return player:sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have enough gold to cancel this hunting task.")
		end
		resetSelection(tasks, slot, data)
	elseif action == ACTION_CLAIM then
		if data.state ~= STATE_REDEEM then
			return
		end
		local option = getTaskOption(data.raceId, data.rarity)
		local points = data.upgraded and option.secondReward or option.firstReward
		addPoints(player:getGuid(), points)
		data.state = STATE_EXHAUSTED
		data.raceId = 0
		data.list = {}
		data.rarity = 1
		data.upgraded = false
		data.kills = 0
		data.disabledUntil = os.time() + TASK_EXHAUST_TIME
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE or MESSAGE_STATUS_CONSOLE_BLUE,
			"You completed your hunting task and earned " .. points .. " Hunting Task points.")
	else
		return
	end

	saveSlot(player:getGuid(), slot, data)
	sendSlot(player, slot)
	sendBalances(player)
end

function HuntingTasks.addKill(player, raceId)
	if not player then
		return
	end

	local tasks = getTasks(player)
	for slot = 0, TASK_SLOTS - 1 do
		local data = tasks[slot]
		if data.state == STATE_ACTIVE and data.raceId == raceId then
			local option = getTaskOption(data.raceId, data.rarity)
			local required = data.upgraded and option.secondKills or option.firstKills
			data.kills = math.min(required, data.kills + 1)
			if data.kills >= required then
				data.state = STATE_REDEEM
			end
			saveSlot(player:getGuid(), slot, data)
			sendSlot(player, slot)
		end
	end
end

local actionHandler = PacketHandler(OPCODE_TASK_ACTION)
function actionHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 5 then
		return
	end
	handleAction(player, msg:getByte(), msg:getByte(), msg:getByte() ~= 0, msg:getU16())
end
actionHandler:register()

local logoutEvent = CreatureEvent("HuntingTasksLogout")
function logoutEvent.onLogout(player)
	taskCache[player:getId()] = nil
	return true
end
logoutEvent:register()

local loginEvent = CreatureEvent("HuntingTasksLogin")
function loginEvent.onLogin(player)
	player:registerEvent("HuntingTasksLogout")
	return true
end
loginEvent:register()

HuntingTasks.sendSlot = sendSlot
HuntingTasks.sendBalances = sendBalances
ensureTables()
