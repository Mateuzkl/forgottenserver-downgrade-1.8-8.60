local MAGIC_SHIELD_POTION_ID = 35563
local REQUIRED_LEVEL = 14
local ALLOWED_VOCATIONS = { 1, 2, 5, 6 }

local magicShield = Condition(CONDITION_MANASHIELD)
magicShield:setParameter(CONDITION_PARAM_TICKS, 3 * 60 * 1000)

local magicShieldPotion = Action()

function magicShieldPotion.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if player:getLevel() < REQUIRED_LEVEL or
		not table.contains(ALLOWED_VOCATIONS, player:getVocation():getId()) then
		player:say("This potion can only be consumed by druids and sorcerers of level 14 or higher.",
			TALKTYPE_MONSTER_SAY)
		return true
	end

	player:addCondition(magicShield)
	player:addAchievementProgress("Potion Addict", 100000)
	player:say("Aaaah...", TALKTYPE_MONSTER_SAY)
	player:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE, player:getInstanceId())

	if sendSupplyTracker then
		sendSupplyTracker(player, item)
	end
	if configManager.getBoolean(configKeys.REMOVE_POTION_CHARGES) then
		item:remove(1)
	end
	return true
end

magicShieldPotion:id(MAGIC_SHIELD_POTION_ID)
magicShieldPotion:register()
