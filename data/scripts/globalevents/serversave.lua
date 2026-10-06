local config = {
    time = "09:55:00"
}

local function ServerSave()
    if CustomMarket and CustomMarket.updateStatistics then
        CustomMarket.updateStatistics()
    end

    if configManager.getBoolean(configKeys.SERVER_SAVE_SHUTDOWN) then
        Game.setGameState(GAME_STATE_SHUTDOWN)
    else
        local closeAtServerSave = configManager.getBoolean(configKeys.SERVER_SAVE_CLOSE)
        local wasClosed = Game.getGameState() == GAME_STATE_CLOSED
        if closeAtServerSave and not wasClosed then
            -- CLOSED invokes the global events and requests its own save.
            Game.setGameState(GAME_STATE_CLOSED)
        else
            -- An already-closed state is a no-op, so request a save explicitly.
            saveServer()
        end

        if configManager.getBoolean(configKeys.SERVER_SAVE_CLEAN_MAP) then
            cleanMap()
        end

        if closeAtServerSave and not wasClosed then
            Game.setGameState(GAME_STATE_NORMAL)
        end
    end
end

local function ServerSaveWarning(remainingTime)
    local nextWarning = remainingTime - 60000
    
    if nextWarning > 0 and configManager.getBoolean(configKeys.SERVER_SAVE_NOTIFY_MESSAGE) then
        local minutes = math.floor(nextWarning / 60000)
        local message = string.format(
            "Server is %s in %d minute(s). Please logout.",
            configManager.getBoolean(configKeys.SERVER_SAVE_SHUTDOWN) and "shutting down" or "saving game",
            minutes
        )
        Game.broadcastMessage(message, MESSAGE_STATUS_WARNING)
        
        if nextWarning > 60000 then
            addEvent(ServerSaveWarning, 60000, nextWarning)
        else
            addEvent(ServerSave, 60000)
        end
    else
        addEvent(ServerSave, nextWarning > 0 and nextWarning or 1000)
    end
end

local serverSave = GlobalEvent("ServerSave")

function serverSave.onTime(interval)
    local notifyDuration = configManager.getNumber(configKeys.SERVER_SAVE_NOTIFY_DURATION)
    local remainingTime = notifyDuration * 60000
    
    if configManager.getBoolean(configKeys.SERVER_SAVE_NOTIFY_MESSAGE) then
        local message = string.format(
            "Server is %s in %d minute(s). Please logout.",
            configManager.getBoolean(configKeys.SERVER_SAVE_SHUTDOWN) and "shutting down" or "saving game",
            notifyDuration
        )
        Game.broadcastMessage(message, MESSAGE_STATUS_WARNING)
    end
    
    if remainingTime > 60000 then
        addEvent(ServerSaveWarning, 60000, remainingTime)
    else
        addEvent(ServerSave, remainingTime)
    end
    
    return not configManager.getBoolean(configKeys.SERVER_SAVE_SHUTDOWN)
end

serverSave:time(config.time)
serverSave:register()
