math.randomseed(os.time())
DATA_DIRECTORY = "data"
CORE_DIRECTORY = DATA_DIRECTORY
dofile(DATA_DIRECTORY .. '/lib/lib.lua')

local startupFile=io.open("data/startup/startup.lua", "r")
if startupFile ~= nil then
	dofile("data/startup/startup.lua")
	io.close(startupFile)
end

-- LuaFormatter off
ropeSpots = { 386, 421, 7762, 12202, 12936, 14238, 17238, 23363, 21965, 21966, 21967, 21968 }
specialRopeSpots = { 12935 }
-- LuaFormatter on

function getDistanceBetween(firstPosition, secondPosition)
	local xDif = math.abs(firstPosition.x - secondPosition.x)
	local yDif = math.abs(firstPosition.y - secondPosition.y)
	local posDif = math.max(xDif, yDif)
	if firstPosition.z ~= secondPosition.z then posDif = posDif + 15 end
	return posDif
end

function getFormattedWorldTime()
	local worldTime = getWorldTime()
	local hours = math.floor(worldTime / 60)
	local minutes = worldTime % 60
	return string.format("%02d:%02d", hours, minutes)
end

function getLootRandom()
	return math.random(0, MAX_LOOTCHANCE) / configManager.getNumber(configKeys.RATE_LOOT)
end

function getListOfContainerItems(container)
    if not container:isContainer() then
        return false
    end
    
    local containers, items = {}, {}
    table.insert(containers, container)
    while #containers > 0 do
        for i = 0, containers[1]:getSize() - 1 do  
            local item = containers[1]:getItem(i)
            table.insert(items, item)
            if item:isContainer() then
                table.insert(containers, item)
            end
        end
        table.remove(containers, 1)
    end
    
    return items
end

function checkDuplicateStorageKeys(varName)
	local keys = _G[varName]
	local seen = {}
	local duplicates = {}
	for k, v in pairs(keys) do
		if seen[v] then
			table.insert(duplicates, v)
		else
			seen[v] = true
		end
	end

	if next(duplicates) == nil then
		return false
	else
		return duplicates
	end
end

---@generic T: table, K, V
---@param array T The table to search in
---@param value K The value to search for
---@return boolean found Whether the value was found
table.contains = function(array, value)
	for _, targetColumn in pairs(array) do if targetColumn == value then return true end end
	return false
end

---@param str string
---@param sep string
---@return table<integer, string>
string.split = function(str, sep)
	local res = {}
	for v in str:gmatch("([^" .. sep .. "]+)") do res[#res + 1] = v end
	return res
end

---@param str string
---@param sep string
---@return table<integer, string>
string.splitTrimmed = function(str, sep)
	local res = {}
	for v in str:gmatch("([^" .. sep .. "]+)") do res[#res + 1] = v:trim() end
	return res
end

---@param str string
---@return string
string.trim = function(str) return str:match '^()%s*$' and '' or str:match '^%s*(.*%S)' end

function getPlayerDatabaseInfo(name_or_guid)
	local sql_where = ""

	if type(name_or_guid) == 'string' then
		sql_where = "WHERE `p`.`name`=" .. db.escapeString(name_or_guid) .. ""
	elseif type(name_or_guid) == 'number' then
		sql_where = "WHERE `p`.`id`='" .. name_or_guid .. "'"
	else
		return false
	end

	local multiWorld = configManager and configKeys and configKeys.MULTI_WORLD_ENABLED and configManager.getBoolean(configKeys.MULTI_WORLD_ENABLED)
	if multiWorld then
		sql_where = sql_where .. " AND `p`.`world_id`=" .. configManager.getNumber(configKeys.WORLD_ID)
	end

	local sql_query = [[
		SELECT
			`p`.`id` as `guid`,
			`p`.`name`,
			CASE WHEN `po`.`player_id` IS NULL
				THEN 0
				ELSE 1
			END AS `online`,
			`p`.`group_id`,
			`p`.`level`,
			`p`.`experience`,
			`p`.`vocation`,
			`p`.`maglevel`,
			`p`.`skill_fist`,
			`p`.`skill_club`,
			`p`.`skill_sword`,
			`p`.`skill_axe`,
			`p`.`skill_dist`,
			`p`.`skill_shielding`,
			`p`.`skill_fishing`,
			`p`.`town_id`,
			`p`.`balance`,
			`gm`.`guild_id`,
			`gm`.`nick`,
			`g`.`name` AS `guild_name`,
			CASE WHEN `p`.`id` = `g`.`ownerid`
				THEN 1
				ELSE 0
			END AS `is_leader`,
			`gr`.`name` AS `rank_name`,
			`gr`.`level` AS `rank_level`,
			`h`.`id` AS `house_id`,
			`h`.`name` AS `house_name`,
			`h`.`town_id` AS `house_town`
		FROM `players` AS `p`
		LEFT JOIN `players_online` AS `po`
			ON `p`.`id` = `po`.`player_id`
			AND (]] .. (multiWorld and "`po`.`world_id` = `p`.`world_id`" or "1 = 1") .. [[)
		LEFT JOIN `guild_membership` AS `gm`
			ON `p`.`id` = `gm`.`player_id`
		LEFT JOIN `guilds` AS `g`
			ON `gm`.`guild_id` = `g`.`id`
			AND (]] .. (multiWorld and "`g`.`world_id` = `p`.`world_id`" or "1 = 1") .. [[)
		LEFT JOIN `guild_ranks` AS `gr`
			ON `gm`.`rank_id` = `gr`.`id`
		LEFT JOIN `houses` AS `h`
			ON `p`.`id` = `h`.`owner`
			AND (]] .. (multiWorld and "`h`.`world_id` = `p`.`world_id`" or "1 = 1") .. [[)
	]] .. sql_where

	local query = db.storeQuery(sql_query)
	if not query then return false end

	local info = {
		["guid"] = result.getNumber(query, "guid"),
		["name"] = result.getString(query, "name"),
		["online"] = result.getNumber(query, "online"),
		["group_id"] = result.getNumber(query, "group_id"),
		["level"] = result.getNumber(query, "level"),
		["experience"] = result.getNumber(query, "experience"),
		["vocation"] = result.getNumber(query, "vocation"),
		["maglevel"] = result.getNumber(query, "maglevel"),
		["skill_fist"] = result.getNumber(query, "skill_fist"),
		["skill_club"] = result.getNumber(query, "skill_club"),
		["skill_sword"] = result.getNumber(query, "skill_sword"),
		["skill_axe"] = result.getNumber(query, "skill_axe"),
		["skill_dist"] = result.getNumber(query, "skill_dist"),
		["skill_shielding"] = result.getNumber(query, "skill_shielding"),
		["skill_fishing"] = result.getNumber(query, "skill_fishing"),
		["town_id"] = result.getNumber(query, "town_id"),
		["balance"] = result.getNumber(query, "balance"),
		["guild_id"] = result.getNumber(query, "guild_id"),
		["nick"] = result.getString(query, "nick"),
		["guild_name"] = result.getString(query, "guild_name"),
		["is_leader"] = result.getNumber(query, "is_leader"),
		["rank_name"] = result.getString(query, "rank_name"),
		["rank_level"] = result.getNumber(query, "rank_level"),
		["house_id"] = result.getNumber(query, "house_id"),
		["house_name"] = result.getString(query, "house_name"),
		["house_town"] = result.getNumber(query, "house_town")
	}

	result.free(query)
	return info
end
