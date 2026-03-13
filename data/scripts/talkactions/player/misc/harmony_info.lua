local harmonyInfo = TalkAction("!harmony")

function harmonyInfo.onSay(player, words, param)
	local vocId = player:getVocation():getId()
	if vocId ~= 9 and vocId ~= 10 then
		player:sendCancelMessage("Only Monks can use this command.")
		return false
	end

	local points = getHarmonyPoints(player)
	local virtue = getVirtueName(player)
	local serene = player:isSerene()
	local bonus = getHarmonyBonus(player)
	local avatar = isAvatarActive(player)

	local sereneStr = serene and "Active" or "Inactive"
	local avatarStr = avatar and "Active" or "Inactive"

	local msg = string.format(
		"===== [ Monk Harmony ] =====\n" ..
		"  Harmony: %d/%d\n" ..
		"  Active Virtue: %s\n" ..
		"  Serene State: %s\n" ..
		"  Avatar of Balance: %s\n" ..
		"  Current Bonus: +%d%%\n" ..
		"============================",
		points, HARMONY_MAX, virtue, sereneStr, avatarStr, bonus
	)

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, msg)
	return false
end

harmonyInfo:separator(" ")
harmonyInfo:register()
