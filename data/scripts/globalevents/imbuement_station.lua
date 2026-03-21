local WORKBENCH_ID = 25334
local WORKBENCH_SLOTS = 6
local WORKBENCH_POSITIONS = {
    Position(1123, 1208, 7),
}
local ABANDON_TIME = 600000
local STORAGE_WORKBENCH = 30050

local globalevent = GlobalEvent("ImbuementStation")
function globalevent.onStartup()
    for _, pos in ipairs(WORKBENCH_POSITIONS) do
        local tile = Tile(pos)
        if tile then
            local existing = tile:getItemById(WORKBENCH_ID)
            if existing then
                local container = Container(existing.uid)
                if not container then
                    existing:remove()
                    Game.createContainer(WORKBENCH_ID, WORKBENCH_SLOTS, pos)
                end
            else
                Game.createContainer(WORKBENCH_ID, WORKBENCH_SLOTS, pos)
            end
        end
    end
    return true
end
globalevent:register()

local function findWorkbench()
    for _, pos in ipairs(WORKBENCH_POSITIONS) do
        local tile = Tile(pos)
        if tile then
            local wb = tile:getItemById(WORKBENCH_ID)
            if wb then
                return Container(wb.uid)
            end
        end
    end
    return nil
end

local ec = Event()
function ec.onMoveItem(self, item, count, fromPosition, toPosition, fromCylinder, toCylinder)
    local isMovingTo   = toCylinder   and toCylinder:isItem()   and toCylinder:getId() == WORKBENCH_ID
    local isMovingFrom = fromCylinder and fromCylinder:isItem() and fromCylinder:getId() == WORKBENCH_ID

    if isMovingTo then
        item:setAttribute(ITEM_ATTRIBUTE_OWNER, self:getId())

        local storedEvent = Game.getStorageValue(STORAGE_WORKBENCH)
        if storedEvent and storedEvent > 0 then
            stopEvent(storedEvent)
        end

        local eventId = addEvent(function(playerId)
            local workbench = findWorkbench()
            if not workbench then return end

            local size = workbench:getSize()
            for i = size - 1, 0, -1 do
                local subItem = workbench:getItem(i)
                if subItem and subItem:hasAttribute(ITEM_ATTRIBUTE_OWNER)
                    and subItem:getAttribute(ITEM_ATTRIBUTE_OWNER) == playerId then
                    local player = Player(playerId)
                    if player then
                        local depot = player:getDepotChest(0, true)
                        subItem:moveTo(depot)
                        player:sendTextMessage(MESSAGE_INFO_DESCR,
                            "You left an item unattended on the imbuement workbench. It has been sent to your depot.")
                        player:save()
                    else
                        subItem:removeAttribute(ITEM_ATTRIBUTE_OWNER)
                    end
                end
            end
            Game.setStorageValue(STORAGE_WORKBENCH, -1)
        end, ABANDON_TIME, self:getId())

        Game.setStorageValue(STORAGE_WORKBENCH, eventId)
        return RETURNVALUE_NOERROR
    end

    if isMovingFrom then
        local ownerId = item:getAttribute(ITEM_ATTRIBUTE_OWNER)
        if ownerId and ownerId ~= 0 and ownerId ~= "" then
            if self:getId() ~= ownerId then
                self:sendCancelMessage("This item does not belong to you.")
                return RETURNVALUE_NOTPOSSIBLE
            end
            item:removeAttribute(ITEM_ATTRIBUTE_OWNER)
            local storedEvent = Game.getStorageValue(STORAGE_WORKBENCH)
            if storedEvent and storedEvent > 0 then
                stopEvent(storedEvent)
                Game.setStorageValue(STORAGE_WORKBENCH, -1)
            end
        end
        return RETURNVALUE_NOERROR
    end

    return RETURNVALUE_NOERROR
end
ec:register(1)
