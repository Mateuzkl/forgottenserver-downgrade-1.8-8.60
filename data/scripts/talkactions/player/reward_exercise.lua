local config = {
	cooldown = 24 * 60 * 60, -- 24 hours in seconds
	charges = 3000,          -- Amount of charges the weapon will have
	weapons = {
		[1] = 28545, -- Sorcerer: Exercise Wand
		[2] = 28544, -- Druid: Exercise Rod
		[3] = 28543, -- Paladin: Exercise Bow
		[4] = 28540, -- Knight: Exercise Sword
		[5] = 28545, -- Master Sorcerer
		[6] = 28544, -- Elder Druid
		[7] = 28543, -- Royal Paladin
		[8] = 28540, -- Elite Knight
		[9] = 50292, -- Monk: Exercise Fist
		[10] = 50292 -- Exalted Monk: Exercise Fist
	}
}

local rewardAction = TalkAction("!reward")
function rewardAction.onSay(player, words, param)
	if param == "" or param:lower() ~= "exercise" then
		player:sendCancelMessage("Command syntax: !reward exercise")
		return false
	end

	local storage = PlayerStorageKeys.rewardExercise or 90705
	local lastUse = player:getStorageValue(storage) or -1
	if lastUse > os.time() then
		local timeLeft = lastUse - os.time()
		local hours = math.floor(timeLeft / 3600)
		local minutes = math.floor((timeLeft % 3600) / 60)
		player:sendCancelMessage(string.format("You need to wait %d hours and %d minutes to claim your next daily exercise weapon.", hours, minutes))
		return false
	end

	local vocId = player:getVocation():getId()
	local weaponId = config.weapons[vocId]
	
	if not weaponId then
		player:sendCancelMessage("Your vocation cannot claim an exercise weapon.")
		return false
	end

	local storage = PlayerStorageKeys.rewardExercise or 90705
	local item = player:addItem(weaponId, 1)
	if item then
		item:setAttribute(ITEM_ATTRIBUTE_CHARGES, config.charges)
		player:setStorageValue(storage, os.time() + config.cooldown)
		player:getPosition():sendMagicEffect(CONST_ME_FIREWORK_YELLOW)
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You have claimed your daily reward! Received an exercise weapon with 3000 charges.")
	else
		player:sendCancelMessage("You do not have enough space in your inventory to receive the reward.")
	end

	return false
end
rewardAction:separator(" ")
rewardAction:register()
