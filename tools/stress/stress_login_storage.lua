--[[
    Server-side login/storage stress diagnostic for TFS 1.8.

    Commands (account type 6 / access group only):
      /stress_login prepare,Player One|Player Two|Player Three,25000
      /stress_login hammer,200000,500
      /stress_login status
      /stress_login stop
      /stress_login clean,Player One|Player Two|Player Three

    "prepare" inserts isolated player_storage rows while the target characters
    are offline. Log in those characters simultaneously afterwards to exercise
    the real IOLoginData::loadPlayer storage path.

    "hammer" repeatedly changes isolated storage keys on every online player
    and periodically calls Player:saveAsync(). Work is chunked with addEvent so the
    dispatcher remains able to process logins while the test is running.
--]]

-- Disabled by default. Enable manually only in a controlled test environment.
local ENABLE_STRESS_LOGIN_COMMAND = false

if not ENABLE_STRESS_LOGIN_COMMAND then
    return
end

local stressLogin = TalkAction("/stress_login")

local CONFIG = {
    storageBase = 2100000000,
    maxPreparedStorages = 100000,
    defaultPreparedStorages = 25000,
    sqlRowsPerBatch = 250,
    sqlBatchDelayMs = 1,

    hammerStorageBase = 2100200000,
    hammerStorageSlots = 4096,
    defaultHammerOperations = 200000,
    defaultHammerChunk = 500,
    maxHammerOperations = 5000000,
    maxHammerChunk = 5000,
    hammerSnapshotSlotsPerChunk = 256,
    hammerDelayMs = 1,
    saveEveryChunks = 4,

    progressEveryMs = 2000,
}

local MSG_INFO = MESSAGE_STATUS_CONSOLE_BLUE or MESSAGE_EVENT_ADVANCE or 19
local MSG_ERROR = MESSAGE_STATUS_CONSOLE_RED or MESSAGE_STATUS_WARNING or MSG_INFO

local activeJob = nil
local nextJobId = 0

local function trim(value)
    return tostring(value or ""):match("^%s*(.-)%s*$")
end

local function clampInteger(value, defaultValue, minimum, maximum)
    local number = tonumber(value) or defaultValue
    number = math.floor(number)
    if number < minimum then
        return minimum
    end
    if number > maximum then
        return maximum
    end
    return number
end

