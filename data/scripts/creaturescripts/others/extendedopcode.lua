local OPCODE_LANGUAGE = 1
local OPCODE_MEHAH_ID = 50
-- Storage key that marks Mehah clients. PlayerStorageKeys is the source of truth.
local STORAGE_MEHAH_CLIENT = PlayerStorageKeys.astraHelperMehahClient

local extendedOpcode = CreatureEvent("ExtendedOpcode")
function extendedOpcode.onExtendedOpcode(player, opcode, buffer)
    if opcode == OPCODE_LANGUAGE then
        -- language opcode received
    elseif opcode == OPCODE_MEHAH_ID then
        if buffer == "Mehah" then
            player:setStorageValue(STORAGE_MEHAH_CLIENT, 1)
        end
    elseif AstraHelper and opcode == AstraHelper.OPCODES.Cavebot then
        AstraHelper.handleMiniBotCavebotOpcode(player, buffer)
    elseif AstraHelper and opcode == AstraHelper.OPCODES.MiniBotState then
        AstraHelper.handleMiniBotOpcode(player, buffer)
    end
    return true
end
extendedOpcode:register()

local login = CreatureEvent("ExtendedOpcodeLogin")
function login.onLogin(player)
    player:registerEvent("ExtendedOpcode")
    player:registerEvent("MiniBotLogout")
    if AstraHelper then
        AstraHelper.onMiniBotLogin(player)
    end
    return true
end
login:register()

local logout = CreatureEvent("MiniBotLogout")
function logout.onLogout(player)
    if AstraHelper then
        AstraHelper.onMiniBotLogout(player)
    end
    return true
end
logout:register()

-- There is deliberately no MiniBot global ticker. Scanning Game.getPlayers() made
-- the cost of the feature proportional to the whole server instead of to the
-- handful of players actually botting. AstraHelper now keeps one session per
-- MiniBot user and schedules a single event per session that publishes the clock,
-- refreshes the AFK indicator and switches an exhausted or banned cavebot off.
