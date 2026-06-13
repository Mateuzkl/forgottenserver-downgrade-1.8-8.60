-- Soulpit System: Arena configuration, soul core fusion, monster variations.
-- Ported from Crystal Server. Uses native code — no JSON, no extended opcodes.

SoulPit = SoulPit or {}

-- ============================================
-- SOUL CORES CONFIGURATION
-- ============================================

SoulPit.SoulCoresConfiguration = {
	chanceToGetSameMonsterSoulCore = 15,   -- 15%
	chanceToDropSoulCore = 5,              -- 5%
	chanceToGetOminousSoulCore = 2,        -- 2%
	chanceToDropSoulPrism = 4,             -- 4%
	monsterVariationsSoulCore = {
		["horse"] = "horse soul core (taupe)",
		["brown horse"] = "horse soul core (brown)",
		["grey horse"] = "horse soul core (gray)",
		["nomad"] = "nomad soul core (basic)",
		["nomad blue"] = "nomad soul core (blue)",
		["nomad female"] = "nomad soul core (female)",
		["purple butterfly"] = "butterfly soul core (purple)",
		["butterfly"] = "butterfly soul core (blue)",
		["blue butterfly"] = "butterfly soul core (blue)",
		["red butterfly"] = "butterfly soul core (red)",
	},
	monstersDifficulties = {
		["Harmless"] = 1,
		["Trivial"] = 2,
		["Easy"] = 3,
		["Medium"] = 4,
		["Hard"] = 5,
		["Challenge"] = 6,
	},
}

-- ============================================
-- WAVES CONFIGURATION
-- ============================================

SoulPit.waves = {
	[1] = { stacks = { [1] = 7 } },                      -- 7 regular monsters (stack 1)
	[2] = { stacks = { [1] = 4, [5] = 3 } },              -- 4 stack-1 + 3 stack-5
	[3] = { stacks = { [1] = 5, [15] = 2 } },             -- 5 stack-1 + 2 stack-15
	[4] = { stacks = { [1] = 3, [5] = 3, [40] = 1 } },   -- 3 stack-1 + 3 stack-5 + 1 boss
}

SoulPit.effects = {
	[1] = CONST_ME_TELEPORT,
	[5] = CONST_ME_TELEPORT, -- fallback if CONST_ME_ORANGETELEPORT not available
	[15] = CONST_ME_TELEPORT, -- fallback if CONST_ME_REDTELEPORT not available
	[40] = CONST_ME_TELEPORT, -- fallback if CONST_ME_PURPLETELEPORT not available
}

-- Boss ability pool (random each boss spawn)
SoulPit.possibleAbilities = {
	"overpowerSoulPit",
	"enrageSoulPit",
	"opressorSoulPit",
}

-- ============================================
-- BOSS ABILITIES
-- ============================================

SoulPit.bossAbilities = {
	-- Overpower: +50% crit chance, +25% crit damage
	overpowerSoulPit = function(monster)
		if not monster then return end
		local ok, err = pcall(function()
			monster:criticalChance(50)
			monster:criticalDamage(25)
		end)
		if not ok then
			print("[Soulpit] Warning: overpowerSoulPit failed: " .. tostring(err))
		end
	end,

	-- Enrage: damage reduction based on HP threshold (handled in creatureevent)
	enrageSoulPit = function(monster)
		if not monster then return end
		local ok, err = pcall(function()
			monster:registerEvent("SoulPitEnrage")
		end)
		if not ok then
			print("[Soulpit] Warning: enrageSoulPit failed: " .. tostring(err))
		end
	end,

	-- Opressor: adds boss spells (handled in fight script)
	opressorSoulPit = function(monster)
		if not monster then return end
		local ok, err = pcall(function()
			monster:addAttackSpell("soulpit opressor", 2000, 25)
			monster:addAttackSpell("soulpit powerless", 2000, 30)
			monster:addAttackSpell("soulpit intensehex", 2000, 15)
		end)
		if not ok then
			print("[Soulpit] Warning: opressorSoulPit failed: " .. tostring(err))
		end
	end,
}

-- ============================================
-- TIMING CONSTANTS
-- ============================================

SoulPit.timeToSpawnMonsters = 4000    -- 4 seconds effects before monsters appear
SoulPit.checkMonstersDelay = 4500     -- 4.5 seconds between stage checks
SoulPit.timeToKick = 600000           -- 10 minutes auto-kick
SoulPit.totalMonsters = 7

-- ============================================
-- ZONE CONFIGURATION
-- (ADJUST THESE TO YOUR MAP!)
-- ============================================

