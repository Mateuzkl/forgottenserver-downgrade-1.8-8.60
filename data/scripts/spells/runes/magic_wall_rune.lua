local combat = Combat()
combat:setParameter(COMBAT_PARAM_DISTANCEEFFECT, CONST_ANI_ENERGY)
combat:setParameter(COMBAT_PARAM_CREATEITEM, ITEM_MAGICWALL)

local spell = Spell("rune")
function spell.onCastSpell(creature, variant, isHotkey)
	return combat:execute(creature, variant)
end


spell:group("attack")
spell:id(3180)
spell:runeId(3180)
spell:name("Magic Wall Rune")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_OR_RUNE)
spell:impactSound(SOUND_EFFECT_TYPE_SPELL_MAGIC_WALL_RUNE)
spell:level(32)
spell:cooldown(2 * 1000)
spell:groupCooldown(0 * 1000)
spell:needLearn(false)
spell:allowFarUse(true)
spell:magicLevel(9)
spell:charges(3)
spell:isBlocking(true, true) -- blockType("all") - bloqueia solid e creature
spell:register()
