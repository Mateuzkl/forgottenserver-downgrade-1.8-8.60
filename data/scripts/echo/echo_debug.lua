local echoCommand = TalkAction("/echo")

function echoCommand.onSay(player, words, param)
	logCommand(player, words, param)
	local success, message = Game.echoRaidCommand(player, param)
	player:sendTextMessage(success and MESSAGE_EVENT_ADVANCE or MESSAGE_STATUS_SMALL, message)
	return false
end

echoCommand:separator(" ")
echoCommand:accountType(ACCOUNT_TYPE_GOD)
echoCommand:access(true)
echoCommand:register()
