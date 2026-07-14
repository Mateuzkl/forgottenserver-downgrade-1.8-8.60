-- data/scripts/network/huntanalyzer.lua

local STORAGE_MEHAH_CLIENT = 99999 -- Must match extendedopcode.lua
local HUNT_ANALYZER_DROP_TRIGGER = 100

local function isItemStackable(itemId)
	local itemType = ItemType(itemId)
	if not itemType then return false end
	return itemType:isStackable() or itemType:isFluidContainer()
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
			local clientId = ItemType(item:getId()):getClientId()
			out:addU16(clientId)
			if isItemStackable(item:getId()) then
				out:addByte(math.min(math.max(item:getCount(), 0), 255))
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

	player:updateKillTracker(monster, corpse)

	-- Astra reads loot from the C++-decoded 0xD1 batch. Keep the legacy
	-- per-item 0xCF stream only for older OTClient variants.
	if not (player.isUsingAstraClient and player:isUsingAstraClient()) then
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
