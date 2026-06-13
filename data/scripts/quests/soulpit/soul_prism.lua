-- Soul Prism: upgrades a soul core to a higher difficulty tier's monster.
-- Ported from Crystal Server.

-- Guard: only load if Soulpit system is enabled
if not configManager or not configManager.getBoolean or not configManager.getBoolean(configKeys.SOULPIT_SYSTEM_ENABLED) then
	return
end

if not SoulPit then
	dofile("data/lib/others/soulpit.lua")
end
if not SoulPit then
	return
end

local soulPrism = Action()

local function deliverTransformedCore(player, item, target, newCoreId, message)
	local targetId = target:getId()
	local added = player:addItem(newCoreId, 1)
	if not added then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You do not have enough room to receive the transformed soul core.")
		return false
	end

	if not target:remove(1) then
		added:remove(1)
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Could not consume the target soul core.")
		return false
	end

	if not item:remove(1) then
		added:remove(1)
		local restored = player:addItem(targetId, 1)
		if not restored then
			local tile = Tile(player:getPosition())
			if tile then
				Game.createItem(targetId, 1, player:getPosition())
			end
			player:sendTextMessage(MESSAGE_INFO_DESCR, "Could not consume the soul prism. The soul core was placed on the ground.")
		else
			player:sendTextMessage(MESSAGE_INFO_DESCR, "Could not consume the soul prism.")
		end
		return false
	end

	player:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)
	player:sendTextMessage(MESSAGE_INFO_DESCR, message)
	return true
end

function soulPrism.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if not player or not item or not target then
		return false
	end

	-- Item must be soul prism (ID 49164)
	if item:getId() ~= SoulPit.itemIds.soulPrism then
		return false
	end

	-- Target must be a soul core
	local targetName = target:getName()
	local monsterName = SoulPit.getSoulCoreMonster(targetName)
	if not monsterName then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You can only use the soul prism on a soul core.")
		return false
	end

	-- Get current monster's difficulty
	local monsterType = MonsterType(monsterName)
	if not monsterType then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "This creature does not exist.")
		return false
	end

	local currentStars = monsterType:bestiaryStars() or 1

	-- Ominous soul core chance (2%)
	if math.random(100) <= SoulPit.SoulCoresConfiguration.chanceToGetOminousSoulCore then
		return deliverTransformedCore(
			player,
			item,
			target,
			SoulPit.itemIds.ominousSoulCore,
			"You have received an Ominous Soul Core!"
		)
	end

	-- Get next difficulty level
	local targetStars = currentStars
	if currentStars < 6 then
		targetStars = currentStars + 1
	end

	-- Find a random monster at the target difficulty
	local candidates = {}
	if CustomBestiary and CustomBestiary.monstersByRaceId then
		for raceId, entry in pairs(CustomBestiary.monstersByRaceId) do
			if entry.stars == targetStars then
				candidates[#candidates + 1] = entry
			end
		end
	end

	if #candidates == 0 then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "No creatures found at the next difficulty tier.")
		return false
	end

	-- Pick random candidate and find their soul core item
	local chosen = candidates[math.random(#candidates)]
	if not chosen or not chosen.name then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Could not determine the target creature.")
		return false
	end
	local newCoreName = (chosen.name:lower() .. " soul core")
	local newCoreType = ItemType(newCoreName)
	if not newCoreType or newCoreType:getId() == 0 then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Soul core for " .. chosen.name .. " not found.")
		return false
	end

	local newCoreId = newCoreType:getId()
	return deliverTransformedCore(
		player,
		item,
		target,
		newCoreId,
		"Soul Prism used successfully! The soul core has been transformed into " .. chosen.name .. "."
	)
end

soulPrism:id(SoulPit.itemIds.soulPrism)
soulPrism:register()
