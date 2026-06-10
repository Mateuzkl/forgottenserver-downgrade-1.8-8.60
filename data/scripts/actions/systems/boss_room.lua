local LEVER_AID = 8890
local EXIT_AID  = 1004

local BOSS_NAME = "Gaz'Haragoth"
local BOSS_POS  = Position(1058, 955, 13)

local PLAYER_DEST = Position(1064, 955, 13)

local INSTANCE_FROM = Position(1045, 947, 13)
local INSTANCE_TO   = Position(1068, 965, 13)

local PLAYER_TILES = {
    Position(1091, 955, 13),
    Position(1092, 955, 13),
    Position(1093, 955, 13),
    Position(1094, 955, 13),
    Position(1095, 955, 13),
    Position(1096, 955, 13),
    Position(1097, 955, 13),
    Position(1098, 955, 13),
    Position(1099, 955, 13),
    Position(1100, 955, 13),
}

local COOLDOWN_TIME = 3600

local instanceCounter = 30000

local function isInsideInstanceArea(pos)
    return pos.x >= INSTANCE_FROM.x and pos.x <= INSTANCE_TO.x
       and pos.y >= INSTANCE_FROM.y and pos.y <= INSTANCE_TO.y
       and pos.z >= INSTANCE_FROM.z and pos.z <= INSTANCE_TO.z
end

