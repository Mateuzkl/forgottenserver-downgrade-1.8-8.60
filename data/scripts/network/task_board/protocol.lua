-- Task Board Protocol: byte-level serialization following Crystal Server/AstraClient wire format.
-- Opcodes: 0x53 (server->client, subtype byte), 0x5F (client->server, action byte),
--           0xEE (resource balance). Task Hunting owns 0xBA/0xBB.
-- All send functions use NetworkMessage. All receive handlers use PacketHandler + NetworkGuard.

local TaskBoardProtocol = {}

-- Resource type constants matching Crystal ServerDefinitions.hpp
local RESOURCE_TASK_HUNTING = TaskBoard.Resources.TASK_HUNTING
local RESOURCE_BOUNTY_POINTS = TaskBoard.Resources.BOUNTY_POINTS
local RESOURCE_SOULSEALS_POINTS = TaskBoard.Resources.SOULSEALS_POINTS
-- AstraClient modules/gamelib/const.lua: ResourceTypes.BOUNTY_REROLL_POINTS = 95.
-- This value intentionally equals the C2S Task Board opcode byte, but it is
-- carried inside the unrelated S2C 0xEE resource-balance packet.
local RESOURCE_BOUNTY_REROLL_POINTS_RAW_VALUE = 0x5F

TaskBoardProtocol.RESOURCE_TASK_HUNTING = RESOURCE_TASK_HUNTING
TaskBoardProtocol.RESOURCE_BOUNTY_POINTS = RESOURCE_BOUNTY_POINTS
TaskBoardProtocol.RESOURCE_SOULSEALS_POINTS = RESOURCE_SOULSEALS_POINTS
TaskBoardProtocol.RESOURCE_BOUNTY_REROLL_POINTS = RESOURCE_BOUNTY_REROLL_POINTS_RAW_VALUE

-- Task board option sub-types (opcode 0x53 first byte after opcode)
local TASK_BOARD_BOUNTY = 0x00
local TASK_BOARD_WEEKLY = 0x01
local TASK_BOARD_HUNT_SHOP = 0x02
local TASK_BOARD_SOUL_SEALS = 0x03
local TASK_BOARD_BOUNTY_KILL_UPDATE = 0x04
local TASK_BOARD_WEEKLY_KILL_UPDATE = 0x05

-- Client action options (opcode 0x5F first byte)
local ACTION_OPEN_BOUNTY = 0
local ACTION_OPEN_WEEKLY = 1
local ACTION_CHANGE_DIFFICULTY = 2
local ACTION_REROLL_TASKS = 3
local ACTION_CLAIM_DAILY = 4
local ACTION_SELECT_TASK = 5
local ACTION_CLAIM_REWARD = 6
local ACTION_TALISMAN_UPGRADE = 7
local ACTION_WEEKLY_DELIVER = 8
local ACTION_WEEKLY_SELECT_DIFFICULTY = 9
local ACTION_OPEN_HUNT_SHOP = 10
local ACTION_BUY_SHOP_OFFER = 11
local ACTION_UNLOCK_PREFERRED = 12
local ACTION_CLEAR_PREFERRED = 13
local ACTION_CLEAR_UNWANTED = 14
local ACTION_ASSIGN_PREFERRED = 15
local ACTION_ASSIGN_UNWANTED = 16
local ACTION_OPEN_SOULSEAL = 17
local ACTION_OPEN_PREFERRED = 18
local ACTION_SOULSEAL_FIGHT = 19

