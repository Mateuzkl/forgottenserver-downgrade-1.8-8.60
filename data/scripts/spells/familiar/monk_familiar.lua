local spell = Spell("instant")
local spellId = 282

function spell.onCastSpell(player, variant)
	return player:createFamiliarSpell(spellId)
end

spell:group("support")
spell:id(spellId)
spell:name("Monk Familiar")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_SUMMON_MONK_FAMILIAR)
spell:words("utevo gran res tio")
spell:level(200)
spell:mana(1500)
spell:cooldown(0) -- calculated in CreateFamiliarSpell
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("monk", "exalted monk")
spell:register()
