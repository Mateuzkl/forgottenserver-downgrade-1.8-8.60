-- Soulpit Fight: Main encounter script.
-- Handles obelisk activation, wave spawning, boss abilities, and arena cleanup.
-- Ported from Crystal Server. Uses native Lua — no JSON, no extended opcodes.

-- Guard: only load if Soulpit system is enabled
if not configManager or not configManager.getBoolean then
	return
end

if not configManager.getBoolean(configKeys.SOULPIT_SYSTEM_ENABLED) then
	return
end

-- Load Soulpit library
if not SoulPit then
	dofile("data/lib/others/soulpit.lua")
end

-- ============================================
-- ENCOUNTER STATE
-- ============================================

SoulPit.encounter = false
SoulPit.monitorEvent = nil
SoulPit.kickTimerEvent = nil

-- ============================================
-- ZONE SETUP
-- ============================================

if Zone then
	-- Create the soulpit zone
	local zone = Zone("soulpit")
	if zone then
		zone:addArea(SoulPit.zoneArea.fromPos, SoulPit.zoneArea.toPos)
		zone:setRemoveDestination(SoulPit.exitDestination)

		-- When all players leave, reset the encounter
		function zone.afterLeave(zoneObj, creature)
			if not creature or not creature:isPlayer() then
				return
			end

			-- Check if any players remain
			local players = zoneObj:getPlayers()
			if #players == 0 then
				-- Reset encounter
				if SoulPit.encounter then
					SoulPit.encounter = false
					if SoulPit.monitorEvent then
						stopEvent(SoulPit.monitorEvent)
						SoulPit.monitorEvent = nil
					end
					if SoulPit.kickTimerEvent then
						stopEvent(SoulPit.kickTimerEvent)
						SoulPit.kickTimerEvent = nil
					end
				end

				-- Deactivate obelisk
				local obeliskTile = Tile(SoulPit.obeliskPos)
				if obeliskTile then
					local obeliskItem = obeliskTile:getItemById(SoulPit.obeliskActiveId)
					if obeliskItem then
						obeliskItem:transform(SoulPit.obeliskInactiveId)
					end
				end
			end
		end

		SoulPit.zone = zone
		SoulPit.log("Zone created successfully")
	end
end

-- ============================================
-- HELPER FUNCTIONS
-- ============================================

local function delayCallback(delay, callback, ...)
	return addEvent(callback, delay, ...)
end

