local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_HEALING)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_BLUE)
combat:setParameter(COMBAT_PARAM_DISPEL, CONDITION_PARALYZE)
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)

-- Vocation Adjustment: base power 15, scales with magic level + shielding.
function onGetFormulaValues(player, level, magicLevel)
	local shielding = (player and player:getEffectiveSkillLevel(SKILL_SHIELD)) or 0
	-- TUNABLE
	local BASE, ML_COEFF, SHIELD_COEFF, LEVEL_COEFF, SPREAD = 15, 0.90, 0.30, 0.20, 5
	local base = level * LEVEL_COEFF + magicLevel * ML_COEFF + shielding * SHIELD_COEFF + BASE
	return base, base + SPREAD
end

combat:setCallback(CALLBACK_PARAM_LEVELMAGICVALUE, "onGetFormulaValues")

local spell = Spell("instant")

function spell.onCastSpell(creature, variant)
	return combat:execute(creature, variant)
end

spell:name("Bruise Bane")
spell:words("exura infir ico")
spell:group("healing")
spell:vocation("knight;true", "elite knight;true")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_BRUISE_BANE)
spell:id(170)
spell:cooldown(1000)
spell:groupCooldown(2 * 1000) -- Phase A rebalance: 1s -> 2s
spell:level(1)
spell:mana(10)
spell:isSelfTarget(true)
spell:isAggressive(false)

spell:isPremium(false)
spell:register()
