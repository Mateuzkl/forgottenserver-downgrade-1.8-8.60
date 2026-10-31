local combat = Combat()
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_BLUE)
combat:setArea(createCombatArea(AREA_SQUARE1X1))

local function callback(creature, target)
	return doChallengeCreature(creature, target)
end

combat:setCallback(CallBackParam.TARGETCREATURE, callback)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant) return combat:execute(creature, variant) end


spell:group("support")
spell:id(131)
spell:name("Challenge")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_CHALLENGE)
spell:words("exeta res")
spell:level(20)
spell:mana(30)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("elite knight")
spell:register()
