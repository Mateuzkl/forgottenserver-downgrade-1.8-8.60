
if not configManager.getBoolean(configKeys.FORGE_SYSTEM_ENABLED) then
	CustomForge = nil
	return
end

local OPCODE_FORGE_REQUEST = 0xE2
local OPCODE_FORGE_SEND = 0xE3
local OPCODE_RESOURCE_BALANCE = 0xEE

local REQUEST_OPEN = 1
local REQUEST_CLOSE = 2
local REQUEST_FUSION = 3
local REQUEST_TRANSFER = 4
local REQUEST_CONVERT = 5
local REQUEST_HISTORY = 6

local RESPONSE_MESSAGE = 0
local RESPONSE_INIT = 1
local RESPONSE_DATA = 2
local RESPONSE_FUSION = 3
local RESPONSE_TRANSFER = 4
local RESPONSE_HISTORY = 5
local RESPONSE_CLOSE = 6

local HISTORY_FUSION = 0
local HISTORY_TRANSFER = 1
local HISTORY_CONVERSION = 2

local RESOURCE_BANK = 0
local RESOURCE_INVENTORY = 1
local RESOURCE_FORGE_DUST = 20
local RESOURCE_FORGE_SLIVERS = 21
local RESOURCE_FORGE_EXALTED_CORE = 22

local FORGE_ITEM_IDS = {
	dust = 37160,
	sliver = 37109,
	exaltedCore = 37110
}

local FORGE_STORAGE = {
	dust = PlayerStorageKeys.forgeDust,
	dustLimit = PlayerStorageKeys.forgeDustLimit
}

local CLASS_MAX_TIER = {
	[1] = 1,
	[2] = 2,
	[3] = 3,
	[4] = 10
}

local FUSION_COSTS = {
	[1] = { [0] = 25000 },
	[2] = { [0] = 750000, [1] = 5000000 },
	[3] = { [0] = 4000000, [1] = 10000000, [2] = 20000000 },
	[4] = {
		[0] = 8000000,
		[1] = 20000000,
		[2] = 40000000,
		[3] = 65000000,
		[4] = 100000000,
		[5] = 250000000,
		[6] = 750000000,
		[7] = 2500000000,
		[8] = 8000000000,
		[9] = 15000000000
	}
}

local TRANSFER_CORES = {
	[1] = 1,
	[2] = 2,
	[3] = 5,
	[4] = 10,
	[5] = 15,
	[6] = 20,
	[7] = 25,
	[8] = 30,
	[9] = 35,
	[10] = 40
}

local CONVERGENCE_PRICES = {
	[0] = 8000000,
	[1] = 20000000,
	[2] = 40000000,
	[3] = 65000000,
	[4] = 100000000,
	[5] = 250000000,
	[6] = 750000000,
	[7] = 2500000000,
	[8] = 8000000000,
	[9] = 15000000000,
	[10] = 15000000000
}

local TRANSFER_PRICES = CONVERGENCE_PRICES

local FORGE_DATA_CACHE = {}
local FORGE_CACHE_TIMEOUT = 5000

local function invalidateForgeCache(player)
	local playerId = player:getGuid()
	if FORGE_DATA_CACHE[playerId] then
		FORGE_DATA_CACHE[playerId] = nil
	end
end

local function getCachedForgeData(player)
	local playerId = player:getGuid()
	local cache = FORGE_DATA_CACHE[playerId]

	if cache and (os.mtime() - cache.timestamp) < FORGE_CACHE_TIMEOUT then
		return cache.fusionData, cache.fusionConvergenceData, cache.transferData, cache.transferConvergenceData
	end

	return nil
end

local function setCachedForgeData(player, fusionData, fusionConvergenceData, transferData, transferConvergenceData)
	local playerId = player:getGuid()
	FORGE_DATA_CACHE[playerId] = {
		fusionData = fusionData,
		fusionConvergenceData = fusionConvergenceData,
		transferData = transferData,
		transferConvergenceData = transferConvergenceData,
		timestamp = os.mtime()
	}
end

local DUST_TO_SLIVERS = 60
local SLIVERS_GENERATED = 3
local SLIVERS_TO_CORE = 50
local MAX_DUST_LIMIT = 325
local FORGE_HISTORY_LIMIT = 50

local DUST_FUSION = 100
local DUST_FUSION_CONVERGENCE = 130
local DUST_TRANSFER = 100
local DUST_TRANSFER_CONVERGENCE = 160
local FUSION_SUCCESS_CHANCE = 50
local IMPROVE_SUCCESS_CHANCE = 15
local TIER_LOSS_REDUCTION = 50

local FORGE_DEBUG = false

local forgeOpenSessions = {}
local forgeLocks = {}
local forgeHistoryTableReady = false

