-- Soulpit Powerless: boss spell — blocks attack spells (CONDITION_POWERLESS).
-- Crystal Server: ConditionGeneric, blocks SPELLGROUP_ATTACK.
-- TFS 1.8: uses CONDITION_POWERLESS (ported from Crystal, 1ULL << 35).
-- Ported from Crystal Server.

local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_UNDEFINEDDAMAGE)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_EXPLOSIONHIT)

local condition = Condition(CONDITION_POWERLESS)
if not condition then
	print("[Warning - soulpit_powerless.lua] CONDITION_POWERLESS is not available.")
	return
end
condition:setParameter(CONDITION_PARAM_TICKS, 3000)
combat:addCondition(condition)

local spell = Spell(SPELL_INSTANT)

function spell.onCastSpell(creature, variant)
	return combat:execute(creature, variant)
end

spell:name("soulpit powerless")
spell:words("###939")
spell:needTarget(true)
spell:isAggressive(true)
spell:range(7)
spell:register()
