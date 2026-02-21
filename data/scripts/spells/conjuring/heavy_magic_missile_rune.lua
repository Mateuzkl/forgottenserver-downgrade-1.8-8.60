-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3198, 10)
end


spell:group("support")
spell:id(180)
spell:name("Heavy Magic Missile Rune")
spell:words("adori vis")
spell:level(25)
spell:mana(350)
spell:soul(2)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
