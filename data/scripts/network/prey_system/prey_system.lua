--[[
    Developed by: Mateus Roberto (mateuskl)
    Date: 06/05/2026
    Version: v1.0
]]

-- data/scripts/network/prey_system/prey_system.lua

if not configManager.getBoolean(configKeys.PREY_SYSTEM_ENABLED) then
	PreySystem = nil
	return
end

dofile("data/scripts/network/prey_system/prey_monsters.lua")

local PREY_OPCODE_OPEN = 0xE8
local PREY_OPCODE_SELECT = 0xE9
local PREY_OPCODE_LIST_REROLL = 0xEA
local PREY_OPCODE_BONUS_REROLL = 0xEB
local PREY_OPCODE_CLEAR = 0xEC
local PREY_OPCODE_TOGGLE_AUTO = 0xD8
local PREY_OPCODE_TOGGLE_LOCK = 0xD9
local PREY_OPCODE_RESOURCE_BALANCE = 0xEE
local PREY_NATIVE_OPCODE_TIME_LEFT = 0xE7
local PREY_NATIVE_OPCODE_REQUEST = 0xED
local PREY_NATIVE_OPCODE_ACTION = 0xEB
local PREY_NATIVE_OPCODE_DATA = 0xE8
local PREY_NATIVE_OPCODE_PRICES = 0xE9

local PREY_STATE_EMPTY = 0
local PREY_STATE_LIST_SELECTION = 1
local PREY_STATE_BONUS_SELECTION = 2
local PREY_STATE_ACTIVE = 3
local PREY_STATE_INACTIVE = 4
local PREY_STATE_WILDCARD_SELECTION = 5

local PREY_NATIVE_STATE_LOCKED = 0
local PREY_NATIVE_STATE_INACTIVE = 1
local PREY_NATIVE_STATE_ACTIVE = 2
local PREY_NATIVE_STATE_SELECTION = 3
local PREY_NATIVE_STATE_WILDCARD = 5
local PREY_NATIVE_STATE_WILDCARD_WITH_MONSTERS = 6
local PREY_NATIVE_UNLOCK_STORE = 1
local PREY_NATIVE_BONUS_NONE = 4

local PREY_NATIVE_ACTION_LIST_REROLL = 0
local PREY_NATIVE_ACTION_BONUS_REROLL = 1
local PREY_NATIVE_ACTION_MONSTER_SELECTION = 2
local PREY_NATIVE_ACTION_REQUEST_ALL_MONSTERS = 3
local PREY_NATIVE_ACTION_CHANGE_FROM_ALL = 4
local PREY_NATIVE_ACTION_LOCK_PREY = 5
local PREY_NATIVE_ACTION_UNLOCK_PERMANENT = 6

local PREY_BONUS_NONE = 0
local PREY_BONUS_DMG_BOOST = 1
local PREY_BONUS_DMG_RED = 2
local PREY_BONUS_XP = 3
local PREY_BONUS_LOOT = 4

local PREY_SLOTS = 3
local PREY_DURATION_SECS = 7200
local PREY_REROLL_CD = 20 * 3600
local PREY_MAX_WILDCARDS = 53
local PREY_LIST_REROLL_COST_PER_LEVEL = 150
local PREY_TICK_INTERVAL = 1000
local PREY_STORAGE_AUTO_BONUS_BASE = 780000
local PREY_STORAGE_LOCK_BASE = 780100
local PREY_STORAGE_PERMANENT_SLOT = 780200
local PREY_PERMANENT_SLOT = 2
local PREY_PERMANENT_SLOT_COST = 900
local PREY_AUTO_BONUS_COST = 1
local PREY_LOCK_COST = 5
local RESOURCE_BANK = 0
local RESOURCE_INVENTORY = 1
local RESOURCE_PREY = 10

