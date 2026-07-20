local spell = Spell("instant")

function spell.onCastSpell(creature, variant)
	creature:removeCondition(CONDITION_MANASHIELD)
	creature:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE, creature:getInstanceId())
	return true
end

spell:group("support")
spell:id(245)
spell:name("Cancel Magic Shield")
spell:words("exana vita")
spell:level(14)
spell:mana(50)
spell:isSelfTarget(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
