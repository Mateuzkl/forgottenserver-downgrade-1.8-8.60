local wheelSystemConfigKey = configKeys and configKeys.WHEEL_SYSTEM_ENABLED or WHEEL_SYSTEM_ENABLED
if wheelSystemConfigKey and not configManager.getBoolean(wheelSystemConfigKey) then
	return
end

local promotionScrolls = {
	[43946] = { name = "abridged", points = 3, itemName = "abridged promotion scroll" },
	[43947] = { name = "basic", points = 5, itemName = "basic promotion scroll" },
	[43948] = { name = "revised", points = 9, itemName = "revised promotion scroll" },
	[43949] = { name = "extended", points = 13, itemName = "extended promotion scroll" },
	[43950] = { name = "advanced", points = 20, itemName = "advanced promotion scroll" },
}

local function unlockPromotionScroll(player, scrollData)
	if player.wheelUnlockScroll then
		return player:wheelUnlockScroll(scrollData.name)
	end

	local scrolls = player:kv():scoped("wheel"):scoped("scrolls")
	if scrolls:get(scrollData.name) == true then
		return false
	end

	scrolls:set(scrollData.name, true)
	return true
end

local scroll = Action()

function scroll.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if player:getLevel() < 51 then
		player:sendTextMessage(MESSAGE_LOOK, "Only a hero of level 51 or above can decipher this scroll.")
		return true
	end

	local scrollData = promotionScrolls[item:getId()]
	if not scrollData then
		return true
	end

	if not unlockPromotionScroll(player, scrollData) then
		player:sendTextMessage(MESSAGE_LOOK, "You have already deciphered this scroll.")
		return true
	end

	player:sendTextMessage(MESSAGE_LOOK, "You have gained " .. scrollData.points .. " promotion points for the Wheel of Destiny by deciphering the " .. scrollData.itemName .. ".")
	item:remove(1)
	return true
end

for id in pairs(promotionScrolls) do
	scroll:id(id)
end

scroll:register()
