local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_HEALING)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_BLUE)
combat:setParameter(COMBAT_PARAM_DISPEL, CONDITION_PARALYZE)
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)

local function callback(player, level, magicLevel)
	local min = (level / 5) + (magicLevel * 6.9) + 40
	local max = (level / 5) + (magicLevel * 13.2) + 82
	return min, max
end

combat:setCallback(CallBackParam.LEVELMAGICVALUE, callback)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant) return combat:execute(creature, variant) end


spell:group("healing")
spell:id(123)
spell:name("Divine Healing")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_DIVINE_HEALING)
spell:words("exura san")
spell:level(35)
spell:mana(160)
spell:isSelfTarget(true)
spell:cooldown(1 * 1000)
spell:groupCooldown(1 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("paladin", "royal paladin")
spell:register()
