-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3200, 6)
end


spell:group("support")
spell:id(174)
spell:name("Explosion Rune")
spell:words("adevo mas hur")
spell:level(31)
spell:mana(570)
spell:soul(4)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
