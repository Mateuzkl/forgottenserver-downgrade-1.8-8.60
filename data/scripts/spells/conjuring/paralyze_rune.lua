-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3165, 1)
end


spell:group("support")
spell:id(185)
spell:name("Paralyze Rune")
spell:words("adana ani")
spell:level(54)
spell:mana(1400)
spell:soul(3)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("druid", "elder druid")
spell:register()
