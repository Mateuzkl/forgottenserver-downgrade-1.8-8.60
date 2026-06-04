local apply = PacketHandler(0xD5)

function apply.onReceive(player, msg)
	if not NetworkGuard.cooldown(player, "imbuing-apply", 500) then
		return
	end
	if not NetworkGuard.canRead(msg, 6) then
		return
	end

	local slot = NetworkGuard.readByte(msg)
	local imbuementId = NetworkGuard.readU32(msg)
	NetworkGuard.readByte(msg) -- legacy protection flag
	if slot == nil or not imbuementId then
		return
	end

	ImbuingWindow.apply(player, slot, imbuementId)
end

apply:register()

local clear = PacketHandler(0xD6)

function clear.onReceive(player, msg)
	if not NetworkGuard.cooldown(player, "imbuing-clear", 500) then
		return
	end

	local slot = NetworkGuard.readByte(msg)
	if slot == nil then
		return
	end

	ImbuingWindow.clear(player, slot)
end

clear:register()

local close = PacketHandler(0xD7)

function close.onReceive(player, msg)
	if not NetworkGuard.cooldown(player, "imbuing-close", 250) then
		return
	end
	ImbuingWindow.close(player)
end

close:register()

local action = PacketHandler(0xB2)

local function getItemFromSelection(player, position, itemId, stackpos)
	if not position then
		return nil
	end

	local item
	if position.x == 0xFFFF then
		if position.y >= 64 then
			local container = player:getContainerById(position.y - 64)
			item = container and container:getItem(position.z) or nil
		else
			item = player:getSlotItem(position.y)
		end
	else
		local tile = Tile(position)
		local thing = tile and tile:getThing(stackpos)
		if thing then
			if thing.isItem and thing:isItem() then
				item = thing
			elseif thing.getItem then
				item = thing:getItem()
			end
		end
	end

	if item and item:getId() == itemId then
		return item
	end
	return nil
end

function action.onReceive(player, msg)
	if not NetworkGuard.cooldown(player, "imbuing-action", 250) then
		return
	end

	local actionType = NetworkGuard.readByte(msg)
	if not actionType then
		return
	end

	if actionType == 1 then
		if not NetworkGuard.canRead(msg, 8) then
			return
		end

		local position = NetworkGuard.readPosition(msg)
		local itemId = NetworkGuard.readU16(msg)
		local stackpos = NetworkGuard.readByte(msg)
		if not position or not itemId or stackpos == nil then
			return
		end

		local item = getItemFromSelection(player, position, itemId, stackpos)
		if item then
			ImbuingWindow.openItem(player, item, false)
		else
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "Select an item with imbuement slots from your backpack.")
		end
	elseif actionType == 2 then
		ImbuingWindow.openScroll(player, true)
	end
end

action:register()