local function splitParameters(param)
    local parts = {}
    for value in tostring(param or ""):gmatch("([^,]+)") do
        parts[#parts + 1] = trim(value)
    end
    return parts
end

local function splitNames(value)
    local names = {}
    local seen = {}
    for rawName in tostring(value or ""):gmatch("([^|]+)") do
        local name = trim(rawName)
        local normalized = name:lower()
        if name ~= "" and not seen[normalized] then
            names[#names + 1] = name
            seen[normalized] = true
        end
    end
    return names
end

local function sqlString(value)
    local escaped = db.escapeString(tostring(value or ""))
    if not escaped or escaped == "" then
        return "''"
    end
    if escaped:sub(1, 1) == "'" and escaped:sub(-1) == "'" then
        return escaped
    end
    return "'" .. escaped .. "'"
end

local function getOwnerPlayer(job)
    local player = Player(job.ownerId)
    if not player or player:getGuid() ~= job.ownerGuid then
        return nil
    end
    return player
end

local function notify(job, message, isError)
    local prefix = "[STRESS LOGIN] "
    print(prefix .. message)
    local player = getOwnerPlayer(job)
    if player then
        player:sendTextMessage(isError and MSG_ERROR or MSG_INFO, prefix .. message)
    end
end

local function notifyPlayer(player, message, isError)
    local prefix = "[STRESS LOGIN] "
    print(prefix .. message)
    player:sendTextMessage(isError and MSG_ERROR or MSG_INFO, prefix .. message)
end

local function elapsedSeconds(job)
    return math.max(0, os.time() - job.startedAt)
end

local function finishJob(job, message, isError)
    if activeJob ~= job then
        return
    end
    notify(job, message .. string.format(" | elapsed=%ds", elapsedSeconds(job)), isError)
    activeJob = nil
end

local function schedule(job, callback, delay)
    addEvent(function(jobId)
        if not activeJob or activeJob.id ~= jobId then
            return
        end
        callback(activeJob)
    end, delay or 0, job.id)
end

local function maybeReport(job, message)
    local now = os.mtime and os.mtime() or (os.time() * 1000)
    if now - job.lastProgressAt >= CONFIG.progressEveryMs then
        job.lastProgressAt = now
        notify(job, message)
    end
end

local function newJob(player, mode)
    nextJobId = nextJobId + 1
    local now = os.mtime and os.mtime() or (os.time() * 1000)
    local job = {
        id = nextJobId,
        mode = mode,
        phase = mode,
        ownerId = player:getId(),
        ownerGuid = player:getGuid(),
        startedAt = os.time(),
        lastProgressAt = now,
        stopRequested = false,
    }
    activeJob = job
    return job
end

local function resolveOfflinePlayers(names)
    local targets = {}
    for _, name in ipairs(names) do
        if Player(name) then
            return nil, "The character '" .. name .. "' must be offline."
        end

        local query = "SELECT `id`, `name` FROM `players` WHERE `name` = " .. sqlString(name) .. " LIMIT 1"
        local resultId = db.storeQuery(query)
        if not resultId then
            return nil, "Character not found: " .. name
        end

        targets[#targets + 1] = {
            guid = result.getNumber(resultId, "id"),
            name = result.getString(resultId, "name"),
        }
        result.free(resultId)
    end
    return targets
end

local runPrepareStep

runPrepareStep = function(job)
    if job.stopRequested then
        finishJob(job, string.format("PREPARE stopped at %d/%d rows", job.completed, job.total), true)
        return
    end

    local target = job.targets[job.targetIndex]
    if not target then
        finishJob(job, string.format(
            "PREPARE completed: %d storages for %d character(s). Keep them offline, then log in simultaneously with 1, 2, and 3 clients",
            job.completed,
            #job.targets
        ))
        return
    end

    if Player(target.name) then
        finishJob(job, "PREPARE cancelled: '" .. target.name .. "' logged in during setup", true)
        return
    end

    if job.offset >= job.rowsPerPlayer then
        job.targetIndex = job.targetIndex + 1
        job.offset = 0
        schedule(job, runPrepareStep, CONFIG.sqlBatchDelayMs)
        return
    end

    local amount = math.min(CONFIG.sqlRowsPerBatch, job.rowsPerPlayer - job.offset)
    local values = {}
    for index = 0, amount - 1 do
        local offset = job.offset + index
        local key = CONFIG.storageBase + offset
        local value = 1000000 + offset
        values[#values + 1] = string.format("(%d,%d,%d)", target.guid, key, value)
    end

    local query = "INSERT INTO `player_storage` (`player_id`, `key`, `value`) VALUES "
        .. table.concat(values, ",")
        .. " ON DUPLICATE KEY UPDATE `value` = VALUES(`value`)"

    if not db.query(query) then
        finishJob(job, string.format("SQL failure while preparing '%s' at offset %d", target.name, job.offset), true)
        return
    end

    job.offset = job.offset + amount
    job.completed = job.completed + amount
    maybeReport(job, string.format(
        "PREPARE %d/%d rows (%.1f%%) | current=%s",
        job.completed,
        job.total,
        (job.completed * 100) / math.max(1, job.total),
        target.name
    ))
    schedule(job, runPrepareStep, CONFIG.sqlBatchDelayMs)
end

local function startPrepare(player, namesValue, rowsValue)
    local names = splitNames(namesValue)
    if #names == 0 then
        notifyPlayer(player, "Usage: /stress_login prepare,Name 1|Name 2|Name 3,25000", true)
        return
    end

    local rows = clampInteger(rowsValue, CONFIG.defaultPreparedStorages, 1, CONFIG.maxPreparedStorages)
    local targets, errorMessage = resolveOfflinePlayers(names)
    if not targets then
        notifyPlayer(player, errorMessage, true)
        return
    end

    local job = newJob(player, "prepare")
    job.targets = targets
    job.targetIndex = 1
    job.offset = 0
    job.rowsPerPlayer = rows
    job.completed = 0
    job.total = rows * #targets

    notify(job, string.format(
        "PREPARE started: %d character(s) x %d storages = %d rows",
        #targets,
        rows,
        job.total
    ))
    schedule(job, runPrepareStep, 0)
end

local runCleanStep

runCleanStep = function(job)
    if job.stopRequested then
        finishJob(job, string.format("CLEAN stopped at %d/%d characters", job.targetIndex - 1, #job.targets), true)
        return
    end

    local target = job.targets[job.targetIndex]
    if not target then
        finishJob(job, string.format("CLEAN completed for %d character(s)", #job.targets))
        return
    end

    if Player(target.name) then
        finishJob(job, "CLEAN cancelled: '" .. target.name .. "' must be offline", true)
        return
    end

    local preparedEnd = CONFIG.storageBase + CONFIG.maxPreparedStorages - 1
    local hammerEnd = CONFIG.hammerStorageBase + CONFIG.hammerStorageSlots - 1
    local query = string.format(
        "DELETE FROM `player_storage` WHERE `player_id` = %d AND ((`key` BETWEEN %d AND %d) OR (`key` BETWEEN %d AND %d))",
        target.guid,
        CONFIG.storageBase,
        preparedEnd,
        CONFIG.hammerStorageBase,
        hammerEnd
    )

    if not db.query(query) then
        finishJob(job, "SQL failure while cleaning '" .. target.name .. "'", true)
        return
    end

    notify(job, "CLEAN removed reserved storages for " .. target.name)
    job.targetIndex = job.targetIndex + 1
    schedule(job, runCleanStep, CONFIG.sqlBatchDelayMs)
end

local function startClean(player, namesValue)
    local names = splitNames(namesValue)
    if #names == 0 then
        notifyPlayer(player, "Usage: /stress_login clean,Name 1|Name 2|Name 3", true)
        return
    end

    local targets, errorMessage = resolveOfflinePlayers(names)
    if not targets then
        notifyPlayer(player, errorMessage, true)
        return
    end

    local job = newJob(player, "clean")
    job.targets = targets
    job.targetIndex = 1
    notify(job, "CLEAN started. Only the storage ranges reserved by this test will be removed")
    schedule(job, runCleanStep, 0)
end

local function getHammerPlayer(entry)
    local player = Player(entry.id)
    if not player or player:getGuid() ~= entry.guid then
        return nil
    end
    return player
end

local runHammerCleanup

local function beginHammerCleanup(job, reason)
    job.phase = "hammer_cleanup"
    job.cleanupPlayerIndex = 1
    job.cleanupSlot = 0
    job.cleanupReason = reason
    notify(job, "HAMMER finishing; restoring temporary storages...")
    schedule(job, runHammerCleanup, 0)
end

runHammerCleanup = function(job)
    local entry = job.players[job.cleanupPlayerIndex]
    if not entry then
        local suffix = job.cleanupReason or "completed"
        local disconnectedCount = 0
        for _ in pairs(job.disconnected) do
            disconnectedCount = disconnectedCount + 1
        end
        finishJob(job, string.format(
            "HAMMER %s: %d operations, %d saves requested, %d player(s), %d disconnected",
            suffix,
            job.completed,
            job.saveRequests,
            #job.players,
            disconnectedCount
        ))
        return
    end

    local player = getHammerPlayer(entry)
    if not player then
        job.disconnected[entry.guid] = true
        notify(job, "WARNING: " .. entry.name .. " disconnected; run CLEAN after the character is offline", true)
        job.cleanupPlayerIndex = job.cleanupPlayerIndex + 1
        job.cleanupSlot = 0
        schedule(job, runHammerCleanup, 0)
        return
    end

    local amount = math.min(job.chunkSize, CONFIG.hammerStorageSlots - job.cleanupSlot)
    for index = 0, amount - 1 do
        local slot = job.cleanupSlot + index
        local oldValue = entry.originalValues[slot + 1]
        player:setStorageValue(CONFIG.hammerStorageBase + slot, oldValue)
    end
    job.cleanupSlot = job.cleanupSlot + amount

    if job.cleanupSlot >= CONFIG.hammerStorageSlots then
        player:saveAsync()
        job.saveRequests = job.saveRequests + 1
        job.cleanupPlayerIndex = job.cleanupPlayerIndex + 1
        job.cleanupSlot = 0
    end
    schedule(job, runHammerCleanup, CONFIG.hammerDelayMs)
end

local runHammerStep
local runHammerSnapshot

runHammerStep = function(job)
    if job.stopRequested then
        beginHammerCleanup(job, "stopped")
        return
    end
    if job.completed >= job.total then
        beginHammerCleanup(job, "completed")
        return
    end

    local amount = math.min(job.chunkSize, job.total - job.completed)
    for index = 1, amount do
        local operation = job.completed + index - 1
        local entry = job.players[(operation % #job.players) + 1]
        local player = getHammerPlayer(entry)
        if player then
            local slot = math.floor(operation / #job.players) % CONFIG.hammerStorageSlots
            local key = CONFIG.hammerStorageBase + slot
            player:setStorageValue(key, job.id * 10000000 + operation + 1)
        else
            job.disconnected[entry.guid] = true
        end
    end

    job.completed = job.completed + amount
    job.chunkNumber = job.chunkNumber + 1

    if job.chunkNumber % CONFIG.saveEveryChunks == 0 then
        for _, entry in ipairs(job.players) do
            local player = getHammerPlayer(entry)
            if player then
                player:saveAsync()
                job.saveRequests = job.saveRequests + 1
            end
        end
    end

    maybeReport(job, string.format(
        "HAMMER %d/%d ops (%.1f%%) | saves=%d",
        job.completed,
        job.total,
        (job.completed * 100) / math.max(1, job.total),
        job.saveRequests
    ))
    schedule(job, runHammerStep, CONFIG.hammerDelayMs)
end

runHammerSnapshot = function(job)
    if job.stopRequested then
        finishJob(job, string.format(
            "HAMMER stopped during snapshot at %d/%d storages; no storage was changed",
            job.snapshotCompleted,
            job.snapshotTotal
        ), true)
        return
    end

    local target = job.snapshotTargets[job.snapshotPlayerIndex]
    if not target then
        job.snapshotTargets = nil
        job.phase = "hammer"
        notify(job, string.format(
            "HAMMER started: %d ops, chunk=%d, players=%d. Use /stress_login stop to abort and clean up",
            job.total,
            job.chunkSize,
            #job.players
        ))
        schedule(job, runHammerStep, 0)
        return
    end

    local entry = job.players[job.snapshotPlayerIndex]
    if not entry then
        entry = target
        entry.originalValues = {}
        job.players[job.snapshotPlayerIndex] = entry
    end

    local player = getHammerPlayer(entry)
    if not player then
        finishJob(job, "HAMMER cancelled: " .. entry.name .. " disconnected during the snapshot; no storage was changed", true)
        return
    end

    local amount = math.min(
        CONFIG.hammerSnapshotSlotsPerChunk,
        CONFIG.hammerStorageSlots - job.snapshotSlot
    )
    for index = 0, amount - 1 do
        local slot = job.snapshotSlot + index
        entry.originalValues[slot + 1] = player:getStorageValue(CONFIG.hammerStorageBase + slot)
    end

    job.snapshotSlot = job.snapshotSlot + amount
    job.snapshotCompleted = job.snapshotCompleted + amount
    maybeReport(job, string.format(
        "HAMMER SNAPSHOT %d/%d storages (%.1f%%) | current=%s",
        job.snapshotCompleted,
        job.snapshotTotal,
        (job.snapshotCompleted * 100) / math.max(1, job.snapshotTotal),
        entry.name
    ))

    if job.snapshotSlot >= CONFIG.hammerStorageSlots then
        job.snapshotPlayerIndex = job.snapshotPlayerIndex + 1
        job.snapshotSlot = 0
    end
    schedule(job, runHammerSnapshot, CONFIG.hammerDelayMs)
end

local function startHammer(player, operationsValue, chunkValue)
    local onlinePlayers = Game.getPlayers()
    if #onlinePlayers == 0 then
        notifyPlayer(player, "No players are online for HAMMER", true)
        return
    end

    local operations = clampInteger(
        operationsValue,
        CONFIG.defaultHammerOperations,
        1,
        CONFIG.maxHammerOperations
    )
    local chunkSize = clampInteger(chunkValue, CONFIG.defaultHammerChunk, 1, CONFIG.maxHammerChunk)
    local job = newJob(player, "hammer")
    job.phase = "hammer_snapshot"
    job.players = {}
    job.snapshotTargets = {}
    job.disconnected = {}
    job.total = operations
    job.completed = 0
    job.chunkSize = chunkSize
    job.chunkNumber = 0
    job.saveRequests = 0
    job.snapshotPlayerIndex = 1
    job.snapshotSlot = 0
    job.snapshotCompleted = 0
    job.snapshotTotal = #onlinePlayers * CONFIG.hammerStorageSlots

    for _, onlinePlayer in ipairs(onlinePlayers) do
        job.snapshotTargets[#job.snapshotTargets + 1] = {
            id = onlinePlayer:getId(),
            guid = onlinePlayer:getGuid(),
            name = onlinePlayer:getName(),
        }
    end

    notify(job, string.format(
        "HAMMER snapshot started: %d player(s) x %d storages",
        #job.snapshotTargets,
        CONFIG.hammerStorageSlots
    ))
    schedule(job, runHammerSnapshot, 0)
end

local function showStatus(player)
    if not activeJob then
        notifyPlayer(player, "No active test")
        return
    end

    local job = activeJob
    if job.mode == "prepare" then
        notifyPlayer(player, string.format(
            "Active: PREPARE %d/%d rows | target %d/%d | elapsed=%ds",
            job.completed,
            job.total,
            job.targetIndex,
            #job.targets,
            elapsedSeconds(job)
        ))
    elseif job.mode == "hammer" then
        if job.phase == "hammer_snapshot" then
            notifyPlayer(player, string.format(
                "Active: HAMMER SNAPSHOT %d/%d storages | elapsed=%ds",
                job.snapshotCompleted,
                job.snapshotTotal,
                elapsedSeconds(job)
            ))
        else
            notifyPlayer(player, string.format(
                "Active: %s %d/%d ops | saves=%d | elapsed=%ds",
                job.phase:upper(),
                job.completed,
                job.total,
                job.saveRequests,
                elapsedSeconds(job)
            ))
        end
    else
        notifyPlayer(player, string.format(
            "Active: %s target %d/%d | elapsed=%ds",
            job.mode:upper(),
            job.targetIndex,
            #job.targets,
            elapsedSeconds(job)
        ))
    end
end

local function showHelp(player)
    notifyPlayer(player, "PREPARE: /stress_login prepare,Name 1|Name 2|Name 3,25000")
    notifyPlayer(player, "Then log in to the prepared characters simultaneously with 1, 2, and 3 clients")
    notifyPlayer(player, "HAMMER online: /stress_login hammer,200000,500")
    notifyPlayer(player, "Controls: /stress_login status  |  /stress_login stop")
    notifyPlayer(player, "Offline cleanup: /stress_login clean,Name 1|Name 2|Name 3")
end

function stressLogin.onSay(player, words, param)
    if not player:getGroup():getAccess() then
        return false
    end

    local parts = splitParameters(param)
    local command = (parts[1] or "help"):lower()

    if command == "status" then
        showStatus(player)
        return false
    end

    if command == "stop" then
        if not activeJob then
            notifyPlayer(player, "No active test")
        elseif activeJob.phase == "hammer_cleanup" then
            notifyPlayer(player, "HAMMER is already cleaning up; please wait")
        else
            activeJob.stopRequested = true
            notifyPlayer(player, "Stop requested; the current chunk will finish")
        end
        return false
    end

    if command == "help" or command == "" then
        showHelp(player)
        return false
    end

    if activeJob then
        notifyPlayer(player, "A test is already active. Use /stress_login status or stop", true)
        return false
    end

    if command == "prepare" then
        startPrepare(player, parts[2], parts[3])
    elseif command == "clean" then
        startClean(player, parts[2])
    elseif command == "hammer" then
        startHammer(player, parts[2], parts[3])
    else
        showHelp(player)
    end
    return false
end

stressLogin:separator(" ")
stressLogin:accountType(6)
stressLogin:register()
