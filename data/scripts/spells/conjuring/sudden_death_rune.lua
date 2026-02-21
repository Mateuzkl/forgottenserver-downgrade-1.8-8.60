-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3155, 3)
end


spell:group("support")
spell:id(193)
spell:name("Sudden Death Rune")
spell:words("adori gran mort")
spell:level(45)
spell:mana(985)
spell:soul(5)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer")
spell:register()
