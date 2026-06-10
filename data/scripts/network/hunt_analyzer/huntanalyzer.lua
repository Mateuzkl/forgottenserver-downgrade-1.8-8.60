-- data/scripts/network/huntanalyzer.lua

local OPCODE_KILL_TRACKER = 0xD1
local STORAGE_MEHAH_CLIENT = 99999 -- Must match extendedopcode.lua
local MAX_ITEMS_PER_CONTAINER = 255
local HUNT_ANALYZER_DROP_TRIGGER = 100

-- Payload: monster string, outfit bytes, then recursive corpse item records.
local function clampByte(value)
	return math.min(math.max(tonumber(value) or 0, 0), 255)
end

local function clampU16(value)
	return math.min(math.max(tonumber(value) or 0, 0), 65535)
end

local function isItemStackable(itemId)
	local itemType = ItemType(itemId)
	if not itemType then return false end
	return itemType:isStackable() or itemType:isFluidContainer()
end

local function writeOutfit(out, monsterType)
	local outfit = monsterType:outfit()
	out:addU16(outfit.lookType or 0)
	out:addByte(outfit.lookHead or 0)
	out:addByte(outfit.lookBody or 0)
	out:addByte(outfit.lookLegs or 0)
	out:addByte(outfit.lookFeet or 0)
	out:addByte(outfit.lookAddons or 0)
end

local itemTypeClientIdCache = {}

local function getClientItemId(item)
	local itemId = item and item:getId()
	if not itemId then
		return 0
	end

	local cached = itemTypeClientIdCache[itemId]
	if cached ~= nil then
		return cached
	end

	local itemType = ItemType(itemId)
	local clientId = itemType and itemType:getClientId() or 0
	itemTypeClientIdCache[itemId] = clientId
	return clientId
end

local function writeContainerItems(out, container, depth)
	depth = depth or 1
	if depth > 4 then
		out:addByte(0)
		return
	end

	local items = container and container:getItems(false) or {}
	local itemCount = math.min(#items, MAX_ITEMS_PER_CONTAINER)
	out:addByte(itemCount)

	for index = 1, itemCount do
		local item = items[index]
		out:addU16(getClientItemId(item))

		local childContainer = item and item:getContainer()
		if childContainer then
			writeContainerItems(out, childContainer, depth + 1)
		else
			out:addByte(clampByte(item and item:getCount() or 0))
			out:addU16(clampU16(item and item.getDefaultPrice and item:getDefaultPrice() or item and item:getWorth() or 0))
			out:addString(item and item:getName() or "")
		end
	end
end

local function sendLootStatsRecursive(player, container)
	local items = container and container:getItems(false) or {}
	for _, item in ipairs(items) do
		local childContainer = item:getContainer()
		if childContainer then
			sendLootStatsRecursive(player, childContainer)
		else
			local out = NetworkMessage(player)
			out:addByte(0xCF) -- OPCODE_LOOT_TRACKER
			local clientId = getClientItemId(item)
			out:addU16(clientId)
			if isItemStackable(item:getId()) then
				out:addByte(clampByte(item:getCount()))
			end
			out:addString(item:getName() or "")
			out:sendToPlayer(player)
		end
	end
end

local function supportsHuntAnalyzer(player)
	if player:getStorageValue(STORAGE_MEHAH_CLIENT) == 1 then
		return true
	end
	if player.isUsingOtcV8 and player:isUsingOtcV8() then
		return true
	end
	return player.isUsingOtClient and player:isUsingOtClient()
end

local function sendHuntAnalyzerKill(player, monster, corpse)
	if not player or not supportsHuntAnalyzer(player) then
		return
	end

	local monsterType = monster:getType()
	if not monsterType then
		return
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_KILL_TRACKER)
	out:addString(monsterType:getName())
	writeOutfit(out, monsterType)
	writeContainerItems(out, corpse)
	out:sendToPlayer(player)

	if player:getStorageValue(STORAGE_MEHAH_CLIENT) == 1 or supportsHuntAnalyzer(player) then
		sendLootStatsRecursive(player, corpse)
	end
end

local _receivers = {}
local _seen = {}

local function getHuntAnalyzerReceivers(owner)
	for k in pairs(_seen) do
		_seen[k] = nil
	end

	local count = 0

	local function addReceiver(player)
		if player and not _seen[player:getId()] then
			_seen[player:getId()] = true
			count = count + 1
			_receivers[count] = player
		end
	end

	addReceiver(owner)

	local party = owner:getParty()
	if party then
		addReceiver(party:getLeader())
		for _, member in ipairs(party:getMembers()) do
			addReceiver(member)
		end
	end

	for index = count + 1, #_receivers do
		_receivers[index] = nil
	end

	return _receivers, count
end

local huntAnalyzerDrop = Event()
function huntAnalyzerDrop.onDropLoot(monster, corpse)
	local owner = Player(corpse:getCorpseOwner())
	if not owner then
		return
	end

	local receivers, count = getHuntAnalyzerReceivers(owner)
	for index = 1, count do
		sendHuntAnalyzerKill(receivers[index], monster, corpse)
	end
end

huntAnalyzerDrop:register(HUNT_ANALYZER_DROP_TRIGGER)
