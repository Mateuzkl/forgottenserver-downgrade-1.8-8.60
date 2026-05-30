local talkaction = TalkAction("/roll")

-- =============================================================================
-- /roll [rare|epic|legendary]
-- Rolls rarity on the item in front of the player.
-- Without arg: natural roll. With arg: forces that tier.
-- =============================================================================
function talkaction.onSay(player, words, param)
	if not RARITY_SYSTEM_ENABLED then
		player:sendCancelMessage("Rarity system is not enabled.")
		return false
	end

	local forcedTier = nil
	if param and param ~= "" then
		local tier = param:lower():trim()
		if tier == "rare" or tier == "epic" or tier == "legendary" then
			forcedTier = tier
		else
			player:sendCancelMessage("Invalid tier. Use: /roll [rare|epic|legendary]")
			return false
		end
	end

	local pos = player:getPosition()
	pos:getNextPosition(player:getDirection())

	local tile = Tile(pos)
	if not tile then
		player:sendCancelMessage("No tile in front of you.")
		return false
	end

	local item = tile:getTopVisibleThing(player)
	if not item or not item:isItem() then
		player:sendCancelMessage("No item in front of you.")
		return false
	end

	local itemObj = Item(item:getId())
	if not itemObj then
		player:sendCancelMessage("Cannot find item.")
		return false
	end

	local tier = rollRarity(itemObj, forcedTier, 0)
	if tier > 0 then
		local tierNames = { "rare", "epic", "legendary" }
		pos:sendMagicEffect(CONST_ME_MAGIC_GREEN)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
			string.format("Rolled %s rarity on %s.", tierNames[tier], itemObj:getNameDescription()))
	else
		player:sendCancelMessage("Failed to roll rarity on this item (ineligible or roll missed).")
	end

	return false
end

talkaction:separator(" ")
talkaction:accountType(6)
talkaction:access(true)
talkaction:register()
