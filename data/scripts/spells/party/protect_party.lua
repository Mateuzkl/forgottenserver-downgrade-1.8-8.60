local combat = Combat()
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)
combat:setArea(createCombatArea(AREA_CIRCLE3X3))

local condition = Condition(CONDITION_ATTRIBUTES)
condition:setParameter(CONDITION_PARAM_TICKS, 2 * 60 * 1000)
condition:setParameter(CONDITION_PARAM_SKILL_SHIELD, 3)
condition:setParameter(CONDITION_PARAM_BUFF_SPELL, true)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	return creature:addPartyCondition(combat, variant, condition, 90)
end


spell:group("support")
spell:id(151)
spell:name("Protect Party")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_PROTECT_PARTY)
spell:words("utamo mas sio")
spell:level(32)
spell:isPremium(true)
spell:isSelfTarget(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("paladin", "royal paladin")
spell:register()
