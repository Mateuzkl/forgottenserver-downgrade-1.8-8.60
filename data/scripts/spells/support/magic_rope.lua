local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	local position = creature:getPosition()
	position:sendMagicEffect(CONST_ME_POFF)

	local tile = Tile(position)
	if not tile:isRopeSpot() then
		creature:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		return false
	end

	tile = Tile(position:moveUpstairs())
	if not tile then
		creature:sendCancelMessage(RETURNVALUE_NOTENOUGHROOM)
		return false
	end

	creature:teleportTo(position, false, CONST_ME_NONE)
	position:sendMagicEffect(CONST_ME_TELEPORT)
	return true
end


spell:group("support")
spell:id(141)
spell:name("Magic Rope")
spell:words("exani tera")
spell:level(9)
spell:mana(20)
spell:isPremium(true)
spell:isSelfTarget(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(0 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("druid", "elder druid", "paladin", "royal paladin", "sorcerer", "master sorcerer", "knight", "elite knight", "monk", "exalted monk")
spell:register()
