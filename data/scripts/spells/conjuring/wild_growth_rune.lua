-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3156, 2)
end


spell:group("support")
spell:id(196)
spell:name("Wild Growth Rune")
spell:words("adevo grav vita")
spell:level(27)
spell:mana(600)
spell:soul(5)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("druid", "elder druid")
spell:register()
