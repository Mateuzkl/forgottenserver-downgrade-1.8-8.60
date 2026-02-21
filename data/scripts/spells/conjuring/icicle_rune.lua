-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3158, 5)
end


spell:group("support")
spell:id(182)
spell:name("Icicle Rune")
spell:words("adori frigo")
spell:level(28)
spell:mana(460)
spell:soul(3)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("druid", "elder druid")
spell:register()
