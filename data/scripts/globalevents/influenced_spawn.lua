local CONFIG = {
    maxInfluenced    = 24,
    maxFiendish      = 7,
    spawnInterval    = 270,   -- 4min30s between spawns
    expireTime       = 3600,  -- 1 hour without death = expire
    sliverItemId     = 37109,
    starLevels = {
        [1] = { hpMult = 1.50, dmgMult = 1.35, sliverMin = 1,  sliverMax = 3, chanceMult = 75.00},
        [2] = { hpMult = 1.65, dmgMult = 1.45, sliverMin = 2,  sliverMax = 6, chanceMult = 82.25},
        [3] = { hpMult = 1.80, dmgMult = 1.55, sliverMin = 3,  sliverMax = 9, chanceMult = 95.30},
        [4] = { hpMult = 1.95, dmgMult = 1.65, sliverMin = 4,  sliverMax = 12, chanceMult = 120.75},
        [5] = { hpMult = 2.10, dmgMult = 1.75, sliverMin = 5,  sliverMax = 15, chanceMult = 150.55},
    },
    fiendish = {
        hpMult      = 3.00,  -- 3x health
        dmgMult     = 1.80,  -- 1.8x damage
        expMult     = 3.00,  -- 3x experience
        dustMin     = 10,    -- Min dust rewarded
        dustMax     = 25,    -- Max dust rewarded
        spawnChance = 30,   -- 30% chance to spawn on each interval tick if below limit
    },
}

local blockedNameParts = {
    "trainer",
    "training",
    "dummy",
}

-- Helper Functions
local function getMiniBotLootMultiplier(player)
    if not player or not AstraHelper or not AstraHelper.getMiniBotLootMultiplier then
        return 1
    end
    return math.max(0, tonumber(AstraHelper.getMiniBotLootMultiplier(player)) or 1)
end

local function getFreeTile(pos, radius)
    for r = 1, radius do
        for dx = -r, r do
            for dy = -r, r do
                if math.abs(dx) == r or math.abs(dy) == r then
                    local testPos = Position(pos.x + dx, pos.y + dy, pos.z)
                    local tile = Tile(testPos)
                    if tile and not tile:hasFlag(TILESTATE_BLOCKSOLID) and not tile:getTopCreature() then
                        return testPos
                    end
                end
            end
        end
    end
    return nil
end

local function hasBlockedName(name)
    local lowerName = name:lower()
    for _, part in ipairs(blockedNameParts) do
        if lowerName:find(part, 1, true) then
            return true
        end
    end
    return false
end

local function isCommonForgeMonster(monster)
    if not monster or monster:getMaster() then
        return false
    end

    local monsterType = monster:getType()
    if not monsterType then
        return false
    end

    if monsterType:isBoss() or monsterType:isRewardBoss() then
        return false
    end

    if not monsterType:isAttackable() or not monsterType:isHostile() then
        return false
    end

    if monsterType:experience() <= 0 then
        return false
    end

    return not hasBlockedName(monster:getName())
end

