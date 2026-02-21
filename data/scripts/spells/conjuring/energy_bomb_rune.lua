-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3149, 2)
end


spell:group("support")
spell:id(171)
spell:name("Energy Bomb Rune")
spell:words("adevo mas vis")
spell:level(37)
spell:mana(880)
spell:soul(5)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer")
spell:register()
