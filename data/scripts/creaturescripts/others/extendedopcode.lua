local OPCODE_LANGUAGE = 1
local OPCODE_MEHAH_ID = 50
local OPCODE_ASTRA_MINIBOT = 0xD0
local STORAGE_MEHAH_CLIENT = 99999 -- Storage key to mark Mehah clients

local extendedOpcode = CreatureEvent("ExtendedOpcode")
function extendedOpcode.onExtendedOpcode(player, opcode, buffer)
    if opcode == OPCODE_LANGUAGE then
        -- language opcode received
    elseif opcode == OPCODE_MEHAH_ID then
        if buffer == "Mehah" then
            player:setStorageValue(STORAGE_MEHAH_CLIENT, 1)
        end
    elseif opcode == OPCODE_ASTRA_MINIBOT and buffer == "request" then
        -- The MiniBot uses regular game actions. This handshake only confirms
        -- that this Astra server explicitly allows the optional client module.
        player:sendExtendedOpcode(OPCODE_ASTRA_MINIBOT, "enabled")
    end
    return true
end
extendedOpcode:register()

local login = CreatureEvent("ExtendedOpcodeLogin")
function login.onLogin(player)
    player:registerEvent("ExtendedOpcode")
    return true
end
login:register()
