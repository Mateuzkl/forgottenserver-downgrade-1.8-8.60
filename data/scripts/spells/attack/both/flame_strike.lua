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

local combat = createCombat(COMBAT_FIREDAMAGE, CONST_ME_FIREATTACK, CONST_ANI_FIRE)
local combatThunder = createCombat(COMBAT_ENERGYDAMAGE, 331, 5)
local combatDecay = createCombat(COMBAT_DEATHDAMAGE, 332, 11)

local function secondThunderEffect(creature, target)
	target:getPosition():sendMagicEffect(333)
	return true
end

local function secondDecayEffect(creature, target)
	target:getPosition():sendMagicEffect(336)
	return true
end

combatThunder:setCallback(CallBackParam.TARGETCREATURE, secondThunderEffect)
combatDecay:setCallback(CallBackParam.TARGETCREATURE, secondDecayEffect)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	local player = creature:getPlayer()
	if player then
		local stance = player:getElementalStance()
		if stance == STANCE_MASTER_OF_THUNDER then
			return combatThunder:execute(creature, variant)
		elseif stance == STANCE_MASTER_OF_DECAY then
			return combatDecay:execute(creature, variant)
		end
	end
	return combat:execute(creature, variant)
end

spell:group("attack")
spell:id(111)
spell:name("Flame Strike")
spell:words("exori flam")
spell:level(14)
spell:mana(20)
spell:isPremium(true)
spell:range(3)
spell:needCasterTargetOrDirection(true)
spell:blockWalls(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:vocation("sorcerer", "master sorcerer", "druid", "elder druid")
spell:register()
