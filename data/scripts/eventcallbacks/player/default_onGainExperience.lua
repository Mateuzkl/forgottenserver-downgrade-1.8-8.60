local soulCondition = Condition(CONDITION_SOUL, CONDITIONID_DEFAULT)
soulCondition:setTicks(4 * 60 * 1000)
soulCondition:setParameter(CONDITION_PARAM_SOULGAIN, 1)

local EXP_COLOR_STORAGE = STORAGE_EXP_COLOR or PlayerStorageKeys.expColor or 50100

local function getAnimatedExpText(expValue)
	if configManager.getBoolean(configKeys.MODIFY_EXP_IN_K) then
		return Game.formatValueK(expValue)
	end
	return tostring(expValue)
end

local function getAnimatedExpColor(player)
	if not configManager.getBoolean(configKeys.MODIFY_EXP_IN_K) then
		return TEXTCOLOR_WHITE
	end

	local storedColor = player:getStorageValue(EXP_COLOR_STORAGE)
	if storedColor and storedColor > 0 then
		return storedColor
	end
	return configManager.getNumber(configKeys.DEFAULT_EXP_COLOR)
end

local function getExperienceText(expValue)
	local value = tostring(expValue)
	if configManager.getBoolean(configKeys.MODIFY_EXP_IN_K) then
		value = Game.formatValueK(expValue)
	end
	return value .. (expValue ~= 1 and " experience points" or " experience point")
end

local event = Event()

function event.onGainExperience(player, source, exp, rawExp, sendText)
	if not source or source:isPlayer() then return exp end

	-- Soul regeneration
	local vocation = player:getVocation()
	if player:getSoul() < vocation:getMaxSoul() and exp >= player:getLevel() then
		soulCondition:setParameter(CONDITION_PARAM_SOULTICKS, vocation:getSoulGainTicks() * 1000)
		player:addCondition(soulCondition)
	end

	-- Apply experience stage multiplier
	local stage = Game.getExperienceStage(player:getLevel())
	exp = exp * stage

	-- Stamina modifier
	player:updateStamina()

	-- Experience Rates
	local staminaRate = player:getExperienceRate(ExperienceRateType.STAMINA)
	if staminaRate ~= 100 then exp = exp * staminaRate / 100 end

	local baseRate = player:getExperienceRate(ExperienceRateType.BASE)
	if baseRate ~= 100 then exp = exp * baseRate / 100 end

	local lowLevelRate = player:getExperienceRate(ExperienceRateType.LOW_LEVEL)
	if lowLevelRate ~= 100 then exp = exp * lowLevelRate / 100 end

	if player.getXpBoostTime and player:getXpBoostTime() > 0 and player:getStamina() > 840 then
		local xpBoostPercent = player:getXpBoostPercent()
		if xpBoostPercent > 0 then
			exp = exp * (100 + xpBoostPercent) / 100
		end
	end

	local bonusRate = player:getExperienceRate(ExperienceRateType.BONUS)
	if bonusRate ~= 100 then exp = exp * bonusRate / 100 end

	if PreySystem and source and source:isMonster() then
		local bonusType, bonusValue = PreySystem.getBonus(player, source:getName())
		if bonusType == PreySystem.BONUS_XP then
			exp = exp + math.floor(exp * bonusValue / 100)
		end
	end

	-- Influenced Monster Multiplier
	if source and source:isMonster() and source:isInfluenced() then
		local level = source:getInfluencedLevel()
		local multipliers = {2, 4, 6, 8, 10}
		local mult = multipliers[level] or 1
		exp = exp * mult
	end

	return exp
end

event:register()


local message = Event()

local expTracker = {}

local expTrackerLogout = CreatureEvent("ExpTrackerLogout")
function expTrackerLogout.onLogout(player)
	expTracker[player:getGuid()] = nil
	return true
end

expTrackerLogout:register()

local function getKillText(monsters, totalCount)
	local firstName
	local monsterTypes = 0

	for monsterName in pairs(monsters) do
		firstName = firstName or monsterName
		monsterTypes = monsterTypes + 1
		if monsterTypes > 1 then
			break
		end
	end

	if monsterTypes == 1 then
		return totalCount > 1 and (totalCount .. " creatures") or firstName
	end

	return totalCount .. " creatures"
end

function message.onGainExperience(self, source, exp, rawExp, sendText)
	if sendText and exp ~= 0 then
		local monsterName = source and source:getName() or "Unknown"
		local playerId = self:getId()
		local playerGuid = self:getGuid()
		local preyXpBonus = 0
		if PreySystem and source and source:isMonster() then
			local bonusType, bonusValue = PreySystem.getBonus(self, monsterName)
			if bonusType == PreySystem.BONUS_XP then
				preyXpBonus = bonusValue or 0
			end
		end

		if not expTracker[playerGuid] then
			expTracker[playerGuid] = { monsters = {}, eventId = nil }
		end

		local playerTracker = expTracker[playerGuid]
		if not playerTracker.monsters[monsterName] then
			playerTracker.monsters[monsterName] = { totalExp = 0, count = 0, preyXpBonus = 0 }
		end

		local trackerState = playerTracker.monsters[monsterName]
		trackerState.totalExp = trackerState.totalExp + exp
		trackerState.count = trackerState.count + 1
		if preyXpBonus > 0 then
			trackerState.preyXpBonus = preyXpBonus
		end

		if playerTracker.eventId then
			return exp
		end

		playerTracker.eventId = addEvent(function()
			local tracker = expTracker[playerGuid]
			if tracker then
				tracker.eventId = nil
			end

			local player = Player(playerId)
			if not player then return end
			if not tracker then return end

			local monsters = tracker.monsters
			tracker.monsters = {}

			local expValue = 0
			local count = 0
			local preyBonus = 0
			for _, monsterTracker in pairs(monsters) do
				expValue = expValue + (monsterTracker.totalExp or 0)
				count = count + (monsterTracker.count or 0)
				preyBonus = math.max(preyBonus, monsterTracker.preyXpBonus or 0)
			end
			expValue = math.floor(expValue)

			if expValue > 0 then
				local expString = getExperienceText(expValue)
				local preySuffix = ""
				if preyBonus > 0 then
					preySuffix = string.format(" (Prey Bonus XP +%d%%)", preyBonus)
				end

				local killText = getKillText(monsters, count)
				local message = "You gained " .. expString .. " for killing " .. killText .. preySuffix .. "."

				player:sendTextMessage(MESSAGE_STATUS_DEFAULT, message)

				local playerInstanceId = player:getInstanceId()
				local spectators = Game.getSpectators(player:getPosition(), false, true, 8, 8, 6, 6)
				local filtered = {}
				for _, spectator in ipairs(spectators) do
					if playerInstanceId == 0 or spectator:getInstanceId() == playerInstanceId then
						table.insert(filtered, spectator)
					end
				end

				Game.sendAnimatedText(getAnimatedExpText(expValue), player:getPosition(), getAnimatedExpColor(player), filtered)

				for _, spectator in ipairs(filtered) do
					if spectator ~= player then
						spectator:sendTextMessage(MESSAGE_STATUS_DEFAULT, player:getName() .. " gained " .. expString .. " for killing " .. killText .. preySuffix .. ".")
					end
				end
			end
		end, 50)
	end
	return exp
end

message:register(math.huge)
