-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3172, 3)
end


spell:group("support")
spell:id(187)
spell:name("Poison Field Rune")
spell:words("adevo grav pox")
spell:level(14)
spell:mana(200)
spell:soul(1)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
