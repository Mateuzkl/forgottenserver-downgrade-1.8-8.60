-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3164, 3)
end


spell:group("support")
spell:id(172)
spell:name("Energy Field Rune")
spell:words("adevo grav vis")
spell:level(18)
spell:mana(320)
spell:soul(2)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
