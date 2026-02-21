-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3177, 1)
end


spell:group("support")
spell:id(165)
spell:name("Convince Creature Rune")
spell:words("adeta sio")
spell:level(16)
spell:mana(200)
spell:soul(3)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("druid", "elder druid")
spell:register()
