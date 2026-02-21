-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3176, 4)
end


spell:group("support")
spell:id(188)
spell:name("Poison Wall Rune")
spell:words("adevo mas grav pox")
spell:level(29)
spell:mana(640)
spell:soul(3)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
