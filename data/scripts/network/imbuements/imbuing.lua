local apply = PacketHandler(0xD5)

function apply.onReceive(player, msg)
	local slot = msg:getByte()
	local imbuementId = msg:getU32()
	msg:getByte() -- legacy protection flag
	ImbuingWindow.apply(player, slot, imbuementId)
end

apply:register()

local clear = PacketHandler(0xD6)

function clear.onReceive(player, msg)
	ImbuingWindow.clear(player, msg:getByte())
end

clear:register()

local close = PacketHandler(0xD7)

function close.onReceive(player, msg)
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
	local actionType = msg:getByte()
	if actionType == 1 then
		local position = msg:getPosition()
		local itemId = msg:getU16()
		local stackpos = msg:getByte()
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
