local ETCHER_ID = 51443
local WORKBENCH_ID = 25334

local SCROLL_IDS = {
    -- Powerful (tier 3)
    51444, 51445, 51446, 51447, 51448, 51449, 51450, 51451,
    51452, 51453, 51454, 51455, 51456, 51457, 51458, 51459,
    51460, 51461, 51462, 51463, 51464, 51465, 51466, 51467,
    -- Intricate (tier 2)
    51724, 51725, 51726, 51727, 51728, 51729, 51730, 51731,
    51732, 51733, 51734, 51735, 51736, 51737, 51738, 51739,
    51740, 51741, 51742, 51743, 51744, 51745, 51746, 51747,
}

local VALID_SCROLLS = {}
for _, id in ipairs(SCROLL_IDS) do
    VALID_SCROLLS[id] = true
end

local TYPE_NAMES = {
    [IMBUEMENT_TYPE_FIST_SKILL]           = "Fist Fighting",
    [IMBUEMENT_TYPE_CLUB_SKILL]           = "Club Fighting",
    [IMBUEMENT_TYPE_SWORD_SKILL]          = "Sword Fighting",
    [IMBUEMENT_TYPE_AXE_SKILL]            = "Axe Fighting",
    [IMBUEMENT_TYPE_DISTANCE_SKILL]       = "Distance Fighting",
    [IMBUEMENT_TYPE_SHIELD_SKILL]         = "Shielding",
    [IMBUEMENT_TYPE_FISHING_SKILL]        = "Fishing",
    [IMBUEMENT_TYPE_MAGIC_LEVEL]          = "Magic Level",
    [IMBUEMENT_TYPE_LIFE_LEECH]           = "Life Leech",
    [IMBUEMENT_TYPE_MANA_LEECH]           = "Mana Leech",
    [IMBUEMENT_TYPE_CRITICAL_CHANCE]      = "Critical Hit Chance",
    [IMBUEMENT_TYPE_CRITICAL_AMOUNT]      = "Critical Hit Amount",
    [IMBUEMENT_TYPE_FIRE_DAMAGE]          = "Fire Damage",
    [IMBUEMENT_TYPE_EARTH_DAMAGE]         = "Earth Damage",
    [IMBUEMENT_TYPE_ICE_DAMAGE]           = "Ice Damage",
    [IMBUEMENT_TYPE_ENERGY_DAMAGE]        = "Energy Damage",
    [IMBUEMENT_TYPE_DEATH_DAMAGE]         = "Death Damage",
    [IMBUEMENT_TYPE_HOLY_DAMAGE]          = "Holy Damage",
    [IMBUEMENT_TYPE_FIRE_RESIST]          = "Fire Protection",
    [IMBUEMENT_TYPE_EARTH_RESIST]         = "Earth Protection",
    [IMBUEMENT_TYPE_ICE_RESIST]           = "Ice Protection",
    [IMBUEMENT_TYPE_ENERGY_RESIST]        = "Energy Protection",
    [IMBUEMENT_TYPE_DEATH_RESIST]         = "Death Protection",
    [IMBUEMENT_TYPE_HOLY_RESIST]          = "Holy Protection",
    [IMBUEMENT_TYPE_PARALYSIS_DEFLECTION] = "Paralysis Deflection",
    [IMBUEMENT_TYPE_SPEED_BOOST]          = "Speed Boost",
    [IMBUEMENT_TYPE_CAPACITY_BOOST]       = "Capacity Boost",
}

local BASE_PRICES = {
    [1] = 7500,
    [2] = 60000,
    [3] = 250000,
}

local ALL_DEFS = Game.getImbuementDefinitions() or {}

local function formatDuration(seconds)
    local hours = math.floor(seconds / 3600)
    local minutes = math.floor((seconds % 3600) / 60)
    return string.format("%dh %02dmin", hours, minutes)
end

local function findMaterialRecipes(availMap, equipment)
    local bestByType = {}
    for _, def in ipairs(ALL_DEFS) do
        if #def.items > 0 and equipment:canApplyImbuement(def.categoryId, def.baseId) then
            local satisfied = true
            for _, req in ipairs(def.items) do
                if (availMap[req.itemId] or 0) < req.count then
                    satisfied = false
                    break
                end
            end
            if satisfied then
                local itype = def.imbuementType
                if not bestByType[itype] or def.baseId > bestByType[itype].baseId then
                    bestByType[itype] = def
                end
            end
        end
    end

    local candidates = {}
    for _, def in pairs(bestByType) do
        table.insert(candidates, def)
    end
    table.sort(candidates, function(a, b)
        if a.baseId ~= b.baseId then return a.baseId > b.baseId end
        return #a.items > #b.items
    end)

    local finalRecipes = {}
    local reservedItems = {}
    for _, def in ipairs(candidates) do
        local canFit = true
        for _, req in ipairs(def.items) do
            local reserved = reservedItems[req.itemId] or 0
            if (availMap[req.itemId] or 0) - reserved < req.count then
                canFit = false
                break
            end
        end
        if canFit then
            table.insert(finalRecipes, def)
            for _, req in ipairs(def.items) do
                reservedItems[req.itemId] = (reservedItems[req.itemId] or 0) + req.count
            end
        end
    end

    return finalRecipes
