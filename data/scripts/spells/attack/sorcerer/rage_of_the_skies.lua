local function callback(player, level, magicLevel)
	local min = (level / 5) + (magicLevel * 4) + 75
	local max = (level / 5) + (magicLevel * 10) + 150
	return -min, -max
end

local function createCombat(combatType, effect)
	local combat = Combat()
	combat:setParameter(COMBAT_PARAM_TYPE, combatType)
	combat:setParameter(COMBAT_PARAM_EFFECT, effect)
	combat:setArea(createCombatArea(AREA_CIRCLE6X6))
	combat:setCallback(CallBackParam.LEVELMAGICVALUE, callback)
	return combat
end

local combat = createCombat(COMBAT_ENERGYDAMAGE, CONST_ME_BIGCLOUDS)
local combatFlames = createCombat(COMBAT_FIREDAMAGE, 339)
local combatDecay = createCombat(COMBAT_DEATHDAMAGE, 340)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	local player = creature:getPlayer()
	if player then
		local stance = player:getElementalStance()
		if stance == STANCE_MASTER_OF_FLAMES then
			return combatFlames:execute(creature, variant)
		elseif stance == STANCE_MASTER_OF_DECAY then
			return combatDecay:execute(creature, variant)
		end
	end
	return combat:execute(creature, variant)
end

spell:group("attack")
spell:id(117)
spell:name("Rage of the Skies")
spell:words("exevo gran mas vis")
spell:level(55)
spell:mana(600)
spell:isPremium(true)
spell:isSelfTarget(true)
spell:cooldown(1 * 1500)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:vocation("sorcerer", "master sorcerer")
spell:register()
