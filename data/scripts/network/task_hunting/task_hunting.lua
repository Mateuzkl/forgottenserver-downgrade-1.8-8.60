-- Global-like Task Hunting for AstraClient.
--
-- Wire format:
--   S2C 0xBA: base data (bestiary difficulty + reward table + prices)
--   S2C 0xBB: one slot state
--   C2S 0xBA: slot(U8), action(U8), upgraded(U8), raceId(U16)
--
-- This module intentionally does not use the Task Board packet (0x53). Bounty,
-- Weekly Tasks and the Hunting Shop remain separate Task Board features.

if not configManager or not configManager.getBoolean
	or not configManager.getBoolean(configKeys.TASK_HUNTING_SYSTEM_ENABLED) then
	return
end

-- The revscript loader can discover this file as well as task_board/init.lua
-- loading it explicitly. Keep one cache and one 0xBA PacketHandler in either
-- load order.
if _TASK_HUNTING_MODULE then
	return _TASK_HUNTING_MODULE
end

local TaskHunting = {}

TaskHunting.DEBUG = false

local OPCODE_BASE_DATA = 0xBA
local OPCODE_SLOT_DATA = 0xBB
local SLOT_COUNT = 3
local LIST_SIZE = 9

local STATE_LOCKED = 0
local STATE_EXHAUSTED = 1
local STATE_SELECT = 2
local STATE_WILDCARD = 3
local STATE_ACTIVE = 4
local STATE_REDEEM = 5

local ACTION_LIST_REROLL = 0
local ACTION_REWARD_REROLL = 1
local ACTION_SELECT_WILDCARD = 2
local ACTION_SELECT = 3
local ACTION_REMOVE = 4
local ACTION_COLLECT = 5

local FREE_REROLL_SECONDS = 20 * 60 * 60
local REROLL_PRICE_PER_LEVEL = 200
local WILDCARD_SELECT_PRICE = 5
local WILDCARD_REWARD_REROLL_PRICE = 1
local KILL_SAVE_INTERVAL = 5
local RESOURCE_BANK = 0
local RESOURCE_INVENTORY_GOLD = 1
local RESOURCE_PREY_WILDCARDS = 10

local taskCache = {}
local schemaReady = nil

local function debug(message, ...)
	if TaskHunting.DEBUG then
		print(string.format("[TaskHunting] " .. message, ...))
	end
end

