local combat = Combat()
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_BLUE)
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)

local condition = Condition(CONDITION_LIGHT)
condition:setParameter(CONDITION_PARAM_LIGHT_LEVEL, 8)
condition:setParameter(CONDITION_PARAM_LIGHT_COLOR, 215)
condition:setParameter(CONDITION_PARAM_TICKS, (11 * 60 + 35) * 1000)
combat:addCondition(condition)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant) return combat:execute(creature, variant) end


spell:group("support")
spell:id(136)
spell:name("Great Light")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_GREAT_LIGHT)
spell:words("utevo gran lux")
spell:level(13)
spell:mana(60)
spell:isSelfTarget(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("druid", "elder druid", "paladin", "royal paladin", "sorcerer", "master sorcerer", "monk", "exalted monk")
spell:register()
