local STORAGE_RESET_LASTTIME = 91000
local RESET_KICK_DELAY = 3000
local RESET_KICK_DELAY_SECONDS = math.floor(RESET_KICK_DELAY / 1000)

local function scheduleResetKick(player)
	local playerId = player:getId()
	addEvent(function(id)
		local onlinePlayer = Player(id)
		if onlinePlayer then
			onlinePlayer:remove()
		end
	end, RESET_KICK_DELAY, playerId)
end

function doPlayerReset(player)
	local currentResets = player:getResetCount()
	local maxResets = ResetBonusConfig.maxResets

	if maxResets > 0 and currentResets >= maxResets then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You reached the maximum resets (" .. maxResets .. ").")
		return false
	end

	if ResetBonusConfig.resetCooldown > 0 then
		local lastReset = player:getStorageValue(STORAGE_RESET_LASTTIME)
		local now = os.time()
		if lastReset > 0 and (now - lastReset) < ResetBonusConfig.resetCooldown then
			local remaining = ResetBonusConfig.resetCooldown - (now - lastReset)
			local hours = math.floor(remaining / 3600)
			local mins = math.floor((remaining % 3600) / 60)
			local secs = remaining % 60
			player:sendTextMessage(MESSAGE_EVENT_ADVANCE,
				string.format("Wait %02d:%02d:%02d to reset again.", hours, mins, secs))
			return false
		end
	end

	local isVip = player:isPremium()
	local requiredLevel = ResetLevelTable.getRequiredLevel(currentResets, isVip)
	if player:getLevel() < requiredLevel then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE,
			"You need level " .. requiredLevel ..
			" to reset " .. (currentResets + 1) ..
			(isVip and " (VIP)" or " (Free)") .. ".")
		return false
	end

	-- A reset restarts the progression axis, unlike dying: the highest-level
	-- watermark and the skill tree allocation both go with it. This has to happen
	-- before the level drops, or the login backfill would rebuild the watermark
	-- from the pre-reset level.
	player:clearHighestLevel()
	if SkillTreeSystem and SkillTreeSystem.clearProfile then
		SkillTreeSystem.clearProfile(player)
	end

	local resetLevel = ResetBonusConfig.resetLevel
	local newExp = Game.getExperienceForLevel(resetLevel)

	local currentExp = player:getExperience()
	if currentExp > newExp then
		player:removeExperience(currentExp - newExp)
	elseif currentExp < newExp then
		player:addExperience(newExp - currentExp)
	end

	player:addResetCount(1)
	player:addHealth(player:getMaxHealth())
	player:addMana(player:getMaxMana())

	if ResetBonusConfig.resetCooldown > 0 then
		player:setStorageValue(STORAGE_RESET_LASTTIME, os.time())
	end

	ResetBonusConfig.applyBonuses(player)

	local newResets = player:getResetCount()
	local resetText = newResets == 1 and "reset" or "resets"
	player:sendTextMessage(MESSAGE_EVENT_ADVANCE,
		"Congratulations. Your character has been reset successfully. You now have " .. newResets .. " " .. resetText .. ". " ..
		"You will be disconnected in " .. RESET_KICK_DELAY_SECONDS .. " seconds so the changes can take effect. Please log in again.")

	scheduleResetKick(player)

	return true
end
