local spell = Spell("instant")
local spellId = 194

function spell.onCastSpell(player, variant)
	return player:createFamiliarSpell(spellId)
end

spell:group("support")
spell:id(spellId)
spell:name("Knight Familiar")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_SUMMON_KNIGHT_FAMILIAR)
spell:words("utevo gran res eq")
spell:level(200)
spell:mana(1000)
spell:cooldown(0) -- calculated in CreateFamiliarSpell
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("knight;true", "elite knight;true")
spell:register()
