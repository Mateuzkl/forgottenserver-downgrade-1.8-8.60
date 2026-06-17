local emoteSpells = TalkAction("!emotespells")

local function sendStatus(player)
	local storageValue = player:getStorageValue(STORAGE_EMOTE_SPELLS)
	local globalEnabled = configManager.getBoolean(configKeys.EMOTE_SPELLS)
	local effectiveEnabled = storageValue == 1 or (storageValue == -1 and globalEnabled)
	local personalState = storageValue == 1 and "enabled" or (storageValue == -1 and "unset" or "disabled")
	local globalState = globalEnabled and "enabled" or "disabled"

	player:sendTextMessage(
		MESSAGE_STATUS_CONSOLE_BLUE,
		string.format(
			effectiveEnabled
				and "Emote spells are currently enabled (personal: %s, global: %s), to disable them type !emotespells off."
				or "Emote spells are currently disabled (personal: %s, global: %s), to enable them type !emotespells on.",
			personalState,
			globalState
		)
	)
end

function emoteSpells.onSay(player, words, param)
	local action = param:lower():gsub("^%s*(.-)%s*$", "%1")

	if action == "on" then
		player:setStorageValue(STORAGE_EMOTE_SPELLS, 1)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Your emote spells have been enabled.")
	elseif action == "off" then
		player:setStorageValue(STORAGE_EMOTE_SPELLS, 0)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Your emote spells have been disabled.")
	elseif action == "" then
		sendStatus(player)
	else
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Usage: !emotespells [on/off]")
	end

	return false
end

emoteSpells:separator(" ")
emoteSpells:register()
