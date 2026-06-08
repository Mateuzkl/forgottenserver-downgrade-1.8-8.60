local findInfluenced = TalkAction("/findinfluenced", "!findinfluenced")

function findInfluenced.onSay(player, words, param)
    -- Retrieve and merge both influenced and fiendish lists
    local list = {}
    for _, m in ipairs(Game.getInfluencedCreatures()) do
        list[#list + 1] = m
    end
    for _, m in ipairs(Game.getFiendishCreatures()) do
        list[#list + 1] = m
    end

    if #list == 0 then
        player:sendTextMessage(MESSAGE_STATUS_WARNING,
            "There are no active influenced or fiendish creatures at the moment.")
        return false
    end

    local playerPos = player:getPosition()
    local closest = list[1]
    local closestDist = math.huge

    for _, monster in ipairs(list) do
        local mPos = monster:getPosition()
        local dist = math.abs(playerPos.x - mPos.x) + math.abs(playerPos.y - mPos.y)
                   + math.abs(playerPos.z - mPos.z) * 10
        if dist < closestDist then
            closestDist = dist
            closest = monster
        end
    end

    local mPos = closest:getPosition()
    local dx = mPos.x - playerPos.x
    local dy = mPos.y - playerPos.y
    local sqmDist = math.max(math.abs(dx), math.abs(dy))

    local direction = ""
    if math.abs(dy) > math.abs(dx) then
        direction = dy < 0 and "North" or "South"
    elseif math.abs(dx) > math.abs(dy) then
        direction = dx > 0 and "East" or "West"
    else
        if dy < 0 then
            direction = dx > 0 and "Northeast" or "Northwest"
        else
            direction = dx > 0 and "Southeast" or "Southwest"
        end
    end

    local monsterName = closest:getName()

    player:sendTextMessage(MESSAGE_INFO_DESCR,
        string.format(
            "The nearest influenced creature is to the %s, approximately %d SQMs from you.",
            direction, sqmDist))
    player:getPosition():sendMagicEffect(CONST_ME_MAGIC_RED)
    return false
end
findInfluenced:exhaustion(6000)
findInfluenced:exhaustionMessage("Please wait {time}s before using the command findInfluenced again.", MESSAGE_STATUS_SMALL)
findInfluenced:register()
