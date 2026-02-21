-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3180, 3)
end


spell:group("support")
spell:id(184)
spell:name("Magic Wall Rune")
spell:words("adevo grav tera")
spell:level(32)
spell:mana(750)
spell:soul(5)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer")
spell:register()
