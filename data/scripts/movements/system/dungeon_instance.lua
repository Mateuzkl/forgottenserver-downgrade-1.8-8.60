local ENTRY_AID = 1005
local EXIT_AID  = 1006

local BOSS_NAME = "Bakragore"
local BOSS_POS  = Position(1046, 999, 13)

local PLAYER_DEST = Position(1044, 993, 13)
local EXIT_DEST   = Position(1054, 1000, 7)

local INSTANCE_FROM = Position(1022, 993, 13)
local INSTANCE_TO   = Position(1061, 1037, 13)

local SPAWN_AREAS = {
    {from = Position(1022, 1011, 13), to = Position(1061, 1037, 13)},
    {from = Position(1035, 993, 13),  to = Position(1061, 1026, 13)},
}

local MONSTER_NAME  = "Demon"
local MONSTER_COUNT = 30
local TIME_LIMIT    = 300

local COOLDOWN_STORAGE = 45891
local COOLDOWN_TIME    = 3600

local instanceCounter = 40000
local dungeonInstances = {}

local function isInsideDungeonArea(pos)
    return pos.x >= INSTANCE_FROM.x and pos.x <= INSTANCE_TO.x
       and pos.y >= INSTANCE_FROM.y and pos.y <= INSTANCE_TO.y
       and pos.z == INSTANCE_FROM.z
end