-- Soulpit zone area: { fromPos = {x, y, z}, toPos = {x, y, z} }
SoulPit.zoneArea = {
	fromPos = Position(32362, 31132, 8),
	toPos = Position(32390, 31153, 8),
}

-- Obelisk position (inactive)
SoulPit.obeliskPos = Position(32375, 31157, 8)
SoulPit.obeliskInactiveId = 47367
SoulPit.obeliskActiveId = 47379

-- Entrance/exit positions
SoulPit.entrancePos = {
	{ fromPos = Position(32350, 31030, 3), toPos = Position(32374, 31171, 8) },
	{ fromPos = Position(32349, 31030, 3), toPos = Position(32374, 31171, 8) },
}

SoulPit.exitPos = Position(32374, 31173, 8)
SoulPit.exitDestination = Position(32349, 31032, 3)

-- Player spawn positions inside arena
SoulPit.playerPositions = {
	Position(32375, 31158, 8),
	Position(32375, 31159, 8),
	Position(32375, 31160, 8),
	Position(32375, 31161, 8),
	Position(32375, 31162, 8),
}

-- Player exit destination inside
SoulPit.playerExitDestination = Position(32373, 31151, 8)

-- ============================================
-- ITEM IDs
-- ============================================

SoulPit.itemIds = {
	ominousSoulCore = 49163,
	soulPrism = 49164,
	exaltedCore = 37110,
	largeObeliskInactive = 47367,
	largeObeliskActive = 47379,
}

-- ============================================
-- FUNCTIONS
-- ============================================

-- Get the base monster name from a soul core item name
function SoulPit.getSoulCoreMonster(name)
	return name and name:match("^(.-) soul core") or nil
end

-- Get variation mapping from monster type name to soul core variant name
function SoulPit.getMonsterVariationNameBySoulCore(searchName)
	local variations = SoulPit.SoulCoresConfiguration.monsterVariationsSoulCore
	-- Case-insensitive lookup (monster name -> soul core variant name)
	local lower = searchName and searchName:lower()
	if lower and variations[lower] then
		return variations[lower]
	end
	-- Fallback: iterate for partial match
	for key, value in pairs(variations) do
		if key:lower() == lower then
			return value
		end
	end
	return nil
end

-- Get difficulty name by stars count
function SoulPit.getDifficultyByStars(stars)
	local diffs = SoulPit.SoulCoresConfiguration.monstersDifficulties
	for name, count in pairs(diffs) do
		if count == stars then
			return name
		end
	end
	return "Unknown"
end

-- Get all soul core items from the game
function SoulPit.getSoulCoreItems()
	if Game and Game.getSoulCoreItems then
		return Game.getSoulCoreItems()
	end
	-- Fallback: manually list known soul cores
	return {}
end

-- Get soul core item for a monster (by name or raceId)
function SoulPit.getSoulCoreForMonster(monsterIdentifier)
	-- This would need to search through loaded items for the matching soul core
	-- The Crystal Server uses C++ Game.getSoulCoreItems() for this
	-- For the port, we store soul core item ID lookups in the soulpit_fight script
	return false
end

