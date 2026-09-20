local increasing = {[419] = 420, [431] = 430, [452] = 453, [563] = 564, [549] = 562, [10145] = 10146}
local decreasing = {[420] = 419, [430] = 431, [453] = 452, [564] = 563, [562] = 549, [10146] = 10145}

local stepIn = MoveEvent()
function stepIn.onStepIn(creature, item, position, fromPosition)
    if not increasing[item.itemid] then return true end

    if not creature:isPlayer() or creature:isInGhostMode() then return true end

    item:transform(increasing[item.itemid])

    if item.actionid >= actionIds.levelDoor then
        if creature:getLevel() < item.actionid - actionIds.levelDoor then
            creature:teleportTo(fromPosition, false, CONST_ME_NONE)
            position:sendMagicEffect(CONST_ME_MAGIC_BLUE)
            creature:sendTextMessage(MESSAGE_EVENT_ADVANCE,
                                     "The tile seems to be protected against unwanted intruders.")
        end
        return true
    end

    if Tile(position):hasFlag(TILESTATE_PROTECTIONZONE) then
        local lookPosition = creature:getPosition()
        lookPosition:getNextPosition(creature:getDirection())
        local depotItem = Tile(lookPosition):getItemByType(ITEM_TYPE_DEPOT)
        if depotItem then
            local depotChest = creature:getDepotChest(getDepotId(depotItem:getUniqueId()), true)
            local depotItems = math.max(0, depotChest:getItemHoldingCount() - 17)
            local inbox = creature:getInbox()
            if inbox then
                depotItems = depotItems + inbox:getItemHoldingCount()
            end
            creature:sendTextMessage(MESSAGE_STATUS_DEFAULT,
                                     "Your depot contains " .. depotItems .. " item" ..
                                         (depotItems ~= 1 and "s." or "."))
            creature:addAchievementProgress("Safely Stored Away", 1000)
            return true
        end
    end

    if item.actionid ~= 0 and creature:getStorageValue(item.actionid) <= 0 then
        creature:teleportTo(fromPosition, false, CONST_ME_NONE)
        position:sendMagicEffect(CONST_ME_MAGIC_BLUE)
        creature:sendTextMessage(MESSAGE_EVENT_ADVANCE,
                                 "The tile seems to be protected against unwanted intruders.")
        return true
    end
    return true
end
stepIn:type("stepin")
for index, value in pairs(increasing) do
    stepIn:id(index)
end
stepIn:register()

local stepOut = MoveEvent()
function stepOut.onStepOut(creature, item, position, fromPosition)
    if not decreasing[item.itemid] then return true end

    if creature:isPlayer() and creature:isInGhostMode() then return true end

    item:transform(decreasing[item.itemid])
    return true
end
stepOut:type("stepout")
for index, value in pairs(decreasing) do
    stepOut:id(index)
end
stepOut:register()