local function spawnForgeMonster(spawnType)
    local allMonsters = Game.getMonsters()
    if #allMonsters == 0 then
        return false
    end

    local candidates = {}
    for _, m in ipairs(allMonsters) do
        if not m:isInfluenced() and not m:isFiendish() and isCommonForgeMonster(m) then
            candidates[#candidates + 1] = m
        end
    end

    if #candidates == 0 then
        return false
    end

    local source = candidates[math.random(#candidates)]
    local sourcePos = source:getPosition()
    local freePos = getFreeTile(sourcePos, 2)

    if not freePos then
        return false
    end

    local monsterName = source:getName()
    local newMonster = Game.createMonster(monsterName, freePos, true, true)
    if not newMonster then
        return false
    end

    local now = os.time()
    newMonster:setStorageValue(PlayerStorageKeys.influencedSpawnTime, now)
    newMonster:registerEvent("InfluencedDeath")
    newMonster:registerEvent("InfluencedDamage")

    if spawnType == "fiendish" then
        newMonster:setFiendish(true)
        newMonster:rename(string.format("%s (Fiendish)", monsterName))

        local baseHP = newMonster:getMaxHealth()
        local newHP = math.floor(baseHP * CONFIG.fiendish.hpMult)
        newMonster:setMaxHealth(newHP)
        newMonster:setHealth(newHP)
    else
        local level = math.random(1, 5)
        newMonster:setInfluenced(true)
        newMonster:setInfluencedLevel(level)
        newMonster:rename(string.format("%s (Level %d)", monsterName, level))

        local starData = CONFIG.starLevels[level]
        local baseHP = newMonster:getMaxHealth()
        local newHP = math.floor(baseHP * starData.hpMult)
        newMonster:setMaxHealth(newHP)
        newMonster:setHealth(newHP)
    end

    return true
end

local influencedSpawn = GlobalEvent("InfluencedSpawn")
function influencedSpawn.onThink(interval)
    if not configManager.getBoolean(configKeys.FORGE_SYSTEM_ENABLED) then
        return true
    end

    local now = os.time()

    local influencedList = Game.getInfluencedCreatures()
    for _, monster in ipairs(influencedList) do
        local spawnTime = monster:getStorageValue(PlayerStorageKeys.influencedSpawnTime)
        if spawnTime and spawnTime > 0 and (now - spawnTime) >= CONFIG.expireTime then
            monster:setInfluenced(false)
            monster:remove()
        end
    end

    local fiendishList = Game.getFiendishCreatures()
    for _, monster in ipairs(fiendishList) do
        local spawnTime = monster:getStorageValue(PlayerStorageKeys.influencedSpawnTime)
        if spawnTime and spawnTime > 0 and (now - spawnTime) >= CONFIG.expireTime then
            monster:setFiendish(false)
            monster:remove()
        end
    end

    local influencedCount = #Game.getInfluencedCreatures()
    if influencedCount < CONFIG.maxInfluenced then
        spawnForgeMonster("influenced")
    end

    local fiendishCount = #Game.getFiendishCreatures()
    if fiendishCount < CONFIG.maxFiendish then
        if math.random(1, 100) <= CONFIG.fiendish.spawnChance then
            spawnForgeMonster("fiendish")
        end
    end

    return true
end
influencedSpawn:interval(CONFIG.spawnInterval * 4000)
influencedSpawn:register()

local influencedDamage = CreatureEvent("InfluencedDamage")
function influencedDamage.onHealthChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType, origin)
    if not attacker or not attacker:isMonster() then
        return primaryDamage, primaryType, secondaryDamage, secondaryType
    end

    local monster = attacker:getMonster()
    if not monster then
        return primaryDamage, primaryType, secondaryDamage, secondaryType
    end

    if monster:isFiendish() then
        primaryDamage = math.floor(primaryDamage * CONFIG.fiendish.dmgMult)
        secondaryDamage = math.floor(secondaryDamage * CONFIG.fiendish.dmgMult)
        return primaryDamage, primaryType, secondaryDamage, secondaryType
    end

    if not monster:isInfluenced() then
        return primaryDamage, primaryType, secondaryDamage, secondaryType
    end

    local level = monster:getInfluencedLevel()
    if level < 1 or level > 5 then
        return primaryDamage, primaryType, secondaryDamage, secondaryType
    end

    local starData = CONFIG.starLevels[level]
    if not starData then
        return primaryDamage, primaryType, secondaryDamage, secondaryType
    end

    primaryDamage = math.floor(primaryDamage * starData.dmgMult)
    secondaryDamage = math.floor(secondaryDamage * starData.dmgMult)

    return primaryDamage, primaryType, secondaryDamage, secondaryType
end
influencedDamage:register()

local influencedDeath = CreatureEvent("InfluencedDeath")
function influencedDeath.onDeath(creature, corpse, killer, mostDamageKiller, lastHitUnjustified, mostDamageUnjustified)
    if not configManager.getBoolean(configKeys.FORGE_SYSTEM_ENABLED) then
        return true
    end

    if not creature or not creature:isMonster() then
        return true
    end

    local monster = creature:getMonster()
    if not monster then
        return true
    end

    local player = nil
    if mostDamageKiller and mostDamageKiller:isPlayer() then
        player = mostDamageKiller:getPlayer()
    elseif killer and killer:isPlayer() then
        player = killer:getPlayer()
    end

    if not player then
        return true
    end

    if monster:isFiendish() then
        local lootMultiplier = getMiniBotLootMultiplier(player)
        -- Guaranteed Dust used no chance roll before Task mode. Keep that exact
        -- RNG path when multiplier=1 and add only the Task gate when reduced.
        if lootMultiplier >= 1 or (lootMultiplier > 0 and math.random() <= lootMultiplier) then
            local dustAmount = math.random(CONFIG.fiendish.dustMin, CONFIG.fiendish.dustMax)
            player:addForgeDust(dustAmount)
            player:sendTextMessage(
                MESSAGE_INFO_DESCR,
                string.format("You killed a Fiendish monster and received %d Dust!", dustAmount)
            )
        end

        local baseExp = monster:getType():experience()
        if baseExp > 0 then
            local extraExp = math.floor(baseExp * (CONFIG.fiendish.expMult - 1))
            if extraExp > 0 then
                local experienceBefore = player:getExperience()
                player:addExperience(extraExp, true)
                local awardedExtraExp = math.max(0, player:getExperience() - experienceBefore)
                if awardedExtraExp > 0 then
                    player:sendTextMessage(
                        MESSAGE_EXPERIENCE,
                        string.format(
                            "You gained %d extra experience points from the Fiendish monster.",
                            awardedExtraExp
                        )
                    )
                end
            end
        end
        return true
    end

    if not monster:isInfluenced() then
        return true
    end

    if not isCommonForgeMonster(monster) then
        return true
    end

    local level = monster:getInfluencedLevel()
    if level < 1 or level > 5 then
        level = 1
    end

    local starData = CONFIG.starLevels[level]
    local chance = starData.chanceMult or 100
    local lootMultiplier = getMiniBotLootMultiplier(player)
    if math.random(1, 10000) <= (chance * 100 * lootMultiplier) then
        local sliverAmount = math.random(starData.sliverMin, starData.sliverMax)
        if corpse and corpse:isContainer() then
            corpse:addItem(CONFIG.sliverItemId, sliverAmount)
        end
    end
    return true
end
influencedDeath:register()

local influencedLogin = CreatureEvent("InfluencedLogin")
function influencedLogin.onLogin(player)
    player:registerEvent("InfluencedDamage")
    return true
end
influencedLogin:register()
