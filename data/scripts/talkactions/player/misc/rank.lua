local rank = TalkAction("!rank")

local top = 10
local rankcolor = MESSAGE_STATUS_CONSOLE_ORANGE
local errorcolor = MESSAGE_STATUS_CONSOLE_BLUE
local popup = true -- true = popup window, false = local message
local exhaustvalue = 78692 -- storage to prevent spam
local exhausttime = 3 -- wait time (seconds)
local maxgroup = 1 -- up to which group_id to include (1 = normal players)

local function worldWhere()
    if configManager and configKeys and configKeys.MULTI_WORLD_ENABLED and configManager.getBoolean(configKeys.MULTI_WORLD_ENABLED) then
        return " AND `world_id` = " .. math.max(1, tonumber(configManager.getNumber(configKeys.WORLD_ID)) or 1)
    end
    return ""
end

-- rank aliases
local ranks = {
    ['level'] = 1, ['lvl'] = 1, ['exp'] = 1, ['xp'] = 1,
    ['magic'] = 2, ['ml'] = 2,
    ['bank'] = 3, ['balance'] = 3, ['cash'] = 3, ['money'] = 3, ['gp'] = 3,
    ['fist'] = 4, ['club'] = 5, ['sword'] = 6, ['axe'] = 7,
    ['distance'] = 8, ['dist'] = 8,
    ['shielding'] = 9, ['shield'] = 9,
    ['fishing'] = 10, ['fish'] = 10,
    ['frags'] = 11
}

-- optional vocations
local voc = {
    ['none'] = 0,
    ['sorcerer'] = {1, 5, 9}, ['ms'] = {1, 5},
    ['druid'] = {2, 6, 10}, ['ed'] = {2, 6},
    ['paladin'] = {3, 7, 11}, ['rp'] = {3, 7},
    ['knight'] = {4, 8, 12}, ['ek'] = {4, 8},
    ['monk'] = {4, 8, 12}, ['em'] = {9, 10}
}

-- SQL columns for each rank
local stats = {
    [1] = {"experience", "level"},
    [2] = {"manaspent", "maglevel"},
    [3] = {"balance"},
    [4] = {"skill_fist"},
    [5] = {"skill_club"},
    [6] = {"skill_sword"},
    [7] = {"skill_axe"},
    [8] = {"skill_dist"},
    [9] = {"skill_shielding"},
    [10] = {"skill_fishing"}
}

local stats_names = {
    [1] = {"exp", "level"},
    [2] = {"mana spent", "magic level"},
    [3] = {"account balance"},
    [4] = {"fist fighting"},
    [5] = {"club fighting"},
    [6] = {"sword fighting"},
    [7] = {"axe fighting"},
    [8] = {"distance fighting"},
    [9] = {"shielding"},
    [10] = {"fishing"},
    [11] = {"frags"}
}

local stats_short = {
    [1] = {"xp: ", ""}, [2] = {"mana: ", ""}, [3] = {""}, [4] = {""},
    [5] = {""}, [6] = {""}, [7] = {""}, [8] = {""}, [9] = {""},
    [10] = {""}, [11] = {""}
}

local function table_find(t, value)
    for i, v in pairs(t) do
        if v == value then return i end
    end
    return nil
end

local function getHighest(check, values)
    local highest, highestVal, highestI = 0, nil, nil
    for i = 1, #values do
        if (check[values[i]] or 0) > highest then
            highest = check[values[i]]
            highestVal = values[i]
            highestI = i
        end
    end
    return {highest, highestVal, highestI}
end

