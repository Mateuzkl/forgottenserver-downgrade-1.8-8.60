local deathListEnabled = true
local maxDeathRecords = 5

local function isMultiWorld()
    return configManager and configKeys and configKeys.MULTI_WORLD_ENABLED
        and configManager.getBoolean(configKeys.MULTI_WORLD_ENABLED) or false
end

local function currentWorldId()
    return isMultiWorld() and math.max(1, tonumber(configManager.getNumber(configKeys.WORLD_ID)) or 1) or 1
end

local function worldWhere()
    return isMultiWorld() and (" AND `world_id` = " .. currentWorldId()) or ""
end

local function insertDeath(playerGuid, timeNow, level, killerName, byPlayer, mostDamageName, byPlayerMostDamage, lastUnjustified, mostDamageUnjustified)
    if isMultiWorld() then
        return string.format("INSERT INTO `player_deaths` (`player_id`, `time`, `level`, `killed_by`, `is_player`, `mostdamage_by`, `mostdamage_is_player`, `unjustified`, `mostdamage_unjustified`, `world_id`) VALUES (%d, %d, %d, %s, %d, %s, %d, %d, %d, %d)",
            playerGuid, timeNow, level, killerName, byPlayer, mostDamageName, byPlayerMostDamage, lastUnjustified, mostDamageUnjustified, currentWorldId())
    end
    return string.format("INSERT INTO `player_deaths` (`player_id`, `time`, `level`, `killed_by`, `is_player`, `mostdamage_by`, `mostdamage_is_player`, `unjustified`, `mostdamage_unjustified`) VALUES (%d, %d, %d, %s, %d, %s, %d, %d, %d)",
        playerGuid, timeNow, level, killerName, byPlayer, mostDamageName, byPlayerMostDamage, lastUnjustified, mostDamageUnjustified)
end

local function getKiller(killer)
    if not killer then
        return false, "field item"
    end

    if killer:isPlayer() then
        return true, killer:getName()
    end

    local master = killer:getMaster()
    if master and master ~= killer and master:isPlayer() then
        return true, master:getName()
    end

    return false, killer:getName()
end

local function playerDeathSuccess(playerId, playerName, killerId, playerGuid, byPlayer, killerName, playerGuildId, killerGuildId, timeNow)
    local resultId = db.storeQuery("SELECT `player_id` FROM `player_deaths` WHERE `player_id` = " .. playerGuid .. worldWhere())
    if not resultId then
        return
    end

    local deathRecords = 0
    local tmpResultId = true
    while tmpResultId do
        tmpResultId = result.next(resultId)
        deathRecords = deathRecords + 1
    end
    result.free(resultId)

    local limit = deathRecords - maxDeathRecords
    if limit > 0 then
        db.asyncQuery("DELETE FROM `player_deaths` WHERE `player_id` = " .. playerGuid .. worldWhere() .. " ORDER BY `time` LIMIT " .. limit)
    end

    -- Guild war kills are now handled in C++ (src/player.cpp and src/guild.cpp)
    -- The code below is kept for reference but is no longer used
    --[[
    if byPlayer and playerGuildId ~= 0 and killerGuildId ~= 0 and playerGuildId ~= killerGuildId and isInWar(playerId, killerId) then
        resultId = db.storeQuery(string.format(
            "SELECT `id` FROM `guild_wars` WHERE `status` = 1 AND ((`guild1` = %d AND `guild2` = %d) OR (`guild1` = %d AND `guild2` = %d))",
            killerGuildId, playerGuildId, playerGuildId, killerGuildId
        ))

        if resultId then
            local warId = result.getNumber(resultId, "id")
            result.free(resultId)

            if warId then
                db.asyncQuery(string.format(
                    "INSERT INTO `guild_war_kills` (`war_id`, `killer_guild`, `killer`, `victim`, `time`) VALUES (%d, %d, %d, %d, %d)",
                    warId, killerGuildId, killerId, playerId, timeNow
                ))
            end
        end
    end
    --]]
end

local playerDeath = CreatureEvent("PlayerDeath")
function playerDeath.onDeath(player, corpse, killer, mostDamageKiller, lastHitUnjustified, mostDamageUnjustified)
    local playerId = player:getId()
    nextUseStaminaTime[playerId] = nil

    player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You are dead.")

    if not deathListEnabled then
        return true
    end

    local timeNow = os.time()
    local byPlayer, killerName = getKiller(killer)
    local byPlayerMostDamage, killerNameMostDamage = getKiller(mostDamageKiller)
    local playerGuid = player:getGuid()
    local playerName = player:getName()
    local playerGuild = player:getGuild()
    local playerGuildId = playerGuild and playerGuild:getId() or 0
    
    local realKiller = killer
    if byPlayer and killer and not killer:isPlayer() then
        realKiller = killer:getMaster()
    end

    local killerGuild = byPlayer and realKiller and realKiller:getGuild() or nil
    local killerGuildId = killerGuild and killerGuild:getId() or 0
    local killerId = byPlayer and realKiller and realKiller:getId() or 0

    db.asyncQuery(insertDeath(
        playerGuid, timeNow, player:getLevel(),
        db.escapeString(killerName), byPlayer and 1 or 0,
        db.escapeString(killerNameMostDamage), byPlayerMostDamage and 1 or 0,
        lastHitUnjustified and 1 or 0, mostDamageUnjustified and 1 or 0
    ), function(success)
        if success then
            playerDeathSuccess(playerId, playerName, killerId, playerGuid, byPlayer, killerName, playerGuildId, killerGuildId, timeNow)
        end
    end)

    return true
end
playerDeath:register()

local login = CreatureEvent("PlayerDeathLogin")
function login.onLogin(player)
    player:registerEvent("PlayerDeath")
    return true
end
login:register()
