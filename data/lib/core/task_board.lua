TaskBoard = TaskBoard or {}

TaskBoard.Resources = {
    TASK_HUNTING = 0x32,
    BOUNTY_POINTS = 0x56,
    SOULSEALS_POINTS = 0x57,
}

TaskBoard.Storage = {
    BOUNTY_KILL_BOOST_UNTIL = PlayerStorageKeys.taskBoardBountyKillBoostUntil,
    WEEKLY_KILL_BOOST_UNTIL = PlayerStorageKeys.taskBoardWeeklyKillBoostUntil,
    WEEKLY_REDUCED_ITEMS_UNTIL = PlayerStorageKeys.taskBoardWeeklyReducedItemsUntil,
}

local function supportsTaskBoardNetwork(player)
    return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function clamp(value, maxValue)
    value = tonumber(value) or 0
    if value < 0 then
        return 0
    end
    return math.min(value, maxValue)
end

function TaskBoard.getResourceBalance(player, resourceType)
    if not player then
        return 0
    end

    if resourceType == TaskBoard.Resources.TASK_HUNTING then
        return player:getTaskHuntingPoints()
    elseif resourceType == TaskBoard.Resources.BOUNTY_POINTS then
        return player:getBountyPoints()
    elseif resourceType == TaskBoard.Resources.SOULSEALS_POINTS then
        return player:getSoulsealsPoints()
    end

    return 0
end

function TaskBoard.sendResourceBalance(player, resourceType, amount)
    if not supportsTaskBoardNetwork(player) then
        return false
    end

    amount = amount == nil and TaskBoard.getResourceBalance(player, resourceType) or amount

    local out = NetworkMessage(player)
    out:addByte(0xEE)
    out:addByte(clamp(resourceType, 0xFF))
    if resourceType == TaskBoard.Resources.BOUNTY_POINTS or
        resourceType == TaskBoard.Resources.SOULSEALS_POINTS then
        out:addU32(clamp(amount, 0xFFFFFFFF))
    else
        out:addU64(math.max(tonumber(amount) or 0, 0))
    end
    return out:sendToPlayer(player)
end

function TaskBoard.sendAll(player)
    if not supportsTaskBoardNetwork(player) then
        return false
    end

    TaskBoard.sendResourceBalance(player, TaskBoard.Resources.TASK_HUNTING)
    TaskBoard.sendResourceBalance(player, TaskBoard.Resources.BOUNTY_POINTS)
    TaskBoard.sendResourceBalance(player, TaskBoard.Resources.SOULSEALS_POINTS)
    return true
end

function TaskBoard.getTimedBoostRemaining(player, storageKey)
    if not player or not storageKey then
        return 0
    end

    local expiresAt = tonumber(player:getStorageValue(storageKey)) or 0
    return math.max(0, expiresAt - os.time())
end

function TaskBoard.activateTimedBoost(player, storageKey, duration)
    duration = tonumber(duration) or 0
    if not player or not storageKey or duration <= 0 then
        return false
    end

    local currentExpiry = tonumber(player:getStorageValue(storageKey)) or 0
    player:setStorageValue(storageKey, math.max(os.time(), currentExpiry) + duration)
    return true
end

function TaskBoard.getBountyKillMultiplier(player)
    return TaskBoard.getTimedBoostRemaining(player, TaskBoard.Storage.BOUNTY_KILL_BOOST_UNTIL) > 0 and 2 or 1
end

function TaskBoard.getWeeklyKillMultiplier(player)
    return TaskBoard.getTimedBoostRemaining(player, TaskBoard.Storage.WEEKLY_KILL_BOOST_UNTIL) > 0 and 2 or 1
end

function TaskBoard.hasWeeklyReducedItems(player)
    return TaskBoard.getTimedBoostRemaining(player, TaskBoard.Storage.WEEKLY_REDUCED_ITEMS_UNTIL) > 0
end

function TaskBoard.getBountyTalismanBonus(player, raceId, pathIndex)
    local bounty = _TASK_BOARD_BOUNTY_MODULE
    if not bounty or not bounty.getTalismanBonus then
        return 0
    end
    return bounty.getTalismanBonus(player, raceId, pathIndex)
end
