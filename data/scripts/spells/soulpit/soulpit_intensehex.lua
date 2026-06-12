-- Soulpit Intensehex: boss spell — +50% damage dealt, +50% healing buff on target.
-- Crystal Server maps INTENSEHEX to ConditionAttributes (stat modifiers).
-- TFS 1.8 equivalent: CONDITION_ATTRIBUTES with skill percent modifiers.
-- Ported from Crystal Server.

local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_UNDEFINEDDAMAGE)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_STUN)

-- Intensehex: buff that increases damage dealt by 50% and healing received by 50%
-- Uses CONDITION_ATTRIBUTES to apply skill percentage modifiers
local condition = Condition(CONDITION_ATTRIBUTES)
condition:setParameter(CONDITION_PARAM_TICKS, 3000)
condition:setParameter(CONDITION_PARAM_SKILL_MELEEPERCENT, 150)
condition:setParameter(CONDITION_PARAM_SKILL_FISTPERCENT, 150)
condition:setParameter(CONDITION_PARAM_SKILL_CLUBPERCENT, 150)
condition:setParameter(CONDITION_PARAM_SKILL_SWORDPERCENT, 150)
condition:setParameter(CONDITION_PARAM_SKILL_AXEPERCENT, 150)
condition:setParameter(CONDITION_PARAM_SKILL_DISTANCEPERCENT, 150)
condition:setParameter(CONDITION_PARAM_STAT_MAGICPOINTSPERCENT, 150)
combat:addCondition(condition)

local spell = Spell(SPELL_INSTANT)

function spell.onCastSpell(creature, variant)
	return combat:execute(creature, variant)
end

spell:name("soulpit intensehex")
spell:words("###940")
spell:needTarget(true)
spell:isAggressive(false) -- it's a buff, not hostile
spell:range(7)
spell:register()
