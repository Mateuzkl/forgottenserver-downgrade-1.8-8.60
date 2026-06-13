local TALISMAN_DAMAGE = 0
local TALISMAN_LIFE_LEECH = 1

local function getPlayerFromAttacker(attacker)
	if not attacker then
		return nil
	end
	if attacker:isPlayer() then
		return attacker
	end
	local master = attacker:getMaster()
	if master and master:isPlayer() then
		return master
	end
	return nil
end

local function getRaceId(monster)
	local monsterType = monster and monster:getType()
	return monsterType and monsterType:raceId() or 0
end

local function isDamage(value, combatType)
	return value ~= 0 and combatType ~= COMBAT_HEALING and combatType ~= COMBAT_NONE
end

local function addDamageBonus(value, combatType, bonus)
	if not isDamage(value, combatType) or bonus <= 0 then
		return value
	end
	local sign = value < 0 and -1 or 1
	local amount = math.abs(value)
	return sign * (amount + math.ceil(amount * bonus / 10000))
end

local bountyTalismanHealth = CreatureEvent("BountyTalismanHealth")

function bountyTalismanHealth.onHealthChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType, origin)
	if not creature or not creature:isMonster() or not TaskBoard or not TaskBoard.getBountyTalismanBonus then
		return primaryDamage, primaryType, secondaryDamage, secondaryType
	end

	local player = getPlayerFromAttacker(attacker)
	local raceId = getRaceId(creature)
	if not player or raceId <= 0 then
		return primaryDamage, primaryType, secondaryDamage, secondaryType
	end

	local damageBonus = TaskBoard.getBountyTalismanBonus(player, raceId, TALISMAN_DAMAGE)
	primaryDamage = addDamageBonus(primaryDamage, primaryType, damageBonus)
	secondaryDamage = addDamageBonus(secondaryDamage, secondaryType, damageBonus)

	if origin ~= ORIGIN_CONDITION then
		local lifeLeechBonus = TaskBoard.getBountyTalismanBonus(player, raceId, TALISMAN_LIFE_LEECH)
		if lifeLeechBonus > 0 then
			local damage = 0
			if isDamage(primaryDamage, primaryType) then
				damage = damage + math.abs(primaryDamage)
			end
			if isDamage(secondaryDamage, secondaryType) then
				damage = damage + math.abs(secondaryDamage)
			end
			damage = math.min(damage, creature:getHealth())
			local healing = math.ceil(damage * lifeLeechBonus / 10000)
			if healing > 0 then
				player:addHealth(healing)
			end
		end
	end

	return primaryDamage, primaryType, secondaryDamage, secondaryType
end

bountyTalismanHealth:register()

local bountyTalismanSpawn = MonsterEvent and MonsterEvent("BountyTalismanSpawn") or Event()

function bountyTalismanSpawn.onSpawn(monster)
	if monster then
		monster:registerEvent("BountyTalismanHealth")
	end
	return true
end

bountyTalismanSpawn:register()
