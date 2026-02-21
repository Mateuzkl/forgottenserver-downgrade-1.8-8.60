-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3152, 1)
end


spell:group("support")
spell:id(183)
spell:name("Intense Healing Rune")
spell:words("adura gran")
spell:level(15)
spell:mana(120)
spell:soul(2)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("druid", "elder druid")
spell:register()
