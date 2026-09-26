local combat = Combat()
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_BLUE)
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)

local condition = Condition(CONDITION_INVISIBLE)
condition:setParameter(CONDITION_PARAM_TICKS, 200000)
combat:addCondition(condition)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant) return combat:execute(creature, variant) end


spell:group("support", "focus")
spell:id(138)
spell:name("Invisibility")
spell:words("utana vid")
spell:level(35)
spell:mana(440)
spell:isSelfTarget(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000, 2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
