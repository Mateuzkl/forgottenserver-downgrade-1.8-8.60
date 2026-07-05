local COOLDOWN_TIME = 2 -- seconds

local constructionKits = Action()

function constructionKits.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	-- Cooldown check
	local cooldown = player:getStorageValue(PlayerStorageKeys.constructionCooldown)
	if cooldown > os.time() then
		local remaining = cooldown - os.time()
		player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("You must wait %d second%s before using this again.", remaining, remaining > 1 and "s" or ""))
		return true
	end

	local kit = houseAutowrapItems[item.itemid]
	if not kit then
		return false
	end

	local tile = Tile(fromPosition)
	local house = tile and tile:getHouse()
	if fromPosition.x == CONTAINER_POSITION then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Put the construction kit on the floor first.")
	elseif not house then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You may construct this only inside a house.")
	elseif not house:isInvited(player) then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You cannot modify items in another person's house.")
	elseif not house:canModifyItems(player) then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You cannot modify items in this protected house.")
	else
		-- Apply cooldown
		player:setStorageValue(PlayerStorageKeys.constructionCooldown, os.time() + COOLDOWN_TIME)

		local kitId = item.itemid
		item:transform(kit)
		-- Store the kit ID so it can be wrapped back later
		item:setAttribute("wrapid", kitId)
		fromPosition:sendMagicEffect(CONST_ME_POFF)
	end
	return true
end

for id, _ in pairs(houseAutowrapItems) do
	constructionKits:id(id)
end

constructionKits:register()

