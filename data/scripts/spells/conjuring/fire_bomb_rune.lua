-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3192, 2)
end


spell:group("support")
spell:id(176)
spell:name("Fire Bomb Rune")
spell:words("adevo mas flam")
spell:level(27)
spell:mana(600)
spell:soul(4)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
