local function callback(player, level, magicLevel)
	local min = (level / 5) + (magicLevel * 1.4) + 8
	local max = (level / 5) + (magicLevel * 2.2) + 14
	return -min, -max
end

local function createCombat(combatType, effect, distanceEffect)
	local combat = Combat()
	combat:setParameter(COMBAT_PARAM_TYPE, combatType)
	combat:setParameter(COMBAT_PARAM_EFFECT, effect)
	combat:setParameter(COMBAT_PARAM_DISTANCEEFFECT, distanceEffect)
	combat:setCallback(CallBackParam.LEVELMAGICVALUE, callback)
	return combat
end

local combat = createCombat(COMBAT_DEATHDAMAGE, CONST_ME_MORTAREA, CONST_ANI_DEATH)
local combatFlames = createCombat(COMBAT_FIREDAMAGE, 334, 4)
local combatThunder = createCombat(COMBAT_ENERGYDAMAGE, 335, 5)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	local player = creature:getPlayer()
	if player then
		local stance = player:getElementalStance()
		if stance == STANCE_MASTER_OF_FLAMES then
			return combatFlames:execute(creature, variant)
		elseif stance == STANCE_MASTER_OF_THUNDER then
			return combatThunder:execute(creature, variant)
		end
	end
	return combat:execute(creature, variant)
end

spell:group("attack")
spell:id(101)
spell:name("Death Strike")
spell:words("exori mort")
spell:level(16)
spell:mana(20)
spell:isPremium(true)
spell:range(3)
spell:needCasterTargetOrDirection(true)
spell:blockWalls(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:vocation("sorcerer", "master sorcerer")
spell:register()
