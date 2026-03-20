local talkaction = TalkAction("/autoloot", "!autoloot")
function talkaction.onSay(player, words, param)
	param = param:gsub("^%s*(.-)%s*$", "%1")

	if param == "list" then
		player:sendAutoLootWindow()
		return false
	end

	if param == "on" then
		if player.setAutoLootEnabled then
			if player:isAutoLootEnabled() then
				player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "AutoLoot is already enabled.")
			else
				player:setAutoLootEnabled(true)
				player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "AutoLoot enabled.")
			end
		else
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Update source code to use this feature.")
		end
		return false
	end

	if param == "off" then
		if player.setAutoLootEnabled then
			if not player:isAutoLootEnabled() then
				player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "AutoLoot is already disabled.")
			else
				player:setAutoLootEnabled(false)
				player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "AutoLoot disabled.")
			end
		else
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Update source code to use this feature.")
		end
		return false
	end

	if param == "clear" then
		if player.clearAutoLoot then
			player:clearAutoLoot()
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "AutoLoot list cleared.")
		else
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Update source code to use this feature.")
		end
		return false
	end

	if param == "gold" then
		if player.setAutoLootGold then
			local current = player:isAutoLootGoldEnabled()
			player:setAutoLootGold(not current)
			if not current then
				player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "AutoLoot Gold: enabled. All coins go directly to your bank account.")
			else
				player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "AutoLoot Gold: disabled.")
			end
		else
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Update source code to use this feature.")
		end
		return false
	end

	local limitFree = 5
	local limitPremium = 10
	
	if AUTOLOOT_MAXITEMS_FREE and configManager.getNumber(AUTOLOOT_MAXITEMS_FREE) > 0 then
		limitFree = configManager.getNumber(AUTOLOOT_MAXITEMS_FREE)
	elseif Autoloot_MaxItemFree then
		limitFree = Autoloot_MaxItemFree
	end
	
	if AUTOLOOT_MAXITEMS_PREMIUM and configManager.getNumber(AUTOLOOT_MAXITEMS_PREMIUM) > 0 then
		limitPremium = configManager.getNumber(AUTOLOOT_MAXITEMS_PREMIUM)
	elseif Autoloot_MaxItemPremium then
		limitPremium = Autoloot_MaxItemPremium
	end

	local usedSlots = 0
	if player.getAutoLootItemCount then
		usedSlots = player:getAutoLootItemCount()
	end

	local status = "On"
	if player.isAutoLootEnabled and not player:isAutoLootEnabled() then
		status = "Off"
	end

	local goldStatus = "Off"
	if player.isAutoLootGoldEnabled and player:isAutoLootGoldEnabled() then
		goldStatus = "On"
	end

	local text = "_________AutoLoot System_________\n\n" ..
	             "AutoLoot Status: " .. status .. "\n" ..
	             "AutoMoney Mode: Bank\n" ..
	             "AutoLoot Gold (coins -> bank): " .. goldStatus .. "\n\n" ..
	             "Commands:\n" ..
	             "!autoloot on/off\n" ..
	             "!autoloot gold  (toggle coin collection)\n" ..
	             "!autoloot list\n" ..
	             "!autoloot clear\n\n" ..
	             "--------------------------------------------------\n" ..
	             "Slots used: " .. usedSlots .. "/" .. (player:isPremium() and limitPremium or limitFree) .. "\n" ..
	             "--------------------------------------------------\n\n" ..
	             "Free Account slots: " .. limitFree .. "\n" ..
	             "Premium Account Slots: " .. limitPremium .. "\n\n" ..
				 "___________Gold Pouch___________\n\n" ..
	             "Buy: !buy gold pouch (50.000 gold)\n" ..
	             "An infinite bag to hold your loot items.\n\n" ..
	             "Free: 30 slots max (no extra pages)\n" ..
	             "VIP: Unlimited slots (infinite pages)"

	player:popupFYI(text)
	return false
end
talkaction:separator(" ")
talkaction:register()
