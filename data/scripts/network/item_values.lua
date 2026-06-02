local OPCODE_ITEM_VALUES = 0x2E

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function sendItemValues(playerId)
	local player = Player(playerId)
	if not supportsCustomNetwork(player) or not Game.getItemPrices then
		return
	end
	if not configManager.getBoolean(configKeys.COLORIZED_LOOT_VALUE) then
		return
	end

	local entries = {}
	for itemId, value in pairs(Game.getItemPrices()) do
		value = tonumber(value) or 0
		if value > 0 then
			entries[#entries + 1] = { itemId = tonumber(itemId) or 0, value = value }
		end
	end

	if #entries == 0 then
		return
	end

	table.sort(entries, function(a, b) return a.itemId < b.itemId end)

	local msg = NetworkMessage(player)
	msg:addByte(OPCODE_ITEM_VALUES)
	msg:addU16(math.min(#entries, 0xFFFF))
	for i = 1, math.min(#entries, 0xFFFF) do
		msg:addU16(entries[i].itemId)
		msg:addU32(math.min(entries[i].value, 0xFFFFFFFF))
	end
	msg:sendToPlayer(player)
end

local loginEvent = CreatureEvent("ItemValuesLogin")

function loginEvent.onLogin(player)
	addEvent(sendItemValues, 1000, player:getId())
	return true
end

loginEvent:register()
