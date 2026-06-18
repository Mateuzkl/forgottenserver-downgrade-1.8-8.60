local function isCleaveSystemEnabled()
	if configManager and configKeys and configKeys.CLEAVE_SYSTEM_ENABLED then
		return configManager.getBoolean(configKeys.CLEAVE_SYSTEM_ENABLED)
	end
	return false
end

local cleaveTalk = TalkAction("!cleave")

function cleaveTalk.onSay(player, words, param)
	if not isCleaveSystemEnabled() then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Cleave system is not enabled on this server.")
		return true
	end

	local settings = player:kv():scoped("settings")

	param = param:trim():lower()
	if param == "on" then
		settings:set("cleaveSystem", true)
		player:resetCachedSettings()
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Cleave system enabled.")
	elseif param == "off" then
		settings:set("cleaveSystem", false)
		player:resetCachedSettings()
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Cleave system disabled.")
	else
		local enabled = settings:get("cleaveSystem")
		if enabled == nil then
			enabled = true
		end
		local stateText = (enabled == true) and "enabled" or "disabled"
		player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Cleave system: %s. Use !cleave on or !cleave off.", stateText))
	end
	return true
end

cleaveTalk:separator(" ")
cleaveTalk:register()
