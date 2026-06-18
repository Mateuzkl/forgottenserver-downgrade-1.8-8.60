local function setMagicWallView(player, enabled)
	player:setMagicWallOldView(enabled)
	player:setStorageValue(STORAGE_MAGIC_WALL_OLD_VIEW, enabled and 1 or 0)
end

local magicWallView = TalkAction("!mw")
function magicWallView.onSay(player, words, param)
	local mode = param:lower():gsub("^%s*(.-)%s*$", "%1")
	local enabled = player:isMagicWallOldViewEnabled()

	if mode == "on" then
		if enabled then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Magic Wall is already set to old style.")
			return false
		end
		setMagicWallView(player, true)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Magic Wall set to old style.")
	elseif mode == "off" then
		if not enabled then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Magic Wall is already set to default.")
			return false
		end
		setMagicWallView(player, false)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Magic Wall set to default.")
	elseif mode == "" then
		local style = enabled and "old" or "default"
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Magic Wall is currently set to " .. style .. ". Use !mw on or !mw off to change.")
	else
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Usage: !mw on, !mw off")
	end
	return false
end
magicWallView:separator(" ")
magicWallView:register()


local magicWallViewLogin = CreatureEvent("magicWallViewLogin")

function magicWallViewLogin.onLogin(player)
	local value = player:getStorageValue(STORAGE_MAGIC_WALL_OLD_VIEW)
	if value and value == 1 then
		player:setMagicWallOldView(true, false)
	end
	return true
end

magicWallViewLogin:register()
