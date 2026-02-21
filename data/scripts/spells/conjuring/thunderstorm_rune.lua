-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3202, 4)
end


spell:group("support")
spell:id(194)
spell:name("Thunderstorm Rune")
spell:words("adori mas vis")
spell:level(28)
spell:mana(430)
spell:soul(3)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer")
spell:register()
