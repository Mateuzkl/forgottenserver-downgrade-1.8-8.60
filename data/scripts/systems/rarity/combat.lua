-- Rarity Combat Modifiers
-- healthChange and manaChange creaturescripts for resistance, leech, and mana shield.

local protectionKeys = {
	[COMBAT_PHYSICALDAMAGE] = "physicalProtection",
	[COMBAT_FIREDAMAGE] = "fireProtection",
	[COMBAT_ICEDAMAGE] = "iceProtection",
	[COMBAT_ENERGYDAMAGE] = "energyProtection",
	[COMBAT_EARTHDAMAGE] = "earthProtection",
	[COMBAT_HOLYDAMAGE] = "holyProtection",
	[COMBAT_DEATHDAMAGE] = "deathProtection",
}

local damageKeys = {
	[COMBAT_PHYSICALDAMAGE] = "physicalDamage",
	[COMBAT_FIREDAMAGE] = "fireDamage",
	[COMBAT_ICEDAMAGE] = "iceDamage",
	[COMBAT_ENERGYDAMAGE] = "energyDamage",
	[COMBAT_EARTHDAMAGE] = "earthDamage",
	[COMBAT_HOLYDAMAGE] = "holyDamage",
	[COMBAT_DEATHDAMAGE] = "deathDamage",
}

-- Tracks applied specialMagicLevel bonuses per player so they can be removed.
local appliedDamageBonuses = {}

-- Cached aggregated rarity stats per player, invalidated on inventory change.
local rarityStatsCache = {}

function getEquippedRarityStats(player)
	local key = player:getId()
	return rarityStatsCache[key] or {}
end

function invalidateRarityStatsCache(player)
	rarityStatsCache[player:getId()] = nil
end

function rebuildRarityStatsCache(player)
	local key = player:getId()
	local stats = {}
	for slot = CONST_SLOT_FIRST, CONST_SLOT_LAST do
		local item = player:getSlotItem(slot)
		if item and item:getRarityTier() > 0 then
			for attrKey, attrDef in pairs(rarityConfig.attributes) do
				local value = item:getRarityStat(attrDef.statKey)
				if value > 0 then
					stats[attrDef.statKey] = (stats[attrDef.statKey] or 0) + value
				end
			end
		end
	end
	rarityStatsCache[key] = stats
	return stats
end

function applyRarityDamageBonuses(player)
	if not player or not player.addSpecialMagicLevel then
		return
	end

	local stats = rebuildRarityStatsCache(player)
	local bonuses = {}
	for combatType, statKey in pairs(damageKeys) do
		local value = stats[statKey] or 0
		if value > 0 then
			bonuses[combatType] = value
		end
	end

	-- Apply
	for combatType, value in pairs(bonuses) do
		player:addSpecialMagicLevel(combatType, value)
	end

	appliedDamageBonuses[key] = bonuses
end
		end
	end

	-- Aggregate from equipped items
	local stats = getEquippedRarityStats(player)
	local bonuses = {}
	for combatType, statKey in pairs(damageKeys) do
		local value = stats[statKey] or 0
		if value > 0 then
			bonuses[combatType] = value
		end
	end

	-- Apply
	for combatType, value in pairs(bonuses) do
		player:addSpecialMagicLevel(combatType, value)
	end

	appliedDamageBonuses[key] = bonuses
end

function removeRarityDamageBonuses(player)
	if not player or not player.addSpecialMagicLevel then
		return
	end

	local key = player:getId()
	local bonuses = appliedDamageBonuses[key]
	if not bonuses then
		return
	end

	for combatType, value in pairs(bonuses) do
		if value ~= 0 then
			player:addSpecialMagicLevel(combatType, -value)
		end
	end
	appliedDamageBonuses[key] = nil
end

local rarityHealthChange = CreatureEvent("rarityHealthChange")
function rarityHealthChange.onHealthChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType, origin)
	-- Skip healing
	if primaryType == COMBAT_HEALING then
		return primaryDamage, primaryType, secondaryDamage, secondaryType, origin
	end

	local player = creature:getPlayer()
	if not player then
		return primaryDamage, primaryType, secondaryDamage, secondaryType, origin
	end

	local stats = getEquippedRarityStats(player)

	-- Apply protections (reduce incoming damage)
	for combatType, statKey in pairs(protectionKeys) do
		local protection = stats[statKey] or 0
		if protection > 0 then
			-- Check elementalProtection (covers all elements except physical)
			if combatType ~= COMBAT_PHYSICALDAMAGE then
				local elementalProt = stats["elementalProtection"] or 0
				if elementalProt > protection then
					protection = elementalProt
				end
			end
			if primaryType == combatType then
				primaryDamage = math.floor(primaryDamage * (1 - protection / 100))
			end
			if secondaryType == combatType then
				secondaryDamage = math.floor(secondaryDamage * (1 - protection / 100))
			end
		end
	end

	-- Life leech (attacker side)
	if attacker and attacker ~= creature then
		local attackerPlayer = attacker:getPlayer()
		if attackerPlayer then
			local attackerStats = getEquippedRarityStats(attackerPlayer)
			local leech = attackerStats["lifeLeech"] or 0
			if leech > 0 and primaryDamage < 0 then
				local totalDamage = math.abs(primaryDamage) + math.abs(secondaryDamage)
				local healAmount = math.floor(totalDamage * (leech / 100))
				if healAmount > 0 then
					attacker:addHealth(healAmount)
					attacker:getPosition():sendMagicEffect(CONST_ME_MAGIC_RED)
				end
			end
		end
	end

	return primaryDamage, primaryType, secondaryDamage, secondaryType, origin
end
rarityHealthChange:register()

local rarityManaChange = CreatureEvent("rarityManaChange")
function rarityManaChange.onManaChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType, origin)
	return primaryDamage, primaryType, secondaryDamage, secondaryType, origin
end
rarityManaChange:register()