local function supportsAstra(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function clamp(value, minimum, maximum)
	value = tonumber(value) or minimum
	return math.max(minimum, math.min(maximum, value))
end

local function ensureSchema()
	if schemaReady == true then
		return true
	end

	local tableReady = db.query([[
		CREATE TABLE IF NOT EXISTS `player_task_hunting` (
			`player_id` INT NOT NULL,
			`slot` TINYINT UNSIGNED NOT NULL,
			`state` TINYINT UNSIGNED NOT NULL DEFAULT 2,
			`selected_raceid` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
			`current_kills` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
			`rarity` TINYINT UNSIGNED NOT NULL DEFAULT 1,
			`upgraded` TINYINT(1) NOT NULL DEFAULT 0,
			`wildcard` TINYINT(1) NOT NULL DEFAULT 0,
			`race_list` TEXT NOT NULL,
			`free_reroll_at` BIGINT NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `slot`),
			KEY `idx_player_task_hunting_state` (`player_id`, `state`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])

	local function columnExists(tableName, columnName)
		local resultId = db.storeQuery(
			"SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = DATABASE()" ..
			" AND TABLE_NAME = " .. db.escapeString(tableName) ..
			" AND COLUMN_NAME = " .. db.escapeString(columnName) .. " LIMIT 1")
		if resultId == false then
			return false
		end
		result.free(resultId)
		return true
	end

	local function ensureColumn(tableName, columnName, ddl)
		if columnExists(tableName, columnName) then
			return true
		end
		return db.query(ddl)
	end

	local wildcardColumnReady = ensureColumn(
		"player_task_hunting",
		"wildcard",
		"ALTER TABLE `player_task_hunting` ADD `wildcard` TINYINT(1) NOT NULL DEFAULT 0 AFTER `upgraded`")
	local preyWildcardColumnReady = ensureColumn(
		"players",
		"bonus_rerolls",
		"ALTER TABLE `players` ADD `bonus_rerolls` BIGINT UNSIGNED NOT NULL DEFAULT 0")

	schemaReady = tableReady and wildcardColumnReady and preyWildcardColumnReady and true or nil
	if not schemaReady then
		print("[TaskHunting] Database schema is not ready; will retry on next use.")
		return false
	end
	return true
end

local function defaultSlot()
	return {
		state = STATE_SELECT,
		selectedRaceId = 0,
		currentKills = 0,
		rarity = 1,
		upgraded = false,
		wildcard = false,
		raceList = {},
		freeRerollAt = 0,
	}
end

local function serializeRaceList(raceList)
	local values = {}
	for _, raceId in ipairs(raceList or {}) do
		values[#values + 1] = tostring(clamp(raceId, 1, 0xFFFF))
	end
	return table.concat(values, ",")
end

local function parseRaceList(raw)
	local raceList = {}
	for value in tostring(raw or ""):gmatch("[^,]+") do
		local raceId = tonumber(value)
		if raceId and raceId > 0 and raceId <= 0xFFFF then
			raceList[#raceList + 1] = raceId
		end
	end
	return raceList
end

local function getSlotLockType(player, slot)
	-- Crystal uses a premium second slot and a purchasable third slot. The TFS
	-- implementation keeps all three usable until the store integration exists,
	-- while still supporting the protocol's locked state.
	if slot < 0 or slot >= SLOT_COUNT then
		return 1
	end
	return nil
end

local function loadTaskData(player)
	if not ensureSchema() then
		return nil
	end

	local playerId = player:getId()
	local cached = taskCache[playerId]
	if cached then
		return cached
	end

	local data = { slots = {} }
	for slot = 0, SLOT_COUNT - 1 do
		data.slots[slot] = defaultSlot()
	end

	local resultId = db.storeQuery("SELECT * FROM `player_task_hunting` WHERE `player_id` = " .. player:getGuid())
	if resultId ~= false then
		repeat
			local slot = result.getDataInt(resultId, "slot")
			if slot >= 0 and slot < SLOT_COUNT then
				data.slots[slot] = {
					state = result.getDataInt(resultId, "state"),
					selectedRaceId = result.getDataInt(resultId, "selected_raceid"),
					currentKills = result.getDataInt(resultId, "current_kills"),
					rarity = clamp(result.getDataInt(resultId, "rarity"), 1, 5),
					upgraded = result.getDataInt(resultId, "upgraded") ~= 0,
					wildcard = result.getDataInt(resultId, "wildcard") ~= 0,
					raceList = parseRaceList(result.getDataString(resultId, "race_list")),
					freeRerollAt = result.getDataLong(resultId, "free_reroll_at"),
				}
			end
		until not result.next(resultId)
		result.free(resultId)
	end

	taskCache[playerId] = data
	return data
end

local function saveSlot(player, slot)
	local data = taskCache[player:getId()]
	local slotData = data and data.slots[slot]
	if not slotData then
		return false
	end

	-- TFS db.escapeString returns a quoted SQL string literal.
	local serializedRaceList = db.escapeString(serializeRaceList(slotData.raceList))

	return db.query(string.format(
		"INSERT INTO `player_task_hunting` (`player_id`, `slot`, `state`, `selected_raceid`, `current_kills`, `rarity`, `upgraded`, `wildcard`, `race_list`, `free_reroll_at`) " ..
		"VALUES (%d, %d, %d, %d, %d, %d, %d, %d, %s, %d) " ..
		"ON DUPLICATE KEY UPDATE `state` = VALUES(`state`), `selected_raceid` = VALUES(`selected_raceid`), " ..
		"`current_kills` = VALUES(`current_kills`), `rarity` = VALUES(`rarity`), `upgraded` = VALUES(`upgraded`), " ..
		"`wildcard` = VALUES(`wildcard`), `race_list` = VALUES(`race_list`), `free_reroll_at` = VALUES(`free_reroll_at`)",
		player:getGuid(),
		slot,
		clamp(slotData.state, STATE_LOCKED, STATE_REDEEM),
		clamp(slotData.selectedRaceId, 0, 0xFFFF),
		clamp(slotData.currentKills, 0, 0xFFFF),
		clamp(slotData.rarity, 1, 5),
		slotData.upgraded and 1 or 0,
		slotData.wildcard and 1 or 0,
		serializedRaceList,
		math.max(0, tonumber(slotData.freeRerollAt) or 0)
	))
end

local function saveAll(player)
	for slot = 0, SLOT_COUNT - 1 do
		saveSlot(player, slot)
	end
end

local function getBestiaryKills(player)
	local kills = {}
	local resultId = db.storeQuery("SELECT `raceid`, `kills` FROM `player_bestiary_kills` WHERE `player_id` = " .. player:getGuid())
	if resultId ~= false then
		repeat
			kills[result.getDataInt(resultId, "raceid")] = result.getDataInt(resultId, "kills")
		until not result.next(resultId)
		result.free(resultId)
	end
	return kills
end

local function getDifficulty(entry)
	local stars = entry and tonumber(entry.stars) or 0
	if stars <= 1 then
		return 1
	elseif stars <= 3 then
		return 2
	end
	return 3
end

local function isBestiaryComplete(entry, kills)
	return entry and (kills[entry.raceId] or 0) >= (entry.toKill or math.huge)
end

local function buildRewardData()
	local rewards = {}
	local kills = 25
	for difficulty = 1, 3 do
		local reward = math.floor((10 * kills) / 25 + 0.5)
		for rarity = 1, 5 do
			rewards[#rewards + 1] = {
				difficulty = difficulty,
				rarity = rarity,
				firstKills = kills,
				firstReward = reward,
				secondKills = kills * 2,
				secondReward = reward * 2,
			}
			reward = math.floor(reward * (115 + difficulty * 5) / 100 + 0.5)
		end
		kills = kills * 4
	end
	return rewards
end

local REWARD_DATA = buildRewardData()

local function getRewardOption(entry, rarity)
	local difficulty = getDifficulty(entry)
	for _, option in ipairs(REWARD_DATA) do
		if option.difficulty == difficulty and option.rarity == rarity then
			return option
		end
	end
	return nil
end

local function shuffle(values)
	for index = #values, 2, -1 do
		local other = math.random(index)
		values[index], values[other] = values[other], values[index]
	end
end

local function collectExcludedRaceIds(data, exceptSlot)
	local excluded = {}
	for slot = 0, SLOT_COUNT - 1 do
		if slot ~= exceptSlot then
			local slotData = data.slots[slot]
			if slotData.selectedRaceId and slotData.selectedRaceId > 0 then
				excluded[slotData.selectedRaceId] = true
			end
			for _, raceId in ipairs(slotData.raceList or {}) do
				excluded[raceId] = true
			end
		end
	end
	return excluded
end

local function generateRaceList(player, data, slot)
	if not CustomBestiary or not CustomBestiary.monstersByRaceId then
		return {}
	end

	local excluded = collectExcludedRaceIds(data, slot)
	local buckets = { {}, {}, {}, {} }
	for raceId, entry in pairs(CustomBestiary.monstersByRaceId) do
		if not excluded[raceId] and (entry.experience or 0) > 0 then
			local stars = clamp(entry.stars, 1, 5)
			local bucket = stars <= 1 and 1 or (stars == 2 and 2 or (stars == 3 and 3 or 4))
			buckets[bucket][#buckets[bucket] + 1] = raceId
		end
	end

	for _, bucket in ipairs(buckets) do
		shuffle(bucket)
	end

	local targets
	local levelStage = math.floor(player:getLevel() / 100)
	if levelStage == 0 then
		targets = { 3, 3, 2, 1 }
	elseif levelStage <= 2 then
		targets = { 1, 3, 3, 2 }
	elseif levelStage <= 4 then
		targets = { 1, 2, 3, 3 }
	else
		targets = { 1, 1, 3, 4 }
	end

	local selected = {}
	local selectedSet = {}
	for bucketIndex, target in ipairs(targets) do
		for index = 1, math.min(target, #buckets[bucketIndex]) do
			local raceId = buckets[bucketIndex][index]
			selected[#selected + 1] = raceId
			selectedSet[raceId] = true
		end
	end

	if #selected < LIST_SIZE then
		local remaining = {}
		for _, bucket in ipairs(buckets) do
			for _, raceId in ipairs(bucket) do
				if not selectedSet[raceId] then
					remaining[#remaining + 1] = raceId
				end
			end
		end
		shuffle(remaining)
		for _, raceId in ipairs(remaining) do
			if #selected >= LIST_SIZE then
				break
			end
			selected[#selected + 1] = raceId
		end
	end

	return selected
end

local function generateWildcardRaceList(data, slot)
	if not CustomBestiary or not CustomBestiary.monstersByRaceId then
		return {}
	end

	local excluded = collectExcludedRaceIds(data, slot)
	local raceList = {}
	for candidateRaceId, entry in pairs(CustomBestiary.monstersByRaceId) do
		if not excluded[candidateRaceId] and (entry.experience or 0) > 0 then
			raceList[#raceList + 1] = candidateRaceId
		end
	end
	table.sort(raceList)
	return raceList
end

local function ensureSlotReady(player, data, slot)
	local slotData = data.slots[slot]
	if not slotData then
		return nil
	end

	if getSlotLockType(player, slot) then
		slotData.state = STATE_LOCKED
		return slotData
	end

	if slotData.state == STATE_LOCKED or slotData.state == STATE_EXHAUSTED then
		slotData.state = STATE_SELECT
		slotData.selectedRaceId = 0
		slotData.currentKills = 0
		slotData.rarity = 1
		slotData.upgraded = false
		slotData.wildcard = false
		slotData.raceList = {}
	end

	if slotData.state == STATE_SELECT and #slotData.raceList == 0 then
		slotData.wildcard = false
		slotData.raceList = generateRaceList(player, data, slot)
	end
	return slotData
end

local function getPlayerWildcards(player)
	local resultId = db.storeQuery("SELECT `bonus_rerolls` FROM `players` WHERE `id` = " .. player:getGuid())
	if resultId == false then
		return 0
	end
	local wildcards = math.max(0, result.getDataInt(resultId, "bonus_rerolls"))
	result.free(resultId)
	return wildcards
end

local function setPlayerWildcards(player, value)
	value = math.floor(math.max(0, tonumber(value) or 0))
	db.query("UPDATE `players` SET `bonus_rerolls` = " .. value .. " WHERE `id` = " .. player:getGuid())
	return value
end

local function removePlayerWildcards(player, amount)
	local current = getPlayerWildcards(player)
	if current < amount then
		return false
	end
	setPlayerWildcards(player, current - amount)
	return true
end

local function getGold(player)
	return math.max(0, player:getMoney()) + math.max(0, player:getBankBalance())
end

local function removeGold(player, amount)
	amount = math.max(0, tonumber(amount) or 0)
	if getGold(player) < amount then
		return false
	end

	local fromInventory = math.min(player:getMoney(), amount)
	if fromInventory > 0 and not player:removeMoney(fromInventory) then
		return false
	end

	local remaining = amount - fromInventory
	if remaining > 0 then
		player:setBankBalance(player:getBankBalance() - remaining)
	end
	return true
end

local function getRerollPrice(player)
	return clamp(player:getLevel() * REROLL_PRICE_PER_LEVEL, 0, 0xFFFFFFFF)
end

local function sendResourceBalance(player, resourceType, amount)
	local out = NetworkMessage(player)
	out:addByte(0xEE)
	out:addByte(resourceType)
	out:addU64(math.max(0, tonumber(amount) or 0))
	return out:sendToPlayer(player)
end

local function sendBalances(player)
	if not supportsAstra(player) then
		return false
	end
	sendResourceBalance(player, RESOURCE_BANK, player:getBankBalance())
	sendResourceBalance(player, RESOURCE_INVENTORY_GOLD, player:getMoney())
	sendResourceBalance(player, RESOURCE_PREY_WILDCARDS, getPlayerWildcards(player))
	sendResourceBalance(player, TaskBoard.Resources.TASK_HUNTING, player:getTaskHuntingPoints())
	return true
end

function TaskHunting.sendBasicData(player)
	if not supportsAstra(player) or not CustomBestiary or not CustomBestiary.monstersByRaceId then
		return false
	end

	local kills = getBestiaryKills(player)
	local raceIds = {}
	for raceId in pairs(CustomBestiary.monstersByRaceId) do
		raceIds[#raceIds + 1] = raceId
	end
	table.sort(raceIds)

	local out = NetworkMessage(player)
	out:addByte(OPCODE_BASE_DATA)
	out:addU16(clamp(#raceIds, 0, 0xFFFF))
	for _, raceId in ipairs(raceIds) do
		local entry = CustomBestiary.getMonster(raceId)
		out:addU16(raceId)
		out:addByte(getDifficulty(entry))
	end

	out:addByte(#REWARD_DATA)
	for _, option in ipairs(REWARD_DATA) do
		out:addByte(option.difficulty)
		out:addByte(option.rarity)
		out:addU16(option.firstKills)
		out:addU16(option.firstReward)
		out:addU16(option.secondKills)
		out:addU16(option.secondReward)
	end

	out:addU32(getRerollPrice(player))
	out:addU32(getRerollPrice(player))
	out:addByte(WILDCARD_SELECT_PRICE)
	out:addByte(WILDCARD_REWARD_REROLL_PRICE)
	debug("send basic data opcode=0xBA player=%s monsters=%d", player:getName(), #raceIds)
	return out:sendToPlayer(player)
end

local function writeRaceList(out, raceList, kills)
	out:addU16(clamp(#raceList, 0, 0xFFFF))
	for _, raceId in ipairs(raceList) do
		local entry = CustomBestiary and CustomBestiary.getMonster(raceId)
		out:addU16(clamp(raceId, 0, 0xFFFF))
		out:addByte(isBestiaryComplete(entry, kills) and 1 or 0)
	end
end

function TaskHunting.sendSlotData(player, slot)
	if not supportsAstra(player) then
		return false
	end

	local data = loadTaskData(player)
	if not data then
		return false
	end
	local slotData = ensureSlotReady(player, data, slot)
	if not slotData then
		return false
	end

	local state = slotData.state
	local lockType = getSlotLockType(player, slot)
	if lockType then
		state = STATE_LOCKED
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_SLOT_DATA)
	out:addByte(slot)
	out:addByte(state)

	if state == STATE_LOCKED then
		out:addByte(lockType)
	elseif state == STATE_SELECT or state == STATE_WILDCARD then
		local raceList = state == STATE_WILDCARD and generateWildcardRaceList(data, slot) or slotData.raceList
		writeRaceList(out, raceList, getBestiaryKills(player))
	elseif state == STATE_ACTIVE or state == STATE_REDEEM then
		local entry = CustomBestiary and CustomBestiary.getMonster(slotData.selectedRaceId)
		local option = getRewardOption(entry, slotData.rarity)
		local requiredKills = option and (slotData.upgraded and option.secondKills or option.firstKills) or 0
		out:addU16(clamp(slotData.selectedRaceId, 0, 0xFFFF))
		out:addByte(slotData.upgraded and 1 or 0)
		out:addU16(requiredKills)
		out:addU16(clamp(slotData.currentKills, 0, requiredKills))
		out:addByte(clamp(slotData.rarity, 1, 5))
	end

	out:addU32(clamp(math.max(0, (slotData.freeRerollAt or 0) - os.time()), 0, 0xFFFFFFFF))
	debug("send slot opcode=0xBB player=%s slot=%d state=%d", player:getName(), slot, state)
	return out:sendToPlayer(player)
end

function TaskHunting.sendFullSync(player)
	if not supportsAstra(player) then
		return false
	end

	local data = loadTaskData(player)
	if not data then
		return false
	end

	for slot = 0, SLOT_COUNT - 1 do
		ensureSlotReady(player, data, slot)
		saveSlot(player, slot)
	end

	TaskHunting.sendBasicData(player)
	for slot = 0, SLOT_COUNT - 1 do
		TaskHunting.sendSlotData(player, slot)
	end
	sendBalances(player)
	debug("full sync sent player=%s", player:getName())
	return true
end

local function resetSlot(player, data, slot)
	local slotData = data.slots[slot]
	slotData.state = STATE_SELECT
	slotData.selectedRaceId = 0
	slotData.currentKills = 0
	slotData.rarity = 1
	slotData.upgraded = false
	slotData.wildcard = false
	slotData.raceList = generateRaceList(player, data, slot)
end

local function containsRace(raceList, raceId)
	for _, listedRaceId in ipairs(raceList or {}) do
		if listedRaceId == raceId then
			return true
		end
	end
	return false
end

local function rerollReward(slotData)
	if slotData.rarity >= 4 then
		slotData.rarity = 5
		return
	end

	-- Paying a reward reroll always improves the current grade. The maximum
	-- intentionally shrinks for higher rarities so the roll cannot downgrade
	-- or stay at the same grade.
	local maximum = ({ [1] = 70, [2] = 45, [3] = 20 })[slotData.rarity] or 100
	local chance = math.random(0, maximum)
	if chance <= 5 then
		slotData.rarity = 5
	elseif chance <= 20 then
		slotData.rarity = 4
	elseif chance <= 45 then
		slotData.rarity = 3
	else
		-- For rarity 2 (maximum = 45) this branch is intentionally unreachable:
		-- the random range is fully covered above, guaranteeing the grade always
		-- improves. It only applies to rarity 1 (maximum = 70/100).
		slotData.rarity = 2
	end
end

local function sendFailure(player, message)
	player:sendTextMessage(MESSAGE_STATUS_SMALL, "[Task Hunting] " .. message)
	return false
end

local function shouldPersistKillProgress(previousKills, currentKills, completedNow)
	if completedNow then
		return true
	end
	if KILL_SAVE_INTERVAL <= 1 then
		return true
	end
	return math.floor(previousKills / KILL_SAVE_INTERVAL) ~= math.floor(currentKills / KILL_SAVE_INTERVAL)
end

local function handleAction(player, slot, action, wantsUpgrade, raceId)
	if slot < 0 or slot >= SLOT_COUNT then
		return sendFailure(player, "Invalid slot.")
	end

	local data = loadTaskData(player)
	if not data then
		return false
	end
	local slotData = ensureSlotReady(player, data, slot)
	if getSlotLockType(player, slot) then
		return sendFailure(player, "This Task Hunting slot is locked.")
	end

	if action == ACTION_LIST_REROLL then
		if slotData.state ~= STATE_SELECT then
			return sendFailure(player, "This slot cannot be rerolled right now.")
		end
		local now = os.time()
		if now >= (slotData.freeRerollAt or 0) then
			slotData.freeRerollAt = now + FREE_REROLL_SECONDS
		elseif not removeGold(player, getRerollPrice(player)) then
			return sendFailure(player, "You do not have enough gold for a list reroll.")
		end
		slotData.raceList = generateRaceList(player, data, slot)

	elseif action == ACTION_REWARD_REROLL then
		if slotData.state ~= STATE_ACTIVE then
			return sendFailure(player, "There is no active task to upgrade.")
		end
		if slotData.rarity >= 5 then
			return sendFailure(player, "This task already has the highest reward grade.")
		end
		if not removePlayerWildcards(player, WILDCARD_REWARD_REROLL_PRICE) then
			return sendFailure(player, "You do not have enough Prey Wildcards.")
		end
		rerollReward(slotData)

	elseif action == ACTION_SELECT_WILDCARD then
		if slotData.state ~= STATE_SELECT then
			return sendFailure(player, "This slot cannot select a wildcard creature now.")
		end
		local wildcardRaceList = generateWildcardRaceList(data, slot)
		if #wildcardRaceList == 0 then
			return sendFailure(player, "There are no valid wildcard creatures for this slot.")
		end
		if not removePlayerWildcards(player, WILDCARD_SELECT_PRICE) then
			return sendFailure(player, "You do not have enough Prey Wildcards.")
		end
		slotData.state = STATE_WILDCARD
		slotData.wildcard = true
		slotData.raceList = {}

	elseif action == ACTION_SELECT then
		if slotData.state ~= STATE_SELECT and slotData.state ~= STATE_WILDCARD then
			return sendFailure(player, "This slot is not waiting for a creature selection.")
		end
		local availableRaceList = slotData.state == STATE_WILDCARD and generateWildcardRaceList(data, slot) or slotData.raceList
		if not containsRace(availableRaceList, raceId) then
			return sendFailure(player, "That creature is not available for this slot.")
		end
		local entry = CustomBestiary and CustomBestiary.getMonster(raceId)
		if not entry then
			return sendFailure(player, "Unknown creature.")
		end
		local bestiaryKills = getBestiaryKills(player)
		slotData.upgraded = wantsUpgrade and isBestiaryComplete(entry, bestiaryKills)
		slotData.state = STATE_ACTIVE
		slotData.selectedRaceId = raceId
		slotData.currentKills = 0
		slotData.rarity = 1
		slotData.wildcard = false
		slotData.raceList = {}

	elseif action == ACTION_REMOVE then
		if slotData.state ~= STATE_ACTIVE then
			return sendFailure(player, "There is no active task to cancel.")
		end
		if not removeGold(player, getRerollPrice(player)) then
			return sendFailure(player, "You do not have enough gold to cancel this task.")
		end
		resetSlot(player, data, slot)

	elseif action == ACTION_COLLECT then
		if slotData.state ~= STATE_REDEEM then
			return sendFailure(player, "This task reward is not ready yet.")
		end
		local entry = CustomBestiary and CustomBestiary.getMonster(slotData.selectedRaceId)
		local option = getRewardOption(entry, slotData.rarity)
		if not option then
			return sendFailure(player, "Task reward data is unavailable.")
		end
		local reward = slotData.upgraded and option.secondReward or option.firstReward
		player:addTaskHuntingPoints(reward)
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, string.format("[Task Hunting] You received %d Task Hunting Points.", reward))
		resetSlot(player, data, slot)

	else
		return sendFailure(player, "Unknown Task Hunting action.")
	end

	saveSlot(player, slot)
	TaskHunting.sendSlotData(player, slot)
	sendBalances(player)
	return true
end

local taskHuntingActionHandler = PacketHandler(OPCODE_BASE_DATA)
function taskHuntingActionHandler.onReceive(player, msg)
	if not supportsAstra(player) or (msg:len() - msg:tell()) < 5 then
		return
	end

	local slot = msg:getByte()
	local action = msg:getByte()
	local wantsUpgrade = msg:getByte() ~= 0
	local raceId = msg:getU16()
	handleAction(player, slot, action, wantsUpgrade, raceId)
end
taskHuntingActionHandler:register()

function TaskHunting.onKill(player, raceId)
	if not supportsAstra(player) or raceId <= 0 then
		return false
	end

	local data = loadTaskData(player)
	if not data then
		return false
	end

	for slot = 0, SLOT_COUNT - 1 do
		local slotData = data.slots[slot]
		if slotData.state == STATE_ACTIVE and slotData.selectedRaceId == raceId then
			local entry = CustomBestiary and CustomBestiary.getMonster(raceId)
			local option = getRewardOption(entry, slotData.rarity)
			if not option then
				return false
			end
			local requiredKills = slotData.upgraded and option.secondKills or option.firstKills
			local previousKills = slotData.currentKills or 0
			slotData.currentKills = math.min(requiredKills, previousKills + 1)
			local completedNow = previousKills < requiredKills and slotData.currentKills >= requiredKills
			if completedNow then
				slotData.state = STATE_REDEEM
				player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "Your Hunting Task is complete. Claim its reward in the Hunting Task window.")
			end
			if shouldPersistKillProgress(previousKills, slotData.currentKills, completedNow) then
				saveSlot(player, slot)
			end
			TaskHunting.sendSlotData(player, slot)
			return true
		end
	end

	return false
end

function TaskHunting.onLogin(player)
	if not supportsAstra(player) then
		return true
	end
	TaskHunting.sendFullSync(player)
	return true
end

function TaskHunting.onLogout(player)
	if taskCache[player:getId()] then
		saveAll(player)
		taskCache[player:getId()] = nil
	end
	return true
end

TaskHunting.sendFullTaskHuntingSync = TaskHunting.sendFullSync
TaskHunting.sendTaskHuntingBasicData = TaskHunting.sendBasicData
TaskHunting.sendTaskHuntingSlotData = TaskHunting.sendSlotData

_TASK_HUNTING_MODULE = TaskHunting
return TaskHunting
