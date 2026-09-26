local crusher = Action()

function crusher.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if not Player.wheelCrushGem then
		return false
	end

	if not target or type(target) ~= "userdata" or not target:isItem() then
		player:sendCancelMessage("You can only use the crusher on gems.")
		return true
	end

	local ok, result, fragmentId = player:wheelCrushGem(item, target)
	if not ok then
		player:sendCancelMessage(result or "You cannot crush this gem.")
		return true
	end

	local fragmentType = ItemType(fragmentId)
	local fragmentName = fragmentType and fragmentType:getName() or "fragments"
	player:sendTextMessage(
		MESSAGE_EVENT_ADVANCE,
		string.format("You crushed the gem and received %d %s.", result, fragmentName)
	)
	player:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
	return true
end

crusher:id(46627, 46628)
crusher:register()
