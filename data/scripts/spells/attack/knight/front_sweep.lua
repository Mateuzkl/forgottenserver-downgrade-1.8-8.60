local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_HITAREA)
combat:setParameter(COMBAT_PARAM_BLOCKARMOR, true)
combat:setParameter(COMBAT_PARAM_USECHARGES, true)
combat:setArea(createCombatArea(AREA_WAVE6, AREADIAGONAL_WAVE6))

local function calculateBaseDamageHealing(level)
	local step = math.floor((math.sqrt(2 * level + 2025) + 5) / 10)
	return math.floor((level + 1000) / step) + 50 * step - 450
end

local function callback(player, skill, attack, factor)
	local skillTotal = skill * attack
	local levelTotal = calculateBaseDamageHealing(player:getLevel())
	local baseScale = 80 / 72
	local min = (((skillTotal * 0.04) + 31) + levelTotal) * 1.1 * baseScale
	local max = (((skillTotal * 0.08) + 45) + levelTotal) * 1.1 * baseScale
	return -min, -max
end

combat:setCallback(CallBackParam.SKILLVALUE, callback)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant) return combat:execute(creature, variant) end

spell:group("attack")
spell:id(59)
spell:name("Front Sweep")
spell:words("exori min")
spell:level(70)
spell:mana(200)
spell:isPremium(true)
spell:needDirection(true)
spell:needWeapon(true)
spell:cooldown(6 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:vocation("knight", "elite knight")
spell:register()
