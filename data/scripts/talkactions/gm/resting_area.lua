local talk = TalkAction("/restingarea")

local STREAK_STORAGE = PlayerStorageKeys.dailyRewardStreak
local FOOD_SECONDS = 20 * 60

local function getStorageNumber(player, key, default)
	local value = tonumber(player:getStorageValue(key)) or -1
	if value < 0 then
		return default or 0
	end
	return value
end

local function sendUsage(player)
	player:sendTextMessage(MESSAGE_INFO_DESCR, "Usage:")
	player:sendTextMessage(MESSAGE_INFO_DESCR, "/restingarea info")
	player:sendTextMessage(MESSAGE_INFO_DESCR, "/restingarea refresh")
	player:sendTextMessage(MESSAGE_INFO_DESCR, "/restingarea feed")
	player:sendTextMessage(MESSAGE_INFO_DESCR, "/restingarea streak,<1-7>[,playerName]")
end

local function resolveTarget(player, targetName)
	if not targetName or targetName == "" then
		return player
	end
	return Player(targetName) or nil
end

local function showInfo(player, target)
	local streak = getStorageNumber(target, STREAK_STORAGE, 0)
	local effectiveStreak = RestingAreaSystem and RestingAreaSystem.getEffectiveStreak and
		RestingAreaSystem.getEffectiveStreak(target) or streak
	local inRestingArea = RestingAreaSystem and RestingAreaSystem.isRestingArea(target:getPosition()) or false
	local message = RestingAreaSystem and RestingAreaSystem.getMessage and RestingAreaSystem.getMessage(target) or "-"
	local vocation = target:getVocation()
	local hpInfo = "-"
	local manaInfo = "-"
	if vocation and effectiveStreak >= 2 then
		hpInfo = string.format("%d HP every %ds", vocation:getHealthGainAmount(), vocation:getHealthGainTicks())
		if effectiveStreak >= 5 then
			hpInfo = hpInfo .. " (x2)"
		end
	end
	if vocation and effectiveStreak >= 3 then
		manaInfo = string.format("%d mana every %ds", vocation:getManaGainAmount(), vocation:getManaGainTicks())
		if effectiveStreak >= 6 then
			manaInfo = manaInfo .. " (x2)"
		end
	end

	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Target: %s", target:getName()))
	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Streak: %d (effective: %d)", streak, effectiveStreak))
	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Premium: %s", target:isPremium() and "yes" or "no"))
	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("In resting area (PZ): %s", inRestingArea and "yes" or "no"))
	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Passive HP regen: %s", hpInfo))
	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Passive mana regen: %s", manaInfo))
	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Client message: %s", message))
end

function talk.onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	if not RestingAreaSystem then
		player:sendCancelMessage("RestingAreaSystem is not loaded.")
		return false
	end

	if param == "" then
		sendUsage(player)
		showInfo(player, player)
		return false
	end

	local split = param:splitTrimmed(",")
	local action = (split[1] or ""):lower()

	if action == "info" then
		local target = resolveTarget(player, split[2])
		if not target then
			player:sendCancelMessage("Player not found.")
			return false
		end
		showInfo(player, target)
		return false
	end

	if action == "refresh" then
		local target = resolveTarget(player, split[2])
		if not target then
			player:sendCancelMessage("Player not found.")
			return false
		end
		RestingAreaSystem.refresh(target)
		player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Resting area status refreshed for %s.", target:getName()))
		showInfo(player, target)
		return false
	end

	if action == "feed" then
		local target = resolveTarget(player, split[2])
		if not target then
			player:sendCancelMessage("Player not found.")
			return false
		end
		target:feed(FOOD_SECONDS)
		player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Applied %d seconds of food regeneration to %s.", FOOD_SECONDS, target:getName()))
		showInfo(player, target)
		return false
	end

	if action == "streak" then
		local streak = tonumber(split[2])
		if not streak or streak < 0 or streak > 99 then
			player:sendCancelMessage("Streak must be a number between 0 and 99.")
			return false
		end

		local target = resolveTarget(player, split[3])
		if not target then
			player:sendCancelMessage("Player not found.")
			return false
		end

		target:setStorageValue(STREAK_STORAGE, streak)
		if target.saveDailyReward then
			target:saveDailyReward()
		end
		RestingAreaSystem.refresh(target)
		player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Set daily reward streak to %d for %s.", streak, target:getName()))
		showInfo(player, target)
		return false
	end

	sendUsage(player)
	return false
end

talk:separator(" ")
talk:register()
