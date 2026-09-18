local area = createCombatArea({
	{ 0, 1, 1, 1, 0 },
	{ 1, 1, 1, 1, 1 },
	{ 1, 1, 3, 1, 1 },
	{ 1, 1, 1, 1, 1 },
	{ 0, 1, 1, 1, 0 },
})

local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_ENERGYHIT)
combat:setParameter(COMBAT_PARAM_DISTANCEEFFECT, CONST_ANI_DIAMONDARROW)
combat:setParameter(COMBAT_PARAM_BLOCKARMOR, true)
function onGetFormulaValues(player, skill, attack, factor)
	local distanceSkill = player:getEffectiveSkillLevel(SKILL_DISTANCE)
	local min = (player:getLevel() * 0.2)
	local max = (0.09 * factor) * distanceSkill * attack + (player:getLevel() * 0.2)
	return -min, -max
end

combat:setCallback(CALLBACK_PARAM_SKILLVALUE, "onGetFormulaValues")
combat:setArea(area)

local function registerDiamondArrow(itemId, consumable)
	local diamondArrow = Weapon(WEAPON_AMMO)

	function diamondArrow.onUseWeapon(player, variant)
		return combat:execute(player, variant)
	end

	diamondArrow:id(itemId)
	diamondArrow:level(150)
	diamondArrow:attack(37)
	if consumable then
		diamondArrow:action("removecount")
	end
	diamondArrow:ammoType("arrow")
	diamondArrow:shootType(CONST_ANI_DIAMONDARROW)
	diamondArrow:maxHitChance(100)
	diamondArrow:wieldUnproperly(true)
	diamondArrow:register()
end

-- 25757: timed ammo (duration/decay), not consumed per shot
registerDiamondArrow(25757, false)
-- 35901: shop ammo, consumed per shot
registerDiamondArrow(35901, true)
