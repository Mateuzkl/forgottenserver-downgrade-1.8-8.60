-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3166, 4)
end


spell:group("support")
spell:id(173)
spell:name("Energy Wall Rune")
spell:words("adevo mas grav vis")
spell:level(41)
spell:mana(1000)
spell:soul(5)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
