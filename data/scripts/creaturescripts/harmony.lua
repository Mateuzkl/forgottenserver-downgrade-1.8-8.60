local function isMonk(player)
    local vocId = player:getVocation():getId()
    return vocId == 9 or vocId == 10
end

-- Sync on Login
local harmonyLogin = CreatureEvent("harmonyLogin")
function harmonyLogin.onLogin(player)
    if isMonk(player) then
        syncHarmonyOpcode(player)
        player:registerEvent("harmonyGain")
    end
    return true
end
harmonyLogin:register()

-- Gain Harmony on Hit
local harmonyGain = CreatureEvent("harmonyGain")
function harmonyGain.onHealthChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType, origin)
    if attacker and attacker:isPlayer() and isMonk(attacker) then
        if primaryDamage < 0 and creature ~= attacker then
            addHarmonyPoint(attacker)
        end
    end
    return primaryDamage, primaryType, secondaryDamage, secondaryType
end
harmonyGain:register()
