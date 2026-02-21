-- gerado por Spell Converter
-- script original
local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_FIREDAMAGE)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_HITBYFIRE)
combat:setParameter(COMBAT_PARAM_DISTANCEEFFECT, CONST_ANI_FIRE)

local function callback(creature, target)
	local min = (creature:getLevel() / 80) + (creature:getMagicLevel() * 0.3) + 2
	local max = (creature:getLevel() / 80) + (creature:getMagicLevel() * 0.6) + 4
	local rounds = math.random(math.floor(min), math.floor(max))
	creature:addDamageCondition(target, CONDITION_FIRE, DAMAGELIST_VARYING_PERIOD,
	                            target:isPlayer() and 5 or 10, {8, 10}, rounds)
end

combat:setCallback(CallBackParam.TARGETCREATURE, callback)

local spell = Spell("rune")
function spell.onCastSpell(creature, variant, isHotkey)
	return combat:execute(creature, variant)
end


spell:group("attack")
spell:id(3195)
spell:runeId(3195)
spell:name("Soulfire Rune")
spell:level(27)
spell:needCasterTargetOrDirection(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:allowFarUse(true)
spell:magicLevel(7)
spell:charges(3)
spell:isBlocking(true) -- True = Solid / False = Creature
spell:register()
