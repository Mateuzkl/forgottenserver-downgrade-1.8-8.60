-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 17112, 10)
end


spell:group("support")
spell:id(189)
spell:name("Practice Magic Missile Rune")
spell:words("adori dis min vis")
spell:level(1)
spell:mana(5)
spell:soul(0)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("none")
spell:register()
