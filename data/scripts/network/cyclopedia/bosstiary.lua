if not configManager.getBoolean(configKeys.BESTIARY_SYSTEM_ENABLED) or not CustomBosstiary then
	return
end

local OPCODE_BOSSTIARY_DATA = 0x61
local OPCODE_BOSSTIARY_SLOTS = 0x62
local OPCODE_BOSSTIARY_WINDOW = 0x73
local OPCODE_BOSSTIARY_OPEN = 0xAE
local OPCODE_BOSSTIARY_OPEN_SLOTS = 0xAF
local OPCODE_BOSSTIARY_SLOT_ACTION = 0xB0
local OPCODE_BOSSTIARY_TRACKER = 0x2A
local MAX_TRACKER_SLOTS = 5
local SLOT_TWO_POINTS = 1500

local function supportsCustomNetwork(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
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

local function ensurePlayerRow(playerGuid)
	CustomBosstiary.ensureTables()
	db.query("INSERT IGNORE INTO `player_bosstiary` (`player_id`) VALUES (" .. playerGuid .. ")")
end

local function loadState(playerGuid)
	ensurePlayerRow(playerGuid)
	local state = {points = 0, slotOne = 0, slotTwo = 0, removeTimes = 0}
	local resultId = db.storeQuery("SELECT `points`, `slot_one`, `slot_two`, `remove_times` FROM `player_bosstiary` WHERE `player_id` = " .. playerGuid)
	if resultId ~= false then
		state.points = result.getDataInt(resultId, "points")
		state.slotOne = result.getDataInt(resultId, "slot_one")
		state.slotTwo = result.getDataInt(resultId, "slot_two")
		state.removeTimes = result.getDataInt(resultId, "remove_times")
		result.free(resultId)
	end
	return state
end

local function loadKills(playerGuid)
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

local function loadTracker(playerGuid)
	local tracker = {}
	local resultId = db.storeQuery("SELECT `bossid` FROM `player_bosstiary_tracker` WHERE `player_id` = " ..
		playerGuid .. " ORDER BY `slot` ASC, `bossid` ASC")
	if resultId ~= false then
		repeat
			tracker[#tracker + 1] = result.getDataInt(resultId, "bossid")
		until not result.next(resultId)
		result.free(resultId)
	end
	return tracker
end

local function getFinishedBosses(kills)
	local bosses = {}
	for raceId, entry in pairs(CustomBosstiary.monstersByRaceId) do
		if CustomBosstiary.getProgress(entry, kills[raceId] or 0) >= 3 then
			bosses[#bosses + 1] = raceId
		end
	end
	table.sort(bosses)
	return bosses
end

local function getBoostedBoss()
	return CustomBosstiary.getBoostedBoss()
end

local function contains(list, value)
	for _, entry in ipairs(list or {}) do
		if entry == value then
			return true
		end
	end
	return false
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

local function writeOptionalCreatureInfo(out, entry)
	if not entry then
		out:addByte(0)
		return
	end

	out:addByte(1)
	writeCreatureInfo(out, entry)
end

local function sendBaseData(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_BOSSTIARY_DATA)
	for category = 1, 3 do
		for _, value in ipairs(CustomBosstiary.getThresholds(category)) do
			out:addU16(value)
		end
	end
	for category = 1, 3 do
		for _, value in ipairs(CustomBosstiary.getRewards(category)) do
			out:addU16(value)
		end
	end
	return out:sendToPlayer(player)
end

local function sendWindow(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	sendBaseData(player)
	local playerGuid = player:getGuid()
	local kills = loadKills(playerGuid)
	local tracker = loadTracker(playerGuid)
	local entries = {}
	for _, entry in pairs(CustomBosstiary.monstersByRaceId) do
		entries[#entries + 1] = entry
	end
	table.sort(entries, function(a, b) return a.raceId < b.raceId end)

	local out = NetworkMessage(player)
	out:addByte(OPCODE_BOSSTIARY_WINDOW)
	out:addU16(math.min(#entries, 0xFFFF))
	for i = 1, math.min(#entries, 0xFFFF) do
		local entry = entries[i]
		out:addU32(entry.raceId)
		out:addByte(entry.category - 1)
		out:addU32(clamp(kills[entry.raceId] or 0, 0, 0xFFFFFFFF))
		out:addByte(0)
		out:addByte(contains(tracker, entry.raceId) and 1 or 0)
		writeCreatureInfo(out, entry)
	end
	return out:sendToPlayer(player)
end

local function getLootBonus(points)
	return math.min(60, math.floor(math.max(0, tonumber(points) or 0) / 50))
end

local function getNextLootBonusPoints(points)
	return (getLootBonus(points) + 1) * 50
end

local function getRemovePrice(removeTimes)
	return math.min(100000000, 100000 * (2 ^ math.min(10, math.max(0, tonumber(removeTimes) or 0))))
end

local function sendSlotBytes(out, entry, kills, lootBonus, killBonus, inactive, removePrice)
	out:addByte(entry.category - 1)
	out:addU32(clamp(kills, 0, 0xFFFFFFFF))
	out:addU16(clamp(lootBonus, 0, 0xFFFF))
	out:addByte(clamp(killBonus, 0, 0xFF))
	out:addByte(entry.category - 1)
	out:addU32(inactive and 0 or clamp(removePrice, 0, 0xFFFFFFFF))
	out:addByte(inactive and 1 or 0)
end

local function sendSlots(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	sendBaseData(player)
	local playerGuid = player:getGuid()
	local state = loadState(playerGuid)
	local kills = loadKills(playerGuid)
	local finished = getFinishedBosses(kills)
	local currentBonus = getLootBonus(state.points)
	local removePrice = getRemovePrice(state.removeTimes)
	local out = NetworkMessage(player)
	out:addByte(OPCODE_BOSSTIARY_SLOTS)
	out:addU32(state.points)
	out:addU32(getNextLootBonusPoints(state.points))
	out:addU16(currentBonus)
	out:addU16(currentBonus + 1)

	local slotOneEntry = CustomBosstiary.getMonster(state.slotOne)
	if state.slotOne > 0 and not slotOneEntry then
		state.slotOne = 0
		db.query("UPDATE `player_bosstiary` SET `slot_one` = 0 WHERE `player_id` = " .. playerGuid)
	end
	local slotOneUnlocked = #finished > 0
	out:addByte(slotOneUnlocked and 1 or 0)
	out:addU32(slotOneUnlocked and state.slotOne or 0)
	if slotOneUnlocked and state.slotOne > 0 then
		local bonus = currentBonus + (CustomBosstiary.getProgress(slotOneEntry, kills[state.slotOne] or 0) >= 3 and 25 or 0)
		sendSlotBytes(out, slotOneEntry, kills[state.slotOne] or 0, bonus, 0, false, removePrice)
	end
	writeOptionalCreatureInfo(out, slotOneUnlocked and state.slotOne > 0 and slotOneEntry or nil)

	local slotTwoEntry = CustomBosstiary.getMonster(state.slotTwo)
	if state.slotTwo > 0 and not slotTwoEntry then
		state.slotTwo = 0
		db.query("UPDATE `player_bosstiary` SET `slot_two` = 0 WHERE `player_id` = " .. playerGuid)
	end
	local slotTwoUnlocked = state.points >= SLOT_TWO_POINTS
	out:addByte(slotTwoUnlocked and 1 or 0)
	out:addU32(slotTwoUnlocked and state.slotTwo or SLOT_TWO_POINTS)
	if slotTwoUnlocked and state.slotTwo > 0 then
		local bonus = currentBonus + (CustomBosstiary.getProgress(slotTwoEntry, kills[state.slotTwo] or 0) >= 3 and 25 or 0)
		sendSlotBytes(out, slotTwoEntry, kills[state.slotTwo] or 0, bonus, 0, false, removePrice)
	end
	writeOptionalCreatureInfo(out, slotTwoUnlocked and state.slotTwo > 0 and slotTwoEntry or nil)

	local boostedBoss = getBoostedBoss()
	out:addByte(boostedBoss and 1 or 0)
	out:addU32(boostedBoss and boostedBoss.raceId or 0)
	if boostedBoss then
		local boostedLootBonus = currentBonus + CustomBosstiary.getBoostedBossLootBonus()
		local boostedKillBonus = CustomBosstiary.getBoostedBossKillBonus()
		sendSlotBytes(out, boostedBoss, kills[boostedBoss.raceId] or 0, boostedLootBonus, boostedKillBonus, false, 0)
	end
	writeOptionalCreatureInfo(out, boostedBoss)

	local selectable = {}
	for _, raceId in ipairs(finished) do
		if raceId ~= state.slotOne and raceId ~= state.slotTwo then
			selectable[#selectable + 1] = raceId
		end
	end
	out:addByte(#selectable > 0 and 1 or 0)
	if #selectable > 0 then
		out:addU16(math.min(#selectable, 0xFFFF))
		for i = 1, math.min(#selectable, 0xFFFF) do
			local entry = CustomBosstiary.getMonster(selectable[i])
			out:addU32(selectable[i])
			out:addByte(entry and (entry.category - 1) or 0)
		end
	end

	return out:sendToPlayer(player)
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

local function handleSlotAction(player, slot, raceId)
	if slot ~= 1 and slot ~= 2 then
		return
	end

	local playerGuid = player:getGuid()
	local state = loadState(playerGuid)
	local kills = loadKills(playerGuid)
	local finished = getFinishedBosses(kills)
	if slot == 1 and #finished == 0 then
		return
	elseif slot == 2 and state.points < SLOT_TWO_POINTS then
		return
	end

	if raceId > 0 and (not CustomBosstiary.getMonster(raceId) or not contains(finished, raceId)) then
		return
	end
	if raceId > 0 and ((slot == 1 and raceId == state.slotTwo) or (slot == 2 and raceId == state.slotOne)) then
		return
	end

	local oldRaceId = slot == 1 and state.slotOne or state.slotTwo
	if oldRaceId > 0 and oldRaceId ~= raceId then
		local price = getRemovePrice(state.removeTimes)
		if not removePlayerGold(player, price) then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have enough gold to remove this boss.")
			return
		end
		db.query("UPDATE `player_bosstiary` SET `remove_times` = `remove_times` + 1 WHERE `player_id` = " .. playerGuid)
	end

	local column = slot == 1 and "slot_one" or "slot_two"
	db.query("UPDATE `player_bosstiary` SET `" .. column .. "` = " .. raceId .. " WHERE `player_id` = " .. playerGuid)
	sendSlots(player)
end

local function toggleTracker(player, raceId, enabled)
	if not CustomBosstiary.getMonster(raceId) then
		return
	end

	local playerGuid = player:getGuid()
	local tracker = loadTracker(playerGuid)
	if enabled then
		if contains(tracker, raceId) or #tracker >= MAX_TRACKER_SLOTS then
			return
		end
		db.query("INSERT INTO `player_bosstiary_tracker` (`player_id`, `bossid`, `slot`) VALUES (" ..
			playerGuid .. ", " .. raceId .. ", " .. (#tracker + 1) .. ") ON DUPLICATE KEY UPDATE `slot` = VALUES(`slot`)")
	else
		db.query("DELETE FROM `player_bosstiary_tracker` WHERE `player_id` = " .. playerGuid .. " AND `bossid` = " .. raceId)
	end
	sendWindow(player)
end

local openHandler = PacketHandler(OPCODE_BOSSTIARY_OPEN)
function openHandler.onReceive(player, msg)
	sendWindow(player)
end
openHandler:register()

local slotsHandler = PacketHandler(OPCODE_BOSSTIARY_OPEN_SLOTS)
function slotsHandler.onReceive(player, msg)
	sendSlots(player)
end
slotsHandler:register()

local slotActionHandler = PacketHandler(OPCODE_BOSSTIARY_SLOT_ACTION)
function slotActionHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 5 then
		return
	end
	handleSlotAction(player, msg:getByte(), msg:getU32())
end
slotActionHandler:register()

local trackerHandler = PacketHandler(OPCODE_BOSSTIARY_TRACKER)
function trackerHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 5 then
		return
	end
	toggleTracker(player, msg:getU32(), msg:getByte() == 1)
end
trackerHandler:register()

CustomBosstiary.sendWindow = sendWindow
CustomBosstiary.sendSlots = sendSlots
