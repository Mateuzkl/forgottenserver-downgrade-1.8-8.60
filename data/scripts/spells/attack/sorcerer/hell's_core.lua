local function callback(player, level, magicLevel)
	local min = (level / 5) + (magicLevel * 8) + 50
	local max = (level / 5) + (magicLevel * 12) + 75
	return -min, -max
end

local function createCombat(combatType, effect)
	local combat = Combat()
	combat:setParameter(COMBAT_PARAM_TYPE, combatType)
	combat:setParameter(COMBAT_PARAM_EFFECT, effect)
	combat:setArea(createCombatArea(AREA_CIRCLE5X5))
	combat:setCallback(CallBackParam.LEVELMAGICVALUE, callback)
	return combat
end

local combat = createCombat(COMBAT_FIREDAMAGE, CONST_ME_FIREAREA)
local combatThunder = createCombat(COMBAT_ENERGYDAMAGE, 337)
local combatDecay = createCombat(COMBAT_DEATHDAMAGE, 338)

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
spell:id(114)
spell:name("Hell's Core")
spell:words("exevo gran mas flam")
spell:level(60)
spell:mana(1100)
spell:isPremium(true)
spell:isSelfTarget(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:vocation("sorcerer", "master sorcerer")
spell:register()