local function getTopFraggers(vocs, limit)
    limit = limit or top
    local fraggers = {}
    local resultId = db.storeQuery("SELECT `killed_by` FROM `player_deaths` WHERE `is_player` = 1" .. worldWhere())
    if resultId then
        repeat
            local killer = result.getDataString(resultId, "killed_by")
            if killer and killer ~= "" then table.insert(fraggers, killer) end
        until not result.next(resultId)
        result.free(resultId)
    end

    if #fraggers == 0 then return "\nNo players with registered frags." end

    local fraggers_names, fraggers_total = {}, {}
    for _, name in ipairs(fraggers) do
        if not table_find(fraggers_names, name) then table.insert(fraggers_names, name) end
        fraggers_total[name] = (fraggers_total[name] or 0) + 1
    end

    local fraggers_top, place = {}, 0
    repeat
        local v = getHighest(fraggers_total, fraggers_names)
        if not v[2] then break end

        if vocs then
            local resultVoc = db.storeQuery("SELECT `vocation` FROM `players` WHERE `name` = " .. db.escapeString(v[2]) .. worldWhere() .. " LIMIT 1")
            if resultVoc then
                local vocId = result.getDataInt(resultVoc, "vocation")
                result.free(resultVoc)
                if isInArray(vocs, vocId) then
                    place = place + 1
                    table.insert(fraggers_top, {v[1], v[2]})
                end
            end
        else
            place = place + 1
            table.insert(fraggers_top, {v[1], v[2]})
        end
        table.remove(fraggers_names, v[3])
    until (place >= limit)

    if #fraggers_top == 0 then return "\nNo players found with this vocation." end

    local msg = ""
    for i, data in ipairs(fraggers_top) do
        msg = msg .. string.format("\n[%d] [%s] [%d]", i, data[2], data[1])
    end
    return msg
end

function rank.onSay(player, words, param)
    -- Anti-spam
    local exhaust = player:getStorageValue(exhaustvalue)
    if exhaust ~= nil and exhaust ~= -1 and exhaust >= os.time() then
        player:sendTextMessage(errorcolor, "Please wait a few seconds before using this again.")
        return false
    end
    player:setStorageValue(exhaustvalue, os.time() + exhausttime)

    local split = param:split(",")
    if #split == 0 or split[1] == "" then
        local ranks2 = {}
        for i = 1, #stats_names do table.insert(ranks2, stats_names[i][#stats_names[i]]) end
        player:popupFYI("Example: " .. words .. " balance, knight (optional)\n\nAvailable ranks:\n\n" .. table.concat(ranks2, "\n"))
        return false
    end

    for i = 1, #split do split[i] = split[i]:gsub("^%s*(.-)%s*$", "%1"):lower() end
    if not ranks[split[1]] then
        player:sendTextMessage(errorcolor, "Invalid rank name. Use the command without parameters to see available options.")
        return false
    end

    local rankIndex = ranks[split[1]]
    local msg = "Top players, " .. stats_names[rankIndex][#stats_names[rankIndex]] ..
        (voc[split[2]] and (", " .. split[2]) or "") .. ":"

    if rankIndex == 11 then
        local resultFrags = msg .. getTopFraggers(voc[split[2]], top)
        if popup then player:popupFYI(resultFrags) else player:sendTextMessage(rankcolor, resultFrags) end
        return false
    end

    -- Build query using COALESCE to avoid nil
    local fields = {}
    for i = 1, #stats[rankIndex] do
        fields[i] = "COALESCE(`" .. stats[rankIndex][i] .. "`,0) AS `" .. stats[rankIndex][i] .. "`"
    end

    local query = string.format(
        "SELECT `name`, %s FROM `players` WHERE `group_id` <= %d%s%s ORDER BY `%s` DESC LIMIT %d",
        table.concat(fields, ", "),
        maxgroup,
        worldWhere(),
        voc[split[2]] and (" AND `vocation` IN (" .. table.concat(voc[split[2]], ",") .. ")") or "",
        stats[rankIndex][#stats[rankIndex]],
        top
    )

    local resultId = db.storeQuery(query)
    if not resultId then
        player:sendTextMessage(errorcolor, "No players found for this rank.")
        return false
    end

    local place = 0
    repeat
        place = place + 1
        local name = result.getDataString(resultId, "name") or "N/A"
        msg = msg .. string.format("\n[%d] [%s] ", place, name)

        for i = 1, #stats[rankIndex] do
            local s = #stats[rankIndex] + 1 - i
            local field = stats[rankIndex][s]
            local value = result.getDataInt(resultId, field) or 0
            msg = msg .. string.format("[%s%d]%s", stats_short[rankIndex][s] or "", value, (s > 1 and " " or ""))
        end
    until not result.next(resultId)
    result.free(resultId)

    if popup then player:popupFYI(msg) else player:sendTextMessage(rankcolor, msg) end
    return false
end

rank:separator(" ")
rank:register()