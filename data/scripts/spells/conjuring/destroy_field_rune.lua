-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3148, 3)
end


spell:group("support")
spell:id(167)
spell:name("Destroy Field Rune")
spell:words("adito grav")
spell:level(17)
spell:mana(120)
spell:soul(2)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid", "paladin", "royal paladin")
spell:register()