local function getPlayersOnTiles()
    local players = {}
    for _, pos in ipairs(PLAYER_TILES) do
        local tile = Tile(pos)
        if tile then
            local creatures = tile:getCreatures()
            if creatures then
                for _, creature in ipairs(creatures) do
                    local player = creature:getPlayer()
                    if player then
                        players[#players + 1] = player
                    end
                end
            end
        end
    end
    return players
end

local function cleanupInstanceCreatures(instanceId)
    if instanceId == 0 then return end

    local area = Game.getInstanceArea(instanceId)
    if not area then return end

    local cx = math.floor((area.fromPos.x + area.toPos.x) / 2)
    local cy = math.floor((area.fromPos.y + area.toPos.y) / 2)
    local searchCenter = Position(cx, cy, area.fromPos.z)
    local rangeX = math.floor(math.abs(area.toPos.x - area.fromPos.x) / 2)
    local rangeY = math.floor(math.abs(area.toPos.y - area.fromPos.y) / 2)

    local spectators = Game.getSpectators(searchCenter, false, false, rangeX, rangeX, rangeY, rangeY)
    for _, spec in ipairs(spectators) do
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

local function cleanupPlayerInstance(player)
    local instanceId = player:getInstanceId()
    if instanceId == 0 then return end

    cleanupInstanceCreatures(instanceId)
    Game.unregisterInstanceArea(instanceId)
end

local leverAction = Action()
function leverAction.onUse(player, item, fromPosition, target, toPosition, isHotkey)
    local playerPos = player:getPosition()
    local onTile = false
    for _, pos in ipairs(PLAYER_TILES) do
        if playerPos.x == pos.x and playerPos.y == pos.y and playerPos.z == pos.z then
            onTile = true
            break
        end
    end

    if not onTile then
        player:sendTextMessage(MESSAGE_EVENT_ORANGE,
            "[Boss Room] You must stand on the tiles to use the lever.")
        return true
    end

    if player:getInstanceId() ~= 0 then
        player:sendTextMessage(MESSAGE_EVENT_ORANGE,
            "[Boss Room] You are already inside a boss room. Leave the current room first.")
        return true
    end

    local now = os.time()
    local playersOnTiles = getPlayersOnTiles()

    if #playersOnTiles == 0 then
        player:sendTextMessage(MESSAGE_EVENT_ORANGE,
            "[Boss Room] No players found on the tiles. Stand on the tiles to enter.")
        return true
    end

    local blocked = {}
    for _, p in ipairs(playersOnTiles) do
        local cooldownEnd = p:getBossCooldown(BOSS_NAME)
        if cooldownEnd and cooldownEnd > now then
            local pRemaining = cooldownEnd - now
            local pMinutes = math.ceil(pRemaining / 60)
            blocked[#blocked + 1] = p:getName()
            p:getPosition():sendMagicEffect(CONST_ME_TUTORIALARROW)
            p:sendTextMessage(MESSAGE_EVENT_ORANGE,
                string.format("[Boss Room] You have already defeated this boss recently.\n" ..
                "You must wait %d minute(s). Leave the tile so others can enter.", pMinutes))
        end
    end

    if #blocked > 0 then
        player:sendTextMessage(MESSAGE_EVENT_ORANGE,
            string.format("[Boss Room] Cannot enter! The following players on the tiles already have cooldown:\n%s\n" ..
            "They must leave the tiles before anyone can enter.", table.concat(blocked, ", ")))
        return true
    end

    instanceCounter = instanceCounter + 1
    local instanceId = instanceCounter

    Game.registerInstanceArea(instanceId, INSTANCE_FROM, INSTANCE_TO)

    for _, p in ipairs(playersOnTiles) do
        p:setInstanceIdRaw(instanceId)

        local dest = p:getClosestFreePosition(PLAYER_DEST, false)
        if dest.x == 0 then dest = PLAYER_DEST end
        p:teleportTo(dest)

    end

    local boss = Game.createMonster(BOSS_NAME, BOSS_POS, false, true, CONST_ME_TELEPORT, instanceId)
    if boss then
        boss:registerEvent("BossRoomBossDeath")
    end

    return true
end
leverAction:aid(LEVER_AID)
leverAction:register()

local exitMovement = MoveEvent()
function exitMovement.onStepIn(creature, item, position, fromPosition)
    local player = creature:getPlayer()
    if not player then return true end

    local instanceId = player:getInstanceId()
    if instanceId == 0 then return true end

    cleanupPlayerInstance(player)

    local dest = Position(1054, 1000, 7)
    player:teleportTo(dest)
    player:setInstanceId(0)


    return true
end
exitMovement:aid(EXIT_AID)
exitMovement:type("stepin")
exitMovement:register()

local bossDeathEvent = CreatureEvent("BossRoomBossDeath")
function bossDeathEvent.onDeath(creature, corpse, killer, mostDamageKiller, lastHitUnjustified, mostDamageUnjustified)
    local instanceId = creature:getInstanceId()
    if instanceId == 0 then return end

    local area = Game.getInstanceArea(instanceId)
    if not area then return end

    local cx = math.floor((area.fromPos.x + area.toPos.x) / 2)
    local cy = math.floor((area.fromPos.y + area.toPos.y) / 2)
    local rangeX = math.ceil((area.toPos.x - area.fromPos.x) / 2)
    local rangeY = math.ceil((area.toPos.y - area.fromPos.y) / 2)

    local damageMap = creature:getDamageMap()
    local now = os.time()
    local spectators = Game.getSpectators(Position(cx, cy, area.fromPos.z),
                                          false, false, rangeX, rangeX, rangeY, rangeY)
    for _, spec in ipairs(spectators) do
        if spec:isPlayer() and spec:getInstanceId() == instanceId then
            if damageMap[spec:getId()] then
                spec:setBossCooldown(BOSS_NAME, now + COOLDOWN_TIME)
                spec:sendTextMessage(MESSAGE_EVENT_ORANGE,
                    "[Boss Room] Congratulations! The boss has been defeated. Your cooldown has started.")
            end
        end
    end
end
bossDeathEvent:type("death")
bossDeathEvent:register()

local logoutEvent = CreatureEvent("BossRoomLogout")
function logoutEvent.onLogout(player)
    local instanceId = player:getInstanceId()
    if instanceId ~= 0 and isInsideInstanceArea(player:getPosition()) then
        cleanupPlayerInstance(player)
        local temple = player:getTown():getTemplePosition()
        player:teleportTo(temple)
        player:setInstanceId(0)
    end
    return true
end
logoutEvent:type("logout")
logoutEvent:register()

local loginEvent = CreatureEvent("BossRoomLogin")
function loginEvent.onLogin(player)
    player:registerEvent("BossRoomLogout")

    local pos = player:getPosition()
    if isInsideInstanceArea(pos) then
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