local function ensureForgeHistoryTable()
	if forgeHistoryTableReady then
		return true
	end

	local created = db.query([[
		CREATE TABLE IF NOT EXISTS `player_forge_history` (
			`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
			`player_id` INT NOT NULL,
			`created_at` INT UNSIGNED NOT NULL,
			`action` TINYINT UNSIGNED NOT NULL,
			`details` VARCHAR(255) NOT NULL,
			PRIMARY KEY (`id`),
			KEY `idx_player_forge_history_player` (`player_id`, `created_at`, `id`),
			CONSTRAINT `player_forge_history_player_fk`
				FOREIGN KEY (`player_id`) REFERENCES `players` (`id`)
				ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;
	]])

	forgeHistoryTableReady = created ~= false
	return forgeHistoryTableReady
end

local function debugForge(player, message)
	if not FORGE_DEBUG then
		return
	end

	print(string.format("[CustomForge][%s] %s", player:getName(), tostring(message)))
end

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function sendForgeMessage(player, message)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_FORGE_SEND)
	out:addByte(RESPONSE_MESSAGE)
	out:addString(message)
	return out:sendToPlayer(player)
end

local function sendResourceBalance(player, resourceType, value)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_RESOURCE_BALANCE)
	out:addByte(resourceType)
	out:addU64(value)
	return out:sendToPlayer(player)
end

local function getForgeDust(player)
	local value = player:getStorageValue(FORGE_STORAGE.dust)
	return value and value > 0 and value or 0
end

local function getForgeDustLimit(player)
	local value = player:getStorageValue(FORGE_STORAGE.dustLimit)
	if not value or value <= 0 then
		player:setStorageValue(FORGE_STORAGE.dustLimit, 100)
		return 100
	end
	return value
end

local function setForgeDust(player, value)
	player:setStorageValue(FORGE_STORAGE.dust, math.max(0, math.min(value, getForgeDustLimit(player))))
end

local function removeForgeDust(player, amount)
	local current = getForgeDust(player)
	if current < amount then
		return false
	end
	player:setStorageValue(FORGE_STORAGE.dust, current - amount)
	return true
end

local function addForgeDust(player, amount)
	setForgeDust(player, getForgeDust(player) + amount)
end

local function getPlayerInventoryMoney(player)
	return math.max(0, tonumber(player:getMoney()) or 0)
end

local function getPlayerBankBalance(player)
	return math.max(0, tonumber(player:getBankBalance()) or 0)
end

local function getPlayerTotalMoney(player)
	return getPlayerInventoryMoney(player) + getPlayerBankBalance(player)
end

local function sendAllResources(player)
	local bankSent = sendResourceBalance(player, RESOURCE_BANK, getPlayerBankBalance(player))
	local inventorySent = sendResourceBalance(player, RESOURCE_INVENTORY, getPlayerInventoryMoney(player))
	local dustSent = sendResourceBalance(player, RESOURCE_FORGE_DUST, getForgeDust(player))
	local sliversSent = sendResourceBalance(player, RESOURCE_FORGE_SLIVERS, player:getItemCount(FORGE_ITEM_IDS.sliver))
	local coresSent = sendResourceBalance(player, RESOURCE_FORGE_EXALTED_CORE, player:getItemCount(FORGE_ITEM_IDS.exaltedCore))
	return bankSent and inventorySent and dustSent and sliversSent and coresSent
end

local function getForgeCategory(itemId)
	local itemType = ItemType(itemId)
	if itemType:isWeapon() then
		return 1
	elseif itemType:isArmor() then
		return 2
	elseif itemType:isHelmet() then
		return 3
	elseif itemType:isLegs() then
		return 4
	elseif itemType:isBoots() then
		return 5
	end
	return 0
end

local function getForgeRejectReason(item)
	if not item then
		return "nil_item"
	end
	if item:isContainer() then
		return "container"
	end
	if item.hasImbuements and item:hasImbuements() then
		return "has_imbuements"
	end

	local classification = item:getClassification()
	if not CLASS_MAX_TIER[classification] then
		return "classification_" .. tostring(classification)
	end

	if getForgeCategory(item:getId()) == 0 then
		return "not_equipment_category"
	end

	return nil
end

local function isForgeableItem(item)
	return getForgeRejectReason(item) == nil
end

local function getForgeItemKey(item)
	if item.getItemUID then
		local uid = item:getItemUID()
		if uid and uid ~= "" and uid ~= "0" then
			return uid
		end
	end

	return tostring(item)
end

local function describeForgeItem(item)
	if not item then
		return "nil"
	end

	return string.format(
		"id=%d tier=%d class=%d cat=%d uid=%s",
		item:getId(),
		item:getTier(),
		item:getClassification(),
		getForgeCategory(item:getId()),
		getForgeItemKey(item)
	)
end

local function newCollectDebug()
	if not FORGE_DEBUG then
		return nil
	end

	return {
		containers = 0,
		candidates = 0,
		accepted = 0,
		duplicates = 0,
		rejects = {},
		samples = {}
	}
end

local function addCollectSample(debugInfo, text)
	if not debugInfo or #debugInfo.samples >= 12 then
		return
	end
	debugInfo.samples[#debugInfo.samples + 1] = text
end

local function addForgeItem(item, items, seen, debugInfo, source)
	if debugInfo then
		debugInfo.candidates = debugInfo.candidates + 1
	end

	local rejectReason = getForgeRejectReason(item)
	if rejectReason then
		if debugInfo then
			debugInfo.rejects[rejectReason] = (debugInfo.rejects[rejectReason] or 0) + 1
			addCollectSample(debugInfo, "reject " .. source .. " " .. describeForgeItem(item) .. " reason=" .. rejectReason)
		end
		return
	end

	local key = getForgeItemKey(item)
	if seen[key] then
		if debugInfo then
			debugInfo.duplicates = debugInfo.duplicates + 1
		end
		return
	end

	seen[key] = true
	items[#items + 1] = item
	if debugInfo then
		debugInfo.accepted = debugInfo.accepted + 1
		addCollectSample(debugInfo, "accept " .. source .. " " .. describeForgeItem(item))
	end
end

local function scanContainer(container, items, seen, debugInfo, source)
	if debugInfo then
		debugInfo.containers = debugInfo.containers + 1
		addCollectSample(debugInfo, string.format("scan %s containerId=%d size=%d", source, container:getId(), container:getSize()))
	end

	local recursiveItems = container:getItems(true)
	if recursiveItems then
		for _, item in ipairs(recursiveItems) do
			addForgeItem(item, items, seen, debugInfo, source)
		end
		return
	end

	for slot = 0, container:getSize() - 1 do
		local item = container:getItem(slot)
		if item then
			if item:isContainer() then
				scanContainer(item, items, seen, debugInfo, source)
			else
				addForgeItem(item, items, seen, debugInfo, source)
			end
		end
	end
end

local function collectForgeItems(player)
	local items = {}
	local seen = {}
	local debugInfo = newCollectDebug()

	for slot = CONST_SLOT_HEAD, CONST_SLOT_AMMO do
		local item = player:getSlotItem(slot)
		if item and item:isContainer() then
			scanContainer(item, items, seen, debugInfo, "slot_" .. slot)
		end
	end

	for containerId = 0, 31 do
		local container = player:getContainerById(containerId)
		if container and container:getTopParent() == player then
			scanContainer(container, items, seen, debugInfo, "open_" .. containerId)
		end
	end

	if debugInfo then
		local rejectParts = {}
		for reason, count in pairs(debugInfo.rejects) do
			rejectParts[#rejectParts + 1] = reason .. "=" .. count
		end
		table.sort(rejectParts)

		debugForge(player, string.format(
			"collect items containers=%d candidates=%d accepted=%d duplicates=%d rejects={%s}",
			debugInfo.containers,
			debugInfo.candidates,
			debugInfo.accepted,
			debugInfo.duplicates,
			table.concat(rejectParts, ", ")
		))
		for _, sample in ipairs(debugInfo.samples) do
			debugForge(player, sample)
		end
	end

	return items
end

local function sameForgeGroup(a, b)
	if not a or not b then
		return false
	end
	return a:getClassification() == b:getClassification() and getForgeCategory(a:getId()) == getForgeCategory(b:getId())
end

local function countEntries(items)
	local entries = {}
	for _, item in ipairs(items) do
		local itemId = item:getId()
		local tier = item:getTier()
		local key = itemId .. ":" .. tier
		local entry = entries[key]

		if not entry then
			entry = {
				itemId = itemId,
				tier = tier,
				count = 0,
				classification = item:getClassification(),
				category = getForgeCategory(itemId),
				items = {}
			}
			entries[key] = entry
		end

		entry.count = entry.count + 1
		entry.items[#entry.items + 1] = item
	end
	return entries
end

local function getSubItems(items, sourceEntry, targetTier)
	local result = {}
	for _, item in ipairs(items) do
		if item:getTier() == targetTier and item:getId() ~= sourceEntry.itemId and
			item:getClassification() == sourceEntry.classification and getForgeCategory(item:getId()) == sourceEntry.category then
			local itemId = item:getId()
			result[itemId] = (result[itemId] or 0) + 1
		end
	end
	return result
end

local function subItemCount(subItems)
	local count = 0
	for _ in pairs(subItems) do
		count = count + 1
	end
	return count
end

local function buildForgeData(player)
	local fusionData, fusionConvergenceData, transferData, transferConvergenceData = getCachedForgeData(player)
	if fusionData then
		return fusionData, fusionConvergenceData, transferData, transferConvergenceData
	end

	local items = collectForgeItems(player)
	local entries = countEntries(items)
	fusionData = {}
	fusionConvergenceData = {}
	transferData = {}
	transferConvergenceData = {}

	for _, entry in pairs(entries) do
		local maxTier = CLASS_MAX_TIER[entry.classification] or 0
		if entry.tier < maxTier then
			fusionData[#fusionData + 1] = entry
			fusionConvergenceData[#fusionConvergenceData + 1] = entry
		end

		if entry.tier >= 2 then
			local subItems = getSubItems(items, entry, 0)
			if subItemCount(subItems) > 0 then
				entry.subItems = subItems
				transferData[#transferData + 1] = entry
			end
		end

		if entry.tier >= 1 then
			local subItems = getSubItems(items, entry, 0)
			if subItemCount(subItems) > 0 then
				local convergenceEntry = {
					itemId = entry.itemId,
					tier = entry.tier,
					count = entry.count,
					classification = entry.classification,
					category = entry.category,
					subItems = subItems
				}
				transferConvergenceData[#transferConvergenceData + 1] = convergenceEntry
			end
		end
	end

	local function sortEntries(a, b)
		if a.tier == b.tier then
			return a.itemId < b.itemId
		end
		return a.tier < b.tier
	end

	table.sort(fusionData, sortEntries)
	table.sort(fusionConvergenceData, sortEntries)
	table.sort(transferData, sortEntries)
	table.sort(transferConvergenceData, sortEntries)

	if FORGE_DEBUG then
		local entryCount = 0
		for _ in pairs(entries) do
			entryCount = entryCount + 1
		end

		debugForge(player, string.format(
			"build data items=%d entries=%d fusion=%d fusionConv=%d transfer=%d transferConv=%d",
			#items,
			entryCount,
			#fusionData,
			#fusionConvergenceData,
			#transferData,
			#transferConvergenceData
		))

		for i = 1, math.min(#fusionData, 12) do
			local entry = fusionData[i]
			debugForge(player, string.format(
				"fusion entry id=%d tier=%d count=%d class=%d cat=%d",
				entry.itemId,
				entry.tier,
				entry.count,
				entry.classification,
				entry.category
			))
		end
	end

	setCachedForgeData(player, fusionData, fusionConvergenceData, transferData, transferConvergenceData)

	return fusionData, fusionConvergenceData, transferData, transferConvergenceData
end

local function writePriceTable(out, prices)
	local classes = {}
	for classification in pairs(prices) do
		classes[#classes + 1] = classification
	end
	table.sort(classes)

	out:addByte(#classes)
	for _, classification in ipairs(classes) do
		local tierPrices = prices[classification]
		local tiers = {}
		for tier in pairs(tierPrices) do
			tiers[#tiers + 1] = tier
		end
		table.sort(tiers)

		out:addByte(classification)
		out:addByte(#tiers)
		for _, tier in ipairs(tiers) do
			out:addByte(tier)
			out:addU64(tierPrices[tier])
		end
	end
end

local function writeNumberMap(out, map)
	local keys = {}
	for key in pairs(map) do
		keys[#keys + 1] = key
	end
	table.sort(keys)

	out:addByte(#keys)
	for _, key in ipairs(keys) do
		out:addByte(key)
		out:addU64(map[key])
	end
end

local function writeByteMap(out, map)
	local keys = {}
	for key in pairs(map) do
		keys[#keys + 1] = key
	end
	table.sort(keys)

	out:addByte(#keys)
	for _, key in ipairs(keys) do
		out:addByte(key)
		out:addByte(map[key])
	end
end

local function writeForgeItems(out, entries)
	out:addU16(math.min(#entries, 0xFFFF))
	for i = 1, math.min(#entries, 0xFFFF) do
		local entry = entries[i]
		out:addU16(entry.itemId)
		out:addByte(entry.tier)
		out:addU16(math.min(entry.count, 0xFFFF))
		out:addByte(entry.classification)
		out:addByte(entry.category)

		local subItems = entry.subItems or {}
		local subItemIds = {}
		for itemId in pairs(subItems) do
			subItemIds[#subItemIds + 1] = itemId
		end
		table.sort(subItemIds)

		out:addU16(math.min(#subItemIds, 0xFFFF))
		for j = 1, math.min(#subItemIds, 0xFFFF) do
			local itemId = subItemIds[j]
			out:addU16(itemId)
			out:addU16(math.min(subItems[itemId], 0xFFFF))
		end
	end
end

local function sendForgeInit(player)
	if not supportsCustomNetwork(player) then
		debugForge(player, "send init blocked: not OTClient")
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_FORGE_SEND)
	out:addByte(RESPONSE_INIT)
	writePriceTable(out, FUSION_COSTS)
	writeByteMap(out, TRANSFER_CORES)
	writeNumberMap(out, CONVERGENCE_PRICES)
	writeNumberMap(out, TRANSFER_PRICES)
	out:addU16(math.floor(DUST_TO_SLIVERS / SLIVERS_GENERATED))
	out:addU16(SLIVERS_GENERATED)
	out:addU16(SLIVERS_TO_CORE)
	out:addU16(0)
	out:addU16(getForgeDustLimit(player))
	out:addU16(MAX_DUST_LIMIT)
	out:addU16(DUST_FUSION)
	out:addU16(DUST_FUSION_CONVERGENCE)
	out:addU16(DUST_TRANSFER)
	out:addU16(DUST_TRANSFER_CONVERGENCE)
	out:addByte(FUSION_SUCCESS_CHANCE)
	out:addByte(IMPROVE_SUCCESS_CHANCE)
	out:addByte(TIER_LOSS_REDUCTION)
	local sent = out:sendToPlayer(player)
	debugForge(player, "send init result=" .. tostring(sent))
	return sent
end

local function sendForgeData(player)
	if not supportsCustomNetwork(player) then
		debugForge(player, "send data blocked: not OTClient")
		return false
	end

	local fusionData, fusionConvergenceData, transferData, transferConvergenceData = buildForgeData(player)
	local dustLimit = getForgeDustLimit(player)
	local bank = getPlayerBankBalance(player)
	local inventoryMoney = getPlayerInventoryMoney(player)
	local dust = getForgeDust(player)
	local slivers = player:getItemCount(FORGE_ITEM_IDS.sliver)
	local exaltedCores = player:getItemCount(FORGE_ITEM_IDS.exaltedCore)

	debugForge(player, string.format(
		"send data balances bank=%d inventory=%d dust=%d/%d slivers=%d cores=%d fusion=%d fusionConv=%d transfer=%d transferConv=%d",
		bank,
		inventoryMoney,
		dust,
		dustLimit,
		slivers,
		exaltedCores,
		#fusionData,
		#fusionConvergenceData,
		#transferData,
		#transferConvergenceData
	))

	local out = NetworkMessage(player)
	out:addByte(OPCODE_FORGE_SEND)
	out:addByte(RESPONSE_DATA)
	out:addU16(dustLimit)
	out:addU64(bank)
	out:addU64(inventoryMoney)
	out:addU64(dust)
	out:addU64(slivers)
	out:addU64(exaltedCores)
	writeForgeItems(out, fusionData)
	writeForgeItems(out, fusionConvergenceData)
	writeForgeItems(out, transferData)
	writeForgeItems(out, transferConvergenceData)
	local sent = out:sendToPlayer(player)
	debugForge(player, "send data result=" .. tostring(sent))
	return sent
end

local function refreshForge(player)
	debugForge(player, "refresh start")
	local resourcesSent = sendAllResources(player)
	local dataSent = sendForgeData(player)
	debugForge(player, "refresh done resources=" .. tostring(resourcesSent) .. " data=" .. tostring(dataSent))
	return dataSent
end

local function addHistory(player, action, details)
	if not ensureForgeHistoryTable() then
		return
	end

	local guid = player:getGuid()
	local now = os.time()
	local historyAction = math.max(0, tonumber(action) or 0)
	db.query("INSERT INTO `player_forge_history` (`player_id`, `created_at`, `action`, `details`) VALUES (" ..
		guid .. ", " .. now .. ", " .. historyAction .. ", " .. db.escapeString(details or "") .. ")")
	db.query("DELETE FROM `player_forge_history` WHERE `player_id` = " .. guid .. " AND `id` NOT IN " ..
		"(SELECT `id` FROM (SELECT `id` FROM `player_forge_history` WHERE `player_id` = " .. guid ..
		" ORDER BY `created_at` DESC, `id` DESC LIMIT " .. FORGE_HISTORY_LIMIT .. ") AS `keep_history`)")
end

local function sendHistory(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local history = {}
	if ensureForgeHistoryTable() then
		local resultId = db.storeQuery("SELECT `created_at`, `action`, `details` FROM `player_forge_history` WHERE `player_id` = " ..
			player:getGuid() .. " ORDER BY `created_at` DESC, `id` DESC LIMIT " .. FORGE_HISTORY_LIMIT)
		if resultId ~= false then
			repeat
				history[#history + 1] = {
					result.getDataInt(resultId, "created_at"),
					result.getDataInt(resultId, "action"),
					result.getDataString(resultId, "details")
				}
			until not result.next(resultId)
			result.free(resultId)
		end
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_FORGE_SEND)
	out:addByte(RESPONSE_HISTORY)
	out:addU16(math.min(#history, 0xFFFF))
	for i = 1, math.min(#history, 0xFFFF) do
		out:addU32(history[i][1])
		out:addByte(history[i][2])
		out:addString(history[i][3])
	end
	return out:sendToPlayer(player)
end

local function sendFusionResult(player, convergence, success, otherItem, otherTier, itemId, tier, resultType, itemResult, tierResult, count)
	local out = NetworkMessage(player)
	out:addByte(OPCODE_FORGE_SEND)
	out:addByte(RESPONSE_FUSION)
	out:addByte(convergence and 1 or 0)
	out:addByte(success and 1 or 0)
	out:addU16(otherItem)
	out:addByte(otherTier)
	out:addU16(itemId)
	out:addByte(tier)
	out:addByte(resultType or 0)
	out:addU16(itemResult or 0)
	out:addByte(tierResult or 0)
	out:addU16(count or 0)
	return out:sendToPlayer(player)
end

local function sendTransferResult(player, convergence, success, otherItem, otherTier, itemId, tier)
	local out = NetworkMessage(player)
	out:addByte(OPCODE_FORGE_SEND)
	out:addByte(RESPONSE_TRANSFER)
	out:addByte(convergence and 1 or 0)
	out:addByte(success and 1 or 0)
	out:addU16(otherItem)
	out:addByte(otherTier)
	out:addU16(itemId)
	out:addByte(tier)
	return out:sendToPlayer(player)
end

local function findItems(player, itemId, tier, collectedItems)
	local result = {}
	for _, item in ipairs(collectedItems or collectForgeItems(player)) do
		if item:getId() == itemId and item:getTier() == tier then
			result[#result + 1] = item
		end
	end
	return result
end

local function findTargetItem(player, itemId, tier, sourceItem, collectedItems)
	for _, item in ipairs(collectedItems or collectForgeItems(player)) do
		if item:getId() == itemId and item:getTier() == tier and item ~= sourceItem then
			return item
		end
	end
	return nil
end

local function canPay(player, dustCost, coreCost, goldCost)
	if getForgeDust(player) < dustCost then
		return false, "You do not have enough Dust."
	end
	if player:getItemCount(FORGE_ITEM_IDS.exaltedCore) < coreCost then
		return false, "You do not have enough Exalted Cores."
	end
	if getPlayerTotalMoney(player) < goldCost then
		return false, "You do not have enough gold."
	end
	return true
end

local function takePayment(player, dustCost, coreCost, goldCost)
	if not removeForgeDust(player, dustCost) then
		return false
	end
	if coreCost > 0 and not player:removeItem(FORGE_ITEM_IDS.exaltedCore, coreCost) then
		addForgeDust(player, dustCost)
		return false
	end
	if goldCost > 0 and not player:removeMoneyBank(goldCost) then
		addForgeDust(player, dustCost)
		if coreCost > 0 then
			player:addItem(FORGE_ITEM_IDS.exaltedCore, coreCost)
		end
		return false
	end
	return true
end

local function withPlayerLock(player, callback)
	local guid = player:getGuid()
	if forgeLocks[guid] then
		sendForgeMessage(player, "Forge action already in progress.")
		return false
	end

	forgeLocks[guid] = true
	local ok, result = pcall(callback)
	forgeLocks[guid] = nil

	if not ok then
		print("[CustomForge] " .. tostring(result))
		sendForgeMessage(player, "Forge action failed.")
		refreshForge(player)
		return false
	end
	return result
end

local function handleFusion(player, msg)
	if msg:len() - msg:tell() < 8 then
		return
	end

	return withPlayerLock(player, function()
		local convergence = msg:getByte() ~= 0
		local itemId = msg:getU16()
		local tier = msg:getByte()
		local secondItemId = msg:getU16()
		local boostSuccess = msg:getByte() ~= 0
		local protectTierLoss = msg:getByte() ~= 0

		local collectedItems = collectForgeItems(player)
		local items = findItems(player, itemId, tier, collectedItems)
		local mainItem = items[1]
		local sacrificeItem = nil

		if convergence then
			sacrificeItem = findTargetItem(player, secondItemId, tier, mainItem, collectedItems)
		else
			sacrificeItem = items[2]
		end

		if not mainItem or not sacrificeItem then
			sendForgeMessage(player, "Required forge items were not found.")
			refreshForge(player)
			return false
		end
		if convergence and not sameForgeGroup(mainItem, sacrificeItem) then
			sendForgeMessage(player, "Items must have the same forge class and category.")
			refreshForge(player)
			return false
		end

		local classification = mainItem:getClassification()
		local maxTier = CLASS_MAX_TIER[classification] or 0
		if tier >= maxTier then
			sendForgeMessage(player, "This item is already at its maximum tier.")
			refreshForge(player)
			return false
		end

		local dustCost = convergence and DUST_FUSION_CONVERGENCE or DUST_FUSION
		local coreCost = (boostSuccess and 1 or 0) + (protectTierLoss and 1 or 0)
		local goldCost = convergence and (CONVERGENCE_PRICES[tier] or 0) or (FUSION_COSTS[classification] and FUSION_COSTS[classification][tier] or 0)
		local valid, message = canPay(player, dustCost, coreCost, goldCost)
		if not valid then
			sendForgeMessage(player, message)
			refreshForge(player)
			return false
		end
		if not takePayment(player, dustCost, coreCost, goldCost) then
			sendForgeMessage(player, "Could not take forge payment.")
			refreshForge(player)
			return false
		end

		local chance = FUSION_SUCCESS_CHANCE + (boostSuccess and IMPROVE_SUCCESS_CHANCE or 0)
		local success = math.random(1, 100) <= chance
		local resultTier = tier
		local otherItemId = sacrificeItem:getId()
		local otherTier = sacrificeItem:getTier()
		local resultType = 0
		local itemResult = 0
		local tierResult = 0
		local resultCount = 0
		local sacrificeResult = "destroyed"

		if success then
			resultTier = tier + 1
			mainItem:setTier(resultTier)
			sacrificeItem:remove(1)
		else
			mainItem:remove(1)
			sacrificeItem:remove(1)
			sacrificeResult = "both items destroyed"
		end

		local historyDetails
		if success then
			historyDetails = string.format("Success %d tier %d -> %d", itemId, tier, resultTier)
		else
			historyDetails = string.format("Fail %d tier %d; second item %s", itemId, tier, sacrificeResult)
		end

		addHistory(player, HISTORY_FUSION, historyDetails)
		sendFusionResult(player, convergence, success, otherItemId, otherTier, itemId, resultTier, resultType, itemResult, tierResult, resultCount)
		invalidateForgeCache(player)
		refreshForge(player)
		return true
	end)
end

local function handleTransfer(player, msg)
	if msg:len() - msg:tell() < 6 then
		return
	end

	return withPlayerLock(player, function()
		local convergence = msg:getByte() ~= 0
		local itemId = msg:getU16()
		local tier = msg:getByte()
		local targetItemId = msg:getU16()

		local collectedItems = collectForgeItems(player)
		local source = findItems(player, itemId, tier, collectedItems)[1]
		local target = findTargetItem(player, targetItemId, 0, source, collectedItems)

		if not source or not target then
			sendForgeMessage(player, "Required transfer items were not found.")
			refreshForge(player)
			return false
		end
		if not sameForgeGroup(source, target) then
			sendForgeMessage(player, "Items must have the same forge class and category.")
			refreshForge(player)
			return false
		end
		if not convergence and tier < 2 then
			sendForgeMessage(player, "Regular transfer requires a source item of tier 2 or higher.")
			refreshForge(player)
			return false
		end

		local classification = source:getClassification()
		local resultTier = convergence and tier or (tier - 1)
		local dustCost = convergence and DUST_TRANSFER_CONVERGENCE or DUST_TRANSFER
		local coreCost = convergence and (TRANSFER_CORES[tier] or 1) or (TRANSFER_CORES[tier - 1] or 1)
		local goldCost = convergence and (TRANSFER_PRICES[tier] or 0) or (FUSION_COSTS[classification] and FUSION_COSTS[classification][tier - 1] or 0)

		local valid, message = canPay(player, dustCost, coreCost, goldCost)
		if not valid then
			sendForgeMessage(player, message)
			refreshForge(player)
			return false
		end
		if not takePayment(player, dustCost, coreCost, goldCost) then
			sendForgeMessage(player, "Could not take forge payment.")
			refreshForge(player)
			return false
		end

		local sourceId = source:getId()
		local sourceTier = source:getTier()
		target:setTier(resultTier)
		source:remove(1)

		addHistory(player, HISTORY_TRANSFER, string.format("Transfer %d tier %d -> %d tier %d", sourceId, sourceTier, targetItemId, resultTier))
		sendTransferResult(player, convergence, true, sourceId, sourceTier, targetItemId, resultTier)
		invalidateForgeCache(player)
		refreshForge(player)
		return true
	end)
end

local function getDustLimitIncreaseCost(player)
	return getForgeDustLimit(player) - 75
end

local function handleConvert(player, msg)
	if msg:len() - msg:tell() < 1 then
		return
	end

	return withPlayerLock(player, function()
		local action = msg:getByte()

		if action == 2 then
			if not removeForgeDust(player, DUST_TO_SLIVERS) then
				sendForgeMessage(player, "You need more Dust to generate Slivers.")
				refreshForge(player)
				return false
			end
			local added = player:addItem(FORGE_ITEM_IDS.sliver, SLIVERS_GENERATED)
			if not added then
				addForgeDust(player, DUST_TO_SLIVERS)
				sendForgeMessage(player, "Your inventory is full.")
				refreshForge(player)
				return false
			end
			addHistory(player, HISTORY_CONVERSION, string.format("%d Dust -> %d Slivers", DUST_TO_SLIVERS, SLIVERS_GENERATED))
		elseif action == 3 then
			if not player:removeItem(FORGE_ITEM_IDS.sliver, SLIVERS_TO_CORE) then
				sendForgeMessage(player, "You need more Slivers to generate an Exalted Core.")
				refreshForge(player)
				return false
			end
			local added = player:addItem(FORGE_ITEM_IDS.exaltedCore, 1)
			if not added then
				player:addItem(FORGE_ITEM_IDS.sliver, SLIVERS_TO_CORE)
				sendForgeMessage(player, "Your inventory is full.")
				refreshForge(player)
				return false
			end
			addHistory(player, HISTORY_CONVERSION, string.format("%d Slivers -> 1 Exalted Core", SLIVERS_TO_CORE))
		elseif action == 4 then
			local limit = getForgeDustLimit(player)
			if limit >= MAX_DUST_LIMIT then
				sendForgeMessage(player, "Your Dust limit is already at maximum.")
				refreshForge(player)
				return false
			end

			local cost = getDustLimitIncreaseCost(player)
			if not removeForgeDust(player, cost) then
				sendForgeMessage(player, "You need more Dust to increase your Dust limit.")
				refreshForge(player)
				return false
			end

			player:setStorageValue(FORGE_STORAGE.dustLimit, limit + 1)
			addHistory(player, HISTORY_CONVERSION, string.format("Dust limit %d -> %d", limit, limit + 1))
		else
			sendForgeMessage(player, "Invalid forge conversion.")
			refreshForge(player)
			return false
		end

		invalidateForgeCache(player)
		refreshForge(player)
		return true
	end)
end

local function openForge(player)
	if not supportsCustomNetwork(player) then
		debugForge(player, "open blocked: not OTClient")
		return false
	end

	debugForge(player, "open start")
	invalidateForgeCache(player)
	forgeOpenSessions[player:getId()] = true
	local initSent = sendForgeInit(player)
	local dataSent = refreshForge(player)
	debugForge(player, "open done init=" .. tostring(initSent) .. " data=" .. tostring(dataSent))
	return true
end

local function closeForge(player)
	if not supportsCustomNetwork(player) then
		debugForge(player, "close blocked: not OTClient")
		return false
	end

	debugForge(player, "close")
	forgeOpenSessions[player:getId()] = nil
	invalidateForgeCache(player)
	local out = NetworkMessage(player)
	out:addByte(OPCODE_FORGE_SEND)
	out:addByte(RESPONSE_CLOSE)
	return out:sendToPlayer(player)
end

local forgeHandler = PacketHandler(OPCODE_FORGE_REQUEST)
function forgeHandler.onReceive(player, msg)
	if not supportsCustomNetwork(player) then
		debugForge(player, "packet blocked: not OTClient")
		return
	end
	if msg:len() - msg:tell() < 1 then
		debugForge(player, "packet blocked: missing action")
		return
	end

	local action = msg:getByte()
	debugForge(player, "packet action=" .. tostring(action))
	if action == REQUEST_OPEN then
		openForge(player)
	elseif action == REQUEST_CLOSE then
		closeForge(player)
	elseif action == REQUEST_FUSION then
		handleFusion(player, msg)
	elseif action == REQUEST_TRANSFER then
		handleTransfer(player, msg)
	elseif action == REQUEST_CONVERT then
		handleConvert(player, msg)
	elseif action == REQUEST_HISTORY then
		sendHistory(player)
	else
		debugForge(player, "packet ignored: unknown action=" .. tostring(action))
	end
end
forgeHandler:register()

local forgeSessionCleanup = CreatureEvent("CustomForgeSessionCleanup")
function forgeSessionCleanup.onLogout(player)
	forgeOpenSessions[player:getId()] = nil
	forgeLocks[player:getGuid()] = nil
	invalidateForgeCache(player)
	return true
end
forgeSessionCleanup:register()

local forgeSessionInit = CreatureEvent("CustomForgeSessionInit")
function forgeSessionInit.onLogin(player)
	player:registerEvent("CustomForgeSessionCleanup")
	return true
end
forgeSessionInit:register()

ensureForgeHistoryTable()

CustomForge = {
	open = openForge,
	close = closeForge,
	refresh = refreshForge,
	isOpen = function(player)
		return forgeOpenSessions[player:getId()] == true
	end
}
