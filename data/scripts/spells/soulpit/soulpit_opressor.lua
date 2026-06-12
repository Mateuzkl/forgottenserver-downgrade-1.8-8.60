-- Soulpit Opressor: boss spell — root + fear area
-- Ported from Crystal Server.

local combatRoot = Combat()
combatRoot:setParameter(COMBAT_PARAM_TYPE, COMBAT_UNDEFINEDDAMAGE)
combatRoot:setArea(createCombatArea({
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 1, 1, 3, 1, 1, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
}))
combatRoot:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_GROUNDSHAKER)

local conditionRoot = Condition(CONDITION_ROOTED)
conditionRoot:setParameter(CONDITION_PARAM_TICKS, 3000)
combatRoot:addCondition(conditionRoot)

local combatFear = Combat()
combatFear:setParameter(COMBAT_PARAM_TYPE, COMBAT_UNDEFINEDDAMAGE)
combatFear:setArea(createCombatArea({
	{0, 0, 0, 0, 0},
	{0, 1, 1, 1, 0},
	{0, 1, 3, 1, 0},
	{0, 1, 1, 1, 0},
	{0, 0, 0, 0, 0},
}))
combatFear:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_FEAR)
combatFear:addCondition(createConditionObject(CONDITION_FEARED, 3000))

local spell = Spell(SPELL_INSTANT)

function spell.onCastSpell(creature, variant)
	combatRoot:execute(creature, variant)
	combatFear:execute(creature, variant)
	return true
end

spell:name("soulpit opressor")
spell:words("###938")
spell:blockWalls(true)
spell:needTarget(false)
spell:isAggressive(true)
spell:register()
