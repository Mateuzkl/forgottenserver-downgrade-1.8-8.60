local function callback(player, level, magicLevel)
	local min = (level / 5) + (magicLevel * 1.2) + 7
	local max = (level / 5) + (magicLevel * 2) + 12
	return -min, -max
end

local function createCombat(combatType, effect)
	local combat = Combat()
	combat:setParameter(COMBAT_PARAM_TYPE, combatType)
	combat:setParameter(COMBAT_PARAM_EFFECT, effect)
	combat:setArea(createCombatArea(AREA_WAVE4, AREADIAGONAL_WAVE4))
	combat:setCallback(CallBackParam.LEVELMAGICVALUE, callback)
	return combat
end

local combat = createCombat(COMBAT_FIREDAMAGE, CONST_ME_HITBYFIRE)
local combatThunder = createCombat(COMBAT_ENERGYDAMAGE, 332)
local combatDecay = createCombat(COMBAT_DEATHDAMAGE, 331)

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
spell:id(110)
spell:name("Fire Wave")
spell:words("exevo flam hur")
spell:level(18)
spell:mana(25)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:needDirection(true)
spell:vocation("sorcerer", "master sorcerer")
spell:register()
