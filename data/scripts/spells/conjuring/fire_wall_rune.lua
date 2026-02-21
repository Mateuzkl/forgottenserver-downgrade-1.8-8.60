-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3190, 4)
end


spell:group("support")
spell:id(177)
spell:name("Fire Wall Rune")
spell:words("adevo mas grav flam")
spell:level(33)
spell:mana(780)
spell:soul(4)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
