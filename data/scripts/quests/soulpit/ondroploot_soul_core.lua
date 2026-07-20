-- OnDropLoot Soul Core: drops soul cores and soul prisms from fiendish monsters.
-- Ported from Crystal Server.

-- Guard: only register if Soulpit system is enabled
if not configManager or not configManager.getBoolean or not configManager.getBoolean(configKeys.SOULPIT_SYSTEM_ENABLED) then
	return
end

if not SoulPit then
	dofile("data/lib/others/soulpit.lua")
end
if not SoulPit then
	return
end

local dropCallback = Event()

local function rollTaskAdjustedChance(player, chancePercent)
	local multiplier = 1
	if player and AstraHelper and AstraHelper.getMiniBotLootMultiplier then
		multiplier = math.max(0, tonumber(AstraHelper.getMiniBotLootMultiplier(player)) or 1)
	end

	-- Preserve the original RNG path when Task mode is not changing loot.
	if multiplier == 1 then
		return math.random(100) <= chancePercent
	end

	local adjustedChance = math.min(100000, math.floor(chancePercent * 1000 * multiplier))
	return adjustedChance > 0 and math.random(100000) <= adjustedChance
end

function dropCallback.onDropLoot(monster, corpse)
	if not monster or not corpse then
		return
	end

	-- Only fiendish monsters drop soul cores
	if not monster:isFiendish() then
		return
	end

	local config = SoulPit.SoulCoresConfiguration
	local monsterType = monster:getType()
	if not monsterType then return end
	local monsterName = monsterType:getName()
	local raceId = monsterType:raceId()
	local player = Player(corpse:getCorpseOwner())

	-- Check for soul core drop (5% chance)
	if rollTaskAdjustedChance(player, config.chanceToDropSoulCore) then
		local soulCoreItemId = nil

		-- 15% chance for same monster soul core
		if math.random(100) <= config.chanceToGetSameMonsterSoulCore then
			-- Find this monster's soul core by name
			local soulCoreName = (monsterName:lower() .. " soul core")
			soulCoreItemId = ItemType(soulCoreName)
			if soulCoreItemId then
				soulCoreItemId = soulCoreItemId:getId()
			end
		end

		-- Otherwise pick from same bestiary race (simplified: skip)
		if not soulCoreItemId or soulCoreItemId == 0 then
			-- Give a random soul core from the pool
			local soulCores = SoulPit.getSoulCoreItems()
			if #soulCores > 0 then
				local randomCore = soulCores[math.random(#soulCores)]
				soulCoreItemId = randomCore:getId()
			end
		end

		if soulCoreItemId and soulCoreItemId > 0 then
			local item = corpse:addItem(soulCoreItemId, 1)
			if item then
				monster:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
			end
		end
	end

	-- Check for soul prism drop (4% chance)
	if rollTaskAdjustedChance(player, config.chanceToDropSoulPrism) then
		local prismId = SoulPit.itemIds.soulPrism
		if prismId and prismId > 0 then
			local item = corpse:addItem(prismId, 1)
			if item then
				monster:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
			end
		end
	end
end

dropCallback:register()
