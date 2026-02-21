-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3173, 2)
end


spell:group("support")
spell:id(186)
spell:name("Poison Bomb Rune")
spell:words("adevo mas pox")
spell:level(25)
spell:mana(520)
spell:soul(2)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("druid", "elder druid")
spell:register()
