local condition = Condition(CONDITION_OUTFIT)
condition:setOutfit({ lookType = AVATAR_LOOKTYPE_STEEL })

local avatarBonus = Condition(CONDITION_ATTRIBUTES)
avatarBonus:setParameter(CONDITION_PARAM_SUBID, 264)
avatarBonus:setParameter(CONDITION_PARAM_SPECIALSKILL_CRITICALHITCHANCE, 1000)
avatarBonus:setParameter(CONDITION_PARAM_SPECIALSKILL_CRITICALHITAMOUNT, 5000)

local spell = Spell("instant")

function spell.onCastSpell(creature, variant)
	if not creature or not creature:isPlayer() then
		return false
	end

	local grade = creature:revelationStageWOD("Avatar of Steel")
	if grade == 0 then
		creature:sendCancelMessage("You cannot cast this spell")
		creature:getPosition():sendMagicEffect(CONST_ME_POFF)
		return false
	end

	local duration = 15000
	condition:setTicks(duration)
	avatarBonus:setTicks(duration)
	creature:getPosition():sendMagicEffect(CONST_ME_AVATAR_APPEAR)
	creature:addCondition(condition)
	creature:addCondition(avatarBonus)
	creature:avatarTimer((os.time() * 1000) + duration)
	creature:reloadData()

	addEvent(function(cid)
		local c = Creature(cid)
		if c then
			c:reloadData()
		end
	end, duration, creature:getId())
	return true
end

spell:group("support")
spell:id(264)
spell:name("Avatar of Steel")
spell:words("uteta res eq")
spell:level(300)
spell:mana(800)
spell:isPremium(true)
spell:cooldown(7200000)
spell:groupCooldown(2000)
spell:vocation("knight", "elite knight")
spell:hasParams(true)
spell:isAggressive(false)
spell:needLearn(false)
spell:register()