TaskBoardProtocol.ACTION_OPEN_BOUNTY = ACTION_OPEN_BOUNTY
TaskBoardProtocol.ACTION_OPEN_WEEKLY = ACTION_OPEN_WEEKLY
TaskBoardProtocol.ACTION_CHANGE_DIFFICULTY = ACTION_CHANGE_DIFFICULTY
TaskBoardProtocol.ACTION_REROLL_TASKS = ACTION_REROLL_TASKS
TaskBoardProtocol.ACTION_CLAIM_DAILY = ACTION_CLAIM_DAILY
TaskBoardProtocol.ACTION_SELECT_TASK = ACTION_SELECT_TASK
TaskBoardProtocol.ACTION_CLAIM_REWARD = ACTION_CLAIM_REWARD
TaskBoardProtocol.ACTION_TALISMAN_UPGRADE = ACTION_TALISMAN_UPGRADE
TaskBoardProtocol.ACTION_WEEKLY_DELIVER = ACTION_WEEKLY_DELIVER
TaskBoardProtocol.ACTION_WEEKLY_SELECT_DIFFICULTY = ACTION_WEEKLY_SELECT_DIFFICULTY
TaskBoardProtocol.ACTION_OPEN_HUNT_SHOP = ACTION_OPEN_HUNT_SHOP
TaskBoardProtocol.ACTION_BUY_SHOP_OFFER = ACTION_BUY_SHOP_OFFER
TaskBoardProtocol.ACTION_UNLOCK_PREFERRED = ACTION_UNLOCK_PREFERRED
TaskBoardProtocol.ACTION_CLEAR_PREFERRED = ACTION_CLEAR_PREFERRED
TaskBoardProtocol.ACTION_CLEAR_UNWANTED = ACTION_CLEAR_UNWANTED
TaskBoardProtocol.ACTION_ASSIGN_PREFERRED = ACTION_ASSIGN_PREFERRED
TaskBoardProtocol.ACTION_ASSIGN_UNWANTED = ACTION_ASSIGN_UNWANTED
TaskBoardProtocol.ACTION_OPEN_SOULSEAL = ACTION_OPEN_SOULSEAL
TaskBoardProtocol.ACTION_OPEN_PREFERRED = ACTION_OPEN_PREFERRED
TaskBoardProtocol.ACTION_SOULSEAL_FIGHT = ACTION_SOULSEAL_FIGHT

TaskBoardProtocol.TASK_BOARD_BOUNTY = TASK_BOARD_BOUNTY
TaskBoardProtocol.TASK_BOARD_WEEKLY = TASK_BOARD_WEEKLY
TaskBoardProtocol.TASK_BOARD_HUNT_SHOP = TASK_BOARD_HUNT_SHOP
TaskBoardProtocol.TASK_BOARD_SOUL_SEALS = TASK_BOARD_SOUL_SEALS
TaskBoardProtocol.TASK_BOARD_BOUNTY_KILL_UPDATE = TASK_BOARD_BOUNTY_KILL_UPDATE
TaskBoardProtocol.TASK_BOARD_WEEKLY_KILL_UPDATE = TASK_BOARD_WEEKLY_KILL_UPDATE

local OPCODE_TASK_BOARD_SEND = 0x53
local OPCODE_RESOURCE_BALANCE = 0xEE
local BOUNTY_EXTENSION_MARKER = 0x5441534B424F4152

local function supportsCustomNetwork(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function clamp(value, minValue, maxValue)
	value = tonumber(value) or minValue
	if value < minValue then return minValue end
	if value > maxValue then return maxValue end
	return value
end

local function normalizeOutfit(outfit)
	outfit = type(outfit) == "table" and outfit or {}
	return {
		type = clamp(outfit.type or outfit.lookType or 0, 0, 0xFFFF),
		head = clamp(outfit.head or outfit.lookHead or 0, 0, 0xFF),
		body = clamp(outfit.body or outfit.lookBody or 0, 0, 0xFF),
		legs = clamp(outfit.legs or outfit.lookLegs or 0, 0, 0xFF),
		feet = clamp(outfit.feet or outfit.lookFeet or 0, 0, 0xFF),
		addons = clamp(outfit.addons or outfit.lookAddons or 0, 0, 0xFF),
	}
end

local function getMonsterDisplay(raceId, fallbackName, fallbackOutfit)
	raceId = tonumber(raceId) or 0
	local entry = nil
	if raceId > 0 and CustomBestiary and CustomBestiary.getMonster then
		entry = CustomBestiary.getMonster(raceId)
	end

	return {
		name = (entry and entry.name) or fallbackName or (raceId > 0 and ("Creature " .. tostring(raceId)) or ""),
		outfit = normalizeOutfit((entry and entry.outfit) or fallbackOutfit),
	}
end

local function writeMonsterDisplay(out, raceId, fallbackName, fallbackOutfit)
	local display = getMonsterDisplay(raceId, fallbackName, fallbackOutfit)
	local outfit = display.outfit or {}
	out:addString(display.name or "")
	out:addU16(clamp(outfit.type or 0, 0, 0xFFFF))
	out:addByte(clamp(outfit.head or 0, 0, 0xFF))
	out:addByte(clamp(outfit.body or 0, 0, 0xFF))
	out:addByte(clamp(outfit.legs or 0, 0, 0xFF))
	out:addByte(clamp(outfit.feet or 0, 0, 0xFF))
	out:addByte(clamp(outfit.addons or 0, 0, 0xFF))
end

local function getClientItemId(itemId)
	itemId = tonumber(itemId) or 0
	if itemId <= 0 then
		return 0
	end

	local itemType = ItemType(itemId)
	local clientId = itemType and itemType:getClientId() or 0
	if clientId and clientId > 0 then
		return clientId
	end
	return itemId
end

-- ============================================
-- RESOURCE BALANCE (opcode 0xEE)
-- ============================================

function TaskBoardProtocol.sendResourceBalance(player, resourceType, amount)
	return TaskBoard.sendResourceBalance(player, resourceType, amount)
end

local function sendKillUpdate(player, subType, raceId, currentKills, totalKills, isCompleted)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_TASK_BOARD_SEND)
	out:addByte(subType)
	out:addU16(clamp(raceId or 0, 0, 0xFFFF))
	out:addU16(clamp(currentKills or 0, 0, 0xFFFF))
	out:addU16(clamp(totalKills or 0, 0, 0xFFFF))
	out:addByte(isCompleted and 1 or 0)
	return out:sendToPlayer(player)
