local talkaction = TalkAction("/creaturestats")

function talkaction.onSay(player, words, param)
	local report = getCreatureSchedulerStats()
	for line in report:gmatch("[^\n]+") do
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, line)
	end
	print(report)
	return false
end

talkaction:access(true)
talkaction:register()
