-- Soul Seal Network Handler
-- Receives soulseal fight actions from the client (opcode 0xBA) and manages soulseal data.
-- Wire format: client sends opcode 0xBA (186) + U16 raceId
-- Ported from Crystal Server. Uses native bytes — no JSON, no extended opcodes.

if not configManager or not configManager.getBoolean then
    return
end

if not configManager.getBoolean(configKeys.TASK_HUNTING_SYSTEM_ENABLED) then
    return
end

if not configManager.getBoolean(configKeys.SOULSEALS_SYSTEM_ENABLED) then
    return
end

-- Load SoulPit library if not already loaded
if not SoulPit then
    dofile("data/lib/others/soulpit.lua")
end

local protocol -- set by init.lua

local SoulSealHandler = {}

local function isNearSoulpitObelisk(player)
    local playerPosition = player and player:getPosition()
    local obeliskPosition = SoulPit and SoulPit.obeliskPos
    if not playerPosition or not obeliskPosition or playerPosition.z ~= obeliskPosition.z then
        return false
    end

    return math.abs(playerPosition.x - obeliskPosition.x) <= 1
        and math.abs(playerPosition.y - obeliskPosition.y) <= 1
end

-- ============================================
-- SOULSEAL DATA (send creature list to client)
-- ============================================

function SoulSealHandler.sendSoulSealsData(player)
    if not protocol or not player then
        return false
    end

    if not isNearSoulpitObelisk(player) then
        player:sendTextMessage(MESSAGE_INFO_DESCR, "Stand next to the Soulpit obelisk to open Soulseals.")
        return false
    end

    local entries = SoulPit.buildSoulsealEntries()
    if #entries == 0 then
        return false -- No bestiary data available
    end

    TaskBoard.sendResourceBalance(player, TaskBoard.Resources.SOULSEALS_POINTS)
    local balance = player:getSoulsealsPoints()
    return protocol.sendSoulSealsData(player, entries, balance)
end

-- ============================================
-- PACKET HANDLER: 0xBA (Client -> Server)
-- ============================================

local SOULSEAL_OPCODE = 0xBA
local soulSealActionHandler = PacketHandler(SOULSEAL_OPCODE)

function soulSealActionHandler.onReceive(player, msg)
    if not player or not SoulPit then
        return
    end

    if not player.isUsingAstraClient or not player:isUsingAstraClient() then
        return
    end

    if not isNearSoulpitObelisk(player) then
        player:sendTextMessage(MESSAGE_INFO_DESCR, "Stand next to the Soulpit obelisk to start a fight.")
        return
    end

    -- Read raceId (U16) — matches AstraClient sendSoulSealsAction wire format
    if (msg:len() - msg:tell()) < 2 then
        return -- Invalid payload
    end

    local raceId = NetworkGuard and NetworkGuard.readU16 and NetworkGuard.readU16(msg)
    if not raceId then
        -- Fallback: read directly
        local ok, val = pcall(msg.getU16, msg)
        if not ok then return end
        raceId = val
    end

    if not raceId or raceId <= 0 then
        player:sendTextMessage(MESSAGE_INFO_DESCR, "Invalid creature selected.")
        return
    end

    -- Look up monster in bestiary
    if not CustomBestiary or not CustomBestiary.getMonster then
        player:sendTextMessage(MESSAGE_INFO_DESCR, "Bestiary system is not available.")
        return
    end

    local monster = CustomBestiary.getMonster(raceId)
    if not monster then
        player:sendTextMessage(MESSAGE_INFO_DESCR, "Unknown creature. Race ID: " .. tostring(raceId))
        return
    end

    local monsterName = monster.name
    if not monsterName or monsterName == "" then
        player:sendTextMessage(MESSAGE_INFO_DESCR, "Unknown creature name.")
        return
    end

    -- Calculate cost
    local cost = SoulPit.getSoulsealCost(raceId)
    if not cost or cost <= 0 then
        player:sendTextMessage(MESSAGE_INFO_DESCR, "Cannot determine soulseal cost for this creature.")
        return
    end

    -- Check player balance
    local balance = player:getSoulsealsPoints()
    if balance < cost then
        player:sendTextMessage(MESSAGE_INFO_DESCR,
            string.format("You need %d soulseal points to fight %s. You have %d.",
                cost, monsterName, balance))
        return
    end

    -- Validate monster exists as MonsterType
    local monsterType = MonsterType(monsterName)
    if not monsterType then
        player:sendTextMessage(MESSAGE_INFO_DESCR, "This creature does not exist: " .. monsterName)
        return
    end

    -- Deduct soulseal points
    if not player:removeSoulsealsPoints(cost) then
        player:sendTextMessage(MESSAGE_INFO_DESCR, "Failed to deduct soulseal points.")
        return
    end

    -- Send updated resource balance to client
    if protocol and protocol.sendResourceBalance then
        protocol.sendResourceBalance(player, protocol.RESOURCE_SOULSEALS_POINTS, player:getSoulsealsPoints())
    end

    -- Start the SoulPit encounter
    local ok, err = SoulPit.startEncounter(player, monsterName)
    if not ok then
        -- Refund points on failure
        player:addSoulsealsPoints(cost)
        if protocol and protocol.sendResourceBalance then
            protocol.sendResourceBalance(player, protocol.RESOURCE_SOULSEALS_POINTS, player:getSoulsealsPoints())
        end
        player:sendTextMessage(MESSAGE_INFO_DESCR, err or "Failed to start Soulpit encounter.")
        return
    end

    player:sendTextMessage(MESSAGE_INFO_DESCR,
        string.format("Soulpit encounter started! Fighting %s for %d soulseal points.",
            monsterName, cost))
end

soulSealActionHandler:register()

-- ============================================
-- API
-- ============================================

function SoulSealHandler.setProtocol(p)
    protocol = p
end

return SoulSealHandler
