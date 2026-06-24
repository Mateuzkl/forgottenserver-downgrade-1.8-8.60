local startup = GlobalEvent("ServerStartup")
function startup.onStartup()
    if configManager and configKeys and configKeys.MULTI_WORLD_ENABLED and configManager.getBoolean(configKeys.MULTI_WORLD_ENABLED) then
        db.query("DELETE FROM `players_online` WHERE `world_id` = " .. configManager.getNumber(configKeys.WORLD_ID))
    else
        db.query("TRUNCATE TABLE `players_online`")
    end
    db.asyncQuery("DELETE FROM `guild_wars` WHERE `status` = 0" .. (configManager and configKeys and configKeys.MULTI_WORLD_ENABLED and configManager.getBoolean(configKeys.MULTI_WORLD_ENABLED) and (" AND `world_id` = " .. configManager.getNumber(configKeys.WORLD_ID)) or ""))
    db.asyncQuery("DELETE FROM `players` WHERE `deletion` != 0 AND `deletion` < " .. os.time())
    db.asyncQuery("DELETE FROM `ip_bans` WHERE `expires_at` != 0 AND `expires_at` <= " .. os.time())

    local resultId = db.storeQuery("SELECT * FROM `account_bans` WHERE `expires_at` != 0 AND `expires_at` <= " .. os.time())
    if resultId ~= false then
        repeat
            local accountId = result.getNumber(resultId, "account_id")
            db.asyncQuery("INSERT INTO `account_ban_history` (`account_id`, `reason`, `banned_at`, `expired_at`, `banned_by`) VALUES (" ..
                accountId .. ", " .. db.escapeString(result.getString(resultId, "reason")) .. ", " ..
                result.getNumber(resultId, "banned_at") .. ", " .. result.getNumber(resultId, "expires_at") .. ", " ..
                result.getNumber(resultId, "banned_by") .. ")")
            db.asyncQuery("DELETE FROM `account_bans` WHERE `account_id` = " .. accountId)
        until not result.next(resultId)
        result.free(resultId)
    end

    local resultId = db.storeQuery("SELECT `id`, `highest_bidder`, `last_bid`, (SELECT `balance` FROM `players` WHERE `players`.`id` = `highest_bidder`) AS `balance` FROM `houses` WHERE `owner` = 0 AND `bid_end` != 0 AND `bid_end` < " .. os.time() .. (configManager and configKeys and configKeys.MULTI_WORLD_ENABLED and configManager.getBoolean(configKeys.MULTI_WORLD_ENABLED) and (" AND `world_id` = " .. configManager.getNumber(configKeys.WORLD_ID)) or ""))
    if resultId ~= false then
        repeat
            local house = House(result.getNumber(resultId, "id"))
            if house then
                local highestBidder = result.getNumber(resultId, "highest_bidder")
                local balance = result.getNumber(resultId, "balance")
                local lastBid = result.getNumber(resultId, "last_bid")
                if balance >= lastBid then
                    db.query("UPDATE `players` SET `balance` = " .. (balance - lastBid) .. " WHERE `id` = " .. highestBidder .. (configManager and configKeys and configKeys.MULTI_WORLD_ENABLED and configManager.getBoolean(configKeys.MULTI_WORLD_ENABLED) and (" AND `world_id` = " .. configManager.getNumber(configKeys.WORLD_ID)) or ""))
                    house:setOwnerGuid(highestBidder)
                end
                db.asyncQuery("UPDATE `houses` SET `last_bid` = 0, `bid_end` = 0, `highest_bidder` = 0, `bid` = 0 WHERE `id` = " .. house:getId() .. (configManager and configKeys and configKeys.MULTI_WORLD_ENABLED and configManager.getBoolean(configKeys.MULTI_WORLD_ENABLED) and (" AND `world_id` = " .. configManager.getNumber(configKeys.WORLD_ID)) or ""))
            end
        until not result.next(resultId)
        result.free(resultId)
    end

    db.query("TRUNCATE TABLE `towns`")
    for i, town in ipairs(Game.getTowns()) do
        local position = town:getTemplePosition()
        db.query("INSERT INTO `towns` (`id`, `name`, `posx`, `posy`, `posz`) VALUES (" ..
            town:getId() .. ", " .. db.escapeString(town:getName()) .. ", " .. position.x .. ", " .. position.y .. ", " .. position.z .. ")")
    end

    return true
end
startup:type("startup")
startup:register()
