local ETCHER_ID = 51443
local WORKBENCH_ID = 25334

local SCROLL_IDS = {
    51444, 51445, 51446, 51447, 51448, 51449, 51450, 51451,
    51452, 51453, 51454, 51455, 51456, 51457, 51458, 51459,
    51460, 51461, 51462, 51463, 51464, 51465, 51466, 51467,
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

local function formatDuration(seconds)
    local hours = math.floor(seconds / 3600)
    local minutes = math.floor((seconds % 3600) / 60)
    return string.format("%dh %02dmin", hours, minutes)
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

    local equipment = nil
    local scrolls = {}
    local size = container:getSize()

    if size == 0 then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "The workbench is empty. Place equipment and scrolls.")
        return true
    end

    for i = 0, size - 1 do
        local subItem = container:getItem(i)
        if subItem then
            local scrollId = subItem:getId()
            if VALID_SCROLLS[scrollId] then
                local def = Game.getImbuementByScroll(scrollId)
                if def then
                    table.insert(scrolls, { item = subItem, def = def })
                end
            elseif not equipment then
                local ok, slots = pcall(function() return subItem:getImbuementSlots() end)
                if ok and slots and slots > 0 then
                    equipment = subItem
                end
            end
        end
    end

    if not equipment then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "Place equipment with imbuement slots on the workbench.")
        return true
    end

    if #scrolls == 0 then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "Place imbuement scrolls on the workbench alongside the equipment.")
        return true
    end

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

    local totalImb = equipment:getImbuements()
    local totalSlots = equipment:getImbuementSlots()
    local defDuration = scrolls[1] and scrolls[1].def.duration or 72000
    player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
        string.format("%d imbuement(s) applied: %s\nDuration: %s | Slots: %d/%d",
            applied,
            table.concat(appliedNames, ", "),
            formatDuration(defDuration),
            totalImb and #totalImb or applied,
            totalSlots))

    toPosition:sendMagicEffect(CONST_ME_MAGIC_GREEN)
    return true
end

action:id(ETCHER_ID)
action:allowFarUse(true)
action:register()
