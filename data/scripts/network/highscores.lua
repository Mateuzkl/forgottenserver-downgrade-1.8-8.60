local OPCODE_HIGHSCORES = 0xB1
local HIGHSCORE_GET_ENTRIES = 0
local HIGHSCORE_OUR_RANK = 1
local ALL_VOCATIONS = 0xFFFFFFFF
local MAX_ENTRIES_PER_PAGE = 30

local categories = {
	[0] = {name = "Experience Points", column = "experience"},
	[1] = {name = "Fist Fighting", column = "skill_fist"},
	[2] = {name = "Club Fighting", column = "skill_club"},
	[3] = {name = "Sword Fighting", column = "skill_sword"},
	[4] = {name = "Axe Fighting", column = "skill_axe"},
	[5] = {name = "Distance Fighting", column = "skill_dist"},
	[6] = {name = "Shielding", column = "skill_shielding"},
	[7] = {name = "Fishing", column = "skill_fishing"},
	[8] = {name = "Magic Level", column = "maglevel"},
}

local categoryOrder = {0, 1, 2, 3, 4, 5, 6, 7, 8}

local vocations = {
	{id = ALL_VOCATIONS, name = "(all)"},
	{id = 1, name = "Knight", ids = {1, 11}},
	{id = 2, name = "Paladin", ids = {2, 12}},
	{id = 3, name = "Sorcerer", ids = {3, 13}},
	{id = 4, name = "Druid", ids = {4, 14}},
	{id = 5, name = "Monk", ids = {5, 15}},
}

local vocationById = {}
for _, vocation in ipairs(vocations) do
	vocationById[vocation.id] = vocation
end

local function supportsAstraClient(player)
	return player and player.isOTCv8 and player:isOTCv8()
end

local function clamp(value, minValue, maxValue)
	value = tonumber(value) or minValue
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function getServerName()
	if configManager and configKeys and configKeys.SERVER_NAME then
		return configManager.getString(configKeys.SERVER_NAME)
	end
	return "Astra"
end

local function getVocationCondition(vocationId)
	local vocation = vocationById[vocationId]
	if not vocation or not vocation.ids then
		return ""
	end

	return " AND `vocation` IN (" .. table.concat(vocation.ids, ",") .. ")"
end

local function getTotalRows(vocationCondition)
	local query = "SELECT COUNT(*) AS `total` FROM `players` WHERE `group_id` <= 1 AND `deletion` = 0" .. vocationCondition
	local resultId = db.storeQuery(query)
	if resultId == false then
		return 0
	end

	local total = result.getDataInt(resultId, "total")
	result.free(resultId)
	return math.max(0, tonumber(total) or 0)
end

local function getPlayerRank(playerGuid, column, vocationCondition)
	local query = string.format([[
		SELECT `rank` FROM (
			SELECT `id`, (@rank := @rank + 1) AS `rank`
			FROM (
				SELECT `id` FROM `players`
				WHERE `group_id` <= 1 AND `deletion` = 0%s
				ORDER BY `%s` DESC, `level` DESC, `name` ASC
			) AS `ordered_players`
			CROSS JOIN (SELECT @rank := 0) AS `rank_init`
		) AS `ranked_players`
		WHERE `id` = %d
		LIMIT 1
	]], vocationCondition, column, playerGuid)

	local resultId = db.storeQuery(query)
	if resultId == false then
		return 1
	end

	local rank = result.getDataInt(resultId, "rank")
	result.free(resultId)
	return math.max(1, tonumber(rank) or 1)
end

