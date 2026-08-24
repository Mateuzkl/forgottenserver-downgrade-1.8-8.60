local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_HEALING)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_BLUE)
combat:setParameter(COMBAT_PARAM_DISPEL, CONDITION_PARALYZE)
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)

-- Vocation Adjustment: base power 500, scales with magic level + shielding.
function onGetFormulaValues(player, level, magicLevel)
	local shielding = (player and player:getEffectiveSkillLevel(SKILL_SHIELD)) or 0
	-- TUNABLE
	local BASE, ML_MIN, ML_MAX, SHIELD_COEFF, LEVEL_COEFF = 500, 70.0, 92.0, 6.0, 0.20
	local common = level * LEVEL_COEFF + shielding * SHIELD_COEFF + BASE
	return common + magicLevel * ML_MIN, common + magicLevel * ML_MAX
end

combat:setCallback(CALLBACK_PARAM_LEVELMAGICVALUE, "onGetFormulaValues")

local spell = Spell("instant")

function spell.onCastSpell(creature, variant)
	return combat:execute(creature, variant)
end

spell:name("Intense Wound Cleansing")
spell:words("exura gran ico")
spell:group("healing")
spell:vocation("knight;true", "elite knight;true")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_INTENSE_WOUND_CLEANSING)
spell:id(158)
spell:cooldown(120000) -- Vocation Adjustment: 10min -> 2min
spell:groupCooldown(2 * 1000) -- Phase A rebalance: 1s -> 2s
spell:level(80)
spell:mana(200)
spell:isSelfTarget(true)
spell:isAggressive(false)
spell:isPremium(true)

spell:register()