local function getRandomSpawnPosition()
    local area = SPAWN_AREAS[math.random(#SPAWN_AREAS)]
    return Position(math.random(area.from.x, area.to.x),
                    math.random(area.from.y, area.to.y),
                    area.from.z)
end

local function getInstanceSpectators(instanceId)
    local area = Game.getInstanceArea(instanceId)
    if not area then return {} end
    local cx = math.floor((area.fromPos.x + area.toPos.x) / 2)
    local cy = math.floor((area.fromPos.y + area.toPos.y) / 2)
    local rangeX = math.ceil((area.toPos.x - area.fromPos.x) / 2)
    local rangeY = math.ceil((area.toPos.y - area.fromPos.y) / 2)
    return Game.getSpectators(Position(cx, cy, area.fromPos.z),
                              false, false, rangeX, rangeX, rangeY, rangeY)
end

local function sendToInstancePlayers(instanceId, message)
    for _, spec in ipairs(getInstanceSpectators(instanceId)) do
        if spec:isPlayer() and spec:getInstanceId() == instanceId then
            spec:sendTextMessage(MESSAGE_EVENT_ORANGE, message)
        end
    end
end

local function cleanupInstanceCreatures(instanceId)
    for _, spec in ipairs(getInstanceSpectators(instanceId)) do
        if spec:isMonster() and spec:getInstanceId() == instanceId then
            local summons = spec:getSummons()
            if summons then
                for _, summon in ipairs(summons) do
                    summon:remove()
                end
            end
            spec:remove()
        end
    end
end

local function cleanupFullInstance(instanceId)
    local data = dungeonInstances[instanceId]
    if data and data.timerEvent then
        stopEvent(data.timerEvent)
    end
    cleanupInstanceCreatures(instanceId)
    Game.unregisterInstanceArea(instanceId)
    dungeonInstances[instanceId] = nil
end

local function expelAllPlayers(instanceId, message)
    for _, spec in ipairs(getInstanceSpectators(instanceId)) do
        if spec:isPlayer() and spec:getInstanceId() == instanceId then
            if message then
                spec:sendTextMessage(MESSAGE_EVENT_ORANGE, message)
            end
            spec:teleportTo(EXIT_DEST)
            spec:setInstanceId(0)

        end
    end
end

local function onTimerExpired(instanceId)
    local data = dungeonInstances[instanceId]
    if not data then return end
    data.timerEvent = nil
    expelAllPlayers(instanceId,
        "[Dungeon] Time's up! You failed to clear the dungeon in time.")
    cleanupFullInstance(instanceId)
end

local function removePlayerFromInstance(player)
    local instanceId = player:getInstanceId()
    if instanceId == 0 then return end

    local data = dungeonInstances[instanceId]
    if not data then
        Game.unregisterInstanceArea(instanceId)
        player:setInstanceId(0)
        return
    end

    data.players[player:getGuid()] = nil

    if next(data.players) == nil then
        cleanupFullInstance(instanceId)
    end

    player:setInstanceId(0)
end

local entryMovement = MoveEvent()
function entryMovement.onStepIn(creature, item, position, fromPosition)
    local player = creature:getPlayer()
    if not player then return true end

    if player:getInstanceId() ~= 0 then
		player:sendTextMessage(MESSAGE_EVENT_ORANGE,
			"[Dungeon] You are already inside an instance.")
		player:teleportTo(fromPosition, false, CONST_ME_NONE)
		return true
	end

    local now = os.time()
    local lastUse = player:getStorageValue(COOLDOWN_STORAGE)
    if lastUse and lastUse > 0 and (now - lastUse) < COOLDOWN_TIME then
        local remaining = COOLDOWN_TIME - (now - lastUse)
        local minutes = math.ceil(remaining / 60)
		player:sendTextMessage(MESSAGE_EVENT_ORANGE,
			string.format("[Dungeon] You have already cleared this dungeon recently.\n" ..
			"You must wait %d minute(s) before entering again.", minutes))
		player:teleportTo(fromPosition, false, CONST_ME_NONE)
		return true
	end

    local participants = {}
    local added = {}
    local party = player:getParty()

    if party then
        local leader = party:getLeader()
        local members = party:getMembers()
        local all = {leader}
        for _, m in ipairs(members) do
            all[#all + 1] = m
        end

        for _, p in ipairs(all) do
            if not added[p:getGuid()] then
                if p:getInstanceId() ~= 0 then
                    p:sendTextMessage(MESSAGE_EVENT_ORANGE,
                        "[Dungeon] You are already inside another instance.")
                else
                    local pLastUse = p:getStorageValue(COOLDOWN_STORAGE)
                    if pLastUse and pLastUse > 0 and (now - pLastUse) < COOLDOWN_TIME then
                        p:getPosition():sendMagicEffect(CONST_ME_TUTORIALARROW)
                        local pMinutes = math.ceil((COOLDOWN_TIME - (now - pLastUse)) / 60)
                        p:sendTextMessage(MESSAGE_EVENT_ORANGE,
                            string.format("[Dungeon] You have already cleared this dungeon recently.\n" ..
                            "You must wait %d minute(s).", pMinutes))
                    else
                        participants[#participants + 1] = p
                        added[p:getGuid()] = true
                    end
                end
            end
        end
    else
        participants[1] = player
        added[player:getGuid()] = true
    end

	if #participants == 0 then
		player:sendTextMessage(MESSAGE_EVENT_ORANGE,
			"[Dungeon] No eligible players to enter the dungeon.")
		player:teleportTo(fromPosition, false, CONST_ME_NONE)
		return true
	end

    instanceCounter = instanceCounter + 1
    local instanceId = instanceCounter

    Game.registerInstanceArea(instanceId, INSTANCE_FROM, INSTANCE_TO)

    local playerTracking = {}
    for _, p in ipairs(participants) do
        p:setInstanceIdRaw(instanceId)
        playerTracking[p:getGuid()] = true

        local dest = p:getClosestFreePosition(PLAYER_DEST, false)
        if dest.x == 0 then dest = PLAYER_DEST end
        p:teleportTo(dest, false, CONST_ME_NONE)
        dest:sendMagicEffect(CONST_ME_TELEPORT, instanceId)

    end

    local spawned = 0
    for _ = 1, MONSTER_COUNT * 10 do
        if spawned >= MONSTER_COUNT then break end
        local spawnPos = getRandomSpawnPosition()
        local monster = Game.createMonster(MONSTER_NAME, spawnPos, true, false, 0, instanceId)
        if monster then
            monster:registerEvent("DungeonMonsterDeath")
            spawned = spawned + 1
        end
    end

    dungeonInstances[instanceId] = {
        monstersAlive = spawned,
        totalMonsters = spawned,
        bossSpawned = false,
        players = playerTracking,
        timerEvent = addEvent(onTimerExpired, TIME_LIMIT * 1000, instanceId),
    }

    sendToInstancePlayers(instanceId,
        string.format("[Dungeon] Kill all %d demons to summon the final boss!\n" ..
        "You have %d minutes to complete the dungeon.", spawned, math.ceil(TIME_LIMIT / 60)))

    return true
end
entryMovement:aid(ENTRY_AID)
entryMovement:type("stepin")
entryMovement:register()

local exitMovement = MoveEvent()
function exitMovement.onStepIn(creature, item, position, fromPosition)
    local player = creature:getPlayer()
    if not player then return true end

    local instanceId = player:getInstanceId()
    if instanceId == 0 then return true end

    player:teleportTo(EXIT_DEST)
    removePlayerFromInstance(player)


    return true
end
exitMovement:aid(EXIT_AID)
exitMovement:type("stepin")
exitMovement:register()

local monsterDeathEvent = CreatureEvent("DungeonMonsterDeath")
function monsterDeathEvent.onDeath(creature, corpse, killer, mostDamageKiller, lastHitUnjustified, mostDamageUnjustified)
    local instanceId = creature:getInstanceId()
    if instanceId == 0 then return end

    local data = dungeonInstances[instanceId]
    if not data or data.bossSpawned then return end

    data.monstersAlive = data.monstersAlive - 1
    local killed = data.totalMonsters - data.monstersAlive

    if data.monstersAlive > 0 then
        sendToInstancePlayers(instanceId,
            string.format("[Dungeon] %s eliminated! (%d/%d) - %d remaining.",
            MONSTER_NAME, killed, data.totalMonsters, data.monstersAlive))
    else
        data.bossSpawned = true
        sendToInstancePlayers(instanceId,
            string.format("[Dungeon] All demons eliminated! The final boss %s has appeared!", BOSS_NAME))

        local boss = Game.createMonster(BOSS_NAME, BOSS_POS, false, true, CONST_ME_TELEPORT, instanceId)
        if boss then
            boss:registerEvent("DungeonBossDeath")
        end
    end
end
monsterDeathEvent:type("death")
monsterDeathEvent:register()

local bossDeathEvent = CreatureEvent("DungeonBossDeath")
function bossDeathEvent.onDeath(creature, corpse, killer, mostDamageKiller, lastHitUnjustified, mostDamageUnjustified)
    local instanceId = creature:getInstanceId()
    if instanceId == 0 then return end

    local data = dungeonInstances[instanceId]
    if not data then return end

    if data.timerEvent then
        stopEvent(data.timerEvent)
        data.timerEvent = nil
    end

    local damageMap = creature:getDamageMap()
    local now = os.time()
    for _, spec in ipairs(getInstanceSpectators(instanceId)) do
        if spec:isPlayer() and spec:getInstanceId() == instanceId then
            if damageMap[spec:getId()] then
                spec:setStorageValue(COOLDOWN_STORAGE, now)
            end
            spec:sendTextMessage(MESSAGE_EVENT_ORANGE,
                "[Dungeon] Congratulations! The boss has been defeated!\n" ..
                "The dungeon will close in 30 seconds.")
        end
    end

    addEvent(function(iid)
        if not dungeonInstances[iid] then return end
        expelAllPlayers(iid,
            "[Dungeon] The dungeon is now closed.")
        cleanupFullInstance(iid)
    end, 30000, instanceId)
end
bossDeathEvent:type("death")
bossDeathEvent:register()

local logoutEvent = CreatureEvent("DungeonLogout")
function logoutEvent.onLogout(player)
    local instanceId = player:getInstanceId()
    if instanceId ~= 0 and isInsideDungeonArea(player:getPosition()) then
        local temple = player:getTown():getTemplePosition()
        player:teleportTo(temple)
        removePlayerFromInstance(player)
    end
    return true
end
logoutEvent:type("logout")
logoutEvent:register()

local loginEvent = CreatureEvent("DungeonLogin")
function loginEvent.onLogin(player)
    player:registerEvent("DungeonLogout")

    local pos = player:getPosition()
    if isInsideDungeonArea(pos) then
        local temple = player:getTown():getTemplePosition()
        if player:getInstanceId() ~= 0 then
            player:teleportTo(temple)
            player:setInstanceId(0)
        else
            player:setInstanceIdRaw(0)
            player:teleportTo(temple)
        end

    end

    return true
end
loginEvent:type("login")
loginEvent:register()
