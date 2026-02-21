-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3188, 3)
end


spell:group("support")
spell:id(175)
spell:name("Fire Field Rune")
spell:words("adevo grav flam")
spell:level(15)
spell:mana(240)
spell:soul(1)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