-- Fuse two soul cores of the same type into a random new one
function SoulPit.onFuseSoulCores(player, item, target)
	if not player or not item or not target then
		return false
	end

	-- Must be same item ID
	if item:getId() ~= target:getId() then
		return false
	end

	-- Source must have stack count <= 1
	if item:getCount() > 1 then
		return false
	end

	-- Both items must be soul cores
	local itemName = item:getName()
	local targetName = target:getName()
	local sourceMonster = SoulPit.getSoulCoreMonster(itemName)
	local targetMonster = SoulPit.getSoulCoreMonster(targetName)

	if not sourceMonster or not targetMonster then
		return false
	end

	-- Validate soul core pool before consuming items
	local soulCores = SoulPit.getSoulCoreItems()
	if #soulCores == 0 then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Soul core fusion is not yet available.")
		return false
	end

	local randomCore = soulCores[math.random(#soulCores)]
	local coreItemId = randomCore:getId()
	local coreName = randomCore:getName()

	-- Consume both items (only after validation)
	item:remove(1)
	target:remove(1)

	-- Give the fused core
	player:addItem(coreItemId, 1)

	-- Visual feedback
	Position(player:getPosition()):sendMagicEffect(CONST_ME_MAGIC_BLUE)
	player:sendTextMessage(MESSAGE_INFO_DESCR, "You have received a " .. coreName .. ".")

	return true
end

-- ============================================
-- SOULSEAL DATA (creature list for client UI)
-- ============================================

-- Build soulseal entries from the bestiary for the client UI
function SoulPit.buildSoulsealEntries()
    local entries = {}
    if not CustomBestiary or not CustomBestiary.monstersByRaceId then
        return entries
    end

    for raceId, monster in pairs(CustomBestiary.monstersByRaceId) do
        local stars = monster.stars or 1
        -- Cost formula: stars * 100 soulseal points per fight
        local cost = stars * 100
        table.insert(entries, {
            raceId = raceId,
            name = monster.name or ("Creature " .. tostring(raceId)),
            outfit = monster.outfit,
            stars = stars,
            cost = cost,
            mastered = false,
        })
    end

    -- Sort by stars then name
    table.sort(entries, function(a, b)
        if a.stars ~= b.stars then return a.stars < b.stars end
        return (a.name or "") < (b.name or "")
    end)

    return entries
end

-- Get soulseal cost for a specific race
function SoulPit.getSoulsealCost(raceId)
    local monster = CustomBestiary and CustomBestiary.getMonster(raceId)
    if not monster then
        return nil
    end
    local stars = monster.stars or 1
    return stars * 100
end

-- Start a SoulPit encounter programmatically (without physical soul core item)
-- Used by the network soulseal fight handler.
function SoulPit.startEncounter(player, monsterName)
    if not player or not monsterName then
        return false, "Invalid player or monster name."
    end

    -- Validate monster exists
    local monsterType = MonsterType(monsterName)
    if not monsterType then
        return false, "This creature does not exist."
    end

    -- Level check
    if player:getLevel() < 100 then
        return false, "You need level 100 to enter Soulpit."
    end

    -- Check if encounter is already running
    if SoulPit.encounter then
        return false, "A Soulpit encounter is already in progress."
    end

    -- Activate obelisk (transform inactive -> active)
    local obeliskTile = Tile(SoulPit.obeliskPos)
    if obeliskTile then
        local obeliskItem = obeliskTile:getItemById(SoulPit.obeliskInactiveId)
        if obeliskItem then
            obeliskItem:transform(SoulPit.obeliskActiveId)
        end
    end

    -- Start encounter state
    SoulPit.encounter = {
        monsterName = monsterName,
        currentStage = 1,
        startTime = os.time(),
    }

    SoulPit.log("Encounter started via network with monster: " .. monsterName)

    -- Spawn first wave using the spawn function from soulpit_fight
    -- We need to re-import the spawn logic here
    local function spawnMonsterWave(waveIndex)
        if not SoulPit.encounter then return end
        if not SoulPit.waves[waveIndex] then return end

        local wave = SoulPit.waves[waveIndex]

        for stack, count in pairs(wave.stacks) do
            for i = 1, count do
                local spawnPos
                if stack == 40 then
                    spawnPos = SoulPit.obeliskPos
                elseif SoulPit.zone then
                    spawnPos = SoulPit.zone:randomPosition()
                end

                if spawnPos then
                    local effect = SoulPit.effects[stack] or CONST_ME_TELEPORT
                    spawnPos:sendMagicEffect(effect)

                    addEvent(function()
                        if not SoulPit.encounter then return end
                        local monster
                        if Game and Game.createSoulPitMonster then
                            monster = Game.createSoulPitMonster(monsterName, spawnPos, stack)
                        else
                            monster = Game.createMonster(monsterName, spawnPos)
                        end

                        if monster and stack == 40 then
                            local abilityName = SoulPit.possibleAbilities[math.random(#SoulPit.possibleAbilities)]
                            local applyFunc = SoulPit.bossAbilities[abilityName]
                            if applyFunc then
                                SoulPit.log("Applying boss ability: " .. abilityName)
                                applyFunc(monster)
                            end
                        end
                    end, SoulPit.timeToSpawnMonsters)
                end
            end
        end
    end

    spawnMonsterWave(1)

    -- Start monitor and kick timer
    addEvent(function()
        if not SoulPit.encounter then return end
        -- These functions are defined in soulpit_fight.lua; call them if available
        if SoulPit.startMonitor then SoulPit.startMonitor() end
        if SoulPit.startKick then SoulPit.startKick() end
    end, SoulPit.timeToSpawnMonsters + 500)

    -- Teleport player into arena
    player:teleportTo(SoulPit.playerExitDestination)
    SoulPit.playerExitDestination:sendMagicEffect(CONST_ME_TELEPORT)

    return true, nil
end

-- ============================================
-- DEBUG / LOGGING
-- ============================================

function SoulPit.log(message)
	if logger and logger.info then
		logger.info(">> [Soulpit] " .. tostring(message))
	else
		print(">> [Soulpit] " .. tostring(message))
	end
end

return SoulPit
