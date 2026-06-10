local OPCODE_LANGUAGE = 1
local OPCODE_MEHAH_ID = 50
local OPCODE_BATTLEPASS = 225
local STORAGE_MEHAH_CLIENT = 99999 -- Storage key to mark Mehah clients

local extendedOpcode = CreatureEvent("ExtendedOpcode")
function extendedOpcode.onExtendedOpcode(player, opcode, buffer)
    if opcode == OPCODE_LANGUAGE then
        -- language opcode received
    elseif opcode == OPCODE_MEHAH_ID then
        if buffer == "Mehah" then
            player:setStorageValue(STORAGE_MEHAH_CLIENT, 1)
        end
    elseif opcode == OPCODE_BATTLEPASS then
        if BattlePassSystem and BattlePassSystem.onExtendedOpcode then
            return BattlePassSystem.onExtendedOpcode(player, buffer)
        end
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
