local condition = Condition(CONDITION_OUTFIT)
condition:setOutfit({ lookType = AVATAR_LOOKTYPE_BALANCE })

local avatarBonus = Condition(CONDITION_ATTRIBUTES)
avatarBonus:setParameter(CONDITION_PARAM_SUBID, 283)
avatarBonus:setParameter(CONDITION_PARAM_SPECIALSKILL_CRITICALHITCHANCE, 1000)
avatarBonus:setParameter(CONDITION_PARAM_SPECIALSKILL_CRITICALHITAMOUNT, 5000)

local spell = Spell("instant")
local AVATAR_DURATION = 30000

function spell.onCastSpell(creature, variant)
	if not creature or not creature:isPlayer() then
		return false
	end

	local grade = creature:revelationStageWOD("Avatar of Balance")
	if grade == 0 then
		creature:sendCancelMessage("You cannot cast this spell")
		creature:getPosition():sendMagicEffect(CONST_ME_POFF)
		return false
	end

	condition:setTicks(AVATAR_DURATION)
	avatarBonus:setTicks(AVATAR_DURATION)
	creature:getPosition():sendMagicEffect(CONST_ME_AVATAR_APPEAR)
	creature:getPosition():sendMagicEffect(CONST_ME_HOLYAREA)
	creature:addCondition(condition)
	creature:addCondition(avatarBonus)
	creature:avatarTimer((os.time() * 1000) + AVATAR_DURATION)
	creature:setSereneCooldown(AVATAR_DURATION)
	creature:setHarmony(5)
	syncHarmonyOpcode(creature)
	creature:reloadData()

	creature:sendTextMessage(MESSAGE_STATUS_CONSOLE_ORANGE,
		"[Avatar of Balance] You channel perfect balance. Duration: 30 seconds.")

	addEvent(function(cid)
		local c = Creature(cid)
		if c and c:isPlayer() then
			c:reloadData()
			c:sendTextMessage(MESSAGE_STATUS_CONSOLE_ORANGE,
				"[Avatar of Balance] The balance fades away.")
		end
	end, AVATAR_DURATION, creature:getId())
	return true
end

spell:group("support")
spell:id(283)
spell:name("Avatar of Balance")
spell:words("uteta res mon")
spell:level(400)
spell:mana(1500)
spell:isPremium(false)
spell:needLearn(false)
spell:groupCooldown(2000)
spell:cooldown(120000)
spell:vocation("monk", "exalted monk")
spell:register()