local function loadRows(column, vocationCondition, page, entriesPerPage)
	local offset = (page - 1) * entriesPerPage
	local query = string.format([[
		SELECT `id`, `name`, `level`, `vocation`, `points`, `rank` FROM (
			SELECT `id`, `name`, `level`, `vocation`, `%s` AS `points`, (@rank := @rank + 1) AS `rank`
			FROM (
				SELECT `id`, `name`, `level`, `vocation`, `%s`
				FROM `players`
				WHERE `group_id` <= 1 AND `deletion` = 0%s
				ORDER BY `%s` DESC, `level` DESC, `name` ASC
			) AS `ordered_players`
			CROSS JOIN (SELECT @rank := 0) AS `rank_init`
		) AS `ranked_players`
		WHERE `rank` > %d AND `rank` <= %d
	]], column, column, vocationCondition, column, offset, offset + entriesPerPage)

	local rows = {}
	local resultId = db.storeQuery(query)
	if resultId == false then
		return rows
	end

	repeat
		rows[#rows + 1] = {
			id = result.getDataInt(resultId, "id"),
			name = result.getDataString(resultId, "name") or "",
			level = result.getDataInt(resultId, "level"),
			vocation = result.getDataInt(resultId, "vocation"),
			points = result.getDataLong and result.getDataLong(resultId, "points") or result.getDataInt(resultId, "points"),
			rank = result.getDataInt(resultId, "rank"),
		}
	until not result.next(resultId)

	result.free(resultId)
	return rows
end

local function sendHighscores(player, requestType, categoryId, vocationId, worldName, page, entriesPerPage)
	if not supportsAstraClient(player) then
		return false
	end

	local category = categories[categoryId] or categories[0]
	categoryId = categories[categoryId] and categoryId or 0
	vocationId = vocationById[vocationId] and vocationId or ALL_VOCATIONS
	entriesPerPage = clamp(entriesPerPage, 5, MAX_ENTRIES_PER_PAGE)
	page = math.max(1, tonumber(page) or 1)

	local vocationCondition = getVocationCondition(vocationId)
	local totalRows = getTotalRows(vocationCondition)
	local pages = math.max(1, math.ceil(totalRows / entriesPerPage))
	if requestType == HIGHSCORE_OUR_RANK then
		local rank = getPlayerRank(player:getGuid(), category.column, vocationCondition)
		page = math.max(1, math.ceil(rank / entriesPerPage))
	else
		page = clamp(page, 1, pages)
	end

	local rows = loadRows(category.column, vocationCondition, page, entriesPerPage)
	local serverName = getServerName()
	local selectedWorld = worldName ~= "" and worldName or "All Game Worlds"
	local out = NetworkMessage(player)
	out:addByte(OPCODE_HIGHSCORES)
	out:addByte(0)
	out:addByte(1)
	out:addString(serverName)
	out:addString(selectedWorld)
	out:addByte(0)
	out:addByte(0)
	out:addByte(#vocations)
	for _, vocation in ipairs(vocations) do
		out:addU32(vocation.id)
		out:addString(vocation.name)
	end
	out:addU32(vocationId)
	out:addByte(#categoryOrder)
	for _, id in ipairs(categoryOrder) do
		out:addByte(id)
		out:addString(categories[id].name)
	end
	out:addByte(categoryId)
	out:addU16(page)
	out:addU16(pages)
	out:addByte(math.min(#rows, 0xFF))
	for i = 1, math.min(#rows, 0xFF) do
		local row = rows[i]
		out:addU32(row.rank)
		out:addString(row.name)
		out:addString("")
		out:addByte(clamp(row.vocation, 0, 0xFF))
		out:addString(serverName)
		out:addU16(clamp(row.level, 0, 0xFFFF))
		out:addByte(row.id == player:getGuid() and 1 or 0)
		out:addU64(math.max(0, tonumber(row.points) or 0))
	end
	out:addByte(0xFF)
	out:addByte(0)
	out:addByte(1)
	out:addU32(os.time())
	return out:sendToPlayer(player)
end

local highscoresHandler = PacketHandler(OPCODE_HIGHSCORES)
function highscoresHandler.onReceive(player, msg)
	if not supportsAstraClient(player) then
		return
	end

	local requestType = NetworkGuard.readByte(msg)
	local categoryId = NetworkGuard.readByte(msg)
	local vocationId = NetworkGuard.readU32(msg)
	local worldName = NetworkGuard.readString(msg, 64)
	if requestType == nil or categoryId == nil or vocationId == nil or worldName == nil then
		return
	end

	NetworkGuard.readByte(msg)
	NetworkGuard.readByte(msg)

	local page = 1
	if requestType == HIGHSCORE_GET_ENTRIES then
		page = NetworkGuard.readU16(msg) or 1
	end

	local entriesPerPage = NetworkGuard.readByte(msg) or 20
	sendHighscores(player, requestType, categoryId, vocationId, worldName, page, entriesPerPage)
end
highscoresHandler:register()
