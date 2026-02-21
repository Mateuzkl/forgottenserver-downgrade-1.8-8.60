-- gerado por Spell Converter
-- script original
local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:conjureItem(3147, 3197, 3)
end


spell:group("support")
spell:id(168)
spell:name("Disintegrate Rune")
spell:words("adito tera")
spell:level(21)
spell:mana(200)
spell:soul(3)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid", "paladin", "royal paladin")
spell:register()
