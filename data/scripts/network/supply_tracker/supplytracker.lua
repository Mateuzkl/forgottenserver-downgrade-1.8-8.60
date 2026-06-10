local function getClientItemId(item)
	local itemId = item and item:getId()
	if not itemId then return 0 end
	local itemType = ItemType(itemId)
	return itemType and itemType:getClientId() or 0
end

local STORAGE_MEHAH_CLIENT = 99999 -- Must match extendedopcode.lua

function sendSupplyTracker(player, item)
    if not player or not item then return end
    if player:getStorageValue(STORAGE_MEHAH_CLIENT) ~= 1 and not player:isUsingOtClient() then return end
    
    local out = NetworkMessage(player)
    out:addByte(0xCE) -- OPCODE_SUPPLY_TRACKER
    out:addU16(getClientItemId(item))
    out:sendToPlayer(player)
end
