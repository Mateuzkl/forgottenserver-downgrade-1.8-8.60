-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3195, 3)
end


spell:group("support")
spell:id(190)
spell:name("Soulfire Rune")
spell:words("adevo res flam")
spell:level(27)
spell:mana(420)
spell:soul(3)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
