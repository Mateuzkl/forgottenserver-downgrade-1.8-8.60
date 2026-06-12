local talk = TalkAction("/clearhirelingstats")

function talk.onSay(player, words, param)
	local target = param ~= "" and Player(param) or player
	if not target then
		player:sendCancelMessage("Player not found.")
		return true
	end

	target:clearAllHirelingStats()
	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Hireling skills and outfits cleared for " .. target:getName() .. ".")
	return true
end

talk:separator(" ")
talk:access(true)
talk:register()
