-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3161, 4)
end


spell:group("support")
spell:id(155)
spell:name("Avalanche Rune")
spell:words("adori mas frigo")
spell:level(30)
spell:mana(530)
spell:soul(3)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("druid", "elder druid")
spell:register()
