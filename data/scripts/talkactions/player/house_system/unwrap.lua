local COOLDOWN_TIME = 2 -- seconds

local unwrapCommand = TalkAction("!unwrap")

function unwrapCommand.onSay(player, words, param)
	-- Cooldown check
	local cooldown = player:getStorageValue(PlayerStorageKeys.constructionCooldown)
	if cooldown > os.time() then
		local remaining = cooldown - os.time()
		player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("You must wait %d second%s before using this command again.", remaining, remaining > 1 and "s" or ""))
		return false
	end

	local position = player:getPosition()
	local tile = position:getTile()

	if not tile or not tile:getHouse() then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You may only unwrap items inside a house.")
		return false
	end
	local house = tile:getHouse()

	if not house:isInvited(player) then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You cannot modify items in another person's house.")
		return false
	end

	if not house:canModifyItems(player) then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You cannot modify items in this protected house.")
		return false
	end

	local lookPosition = position
	local dir = player:getDirection()
	if dir == DIRECTION_NORTH then
		lookPosition.y = lookPosition.y - 1
	elseif dir == DIRECTION_SOUTH then
		lookPosition.y = lookPosition.y + 1
	elseif dir == DIRECTION_EAST then
		lookPosition.x = lookPosition.x + 1
	elseif dir == DIRECTION_WEST then
		lookPosition.x = lookPosition.x - 1
	end

	local lookTile = Tile(lookPosition)
	if not lookTile then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "No items found in front of you.")
		return false
	end

	if lookTile:getHouse() ~= house then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You may only unwrap items inside your current house.")
		return false
	end

	local items = lookTile:getItems()
	if not items then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "No items found in front of you.")
		return false
	end

	for _, item in pairs(items) do
		local itemId = item:getId()
		local itemType = item:getType()

		local constructedId = item:getAttribute("wrapid")
		if not constructedId or constructedId == 0 then
			constructedId = houseAutowrapItems[itemId]
		end

		if constructedId and constructedId ~= 0 then
			-- Apply cooldown
			player:setStorageValue(PlayerStorageKeys.constructionCooldown, os.time() + COOLDOWN_TIME)

			item:transform(constructedId)
			item:setAttribute("wrapid", itemId) -- Store the kit ID on the furniture
			lookPosition:sendMagicEffect(CONST_ME_POFF)
			player:sendTextMessage(MESSAGE_INFO_DESCR, "Item placed successfully.")
			return false
		end

		-- Check if it's already constructed (has wrapid attribute OR wrapableto in ItemType)
		local wrapId = item:getAttribute("wrapid")
		if not wrapId or wrapId == 0 then
			wrapId = itemType:getWrapableTo()
		end

		if wrapId and wrapId ~= 0 then
			-- Apply cooldown
			player:setStorageValue(PlayerStorageKeys.constructionCooldown, os.time() + COOLDOWN_TIME)

			local furnitureId = item:getId()
			item:transform(wrapId)
			item:setAttribute("wrapid", furnitureId) -- Store the furniture ID on the kit
			lookPosition:sendMagicEffect(CONST_ME_POFF)
			player:sendTextMessage(MESSAGE_INFO_DESCR, "Item wrapped successfully.")
			return false
		end
	end

	player:sendTextMessage(MESSAGE_INFO_DESCR, "No wrappable items found in front of you.")
	return false
end

unwrapCommand:separator(" ")
unwrapCommand:register()

