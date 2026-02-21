-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3160, 1)
end


spell:group("support")
spell:id(195)
spell:name("Ultimate Healing Rune")
spell:words("adura vita")
spell:level(24)
spell:mana(400)
spell:soul(3)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("druid", "elder druid")
spell:register()
