local combat = Combat()
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)
combat:setArea(createCombatArea(AREA_CIRCLE3X3))

local condition = Condition(CONDITION_ATTRIBUTES)
condition:setParameter(CONDITION_PARAM_TICKS, 2 * 60 * 1000)
condition:setParameter(CONDITION_PARAM_STAT_MAGICPOINTS, 1)
condition:setParameter(CONDITION_PARAM_BUFF_SPELL, true)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:addPartyCondition(combat, variant, condition, 120)
end


spell:group("support")
spell:id(149)
spell:name("Enchant Party")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_ENCHANT_PARTY)
spell:words("utori mas sio")
spell:level(32)
spell:isPremium(true)
spell:isSelfTarget(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer")
spell:register()
