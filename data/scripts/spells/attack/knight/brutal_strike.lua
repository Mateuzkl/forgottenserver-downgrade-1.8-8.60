local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_HITAREA)
combat:setParameter(COMBAT_PARAM_DISTANCEEFFECT, CONST_ANI_WEAPONTYPE)
combat:setParameter(COMBAT_PARAM_BLOCKARMOR, true)
combat:setParameter(COMBAT_PARAM_USECHARGES, true)

local function calculateBaseDamageHealing(level)
	local step = math.floor((math.sqrt(2 * level + 2025) + 5) / 10)
	return math.floor((level + 1000) / step) + 50 * step - 450
end

local function callback(player, skill, attack, factor)
	local skillTotal = skill * attack
	local levelTotal = calculateBaseDamageHealing(player:getLevel())
	local min = (((skillTotal * 0.02) + 4) + levelTotal) * 1.28
	local max = (((skillTotal * 0.04) + 9) + levelTotal) * 1.28
	return -min, -max
end

combat:setCallback(CallBackParam.SKILLVALUE, callback)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant) return combat:execute(creature, variant) end

spell:group("attack")
spell:id(61)
spell:name("Brutal Strike")
spell:words("exori ico")
spell:level(16)
spell:mana(30)
spell:isPremium(false)
spell:range(1)
spell:needTarget(true)
spell:isBlockingWalls(true)
spell:needWeapon(true)
spell:cooldown(6 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:vocation("knight", "elite knight")
spell:register()
