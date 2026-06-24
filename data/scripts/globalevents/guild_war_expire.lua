local guildWarExpire = GlobalEvent("GuildWarExpire")

local function worldWhere()
    if configManager and configKeys and configKeys.MULTI_WORLD_ENABLED and configManager.getBoolean(configKeys.MULTI_WORLD_ENABLED) then
        return " AND `world_id` = " .. configManager.getNumber(configKeys.WORLD_ID)
    end
    return ""
end

function guildWarExpire.onThink(interval)
    local now = os.time()
    local resultId = db.storeQuery(string.format(
        "SELECT `id`, `guild1`, `guild2`, `name1`, `name2` FROM `guild_wars`" ..
        " WHERE `status` = 1 AND `ended` > 0 AND `ended` < %d%s",
        now, worldWhere()
    ))

    if not resultId then
        return true
    end

    repeat
        local warId    = result.getNumber(resultId, "id")
        local guild1Id = result.getNumber(resultId, "guild1")
        local guild2Id = result.getNumber(resultId, "guild2")
        local name1    = result.getString(resultId, "name1")
        local name2    = result.getString(resultId, "name2")

        local killResult = db.storeQuery(string.format(
            "SELECT COUNT(*) AS total FROM `guild_war_kills` WHERE `war_id` = %d%s",
            warId, worldWhere()
        ))

        local hasKills = false
        if killResult then
            hasKills = result.getNumber(killResult, "total") > 0
            result.free(killResult)
        end

        if not hasKills then
            local updated = db.query(string.format(
                "UPDATE `guild_wars` SET `status` = 5, `ended` = %d WHERE `id` = %d AND `status` = 1%s",
                now, warId, worldWhere()
            ))

            if updated then
                Game.broadcastMessage(string.format(
                    "[Guild War] A guerra entre %s e %s encerrou automaticamente (sem combates).",
                    name1, name2
                ), MESSAGE_EVENT_ORANGE)

                for _, player in ipairs(Game.getPlayers()) do
                    local guild = player:getGuild()
                    if guild and (guild:getId() == guild1Id or guild:getId() == guild2Id) then
                        player:reloadWarList()
                    end
                end
            end
        end
    until not result.next(resultId)

    result.free(resultId)
    return true
end
guildWarExpire:type("think")
guildWarExpire:interval(10 * 60 * 1000)
guildWarExpire:register()
