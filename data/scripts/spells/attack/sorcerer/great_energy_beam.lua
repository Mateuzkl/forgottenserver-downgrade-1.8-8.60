local function callback(player, level, magicLevel)
	local min = (level / 5) + (magicLevel * 3.6) + 22
	local max = (level / 5) + (magicLevel * 6) + 37
	return -min, -max
end

local function createCombat(combatType, effect)
	local combat = Combat()
	combat:setParameter(COMBAT_PARAM_TYPE, combatType)
	combat:setParameter(COMBAT_PARAM_EFFECT, effect)
	combat:setArea(createCombatArea(AREA_BEAM8))
	combat:setCallback(CallBackParam.LEVELMAGICVALUE, callback)
	return combat
end

local combat = createCombat(COMBAT_ENERGYDAMAGE, CONST_ME_ENERGYAREA)
local combatFlames = createCombat(COMBAT_FIREDAMAGE, 329)
local combatDecay = createCombat(COMBAT_DEATHDAMAGE, 330)

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
spell:id(112)
spell:name("Great Energy Beam")
spell:words("exevo gran vis lux")
spell:level(29)
spell:mana(110)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:needDirection(true)
spell:vocation("sorcerer", "master sorcerer")
spell:register()
