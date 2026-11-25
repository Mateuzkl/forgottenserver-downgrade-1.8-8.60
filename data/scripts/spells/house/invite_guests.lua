local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	local house = creature:getTile():getHouse()
	if not house or not house:canEditAccessList(GUEST_LIST, creature) then
		creature:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		creature:getPosition():sendMagicEffect(CONST_ME_POFF)
		return false
	end

	creature:setEditHouse(house, GUEST_LIST)
	creature:sendHouseWindow(house, GUEST_LIST)
	return true
end


spell:group("support")
spell:id(198)
spell:name("House Guest List")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_INVITE_GUESTS)
spell:words("aleta sio")
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:register()