end

local action = Action()
function action.onUse(player, item, fromPosition, target, toPosition, isHotkey)
    if not target then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "Use the Etcher on the imbuement workbench.")
        return true
    end

    local container = Container(target.uid)
    if not container or target:getId() ~= WORKBENCH_ID then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "Use the Etcher on the imbuement workbench.")
        return true
    end

    local size = container:getSize()
    if size == 0 then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "The workbench is empty. Place equipment and scrolls or crafting materials.")
        return true
    end

    local equipment = nil
    local equipIdx = -1
    for i = 0, size - 1 do
        local subItem = container:getItem(i)
        if subItem then
            local id = subItem:getId()
            if not VALID_SCROLLS[id] then
                local ok, slots = pcall(function() return subItem:getImbuementSlots() end)
                if ok and slots and slots > 0 then
                    equipment = subItem
                    equipIdx = i
                    break
                end
            end
        end
    end

    if not equipment then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "Place equipment with imbuement slots on the workbench.")
        return true
    end

    local equipOwner = equipment:getAttribute(ITEM_ATTRIBUTE_OWNER)
    if equipOwner and equipOwner ~= 0 and equipOwner ~= player:getId() then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "This equipment does not belong to you.")
        return true
    end

    local scrolls = {}
    local materialItems = {}
    for i = 0, size - 1 do
        if i ~= equipIdx then
            local subItem = container:getItem(i)
            if subItem then
                local id = subItem:getId()
                if VALID_SCROLLS[id] then
                    local def = Game.getImbuementByScroll(id)
                    if def then
                        table.insert(scrolls, { item = subItem, def = def })
                    end
                else
                    table.insert(materialItems, subItem)
                end
            end
        end
    end

    if #scrolls == 0 then
        if #materialItems == 0 then
            player:sendTextMessage(MESSAGE_STATUS_SMALL, "Place imbuement scrolls or crafting materials on the workbench alongside the equipment.")
            return true
        end

        local availMap = {}
        for _, it in ipairs(materialItems) do
            local id = it:getId()
            availMap[id] = (availMap[id] or 0) + it:getCount()
        end

        local recipes = findMaterialRecipes(availMap, equipment)
        if #recipes == 0 then
            player:sendTextMessage(MESSAGE_STATUS_SMALL, "The items on the workbench don't match any imbuement recipe for this equipment.")
            return true
        end

        local freeSlots = equipment:getFreeImbuementSlots()
        if freeSlots == 0 then
            local total = equipment:getImbuementSlots()
            player:sendTextMessage(MESSAGE_STATUS_SMALL,
                string.format("The equipment has no free slots. (%d/%d)", total, total))
            return true
        end

        if #recipes > freeSlots then
            while #recipes > freeSlots do
                table.remove(recipes)
            end
        end

        local typesUsed = {}
        local existingImbuements = equipment:getImbuements()
        for _, imbue in ipairs(existingImbuements) do
            typesUsed[imbue:getType()] = true
        end
        for _, def in ipairs(recipes) do
            if typesUsed[def.imbuementType] then
                local typeName = TYPE_NAMES[def.imbuementType] or "Unknown"
                player:sendTextMessage(MESSAGE_STATUS_SMALL,
                    string.format("The equipment already has '%s'. Remove it first.", typeName))
                return true
            end
            typesUsed[def.imbuementType] = true
        end

        local totalCost = 0
        for _, def in ipairs(recipes) do
            totalCost = totalCost + (BASE_PRICES[def.baseId] or 0)
        end
        if totalCost > 0 and player:getMoney() < totalCost then
            player:sendTextMessage(MESSAGE_STATUS_SMALL,
                string.format("You need %d gold coins to apply these imbuements.", totalCost))
            return true
        end

        local toConsume = {}
        for _, def in ipairs(recipes) do
            for _, req in ipairs(def.items) do
                toConsume[req.itemId] = (toConsume[req.itemId] or 0) + req.count
            end
        end
        for itemId, totalNeeded in pairs(toConsume) do
            local remaining = totalNeeded
            for _, it in ipairs(materialItems) do
                if remaining <= 0 then break end
                if it:getId() == itemId then
                    local count = it:getCount()
                    if count <= remaining then
                        it:remove(count)
                        remaining = remaining - count
                    else
                        it:remove(remaining)
                        remaining = 0
                    end
                end
            end
        end

        if totalCost > 0 then
            player:removeMoney(totalCost)
        end

        local applied = 0
        local appliedNames = {}
        for _, def in ipairs(recipes) do
            local imbuement = Imbuement(def.imbuementType, def.value, def.duration, def.decayType, def.baseId)
            if equipment:addImbuement(imbuement) then
                applied = applied + 1
                table.insert(appliedNames, def.baseName .. " " .. def.name)
            end
        end

        if applied == 0 then
            player:sendTextMessage(MESSAGE_STATUS_SMALL, "Failed to apply imbuements. Unequip the item first.")
            return true
        end

        item:remove(1)

        local totalImb = equipment:getImbuements()
        local totalSlots = equipment:getImbuementSlots()
        local defDuration = recipes[1] and recipes[1].duration or 72000
        player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
            string.format("%d imbuement(s) applied: %s\nDuration: %s | Slots: %d/%d | Cost: %d gp",
                applied,
                table.concat(appliedNames, ", "),
                formatDuration(defDuration),
                totalImb and #totalImb or applied,
                totalSlots,
                totalCost))

        toPosition:sendMagicEffect(199)
        return true
    end

    -- Fluxo via scrolls
    local freeSlots = equipment:getFreeImbuementSlots()
    if freeSlots == 0 then
        local total = equipment:getImbuementSlots()
        player:sendTextMessage(MESSAGE_STATUS_SMALL,
            string.format("The equipment has no free slots. (%d/%d)", total, total))
        return true
    end

    if #scrolls > freeSlots then
        player:sendTextMessage(MESSAGE_STATUS_SMALL,
            string.format("Too many scrolls! Free slots: %d, Scrolls: %d. Remove the excess.", freeSlots, #scrolls))
        return true
    end

    local typesUsed = {}
    local existingImbuements = equipment:getImbuements()
    if existingImbuements then
        for _, imbue in ipairs(existingImbuements) do
            typesUsed[imbue:getType()] = true
        end
    end

    for _, scroll in ipairs(scrolls) do
        if typesUsed[scroll.def.imbuementType] then
            local typeName = TYPE_NAMES[scroll.def.imbuementType] or "Unknown"
            player:sendTextMessage(MESSAGE_STATUS_SMALL,
                string.format("Duplicate type: '%s'. Remove the repeated scroll.", typeName))
            return true
        end
        typesUsed[scroll.def.imbuementType] = true
    end

    for _, scroll in ipairs(scrolls) do
        local def = scroll.def
        if not equipment:canApplyImbuement(def.categoryId, def.baseId) then
            local typeName = TYPE_NAMES[def.imbuementType] or def.name
            player:sendTextMessage(MESSAGE_STATUS_SMALL,
                string.format("This equipment does not accept imbuement '%s' (tier %d).", typeName, def.baseId))
            return true
        end
    end

    local totalCost = 0
    for _, scroll in ipairs(scrolls) do
        local price = BASE_PRICES[scroll.def.baseId] or 0
        totalCost = totalCost + price
    end

    if totalCost > 0 then
        if player:getMoney() < totalCost then
            player:sendTextMessage(MESSAGE_STATUS_SMALL,
                string.format("You need %d gold coins to apply these imbuements.", totalCost))
            return true
        end
        player:removeMoney(totalCost)
    end

    local applied = 0
    local appliedNames = {}
    for _, scroll in ipairs(scrolls) do
        local def = scroll.def
        local imbuement = Imbuement(def.imbuementType, def.value, def.duration, def.decayType, def.baseId)
        if equipment:addImbuement(imbuement) then
            applied = applied + 1
            table.insert(appliedNames, def.baseName .. " " .. def.name)
        end
    end

    if applied == 0 then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "Failed to apply imbuements. Unequip the item first.")
        return true
    end

    for i = #scrolls, 1, -1 do
        scrolls[i].item:remove(1)
    end

    item:remove(1)

    local totalImb = equipment:getImbuements()
    local totalSlots = equipment:getImbuementSlots()
    local defDuration = scrolls[1] and scrolls[1].def.duration or 72000
    player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
        string.format("%d imbuement(s) applied: %s\nDuration: %s | Slots: %d/%d | Cost: %d gp",
            applied,
            table.concat(appliedNames, ", "),
            formatDuration(defDuration),
            totalImb and #totalImb or applied,
            totalSlots,
            totalCost))

    toPosition:sendMagicEffect(199)
    return true
end

action:id(ETCHER_ID)
action:allowFarUse(true)
action:register()
