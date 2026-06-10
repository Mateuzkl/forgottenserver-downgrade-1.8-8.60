local STORAGE_MEHAH_CLIENT = 99999 -- Must match extendedopcode.lua

local function getCombatTypeByte(combatType)
    local types = {
        [COMBAT_PHYSICALDAMAGE] = 0,
        [COMBAT_FIREDAMAGE] = 1,
        [COMBAT_EARTHDAMAGE] = 2,
        [COMBAT_ENERGYDAMAGE] = 3,
        [COMBAT_ICEDAMAGE] = 4,
        [COMBAT_HOLYDAMAGE] = 5,
        [COMBAT_DEATHDAMAGE] = 6,
        [COMBAT_HEALING] = 7,
        [COMBAT_DROWNDAMAGE] = 8,
        [COMBAT_LIFEDRAIN] = 9,
        [COMBAT_MANADRAIN] = 10,
    }
    return types[combatType] or 0
end

local function sendImpactTracker(player, analyzerType, amount, effect, targetName)
    if not player or not amount or amount == 0 then return end
    if player:getStorageValue(STORAGE_MEHAH_CLIENT) ~= 1 and not player:isUsingOtClient() then return end
    local out = NetworkMessage(player)
    out:addByte(0xCC) -- OPCODE_IMPACT_TRACKER
    out:addByte(analyzerType) -- 0: HEAL, 1: DAMAGE_DEALT, 2: DAMAGE_RECEIVED
    out:addU32(math.abs(amount))

    if analyzerType == 1 then
        out:addByte(effect)
    elseif analyzerType == 2 then
        out:addByte(effect)
        out:addString(targetName or "Unknown")
    end
    out:sendToPlayer(player)
end

local function processImpact(creature, attacker, damage, combatType)
    local amount = math.abs(damage)
    if amount == 0 then return end

    local isHealing = (combatType == COMBAT_HEALING)

    if isHealing then
        if creature and creature:isPlayer() then
            sendImpactTracker(creature, 0, amount, getCombatTypeByte(combatType))
        end
    else
        -- It is Damage
        if attacker and attacker:isPlayer() then
            -- Damage Dealt
            sendImpactTracker(attacker, 1, amount, getCombatTypeByte(combatType))
        end
        if creature and creature:isPlayer() then
            -- Damage Received
            local targetName = "Environment"
            if attacker then targetName = attacker:getName() end
            sendImpactTracker(creature, 2, amount, getCombatTypeByte(combatType), targetName)
        end
    end
end

local impactGain = CreatureEvent("impactTrackerGain")
function impactGain.onHealthChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType, origin)
    processImpact(creature, attacker, primaryDamage, primaryType)
    if secondaryDamage and secondaryDamage ~= 0 then
        processImpact(creature, attacker, secondaryDamage, secondaryType)
    end
    return primaryDamage, primaryType, secondaryDamage, secondaryType
end
impactGain:register()

local impactLogin = CreatureEvent("impactTrackerLogin")
function impactLogin.onLogin(player)
    player:registerEvent("impactTrackerGain")
    return true
end
impactLogin:register()

-- Global hook to ensure ALL monsters get the event as soon as they are targeted or hit
local ec = Event()
function ec.onTargetCombat(player, target)
    if target and target:isMonster() then
        target:registerEvent("impactTrackerGain")
    end
    return RETURNVALUE_NOERROR
end
ec:register()

-- Fallback for new spawns
local monsterSpawn = Event()
function monsterSpawn.onSpawn(monster)
    monster:registerEvent("impactTrackerGain")
    return true
end
monsterSpawn:register()