local function getMonstersInZone()
	if not SoulPit.zone then
		return {}
	end

	local monsters = {}
	local spectators = Game.getSpectators(SoulPit.zoneArea.fromPos, SoulPit.zoneArea.toPos, false, false)
	if spectators then
		for _, creature in ipairs(spectators) do
			if creature and creature:isMonster() then
				monsters[#monsters + 1] = creature
			end
		end
	end
	return monsters
end

local function getPlayersInZone()
	if not SoulPit.zone then
		return {}
	end

	local players = {}
	local spectators = Game.getSpectators(SoulPit.zoneArea.fromPos, SoulPit.zoneArea.toPos, false, true)
	if spectators then
		for _, creature in ipairs(spectators) do
			if creature and creature:isPlayer() then
				players[#players + 1] = creature
			end
		end
	end
	return players
end

-- ============================================
-- WAVE SPAWNING
-- ============================================

local function spawnMonsterWave(monsterName, waveIndex)
	if not SoulPit.encounter then return end
	if not SoulPit.waves[waveIndex] then return end

	local wave = SoulPit.waves[waveIndex]
	local stage = SoulPit.encounter.currentStage

	for stack, count in pairs(wave.stacks) do
		for i = 1, count do
			local spawnPos
			if stack == 40 then
				-- Boss spawns at center
				spawnPos = SoulPit.obeliskPos
			else
				if not SoulPit.zone then return end
				spawnPos = SoulPit.zone:randomPosition()
			end

			-- Visual effect
			local effect = SoulPit.effects[stack] or CONST_ME_TELEPORT
			spawnPos:sendMagicEffect(effect)

			-- Create monster with delay
			delayCallback(SoulPit.timeToSpawnMonsters, function()
				if not SoulPit.encounter then return end

				-- Create monster
				local monster
				if Game and Game.createSoulPitMonster then
					monster = Game.createSoulPitMonster(monsterName, spawnPos, stack)
				else
					monster = Game.createMonster(monsterName, spawnPos)
				end

				if monster then
					-- Apply boss abilities for stack 40
					if stack == 40 then
						local abilityName = SoulPit.possibleAbilities[math.random(#SoulPit.possibleAbilities)]
						local applyFunc = SoulPit.bossAbilities[abilityName]
						if applyFunc then
							SoulPit.log("Applying boss ability: " .. abilityName)
							applyFunc(monster)
						end
					end
				end
			end)
		end
	end
end

-- ============================================
-- ENCOUNTER/STAGE LOGIC
-- ============================================

local function checkStageCompletion()
	if not SoulPit.encounter then return end

	local stage = SoulPit.encounter.currentStage
	local maxStages = #SoulPit.waves

	local monsters = getMonstersInZone()
	if #monsters == 0 then
		-- Stage complete
		if stage >= maxStages then
			-- All stages complete — encounter won!
			local monsterName = SoulPit.encounter and SoulPit.encounter.monsterName
			SoulPit.encounter = false

			-- Reward all players
			local players = getPlayersInZone()
			for _, player in ipairs(players) do
				player:sendTextMessage(MESSAGE_INFO_DESCR, "You have conquered the Soulpit!")
				-- Give animus mastery if available
				if monsterName and player.addAnimusMastery then
					player:addAnimusMastery(monsterName)
				end
				-- Teleport out
				player:teleportTo(SoulPit.exitDestination)
				SoulPit.exitDestination:sendMagicEffect(CONST_ME_TELEPORT)
			end

			-- Deactivate obelisk
			local obeliskTile = Tile(SoulPit.obeliskPos)
			if obeliskTile then
				local obeliskItem = obeliskTile:getItemById(SoulPit.obeliskActiveId)
				if obeliskItem then
					obeliskItem:transform(SoulPit.obeliskInactiveId)
				end
			end

			if SoulPit.monitorEvent then
				stopEvent(SoulPit.monitorEvent)
				SoulPit.monitorEvent = nil
			end
			if SoulPit.kickTimerEvent then
				stopEvent(SoulPit.kickTimerEvent)
				SoulPit.kickTimerEvent = nil
			end

			SoulPit.log("Encounter completed! All waves cleared.")
		else
			-- Advance to next stage
			SoulPit.encounter.currentStage = stage + 1
			SoulPit.log("Stage " .. stage .. " complete. Spawning stage " .. (stage + 1))
			spawnMonsterWave(SoulPit.encounter.monsterName, stage + 1)
		end
	end
end

local function monitorEncounter()
	if not SoulPit.encounter then
		SoulPit.monitorEvent = nil
		return
	end

	checkStageCompletion()

	-- Schedule next check
	if SoulPit.encounter then
		SoulPit.monitorEvent = addEvent(monitorEncounter, SoulPit.checkMonstersDelay)
	end
end

-- ============================================
-- KICK TIMER (auto-kick after timeout)
-- ============================================

local function startKickTimer()
	SoulPit.kickTimerEvent = addEvent(function()
		if not SoulPit.encounter then return end

		SoulPit.log("Kick timer expired. Removing all players from arena.")

		local players = getPlayersInZone()
		for _, player in ipairs(players) do
			player:teleportTo(SoulPit.exitDestination)
			SoulPit.exitDestination:sendMagicEffect(CONST_ME_TELEPORT)
			player:sendTextMessage(MESSAGE_INFO_DESCR, "You did not complete the Soulpit in time!")
		end

		-- Clean up
		SoulPit.encounter = false
		SoulPit.monitorEvent = nil
		SoulPit.kickTimerEvent = nil

		-- Deactivate obelisk
		local obeliskTile = Tile(SoulPit.obeliskPos)
		if obeliskTile then
			local obeliskItem = obeliskTile:getItemById(SoulPit.obeliskActiveId)
			if obeliskItem then
				obeliskItem:transform(SoulPit.obeliskInactiveId)
			end
		end
	end, SoulPit.timeToKick)
end

-- ============================================
-- OBELISK ACTION (soul core use)
-- ============================================

local soulPitAction = Action()

function soulPitAction.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if not player or not item then
		return false
	end

	if item:getId() == SoulPit.obeliskInactiveId then
		if not player.isUsingAstraClient or not player:isUsingAstraClient() then
			return false
		end
		if not TaskBoardSoulSealHandler or not TaskBoardSoulSealHandler.sendSoulSealsData then
			return false
		end
		return TaskBoardSoulSealHandler.sendSoulSealsData(player)
	end

	-- Check if target is the inactive obelisk
	if not target or not target.isItem or not target:isItem() then
		return false
	end

	local targetId = target:getId()
	if targetId ~= SoulPit.obeliskInactiveId then
		-- Check if it's soul core fusion (using one soul core on another)
		if targetId == item:getId() then
			return SoulPit.onFuseSoulCores(player, item, target)
		end
		return false
	end

	-- Target position must be the obelisk position
	if toPosition ~= SoulPit.obeliskPos then
		return false
	end

	-- Get item name and extract monster name
	local itemName = item:getName()
	local monsterName = SoulPit.getSoulCoreMonster(itemName)
	if not monsterName then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "This is not a valid soul core.")
		return false
	end

	-- Check for variation mapping
	local variationName = SoulPit.getMonsterVariationNameBySoulCore(monsterName)
	if variationName then
		monsterName = variationName:match("^(.-) soul core") or monsterName
	end

	-- Validate monster exists
	local monsterType = MonsterType(monsterName)
	if not monsterType then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "This creature does not exist.")
		return false
	end

	-- Level check
	if player:getLevel() < 100 then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You need level 100 to enter Soulpit.")
		return false
	end

	-- Check if encounter is already running
	if SoulPit.encounter then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "A Soulpit encounter is already in progress.")
		return false
	end

	-- Consume the soul core
	item:remove(1)

	-- Activate obelisk (transform inactive -> active)
	local obeliskTile = Tile(SoulPit.obeliskPos)
	if obeliskTile then
		local obeliskItem = obeliskTile:getItemById(SoulPit.obeliskInactiveId)
		if obeliskItem then
			obeliskItem:transform(SoulPit.obeliskActiveId)
		end
	end

	-- Start encounter
	SoulPit.encounter = {
		monsterName = monsterName,
		currentStage = 1,
		startTime = os.time(),
	}

	SoulPit.log("Encounter started with monster: " .. monsterName)

	-- Spawn first wave
	spawnMonsterWave(monsterName, 1)

	-- Start monitoring and kick timer after the spawn delay
	addEvent(function()
		if not SoulPit.encounter then return end
		monitorEncounter()
		startKickTimer()
	end, SoulPit.timeToSpawnMonsters + 500)

	-- Teleport player into arena
	player:teleportTo(SoulPit.playerExitDestination)
	SoulPit.playerExitDestination:sendMagicEffect(CONST_ME_TELEPORT)

	return true
end

-- Register for all soul core items (any item that has "soul core" in the name will work)
-- The script checks the obelisk target ID, so it only activates on the obelisk
-- TFS useItemEx dispatches by the INVENTORY item ID (the held item).
-- Register on soul core items for obelisk activation and fusion.
-- Skip prism/exalted core (they have dedicated actions).
local skipIds = {
	[SoulPit.itemIds.soulPrism] = true,
	[SoulPit.itemIds.exaltedCore] = true,
}
local registeredIds = {}
for id = SoulPit.itemIds.ominousSoulCore - 100, SoulPit.itemIds.ominousSoulCore + 200 do
	if skipIds[id] then goto continue end
	local itemType = ItemType(id)
	if itemType and itemType:getId() ~= 0 then
		local name = itemType:getName():lower()
		if name:find("soul core") and not registeredIds[id] then
			soulPitAction:id(id)
			registeredIds[id] = true
		end
	end
	::continue::
end
-- Also register the obelisks as use-targets
soulPitAction:id(SoulPit.obeliskActiveId)
soulPitAction:id(SoulPit.obeliskInactiveId)
soulPitAction:register()

-- Export monitor and kick timer for network-based encounter start
SoulPit.startMonitor = function()
    if not SoulPit.encounter then return end
    SoulPit.monitorEvent = addEvent(monitorEncounter, SoulPit.checkMonstersDelay)
end

SoulPit.startKick = function()
    if not SoulPit.encounter then return end
    startKickTimer()
end

SoulPit.log("Soulpit fight script loaded")
