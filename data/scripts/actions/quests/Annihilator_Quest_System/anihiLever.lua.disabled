local config = {
    duration     = 1,
    level_req    = 100,
    lever_id     = 2772,
    pulled_id    = 2773,

    monster_skull  = SKULL_RED,
    monster_emblem = GUILDEMBLEM_ENEMY,

    quest_range = {
        fromPos = Position(934, 980, 9),
        toPos   = Position(951, 991, 9),
        exit    = Position(950, 987, 9)
    }
}

local player_positions = {
    [1] = {fromPos = Position(942, 998, 9), toPos = Position(939, 987, 9)},
    [2] = {fromPos = Position(941, 998, 9), toPos = Position(938, 987, 9)},
    [3] = {fromPos = Position(940, 998, 9), toPos = Position(937, 987, 9)},
    [4] = {fromPos = Position(939, 998, 9), toPos = Position(936, 987, 9)},
}

local monsters = {
    {pos = Position(936, 984, 9), name = "Demon"},
    {pos = Position(938, 984, 9), name = "Demon"},
    {pos = Position(937, 990, 9), name = "Demon"},
    {pos = Position(939, 990, 9), name = "Demon"},
    {pos = Position(940, 987, 9), name = "Orshabaal"},
    {pos = Position(941, 987, 9), name = "Orshabaal"},
}

local playersInRoom = {}
local playerCount   = 0

local function addPlayerToRoom(player)
    local pid = player:getId()
    if not playersInRoom[pid] then
        playersInRoom[pid] = true
        playerCount = playerCount + 1
    end
end

local function removePlayerFromRoom(player)
    local pid = player:getId()
    if playersInRoom[pid] then
        playersInRoom[pid] = nil
        playerCount = math.max(0, playerCount - 1)
    end
end

local function doResetAnnihilator(leverPos)
    local tile = Tile(leverPos)
    if tile then
        local lever = tile:getItemById(config.pulled_id)
        if lever then
            lever:transform(config.lever_id)
        end
    end

    for x = config.quest_range.fromPos.x, config.quest_range.toPos.x do
        for y = config.quest_range.fromPos.y, config.quest_range.toPos.y do
            local t = Tile(Position(x, y, config.quest_range.fromPos.z))
            if not t then goto continue end

            for _, creature in pairs(t:getCreatures() or {}) do
                if creature:isPlayer() then
                    creature:teleportTo(config.quest_range.exit)
                    config.quest_range.exit:sendMagicEffect(CONST_ME_TELEPORT)
                    creature:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Your time has run out!")
                elseif creature:isMonster() then
                    creature:remove()
                end
            end

            ::continue::
        end
    end

    playersInRoom = {}
    playerCount   = 0
end

local annihilator = Action()
function annihilator.onUse(player, item, fromPosition, target, toPosition, isHotkey)
    if item.itemid ~= config.lever_id then
        player:sendCancelMessage("The quest is currently in use. Please wait for the reset.")
        return true
    end

    if playerCount > 0 then
        player:sendCancelMessage("A team is already inside the quest room.")
        return true
    end

    local participants = {}
    local puller_found = false
    local hasAccess = player:getGroup():getAccess()

    for i = 1, #player_positions do
        local t = Tile(player_positions[i].fromPos)
        local creature = t and t:getBottomCreature()

        if not creature or not creature:isPlayer() then
            if not hasAccess then
                player:sendCancelMessage("All 4 tiles must be occupied to start the quest.")
                return true
            end
        else
            if creature:getLevel() < config.level_req and not hasAccess then
                player:sendCancelMessage(creature:getName() .. " does not meet the level requirement (" .. config.level_req .. ").")
                return true
            end

            if creature:getGuid() == player:getGuid() then
                puller_found = true
            end

            table.insert(participants, {ptr = creature, toPos = player_positions[i].toPos})
        end
    end

    if not puller_found and not hasAccess then
        player:sendCancelMessage("You must stand on one of the starting tiles to pull the lever.")
        return true
    end

    if #participants == 0 and hasAccess then
        table.insert(participants, {ptr = player, toPos = player_positions[1].toPos})
    end

    for _, mData in pairs(monsters) do
        local m = Game.createMonster(mData.name, mData.pos, false, true)
        if m then
            m:setSkull(config.monster_skull)
            m:setEmblem(config.monster_emblem)
        end
    end

    for _, info in pairs(participants) do
        info.ptr:teleportTo(info.toPos)
        info.toPos:sendMagicEffect(CONST_ME_TELEPORT)
        addPlayerToRoom(info.ptr)
    end

    item:transform(config.pulled_id)
    addEvent(doResetAnnihilator, config.duration * 60 * 1000, toPosition)
    return true
end
annihilator:aid(1940)
annihilator:register()

local exitEvent = MoveEvent()
function exitEvent.onStepOut(creature, item, position, fromPosition)
    local player = creature:getPlayer()
    if not player then return true end

    if not position:isInRange(config.quest_range.fromPos, config.quest_range.toPos) then
        removePlayerFromRoom(player)
    end
    return true
end

for x = config.quest_range.fromPos.x, config.quest_range.toPos.x do
    for y = config.quest_range.fromPos.y, config.quest_range.toPos.y do
        exitEvent:position(Position(x, y, config.quest_range.fromPos.z))
    end
end
exitEvent:type("stepout")
exitEvent:register()