local preyCache = {}
local preySchemaChecked = false

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function supportsAstraPreyExtension(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function isPreySlotUnlocked(player, slot)
	return slot ~= PREY_PERMANENT_SLOT
		or player:getStorageValue(PREY_STORAGE_PERMANENT_SLOT) == 1
end

local function ensurePreySchema()
	if preySchemaChecked then
		return
	end

	local query = db.storeQuery(
		"SELECT COUNT(*) AS `count` FROM `information_schema`.`COLUMNS`"
		.. " WHERE `TABLE_SCHEMA` = DATABASE()"
		.. " AND `TABLE_NAME` = 'player_prey'"
		.. " AND `COLUMN_NAME` = 'list_reroll_used'"
	)
	local exists = false
	if query ~= false then
		exists = result.getDataInt(query, "count") > 0
		result.free(query)
	end

	if not exists then
		db.query("ALTER TABLE `player_prey` ADD `list_reroll_used` TINYINT(1) NOT NULL DEFAULT 0")
	end

	preySchemaChecked = true
end

PreySystem = PreySystem or {}
PreySystem.BONUS_DAMAGE_BOOST = PREY_BONUS_DMG_BOOST
PreySystem.BONUS_DAMAGE_REDUCTION = PREY_BONUS_DMG_RED
PreySystem.BONUS_XP = PREY_BONUS_XP
PreySystem.BONUS_LOOT = PREY_BONUS_LOOT

local PREY_BONUS_MESSAGES = {
	[PREY_BONUS_DMG_BOOST] = {
		name = "Damage Boost",
		text = "You are dealing %d%% extra damage to %s."
	},
	[PREY_BONUS_DMG_RED] = {
		name = "Damage Reduction",
		text = "You are reducing damage received from %s by %d%%."
	},
	[PREY_BONUS_XP] = {
		name = "Bonus XP",
		text = "You will gain %d%% extra experience when killing %s."
	},
	[PREY_BONUS_LOOT] = {
		name = "Improved Loot",
		text = "You have a %d%% improved loot bonus when killing %s."
	}
}

local function sendActiveBonusMessage(player, slotData)
	if not player or not slotData or (slotData.monster_name or "") == "" then
		return
	end

	local message = PREY_BONUS_MESSAGES[slotData.bonus_type]
	if not message or (slotData.bonus_value or 0) <= 0 then
		return
	end

	local details
	if slotData.bonus_type == PREY_BONUS_DMG_RED then
		details = string.format(message.text, slotData.monster_name, slotData.bonus_value)
	else
		details = string.format(message.text, slotData.bonus_value, slotData.monster_name)
	end
	player:sendTextMessage(MESSAGE_STATUS_DEFAULT, string.format("[Prey] %s active: %s", message.name, details))
end

local function defaultSlot()
	return {
		state = PREY_STATE_LIST_SELECTION,
		monster_name = "",
		bonus_type = PREY_BONUS_NONE,
		bonus_value = 0,
		time_left = 0,
		list_monsters = {},
		reroll_at = 0,
		list_reroll_used = false,
	}
end

local function ensurePlayerRows(playerGuid)
	ensurePreySchema()

	for slot = 0, PREY_SLOTS - 1 do
		db.query(string.format(
			"INSERT IGNORE INTO `player_prey` (`player_id`, `slot`, `state`, `list_monsters`, `reroll_at`, `list_reroll_used`) VALUES (%d, %d, %d, '', 0, 0)",
			playerGuid, slot, PREY_STATE_LIST_SELECTION
		))
	end
end

local function serializeMonsterList(list)
	return table.concat(list or {}, ";")
end

local function parseMonsterList(rawList)
	local list = {}
	if rawList and rawList ~= "" then
		for name in rawList:gmatch("[^;]+") do
			table.insert(list, name)
		end
	end
	return list
end

local function getPlayerBonusRerolls(player)
	local resultId = db.storeQuery("SELECT `bonus_rerolls` FROM `players` WHERE `id` = " .. player:getGuid())
	if resultId == false then
		return 0
	end

	local rerolls = math.min(result.getDataInt(resultId, "bonus_rerolls"), PREY_MAX_WILDCARDS)
	result.free(resultId)
	return math.max(rerolls, 0)
end

local function setPlayerBonusRerolls(player, rerolls)
	rerolls = math.min(math.max(tonumber(rerolls) or 0, 0), PREY_MAX_WILDCARDS)
	db.query(string.format(
		"UPDATE `players` SET `bonus_rerolls` = %d WHERE `id` = %d",
		rerolls,
		player:getGuid()
	))
	return rerolls
end

local function loadPreyFromDB(playerGuid)
	local data = { wildcards = 0, legacyWildcards = 0, slots = {}, saveTicker = 0 }
	for slot = 0, PREY_SLOTS - 1 do
		data.slots[slot] = defaultSlot()
	end

	ensurePlayerRows(playerGuid)

	local resultId = db.storeQuery("SELECT * FROM `player_prey` WHERE `player_id` = " .. playerGuid)
	if resultId == false then
		return data
	end

	repeat
		local slot = result.getDataInt(resultId, "slot")
		if slot >= 0 and slot < PREY_SLOTS then
			data.slots[slot] = {
				state = result.getDataInt(resultId, "state"),
				monster_name = result.getDataString(resultId, "monster_name") or "",
				bonus_type = result.getDataInt(resultId, "bonus_type"),
				bonus_value = result.getDataInt(resultId, "bonus_value"),
				time_left = result.getDataInt(resultId, "time_left"),
				list_monsters = parseMonsterList(result.getDataString(resultId, "list_monsters")),
				reroll_at = result.getDataLong(resultId, "reroll_at"),
				list_reroll_used = result.getDataInt(resultId, "list_reroll_used") ~= 0,
			}
			if slot == 0 then
				data.legacyWildcards = math.min(result.getDataInt(resultId, "wildcards"), PREY_MAX_WILDCARDS)
			end
		end
	until not result.next(resultId)

	result.free(resultId)

	for slot = 0, PREY_SLOTS - 1 do
		local slotData = data.slots[slot]
		if slotData.state == PREY_STATE_EMPTY then
			slotData.state = PREY_STATE_LIST_SELECTION
		end
		if not slotData.list_reroll_used and slotData.state == PREY_STATE_LIST_SELECTION and (slotData.monster_name or "") == "" then
			slotData.reroll_at = 0
		end
		if slotData.state == PREY_STATE_ACTIVE and ((slotData.monster_name or "") == "" or slotData.time_left <= 0 or slotData.bonus_type == PREY_BONUS_NONE or #(slotData.list_monsters or {}) > 0) then
			slotData.state = PREY_STATE_LIST_SELECTION
			slotData.monster_name = ""
			slotData.bonus_type = PREY_BONUS_NONE
			slotData.bonus_value = 0
			slotData.time_left = 0
		end
	end

	return data
end

local function saveSlotToDB(playerGuid, slot, slotData, wildcards)
	ensurePreySchema()

	db.asyncQuery(string.format(
		"INSERT INTO `player_prey` (`player_id`, `slot`, `state`, `monster_name`, `bonus_type`, `bonus_value`, `time_left`, `list_monsters`, `reroll_at`, `wildcards`, `list_reroll_used`) " ..
		"VALUES (%d, %d, %d, %s, %d, %d, %d, %s, %d, %d, %d) " ..
		"ON DUPLICATE KEY UPDATE `state` = VALUES(`state`), `monster_name` = VALUES(`monster_name`), `bonus_type` = VALUES(`bonus_type`), " ..
		"`bonus_value` = VALUES(`bonus_value`), `time_left` = VALUES(`time_left`), `list_monsters` = VALUES(`list_monsters`), " ..
		"`reroll_at` = VALUES(`reroll_at`), `wildcards` = VALUES(`wildcards`), `list_reroll_used` = VALUES(`list_reroll_used`)",
		playerGuid,
		slot,
		slotData.state,
		db.escapeString(slotData.monster_name or ""),
		slotData.bonus_type,
		slotData.bonus_value,
		slotData.time_left,
		db.escapeString(serializeMonsterList(slotData.list_monsters)),
		slotData.reroll_at,
		0,
		slotData.list_reroll_used and 1 or 0
	))
end

local function saveAllSlots(player)
	local prey = preyCache[player:getId()]
	if not prey then
		return
	end

	local guid = player:getGuid()
	for slot = 0, PREY_SLOTS - 1 do
		saveSlotToDB(guid, slot, prey.slots[slot], prey.wildcards)
	end
end

local function getPlayerPrey(player)
	local playerId = player:getId()
	if not preyCache[playerId] then
		preyCache[playerId] = loadPreyFromDB(player:getGuid())
		if getPlayerBonusRerolls(player) == 0 and preyCache[playerId].legacyWildcards > 0 then
			setPlayerBonusRerolls(player, preyCache[playerId].legacyWildcards)
		end
	end
	preyCache[playerId].wildcards = getPlayerBonusRerolls(player)
	return preyCache[playerId]
end

local function syncPreyCombatBonuses(player, prey)
	if not player or not player.clearPreyCombatBonuses then
		return
	end

	prey = prey or preyCache[player:getId()]
	if not prey or not prey.slots then
		return
	end

	player:clearPreyCombatBonuses()
	for slot = 0, PREY_SLOTS - 1 do
		local slotData = prey.slots[slot]
		local isActiveCombatPrey = isPreySlotUnlocked(player, slot)
			and slotData
			and slotData.state == PREY_STATE_ACTIVE
			and slotData.time_left > 0
			and (slotData.monster_name or "") ~= ""
			and (slotData.bonus_value or 0) > 0
			and #(slotData.list_monsters or {}) == 0
		if isActiveCombatPrey then
			if slotData.bonus_type == PREY_BONUS_DMG_BOOST then
				player:setPreyDamageBoost(slotData.monster_name, slotData.bonus_value)
			elseif slotData.bonus_type == PREY_BONUS_DMG_RED then
				player:setPreyDamageReduction(slotData.monster_name, slotData.bonus_value)
			end
		end
	end
end

local function rollBonusType(excludedType)
	if excludedType and excludedType >= PREY_BONUS_DMG_BOOST and excludedType <= PREY_BONUS_LOOT then
		local types = {}
		for bonusType = PREY_BONUS_DMG_BOOST, PREY_BONUS_LOOT do
			if bonusType ~= excludedType then
				types[#types + 1] = bonusType
			end
		end
		return types[math.random(#types)]
	end

	return math.random(PREY_BONUS_DMG_BOOST, PREY_BONUS_LOOT)
end

local function rollBonusValue(currentValue)
	currentValue = tonumber(currentValue) or 0
	if currentValue >= 40 then
		return 40
	end

	local minValue = currentValue > 0 and currentValue + 5 or 5
	local value = math.random(minValue, 40)
	value = math.floor((value + 2) / 5) * 5
	return math.min(math.max(value, 5), 40)
end

local function rerollBonus(slotData)
	local currentValue = tonumber(slotData.bonus_value) or 0
	if currentValue >= 40 then
		slotData.bonus_type = rollBonusType(slotData.bonus_type)
		slotData.bonus_value = 40
		return
	end

	slotData.bonus_type = rollBonusType()
	slotData.bonus_value = rollBonusValue(currentValue)
end

local function getStorageFlag(player, baseStorage, slot)
	return player:getStorageValue(baseStorage + slot) == 1
end

local function setStorageFlag(player, baseStorage, slot, enabled)
	player:setStorageValue(baseStorage + slot, enabled and 1 or -1)
end

local function getPreyLockType(player, slot)
	if getStorageFlag(player, PREY_STORAGE_AUTO_BONUS_BASE, slot) then
		return 1
	end
	if getStorageFlag(player, PREY_STORAGE_LOCK_BASE, slot) then
		return 2
	end
	return 0
end

local function writePreyLockType(out, player, slot)
	if supportsAstraPreyExtension(player) then
		out:addByte(getPreyLockType(player, slot))
	end
end

local function sendResourceBalance(player, resourceType, value)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(PREY_OPCODE_RESOURCE_BALANCE)
	out:addByte(resourceType)
	out:addU64(value)
	return out:sendToPlayer(player)
end

function Player.sendResource(self, resourceType, value)
	if not supportsCustomNetwork(self) then
		return false
	end

	local typeByte = resourceType
	if resourceType == "bank" then
		typeByte = RESOURCE_BANK
	elseif resourceType == "inventory" then
		typeByte = RESOURCE_INVENTORY
	elseif resourceType == "prey" then
		typeByte = RESOURCE_PREY
	end

	return sendResourceBalance(self, typeByte, value or 0)
end

local function sendPreyBalances(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local prey = getPlayerPrey(player)
	prey.wildcards = getPlayerBonusRerolls(player)
	player:sendResource("prey", prey.wildcards)
	player:sendResource("bank", player:getBankBalance())
	player:sendResource("inventory", player:getMoney())
	return true
end

local function sendError(player, message)
	if not supportsCustomNetwork(player) then
		return false
	end

	player:sendTextMessage(MESSAGE_STATUS_SMALL, message)
	return false
end

local function requirePreySlotUnlocked(player, slot)
	if isPreySlotUnlocked(player, slot) then
		return true
	end

	return sendError(player, "This Prey slot is locked.")
end

local function getMonsterOutfit(name)
	local monsterType = name and name ~= "" and MonsterType(name)
	if not monsterType then
		return {
			lookType = 21,
			lookHead = 0,
			lookBody = 0,
			lookLegs = 0,
			lookFeet = 0,
			lookAddons = 0,
		}
	end

	local outfit = monsterType:outfit()
	return {
		lookType = outfit.lookType or 21,
		lookTypeEx = outfit.lookTypeEx or 0,
		lookHead = outfit.lookHead or 0,
		lookBody = outfit.lookBody or 0,
		lookLegs = outfit.lookLegs or 0,
		lookFeet = outfit.lookFeet or 0,
		lookAddons = outfit.lookAddons or 0,
	}
end

local function getMonsterRaceId(name)
	local monsterType = name and name ~= "" and MonsterType(name)
	if not monsterType then
		return nil
	end

	local raceId = tonumber(monsterType:raceId()) or 0
	if raceId <= 0 or raceId > 0xFFFF then
		return nil
	end
	return raceId
end

local function writeMonster(out, name)
	out:addString(name or "")

	local outfit = getMonsterOutfit(name)
	out:addU16(outfit.lookType)
	if outfit.lookType == 0 then
		out:addU16(outfit.lookTypeEx or 0)
		return
	end
	out:addByte(outfit.lookHead)
	out:addByte(outfit.lookBody)
	out:addByte(outfit.lookLegs)
	out:addByte(outfit.lookFeet)
	out:addByte(outfit.lookAddons)
end

local function getListRerollCost(player)
	return player:getLevel() * PREY_LIST_REROLL_COST_PER_LEVEL
end

local function getPlayerTotalGold(player)
	return math.max(0, tonumber(player:getMoney()) or 0) + math.max(0, tonumber(player:getBankBalance()) or 0)
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

local function getTimeUntilFreeReroll(slotData)
	return math.max(0, math.ceil(((slotData.reroll_at or 0) - os.time()) / 60))
end

local function getNativeBonusType(slotData)
	if not slotData or slotData.bonus_type == PREY_BONUS_NONE then
		return PREY_NATIVE_BONUS_NONE
	end
	return math.max(0, slotData.bonus_type - 1)
end

local function getNativeBonusValue(slotData)
	if not slotData or slotData.bonus_type == PREY_BONUS_NONE then
		return 0
	end
	return math.max(0, slotData.bonus_value or 0)
end

local function getNativeBonusGrade(slotData)
	if not slotData or slotData.bonus_type == PREY_BONUS_NONE then
		return 0
	end
	return math.max(1, math.min(10, math.ceil((slotData.bonus_value or 0) / 5)))
end

local function normalizeSlot(slotData)
	if slotData.state == PREY_STATE_EMPTY then
		slotData.state = PREY_STATE_LIST_SELECTION
	end

	if slotData.state == PREY_STATE_ACTIVE and ((slotData.monster_name or "") == "" or slotData.time_left <= 0 or slotData.bonus_type == PREY_BONUS_NONE or #(slotData.list_monsters or {}) > 0) then
		slotData.state = PREY_STATE_LIST_SELECTION
		slotData.monster_name = ""
		slotData.bonus_type = PREY_BONUS_NONE
		slotData.bonus_value = 0
		slotData.time_left = 0
	end
end

local getOtherSlotMonsters
local buildWildcardRaceIds
local getPreyMonsterNameByRaceId
local preyMonsterNamesByRaceId

local function ensureSelectionList(player, slot, slotData)
	if slotData.state ~= PREY_STATE_LIST_SELECTION then
		return false
	end

	if #(slotData.list_monsters or {}) > 0 then
		return false
	end

	local prey = getPlayerPrey(player)
	slotData.list_monsters = PreyMonsters.generateList(player:getLevel(), getOtherSlotMonsters(player, prey, slot))
	return true
end

local function writeSlot(out, player, slot, slotData, wildcardRaceIds)
	out:addByte(slot)
	if not isPreySlotUnlocked(player, slot) then
		out:addByte(PREY_NATIVE_STATE_LOCKED)
		out:addByte(PREY_NATIVE_UNLOCK_STORE)
		out:addU16(0)
		writePreyLockType(out, player, slot)
		if supportsAstraPreyExtension(player) then
			out:addU32(PREY_PERMANENT_SLOT_COST)
		end
		return
	end

	normalizeSlot(slotData)
	ensureSelectionList(player, slot, slotData)
	if slotData.state == PREY_STATE_LIST_SELECTION then
		out:addByte(PREY_NATIVE_STATE_SELECTION)
		local list = slotData.list_monsters or {}
		out:addByte(#list)
		for _, name in ipairs(list) do
			writeMonster(out, name)
		end
	elseif slotData.state == PREY_STATE_WILDCARD_SELECTION then
		local includeMonsterData = supportsAstraPreyExtension(player)
		out:addByte(includeMonsterData and PREY_NATIVE_STATE_WILDCARD_WITH_MONSTERS or PREY_NATIVE_STATE_WILDCARD)
		local raceIds = wildcardRaceIds or buildWildcardRaceIds(player, getPlayerPrey(player), slot)
		out:addU16(#raceIds)
		for _, raceId in ipairs(raceIds) do
			out:addU16(raceId)
			if includeMonsterData then
				writeMonster(out, getPreyMonsterNameByRaceId(raceId))
			end
		end
	elseif slotData.state == PREY_STATE_ACTIVE then
		out:addByte(PREY_NATIVE_STATE_ACTIVE)
		writeMonster(out, slotData.monster_name)
		out:addByte(getNativeBonusType(slotData))
		out:addU16(getNativeBonusValue(slotData))
		out:addByte(getNativeBonusGrade(slotData))
		out:addU16(slotData.time_left)
	else
		out:addByte(PREY_NATIVE_STATE_INACTIVE)
	end
	out:addU16(getTimeUntilFreeReroll(slotData))
	writePreyLockType(out, player, slot)
end

local function sendPreyPrices(player)
	local out = NetworkMessage(player)
	out:addByte(PREY_NATIVE_OPCODE_PRICES)
	out:addU32(getListRerollCost(player))
	return out:sendToPlayer(player)
end

local function sendFullPrey(player, sendBalances)
	if not supportsCustomNetwork(player) then
		return false
	end

	local prey = getPlayerPrey(player)
	for slot = 0, PREY_SLOTS - 1 do
		local out = NetworkMessage(player)
		out:addByte(PREY_NATIVE_OPCODE_DATA)
		writeSlot(out, player, slot, prey.slots[slot])
		out:sendToPlayer(player)
	end
	syncPreyCombatBonuses(player, prey)
	sendPreyPrices(player)
	if sendBalances ~= false then
		sendPreyBalances(player)
	end
	return true
end

local function sendSlotUpdate(player, slot, wildcardRaceIds)
	if not supportsCustomNetwork(player) then
		return false
	end

	local prey = getPlayerPrey(player)
	local out = NetworkMessage(player)
	out:addByte(PREY_NATIVE_OPCODE_DATA)
	writeSlot(out, player, slot, prey.slots[slot], wildcardRaceIds)
	syncPreyCombatBonuses(player, prey)
	local sent = out:sendToPlayer(player)
	saveSlotToDB(player:getGuid(), slot, prey.slots[slot], prey.wildcards)
	return sent
end

local function sendPreyTimeLeft(player, slot, timeLeft)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(PREY_NATIVE_OPCODE_TIME_LEFT)
	out:addByte(slot)
	out:addU16(math.max(0, math.min(0xFFFF, timeLeft or 0)))
	return out:sendToPlayer(player)
end

getOtherSlotMonsters = function(player, prey, excludedSlot)
	local names = {}
	for slot = 0, PREY_SLOTS - 1 do
		if slot ~= excludedSlot and isPreySlotUnlocked(player, slot) then
			local slotData = prey.slots[slot]
			if slotData.monster_name and slotData.monster_name ~= "" then
				table.insert(names, slotData.monster_name)
			end
			for _, name in ipairs(slotData.list_monsters or {}) do
				table.insert(names, name)
			end
		end
	end
	return names
end

local function isMonsterUsedByOtherSlot(player, prey, excludedSlot, monsterName)
	local lowerName = (monsterName or ""):lower()
	for slot = 0, PREY_SLOTS - 1 do
		if slot ~= excludedSlot and isPreySlotUnlocked(player, slot) then
			local slotData = prey.slots[slot]
			if (slotData.monster_name or ""):lower() == lowerName then
				return true
			end
		end
	end
	return false
end

local function isWildcardRaceAvailable(player, prey, slot, raceId)
	for _, availableRaceId in ipairs(buildWildcardRaceIds(player, prey, slot)) do
		if availableRaceId == raceId then
			return true
		end
	end
	return false
end

getPreyMonsterNameByRaceId = function(raceId)
	raceId = tonumber(raceId) or 0
	if raceId <= 0 then
		return nil
	end

	if not preyMonsterNamesByRaceId then
		preyMonsterNamesByRaceId = {}
		for _, name in ipairs(PreyMonsters.getAllNames()) do
			local monsterRaceId = getMonsterRaceId(name)
			if monsterRaceId then
				preyMonsterNamesByRaceId[monsterRaceId] = name
			end
		end
	end

	return preyMonsterNamesByRaceId[raceId]
end

buildWildcardRaceIds = function(player, prey, slot)
	local raceIds = {}
	local seen = {}
	local names = PreyMonsters.getAllNames(player:getLevel(), getOtherSlotMonsters(player, prey, slot))
	for _, name in ipairs(names) do
		local raceId = getMonsterRaceId(name)
		if raceId and not seen[raceId] then
			seen[raceId] = true
			table.insert(raceIds, raceId)
		end
	end

	table.sort(raceIds)
	return raceIds
end

local function initializeSlot(player, slot)
	if not isPreySlotUnlocked(player, slot) then
		return false
	end

	local prey = getPlayerPrey(player)
	local slotData = prey.slots[slot]
	if slotData.state ~= PREY_STATE_EMPTY then
		return false
	end

	slotData.state = PREY_STATE_LIST_SELECTION
	slotData.monster_name = ""
	slotData.bonus_type = PREY_BONUS_NONE
	slotData.bonus_value = 0
	slotData.time_left = 0
	slotData.list_monsters = PreyMonsters.generateList(player:getLevel(), getOtherSlotMonsters(player, prey, slot))
	slotData.reroll_at = 0
	slotData.list_reroll_used = false
	return true
end

local function initializeEmptySlots(player)
	local changed = false
	local prey = getPlayerPrey(player)
	for slot = 0, PREY_SLOTS - 1 do
		if isPreySlotUnlocked(player, slot) and initializeSlot(player, slot) then
			saveSlotToDB(player:getGuid(), slot, prey.slots[slot], prey.wildcards)
			changed = true
		end
	end
	return changed
end

local openHandler = PacketHandler(PREY_OPCODE_OPEN)
function openHandler.onReceive(player, msg)
	initializeEmptySlots(player)
	sendFullPrey(player)
	saveAllSlots(player)
end
openHandler:register()

local selectHandler = PacketHandler(PREY_OPCODE_SELECT)
function selectHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 2 then
		return
	end

	local slot = msg:getByte()
	local listIndex = msg:getByte()
	if slot >= PREY_SLOTS then
		return sendError(player, "Invalid slot.")
	end
	if not requirePreySlotUnlocked(player, slot) then
		return false
	end

	local prey = getPlayerPrey(player)
	local slotData = prey.slots[slot]
	if slotData.state ~= PREY_STATE_LIST_SELECTION then
		return sendError(player, "This slot is not in list selection.")
	end

	if listIndex >= #(slotData.list_monsters or {}) then
		return sendError(player, "Invalid list index.")
	end

	slotData.state = PREY_STATE_BONUS_SELECTION
	slotData.monster_name = slotData.list_monsters[listIndex + 1]
	slotData.list_monsters = {}
	sendSlotUpdate(player, slot)

	local playerId = player:getId()
	addEvent(function()
		local delayedPlayer = Player(playerId)
		if not delayedPlayer then
			return
		end

		local delayedPrey = getPlayerPrey(delayedPlayer)
		local delayedSlot = delayedPrey.slots[slot]
		if delayedSlot.state ~= PREY_STATE_BONUS_SELECTION then
			return
		end

		delayedSlot.bonus_type = rollBonusType()
		delayedSlot.bonus_value = rollBonusValue(nil)
		delayedSlot.time_left = PREY_DURATION_SECS
		delayedSlot.state = PREY_STATE_ACTIVE
		sendSlotUpdate(delayedPlayer, slot)
		sendActiveBonusMessage(delayedPlayer, delayedSlot)
	end, 300)
end
selectHandler:register()

local listRerollHandler = PacketHandler(PREY_OPCODE_LIST_REROLL)
function listRerollHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 1 then
		return
	end

	local slot = msg:getByte()
	if slot >= PREY_SLOTS then
		return sendError(player, "Invalid slot.")
	end
	if not requirePreySlotUnlocked(player, slot) then
		return false
	end

	local prey = getPlayerPrey(player)
	local slotData = prey.slots[slot]
	local now = os.time()
	local isFree = not slotData.list_reroll_used or now >= slotData.reroll_at

	if isFree then
		slotData.list_reroll_used = true
		slotData.reroll_at = now + PREY_REROLL_CD
	else
		local cost = getListRerollCost(player)
		if getPlayerTotalGold(player) < cost then
			return sendError(player, string.format(
				"You need %d gold in your inventory or bank to reroll the list (next free reroll in %d minutes).",
				cost,
				math.ceil((slotData.reroll_at - now) / 60)
			))
		end

		if not removePlayerGold(player, cost) then
			return sendError(player, "Failed to remove gold.")
		end
	end

	local newList = PreyMonsters.generateList(player:getLevel(), getOtherSlotMonsters(player, prey, slot))
	if #newList < 1 then
		return sendError(player, "Could not generate a monster list.")
	end

	slotData.state = PREY_STATE_LIST_SELECTION
	slotData.monster_name = ""
	slotData.bonus_type = PREY_BONUS_NONE
	slotData.bonus_value = 0
	slotData.time_left = 0
	slotData.list_monsters = newList
	sendSlotUpdate(player, slot)
	sendPreyBalances(player)
end
listRerollHandler:register()

local bonusRerollHandler = PacketHandler(PREY_OPCODE_BONUS_REROLL)
function bonusRerollHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 1 then
		return
	end

	nativeBonusReroll(player, msg:getByte())
end
bonusRerollHandler:register()

local clearHandler = PacketHandler(PREY_OPCODE_CLEAR)
function clearHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 1 then
		return
	end

	local slot = msg:getByte()
	if slot >= PREY_SLOTS then
		return sendError(player, "Invalid slot.")
	end
	if not requirePreySlotUnlocked(player, slot) then
		return false
	end

	local prey = getPlayerPrey(player)
	prey.slots[slot] = defaultSlot()
	sendSlotUpdate(player, slot)
end
clearHandler:register()

local autoBonusHandler = PacketHandler(PREY_OPCODE_TOGGLE_AUTO)
function autoBonusHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 2 then
		return
	end

	local slot = msg:getByte()
	local enabled = msg:getByte() ~= 0
	if slot >= PREY_SLOTS then
		return sendError(player, "Invalid slot.")
	end
	if not requirePreySlotUnlocked(player, slot) then
		return false
	end

	setStorageFlag(player, PREY_STORAGE_AUTO_BONUS_BASE, slot, enabled)
	if enabled then
		setStorageFlag(player, PREY_STORAGE_LOCK_BASE, slot, false)
	end
	sendSlotUpdate(player, slot)
end
autoBonusHandler:register()

local lockPreyHandler = PacketHandler(PREY_OPCODE_TOGGLE_LOCK)
function lockPreyHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 2 then
		return
	end

	local slot = msg:getByte()
	local enabled = msg:getByte() ~= 0
	if slot >= PREY_SLOTS then
		return sendError(player, "Invalid slot.")
	end
	if not requirePreySlotUnlocked(player, slot) then
		return false
	end

	setStorageFlag(player, PREY_STORAGE_LOCK_BASE, slot, enabled)
	if enabled then
		setStorageFlag(player, PREY_STORAGE_AUTO_BONUS_BASE, slot, false)
	end
	sendSlotUpdate(player, slot)
end
lockPreyHandler:register()

local function playerIsInCombat(player)
	if player.isInCombat then
		return player:isInCombat()
	end
	return player:getCondition(CONDITION_INFIGHT, CONDITIONID_DEFAULT) or player:getCondition(CONDITION_INFIGHT, CONDITIONID_COMBAT)
end

local preyTickToken = 0

local function preyTick()
	if PreySystem._tickToken ~= preyTickToken then
		return
	end

	for _, player in ipairs(Game.getPlayers()) do
		if playerIsInCombat(player) then
			local prey = preyCache[player:getId()]
			if prey then
				local changed = false
				for slot = 0, PREY_SLOTS - 1 do
					local slotData = prey.slots[slot]
					if isPreySlotUnlocked(player, slot) and slotData.state == PREY_STATE_ACTIVE and slotData.time_left > 0 then
						slotData.time_left = math.max(slotData.time_left - 1, 0)
						changed = true
						if slotData.time_left == 0 then
							local locked = getStorageFlag(player, PREY_STORAGE_LOCK_BASE, slot)
							local autoBonus = getStorageFlag(player, PREY_STORAGE_AUTO_BONUS_BASE, slot)
							if locked and autoBonus then
								setStorageFlag(player, PREY_STORAGE_AUTO_BONUS_BASE, slot, false)
								autoBonus = false
							end

							if locked then
								prey.wildcards = getPlayerBonusRerolls(player)
								if prey.wildcards >= PREY_LOCK_COST then
									prey.wildcards = setPlayerBonusRerolls(player, prey.wildcards - PREY_LOCK_COST)
									slotData.time_left = PREY_DURATION_SECS
									slotData.state = PREY_STATE_ACTIVE
								else
									setStorageFlag(player, PREY_STORAGE_LOCK_BASE, slot, false)
									slotData.state = PREY_STATE_INACTIVE
								end
							elseif autoBonus then
								prey.wildcards = getPlayerBonusRerolls(player)
								if prey.wildcards >= PREY_AUTO_BONUS_COST then
									prey.wildcards = setPlayerBonusRerolls(player, prey.wildcards - PREY_AUTO_BONUS_COST)
									rerollBonus(slotData)
									slotData.time_left = PREY_DURATION_SECS
									slotData.state = PREY_STATE_ACTIVE
									sendActiveBonusMessage(player, slotData)
								else
									setStorageFlag(player, PREY_STORAGE_AUTO_BONUS_BASE, slot, false)
									slotData.state = PREY_STATE_INACTIVE
								end
							else
								slotData.state = PREY_STATE_INACTIVE
							end
							sendSlotUpdate(player, slot)
						else
							sendPreyTimeLeft(player, slot, slotData.time_left)
						end
					end
				end

				if changed then
					prey.saveTicker = (prey.saveTicker or 0) + 1
					if prey.saveTicker >= 60 then
						for slot = 0, PREY_SLOTS - 1 do
							saveSlotToDB(player:getGuid(), slot, prey.slots[slot], prey.wildcards)
						end
						prey.saveTicker = 0
					end
				end
			end
		end
	end

	addEvent(preyTick, PREY_TICK_INTERVAL)
end

PreySystem._tickToken = (PreySystem._tickToken or 0) + 1
preyTickToken = PreySystem._tickToken
addEvent(preyTick, PREY_TICK_INTERVAL)

local loginEvent = CreatureEvent("PreySystemLogin")
function loginEvent.onLogin(player)
	preyCache[player:getId()] = loadPreyFromDB(player:getGuid())
	syncPreyCombatBonuses(player, preyCache[player:getId()])
	player:registerEvent("PreySystemLogout")
	return true
end
loginEvent:register()

local logoutEvent = CreatureEvent("PreySystemLogout")
function logoutEvent.onLogout(player)
	saveAllSlots(player)
	if player.clearPreyCombatBonuses then
		player:clearPreyCombatBonuses()
	end
	preyCache[player:getId()] = nil
	return true
end
logoutEvent:register()

function PreySystem.getBonus(player, monsterName)
	if not player or not monsterName or monsterName == "" then
		return nil
	end

	local prey = getPlayerPrey(player)
	local targetName = monsterName:lower()
	for slot = 0, PREY_SLOTS - 1 do
		local slotData = prey.slots[slot]
		if isPreySlotUnlocked(player, slot) and slotData.state == PREY_STATE_ACTIVE and slotData.time_left > 0 and (slotData.monster_name or ""):lower() == targetName then
			return slotData.bonus_type, slotData.bonus_value
		end
	end
	return nil
end

function PreySystem.addWildcards(player, amount)
	amount = tonumber(amount) or 0
	if amount <= 0 then
		return false
	end

	local prey = getPlayerPrey(player)
	prey.wildcards = setPlayerBonusRerolls(player, getPlayerBonusRerolls(player) + amount)
	sendFullPrey(player, false)
	sendPreyBalances(player)
	return true
end

function PreySystem.initSlot(player, slot)
	if slot < 0 or slot >= PREY_SLOTS then
		return false
	end
	if not isPreySlotUnlocked(player, slot) then
		return false
	end

	if initializeSlot(player, slot) then
		sendSlotUpdate(player, slot)
		return true
	end
	return false
end

local function nativeSelectMonster(player, slot, listIndex)
	if not requirePreySlotUnlocked(player, slot) then
		return false
	end

	local prey = getPlayerPrey(player)
	local slotData = prey.slots[slot]
	if not slotData or slotData.state ~= PREY_STATE_LIST_SELECTION then
		return sendError(player, "This slot is not in list selection.")
	end
	if listIndex < 0 or listIndex >= #(slotData.list_monsters or {}) then
		return sendError(player, "Invalid list index.")
	end

	slotData.state = PREY_STATE_BONUS_SELECTION
	slotData.monster_name = slotData.list_monsters[listIndex + 1]
	slotData.list_monsters = {}
	sendSlotUpdate(player, slot)

	local playerId = player:getId()
	addEvent(function()
		local delayedPlayer = Player(playerId)
		if not delayedPlayer then
			return
		end
		local delayedPrey = getPlayerPrey(delayedPlayer)
		local delayedSlot = delayedPrey.slots[slot]
		if delayedSlot.state ~= PREY_STATE_BONUS_SELECTION then
			return
		end
		delayedSlot.bonus_type = rollBonusType()
		delayedSlot.bonus_value = rollBonusValue(nil)
		delayedSlot.time_left = PREY_DURATION_SECS
		delayedSlot.state = PREY_STATE_ACTIVE
		sendSlotUpdate(delayedPlayer, slot)
		sendActiveBonusMessage(delayedPlayer, delayedSlot)
	end, 300)
end

local function nativeListReroll(player, slot)
	if not requirePreySlotUnlocked(player, slot) then
		return false
	end

	local prey = getPlayerPrey(player)
	local slotData = prey.slots[slot]
	local now = os.time()
	local isFree = not slotData.list_reroll_used or now >= slotData.reroll_at
	if isFree then
		slotData.list_reroll_used = true
		slotData.reroll_at = now + PREY_REROLL_CD
	else
		local cost = getListRerollCost(player)
		if not removePlayerGold(player, cost) then
			return sendError(player, "You do not have enough gold to reroll the prey list.")
		end
	end

	slotData.state = PREY_STATE_LIST_SELECTION
	slotData.monster_name = ""
	slotData.bonus_type = PREY_BONUS_NONE
	slotData.bonus_value = 0
	slotData.time_left = 0
	slotData.list_monsters = PreyMonsters.generateList(player:getLevel(), getOtherSlotMonsters(player, prey, slot))
	sendSlotUpdate(player, slot)
	sendPreyBalances(player)
end

local function nativeBonusReroll(player, slot)
	if slot < 0 or slot >= PREY_SLOTS then
		return sendError(player, "Invalid slot.")
	end
	if not requirePreySlotUnlocked(player, slot) then
		return false
	end

	local prey = getPlayerPrey(player)
	prey.wildcards = getPlayerBonusRerolls(player)
	if prey.wildcards < 1 then
		return sendError(player, "You do not have enough Prey Wildcards.")
	end

	local slotData = prey.slots[slot]
	if slotData.state ~= PREY_STATE_ACTIVE and slotData.state ~= PREY_STATE_BONUS_SELECTION then
		return sendError(player, "This slot does not have an active bonus to reroll.")
	end

	prey.wildcards = setPlayerBonusRerolls(player, prey.wildcards - 1)
	rerollBonus(slotData)
	slotData.time_left = PREY_DURATION_SECS
	slotData.state = PREY_STATE_ACTIVE
	sendSlotUpdate(player, slot)
	sendActiveBonusMessage(player, slotData)
	sendPreyBalances(player)
end

local function nativeRequestAllMonsters(player, slot)
	if not requirePreySlotUnlocked(player, slot) then
		return false
	end

	local prey = getPlayerPrey(player)
	local slotData = prey.slots[slot]
	if slotData.state == PREY_STATE_WILDCARD_SELECTION then
		return sendError(player, "You are already selecting a creature from the list.")
	end

	prey.wildcards = getPlayerBonusRerolls(player)
	if prey.wildcards < PREY_LOCK_COST then
		return sendError(player, "You do not have enough Prey Wildcards.")
	end

	local raceIds = buildWildcardRaceIds(player, prey, slot)
	if #raceIds == 0 then
		return sendError(player, "Could not build the Prey creature list.")
	end

	prey.wildcards = setPlayerBonusRerolls(player, prey.wildcards - PREY_LOCK_COST)
	slotData.state = PREY_STATE_WILDCARD_SELECTION
	slotData.monster_name = ""
	slotData.list_monsters = {}
	slotData.time_left = 0
	sendSlotUpdate(player, slot, raceIds)
	sendPreyBalances(player)
end

local function nativeChangeFromAll(player, slot, raceId)
	if not requirePreySlotUnlocked(player, slot) then
		return false
	end

	local monsterName = getPreyMonsterNameByRaceId(raceId)
	if not monsterName then
		return sendError(player, "Invalid Prey creature.")
	end

	local prey = getPlayerPrey(player)
	local slotData = prey.slots[slot]
	if slotData.state ~= PREY_STATE_WILDCARD_SELECTION then
		return sendError(player, "There is no active Prey creature list for this slot.")
	end
	if not isWildcardRaceAvailable(player, prey, slot, raceId) then
		return sendError(player, "Invalid Prey creature.")
	end
	if isMonsterUsedByOtherSlot(player, prey, slot, monsterName) then
		return sendError(player, "This creature is already used by another Prey slot.")
	end

	slotData.monster_name = monsterName
	slotData.list_monsters = {}
	if slotData.bonus_type == PREY_BONUS_NONE or (slotData.bonus_value or 0) <= 0 then
		rerollBonus(slotData)
	end
	slotData.time_left = PREY_DURATION_SECS
	slotData.state = PREY_STATE_ACTIVE
	sendSlotUpdate(player, slot)
	sendActiveBonusMessage(player, slotData)
	sendPreyBalances(player)
end

local nativeRequestHandler = PacketHandler(PREY_NATIVE_OPCODE_REQUEST)
function nativeRequestHandler.onReceive(player, msg)
	initializeEmptySlots(player)
	sendFullPrey(player)
	saveAllSlots(player)
end
nativeRequestHandler:register()

local function unlockPermanentPreySlot(player, slot)
	if slot ~= PREY_PERMANENT_SLOT then
		return sendError(player, "This slot cannot be purchased.")
	end
	if isPreySlotUnlocked(player, slot) then
		sendFullPrey(player)
		return sendError(player, "This Prey slot is already unlocked.")
	end
	if player:getTibiaCoins() < PREY_PERMANENT_SLOT_COST then
		return sendError(player, string.format(
			"You need %d Tibia Coins to unlock this Prey slot permanently.",
			PREY_PERMANENT_SLOT_COST
		))
	end
	if not player:removeTibiaCoins(PREY_PERMANENT_SLOT_COST) then
		return sendError(player, "Failed to remove Tibia Coins.")
	end

	local saved = db.query(string.format(
		"INSERT INTO `player_storage` (`player_id`, `key`, `value`) VALUES (%d, %d, 1) "
			.. "ON DUPLICATE KEY UPDATE `value` = 1",
		player:getGuid(),
		PREY_STORAGE_PERMANENT_SLOT
	))
	if not saved then
		player:addTibiaCoins(PREY_PERMANENT_SLOT_COST)
		return sendError(player, "The Prey slot could not be unlocked. Your Tibia Coins were refunded.")
	end

	player:setStorageValue(PREY_STORAGE_PERMANENT_SLOT, 1)
	initializeSlot(player, slot)
	sendFullPrey(player)
	player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "Your third Prey slot is now permanently unlocked.")
	return true
end

local nativeActionHandler = PacketHandler(PREY_NATIVE_OPCODE_ACTION)
function nativeActionHandler.onReceive(player, msg)
	local remaining = msg:len() - msg:tell()
	if remaining == 1 then
		return nativeBonusReroll(player, msg:getByte())
	end
	if remaining < 2 then
		return
	end

	local slot = msg:getByte()
	local action = msg:getByte()
	if slot >= PREY_SLOTS then
		return sendError(player, "Invalid slot.")
	end
	if action == PREY_NATIVE_ACTION_UNLOCK_PERMANENT then
		return unlockPermanentPreySlot(player, slot)
	end
	if not requirePreySlotUnlocked(player, slot) then
		return false
	end

	if action == PREY_NATIVE_ACTION_LIST_REROLL then
		return nativeListReroll(player, slot)
	elseif action == PREY_NATIVE_ACTION_BONUS_REROLL then
		return nativeBonusReroll(player, slot)
	elseif action == PREY_NATIVE_ACTION_MONSTER_SELECTION then
		if msg:len() - msg:tell() < 1 then
			return
		end
		return nativeSelectMonster(player, slot, msg:getByte())
	elseif action == PREY_NATIVE_ACTION_REQUEST_ALL_MONSTERS then
		return nativeRequestAllMonsters(player, slot)
	elseif action == PREY_NATIVE_ACTION_CHANGE_FROM_ALL then
		if msg:len() - msg:tell() < 2 then
			return
		end
		return nativeChangeFromAll(player, slot, msg:getU16())
	elseif action == PREY_NATIVE_ACTION_LOCK_PREY then
		if msg:len() - msg:tell() < 1 then
			return
		end
		local option = msg:getByte()
		setStorageFlag(player, PREY_STORAGE_AUTO_BONUS_BASE, slot, option == 1)
		setStorageFlag(player, PREY_STORAGE_LOCK_BASE, slot, option == 2)
		return sendSlotUpdate(player, slot)
	end

	return sendError(player, "Unsupported prey action.")
end
nativeActionHandler:register()

PreySystem.sendWindow = sendFullPrey
PreySystem.sendSlots = sendFullPrey