end

function TaskBoardProtocol.sendBountyKillUpdate(player, raceId, currentKills, totalKills, isCompleted)
	return sendKillUpdate(player, TASK_BOARD_BOUNTY_KILL_UPDATE, raceId, currentKills, totalKills, isCompleted)
end

function TaskBoardProtocol.sendWeeklyKillUpdate(player, raceId, currentKills, totalKills, isCompleted)
	return sendKillUpdate(player, TASK_BOARD_WEEKLY_KILL_UPDATE, raceId, currentKills, totalKills, isCompleted)
end

-- ============================================
-- BOUNTY TASK DATA (opcode 0x53, subType 0x00)
-- ============================================

function TaskBoardProtocol.sendBountyTaskData(player, data)
	if not supportsCustomNetwork(player) then
		return false
	end

	TaskBoard.sendAll(player)

	local out = NetworkMessage(player)
	out:addByte(OPCODE_TASK_BOARD_SEND)
	out:addByte(TASK_BOARD_BOUNTY)

	-- state, difficulty
	out:addByte(clamp(data.state or 0, 0, 0xFF))
	out:addByte(clamp(data.difficulty or 0, 0, 0xFF))

	-- 3 creature entries
	local creatures = data.creatures or {}
	for i = 1, 3 do
		local c = creatures[i] or {}
		local raceId = clamp(c.raceId or 0, 0, 0xFFFF)
		out:addU16(raceId)
		writeMonsterDisplay(out, raceId, c.name, c.outfit)
		out:addU16(clamp(c.kills or 0, 0, 0xFFFF))
		out:addU16(clamp(c.required or 0, 0, 0xFFFF))
		out:addU16(clamp(c.reward or 0, 0, 0xFFFF))
		out:addU16(clamp(c.bountyPts or 0, 0, 0xFFFF))
		out:addByte(clamp(c.grade or 0, 0, 0xFF))
		out:addByte(clamp(c.claimState or 0, 0, 0xFF))
		out:addByte(clamp(c.index or i - 1, 0, 0xFF))
	end

	-- reroll info
	out:addByte(clamp(data.rerollTokens or 0, 0, 0xFF))
	out:addByte(clamp(data.rerollMode or 0, 0, 0xFF))
	out:addU32(clamp(data.rerollTimestamp or 0, 0, 0xFFFFFFFF))
	out:addByte(clamp(data.upgrade or 0, 0, 0xFF))

	-- 4 talisman paths
	local talismans = data.talismans or {}
	for i = 1, 4 do
		local t = talismans[i] or {}
		out:addByte(clamp(t.tier1 or 0, 0, 0xFF))
		out:addByte(clamp(t.tier2 or 0, 0, 0xFF))
		out:addByte(clamp(t.upgrade or 0, 0, 0xFF))
		out:addU16(clamp(t.ptsToUpgrade or 0, 0, 0xFFFF))
	end

	-- preferred lists (5 slots)
	local preferred = data.preferred or {}
	out:addByte(clamp(data.preferredSlots or 0, 0, 0xFF))
	for i = 1, 5 do
		local p = preferred[i] or {}
		out:addByte(p.active and 1 or 0)
		out:addU16(clamp(p.preferredRaceId or 0, 0, 0xFFFF))
		out:addU16(clamp(p.unwantedRaceId or 0, 0, 0xFFFF))
	end

	-- Optional, marked extension used by the Preferred List UI.
	if data.availableRaceIds ~= nil then
		local availableRaceIds = data.availableRaceIds
		local availableCount = math.min(#availableRaceIds, 0xFFFF)
		out:addU64(BOUNTY_EXTENSION_MARKER)
		out:addU16(clamp(data.preferredClearCost or 0, 0, 0xFFFF))
		out:addU16(availableCount)
		for i = 1, availableCount do
			local raceId = clamp(availableRaceIds[i], 0, 0xFFFF)
			out:addU16(raceId)
			writeMonsterDisplay(out, raceId)
		end
	end

	return out:sendToPlayer(player)
end

-- ============================================
-- WEEKLY TASK DATA (opcode 0x53, subType 0x01)
-- ============================================

function TaskBoardProtocol.sendWeeklyTaskData(player, data)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_TASK_BOARD_SEND)
	out:addByte(TASK_BOARD_WEEKLY)

	-- any creature counters
	out:addU16(clamp(data.anyCreatureKills or 0, 0, 0xFFFF))
	out:addU16(clamp(data.anyCreatureTotal or 0, 0, 0xFFFF))

	-- kill tasks
	local killTasks = data.killTasks or {}
	out:addByte(clamp(#killTasks, 0, 0xFF))
	for _, kt in ipairs(killTasks) do
		local raceId = clamp(kt.raceId or 0, 0, 0xFFFF)
		out:addU16(raceId)
		writeMonsterDisplay(out, raceId, kt.name, kt.outfit)
		out:addU16(clamp(kt.kills or 0, 0, 0xFFFF))
		out:addU16(clamp(kt.required or 0, 0, 0xFFFF))
		out:addByte(clamp(kt.grade or 0, 0, 0xFF))
	end

	-- delivery tasks
	local deliveryTasks = data.deliveryTasks or {}
	out:addByte(clamp(#deliveryTasks, 0, 0xFF))
	for _, dt in ipairs(deliveryTasks) do
		out:addU16(clamp(dt.itemId or 0, 0, 0xFFFF))
		out:addU16(clamp(dt.clientId or getClientItemId(dt.itemId), 0, 0xFFFF))
		out:addByte(clamp(dt.amount or 0, 0, 0xFF))
		out:addByte(clamp(dt.required or 0, 0, 0xFF))
		out:addU32(clamp(dt.available or 0, 0, 0xFFFFFFFF))
		out:addByte(clamp(dt.grade or 0, 0, 0xFF))
	end

	-- difficulty / reward data
	out:addByte(clamp(data.difficulty or 0, 0, 0xFF))
	out:addU32(clamp(data.killExp or 0, 0, 0xFFFFFFFF))
	out:addU32(clamp(data.deliveryExp or 0, 0, 0xFFFFFFFF))
	out:addByte(clamp(data.completedKills or 0, 0, 0xFF))
	out:addByte(clamp(data.completedDeliveries or 0, 0, 0xFF))
	out:addByte(clamp(data.weeklyProgress or 0, 0, 0xFF))
	out:addU32(clamp(data.huntingPts or 0, 0, 0xFFFFFFFF))
	out:addU32(clamp(data.soulseals or 0, 0, 0xFFFFFFFF))
	out:addU32(clamp(data.soulsealsBalance or 0, 0, 0xFFFFFFFF))
	out:addByte(data.needsReward and 1 or 0)
	out:addByte(data.hasExpansion and 1 or 0)

	return out:sendToPlayer(player)
end

-- ============================================
-- HUNTING SHOP DATA (opcode 0x53, subType 0x02)
-- ============================================

function TaskBoardProtocol.sendHuntingTaskShopData(player, offers, taskHuntingPoints)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_TASK_BOARD_SEND)
	out:addByte(TASK_BOARD_HUNT_SHOP)

	out:addU32(clamp(taskHuntingPoints or 0, 0, 0xFFFFFFFF))

	local offerCount = #offers
	out:addByte(clamp(offerCount, 0, 0xFF))

	for _, offer in ipairs(offers) do
		out:addByte(clamp(offer.id or 0, 0, 0xFF))
		out:addString(offer.name or "")
		out:addU16(clamp(offer.count or 0, 0, 0xFFFF))
		out:addU32(clamp(offer.price or 0, 0, 0xFFFFFFFF))
		out:addByte(offer.purchased and 1 or 0)
		out:addByte(clamp(offer.type or 0, 0, 0xFF))

		-- type-specific fields
		if offer.type == 0 then -- item
			out:addU16(clamp(offer.clientId or getClientItemId(offer.itemId), 0, 0xFFFF))
		elseif offer.type == 1 then -- mount
			out:addU16(clamp(offer.mountClientId or offer.mountId or 0, 0, 0xFFFF))
		elseif offer.type == 2 then -- outfit
			out:addU16(clamp(offer.outfitId or 0, 0, 0xFFFF))
			out:addU16(clamp(offer.addons or 0, 0, 0xFFFF))
		elseif offer.type == 3 then -- item double
			out:addU16(clamp(offer.clientId or getClientItemId(offer.itemId), 0, 0xFFFF))
		elseif offer.type == 5 then -- weekly expansion
			out:addByte(0) -- placeholder
		end
		-- type 4 (bonus promotion) has no extra fields
	end

	return out:sendToPlayer(player)
end

-- ============================================
-- SOUL SEALS DATA (opcode 0x53, subType 0x03)
-- ============================================

function TaskBoardProtocol.sendSoulSealsData(player, entries, balance)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_TASK_BOARD_SEND)
	out:addByte(TASK_BOARD_SOUL_SEALS)

	out:addU32(clamp(balance or 0, 0, 0xFFFFFFFF))

	local count = #(entries or {})
	out:addU16(clamp(count, 0, 0xFFFF))

	for _, entry in ipairs(entries or {}) do
		local raceId = clamp(entry.raceId or 0, 0, 0xFFFF)
		out:addU16(raceId)
		writeMonsterDisplay(out, raceId, entry.name or "?", entry.outfit)
		out:addByte(clamp(entry.stars or 0, 0, 0xFF))
		out:addU32(clamp(entry.cost or 0, 0, 0xFFFFFFFF))
		out:addByte(entry.mastered and 1 or 0)
	end

	return out:sendToPlayer(player)
end

-- ============================================
-- PARSE CLIENT ACTION (opcode 0x5F)
-- ============================================

function TaskBoardProtocol.parseTaskBoardAction(msg)
	-- Returns: option, payload table
	-- Returns nil if invalid
	if (msg:len() - msg:tell()) < 1 then
		return nil
	end

	local option = NetworkGuard.readByte(msg)
	if option == nil then
		return nil
	end

	local payload = { option = option }

	if option == ACTION_CHANGE_DIFFICULTY then
		-- payload: difficulty U8
		if (msg:len() - msg:tell()) < 1 then return nil end
		payload.difficulty = NetworkGuard.readByte(msg)
		if payload.difficulty == nil then return nil end

	elseif option == ACTION_SELECT_TASK then
		-- payload: taskIndex U8
		if (msg:len() - msg:tell()) < 1 then return nil end
		payload.taskIndex = NetworkGuard.readByte(msg)
		if payload.taskIndex == nil then return nil end

	elseif option == ACTION_TALISMAN_UPGRADE then
		-- payload: pathIndex U8
		if (msg:len() - msg:tell()) < 1 then return nil end
		payload.pathIndex = NetworkGuard.readByte(msg)
		if payload.pathIndex == nil then return nil end

	elseif option == ACTION_WEEKLY_DELIVER then
		-- payload: taskIndex U8
		if (msg:len() - msg:tell()) < 1 then return nil end
		payload.taskIndex = NetworkGuard.readByte(msg)
		if payload.taskIndex == nil then return nil end

	elseif option == ACTION_WEEKLY_SELECT_DIFFICULTY then
		-- payload: difficulty U8
		if (msg:len() - msg:tell()) < 1 then return nil end
		payload.difficulty = NetworkGuard.readByte(msg)
		if payload.difficulty == nil then return nil end

	elseif option == ACTION_BUY_SHOP_OFFER then
		-- payload: offerIndex U8
		if (msg:len() - msg:tell()) < 1 then return nil end
		payload.offerIndex = NetworkGuard.readByte(msg)
		if payload.offerIndex == nil then return nil end

	elseif option == ACTION_UNLOCK_PREFERRED then
		-- payload: slot U16
		if (msg:len() - msg:tell()) < 2 then return nil end
		payload.slot = NetworkGuard.readU16(msg)
		if payload.slot == nil then return nil end

	elseif option == ACTION_CLEAR_PREFERRED or option == ACTION_CLEAR_UNWANTED then
		-- payload: slot U16
		if (msg:len() - msg:tell()) < 2 then return nil end
		payload.slot = NetworkGuard.readU16(msg)
		if payload.slot == nil then return nil end

	elseif option == ACTION_ASSIGN_PREFERRED or option == ACTION_ASSIGN_UNWANTED then
		-- payload: slot U16, raceId U16
		if (msg:len() - msg:tell()) < 4 then return nil end
		payload.slot = NetworkGuard.readU16(msg)
		payload.raceId = NetworkGuard.readU16(msg)
		if payload.slot == nil or payload.raceId == nil then return nil end

	elseif option == ACTION_SOULSEAL_FIGHT then
		-- payload: raceId U16
		if (msg:len() - msg:tell()) < 2 then return nil end
		payload.raceId = NetworkGuard.readU16(msg)
		if payload.raceId == nil then return nil end
	end

	return payload
end

return TaskBoardProtocol
