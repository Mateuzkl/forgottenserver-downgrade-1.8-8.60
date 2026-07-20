local OPCODE_LANGUAGE = 1
local OPCODE_MEHAH_ID = 50
local STORAGE_MEHAH_CLIENT = 99999 -- Storage key to mark Mehah clients

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

local ticker = GlobalEvent("MiniBotStateTicker")
function ticker.onThink(interval)
    if not AstraHelper then
        return true
    end

    for _, player in ipairs(Game.getPlayers()) do
        -- Publishing every session is intentional: this transition disables an
        -- expired/banned cavebot even if no client request is received.
        AstraHelper.sendMiniBotState(player)
    end
    return true
end
ticker:interval(30000)
ticker:register()
