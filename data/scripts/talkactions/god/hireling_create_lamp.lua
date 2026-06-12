local talk = TalkAction("/hireling")

function talk.onSay(player, words, param)
	if not configManager.getBoolean(configKeys.HIRELING_SYSTEM_ENABLED) then
		player:sendCancelMessage("Hireling system is disabled.")
		return true
	end

	local split = param:splitTrimmed(",")
	local name = split[1] and split[1] ~= "" and split[1] or player:getName()
	local sex = tonumber(split[2]) or HIRELING_SEX.MALE
	local hireling, err = player:addNewHireling(name, sex)
	if not hireling then
		player:sendCancelMessage(err or "Failed to create hireling.")
		return true
	end

	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Hireling lamp created in your Store Inbox.")
	return true
end

talk:separator(" ")
talk:access(true)
talk:register()
