local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	local house = creature:getTile():getHouse()
	local doorId = house and house:getDoorIdByPosition(variant:getPosition())
	if not doorId or not house:canEditAccessList(doorId, creature) then
		creature:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		creature:getPosition():sendMagicEffect(CONST_ME_POFF)
		return false
	end

	creature:setEditHouse(house, doorId)
	creature:sendHouseWindow(house, doorId)
	return true
end


spell:group("support")
spell:id(197)
spell:name("House Door List")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_EDIT_DOOR)
spell:words("aleta grav")
spell:needCasterTargetOrDirection(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:register()
