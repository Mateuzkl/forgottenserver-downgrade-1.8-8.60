local soulCondition = Condition(CONDITION_SOUL, CONDITIONID_DEFAULT)
soulCondition:setTicks(4 * 60 * 1000)
soulCondition:setParameter(CONDITION_PARAM_SOULGAIN, 1)

local event = Event()

function event.onGainExperience(player, source, exp, rawExp)
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

	local bonusRate = player:getExperienceRate(ExperienceRateType.BONUS)
	if bonusRate ~= 100 then exp = exp * bonusRate / 100 end

	return exp
end

event:register()

