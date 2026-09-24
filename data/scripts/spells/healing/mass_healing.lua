local healMonsters = false

local function shouldHealTarget(creature, target)
	if healMonsters then
		return true
	end

	local master = target:getMaster()
	if target:isMonster() and (not master or master:isMonster()) then
		return false
	end

	return true
end

local function healTarget(creature, target)
	if not shouldHealTarget(creature, target) then
		return true
	end

	local player = creature:getPlayer()
	if not player then
		return true
	end

	local level = player:getLevel()
	local magicLevel = player:getMagicLevel()
	local min = (level / 5) + (magicLevel * 4.6) + 100
	local max = (level / 5) + (magicLevel * 9.6) + 125

	local healingBonus = player:getWheelSpellHealingPercentBonus("Mass Healing")
	if healingBonus > 0 then
		min = math.floor(min * (1 + healingBonus))
		max = math.floor(max * (1 + healingBonus))
	end

	doTargetCombatHealth(creature:getId(), target, COMBAT_HEALING, min, max, CONST_ME_NONE)
	return true
end

function onTargetCreature(creature, target)
	return healTarget(creature, target)
end

function onTargetCreatureWOD(creature, target)
	return healTarget(creature, target)
end

local function createMassHealingCombat(area, callbackName)
	local combat = Combat()
	combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_BLUE)
	combat:setParameter(COMBAT_PARAM_DISPEL, CONDITION_PARALYZE)
	combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)
	combat:setArea(createCombatArea(area))
	combat:setCallback(CALLBACK_PARAM_TARGETCREATURE, callbackName)
	return combat
end

local combat = createMassHealingCombat(AREA_CIRCLE3X3, "onTargetCreature")
local combatWOD = createMassHealingCombat(AREA_CIRCLE5X5, "onTargetCreatureWOD")

local spell = Spell("instant")

function spell.onCastSpell(creature, variant)
	local player = creature:getPlayer()
	if player and player:getWheelSpellAdditionalArea("Mass Healing") then
		return combatWOD:execute(creature, variant)
	end
	return combat:execute(creature, variant)
end

spell:group("healing")
spell:id(127)
spell:name("Mass Healing")
spell:words("exura gran mas res")
spell:level(36)
spell:mana(150)
spell:isPremium(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(1 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("druid", "elder druid")
spell:register()
