local function setEffectsToggle(player, disabled)
	player:setEffectsDisabled(disabled)
	player:setStorageValue(STORAGE_EFFECTS_TOGGLE, disabled and 1 or 0)
end

local function setDistanceShootsToggle(player, disabled)
	player:setDistanceShootsDisabled(disabled)
	player:setStorageValue(STORAGE_DISTANCE_SHOOTS_TOGGLE, disabled and 1 or 0)
end

local effectToggle = TalkAction("!effect")
function effectToggle.onSay(player, words, param)
	local mode = param:lower():gsub("^%s*(.-)%s*$", "%1")
	local disabled = player:isEffectsDisabled()

	if mode == "off" then
		if disabled then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Magic effects are already disabled.")
			return false
		end
		setEffectsToggle(player, true)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Magic effects are now OFF.")
	elseif mode == "on" then
		if not disabled then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Magic effects are already enabled.")
			return false
		end
		setEffectsToggle(player, false)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Magic effects are now ON.")
	elseif mode == "" then
		local state = disabled and "disabled" or "enabled"
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Magic effects are currently " .. state .. ". Use !effect on or !effect off to change.")
	else
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Usage: !effect on, !effect off")
	end
	return false
end
effectToggle:separator(" ")
effectToggle:register()

local shotToggle = TalkAction("!shot")
function shotToggle.onSay(player, words, param)
	local mode = param:lower():gsub("^%s*(.-)%s*$", "%1")
	local disabled = player:isDistanceShootsDisabled()

	if mode == "off" then
		if disabled then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Distance shoot effects are already disabled.")
			return false
		end
		setDistanceShootsToggle(player, true)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Distance shoot effects are now OFF.")
	elseif mode == "on" then
		if not disabled then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Distance shoot effects are already enabled.")
			return false
		end
		setDistanceShootsToggle(player, false)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Distance shoot effects are now ON.")
	elseif mode == "" then
		local state = disabled and "disabled" or "enabled"
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Distance shoot effects are currently " .. state .. ". Use !shot on or !shot off to change.")
	else
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Usage: !shot on, !shot off")
	end
	return false
end
shotToggle:separator(" ")
shotToggle:register()

local effectToggleLogin = CreatureEvent("effectToggleLogin")

function effectToggleLogin.onLogin(player)
	local effectVal = player:getStorageValue(STORAGE_EFFECTS_TOGGLE)
	if effectVal and effectVal == 1 then
		player:setEffectsDisabled(true)
	end

	local shotVal = player:getStorageValue(STORAGE_DISTANCE_SHOOTS_TOGGLE)
	if shotVal and shotVal == 1 then
		player:setDistanceShootsDisabled(true)
	end
	return true
end

effectToggleLogin:register()
