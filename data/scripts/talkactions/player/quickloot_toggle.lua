local talkaction = TalkAction("!quickloot")

function talkaction.onSay(player, words, param)
	local settings = player:kv():scoped("settings")

	if param == "on" then
		settings:set("quickLoot", true)
		player:registerEvent("QuickLootKill")
		player:sendTextMessage(MESSAGE_INFO_DESCR, "QuickLoot auto-loot has been enabled.")
	elseif param == "off" then
		settings:set("quickLoot", false)
		player:unregisterEvent("QuickLootKill")
		player:sendTextMessage(MESSAGE_INFO_DESCR, "QuickLoot auto-loot has been disabled.")
	else
		local current = settings:get("quickLoot")
		player:sendTextMessage(MESSAGE_INFO_DESCR, "QuickLoot is currently " .. (current and "enabled" or "disabled") .. ". Use !quickloot on/off to toggle.")
	end
	return true
end

talkaction:separator(" ")
talkaction:register()
