-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3203, 1)
end


spell:group("support")
spell:id(153)
spell:name("Animate Dead Rune")
spell:words("adana mort")
spell:level(27)
spell:mana(600)
spell:soul(5)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
