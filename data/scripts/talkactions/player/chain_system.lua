local chainStorage = 40001

local function isChainSystemEnabled()
	if ChainSystem and ChainSystem.enabled ~= nil then
		return ChainSystem.enabled
	end

	if configManager and configKeys then
		if configKeys.CHAIN_SYSTEM_ENABLED then
			return configManager.getBoolean(configKeys.CHAIN_SYSTEM_ENABLED)
		end

		if configKeys.TOGGLE_CHAIN_SYSTEM then
			return configManager.getBoolean(configKeys.TOGGLE_CHAIN_SYSTEM)
		end
	end

	return true
end

local chainSystem = TalkAction("!chain")

function chainSystem.onSay(player, words, param)
	if not isChainSystemEnabled() then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Chain system is not enabled on this server.")
		return true
	end

	local settings = player:kv():scoped("settings")

	param = param:trim():lower()
	if param == "on" then
		settings:set("chainSystem", true)
		player:resetCachedSettings()
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Chain system enabled.")
	elseif param == "off" then
		settings:set("chainSystem", false)
		player:resetCachedSettings()
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Chain system disabled.")
	else
		local enabled = settings:get("chainSystem")
		if enabled == nil then
			enabled = player:getStorageValue(chainStorage) == 1
			settings:set("chainSystem", enabled)
			player:resetCachedSettings()
		end
		local stateText = (enabled == true) and "enabled" or "disabled"
		player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Chain system: %s. Use !chain on or !chain off.", stateText))
	end
	return true
end

chainSystem:separator(" ")
chainSystem:register()
