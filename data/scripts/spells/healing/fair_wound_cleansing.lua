local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_HEALING)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_BLUE)
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)
combat:setParameter(COMBAT_PARAM_DISPEL, CONDITION_PARALYZE)

-- Vocation Adjustment: base power 225, scales with magic level + shielding.
function onGetFormulaValues(player, level, magicLevel)
	local shielding = (player and player:getEffectiveSkillLevel(SKILL_SHIELD)) or 0
	-- TUNABLE
	local BASE, ML_MIN, ML_MAX, SHIELD_COEFF, LEVEL_COEFF = 225, 8.0, 15.9, 2.5, 0.40
	local common = level * LEVEL_COEFF + shielding * SHIELD_COEFF + BASE
	return common + magicLevel * ML_MIN, common + magicLevel * ML_MAX
end

combat:setCallback(CALLBACK_PARAM_LEVELMAGICVALUE, "onGetFormulaValues")

local spell = Spell("instant")

function spell.onCastSpell(creature, variant)
	return combat:execute(creature, variant)
end

spell:group("healing")
spell:id(239)
spell:name("Fair Wound Cleansing")
spell:words("exura med ico")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_FAIR_WOUND_CLEANSING)
spell:level(300)
spell:mana(90)
spell:isPremium(true)
spell:isSelfTarget(true)
spell:cooldown(1000)
spell:groupCooldown(2 * 1000) -- Phase A rebalance: 1s -> 2s
spell:isAggressive(false)
spell:vocation("knight;true", "elite knight;true")

spell:register()
