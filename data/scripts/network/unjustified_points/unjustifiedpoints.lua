local OPCODE_UNJUSTIFIED_REQUEST = 0x2E
local OPCODE_UNJUSTIFIED_SEND = 0x2F

local ACTION_REFRESH = 1
local REFRESH_INTERVAL = 30000

local PERIOD_DAY = 24 * 60 * 60
local PERIOD_WEEK = 7 * 24 * 60 * 60
local PERIOD_MONTH = 30 * 24 * 60 * 60
local COUNT_CACHE_SECONDS = math.max(1, math.floor(REFRESH_INTERVAL / 1000))
local UNJUSTIFIED_KILLS_INDEX = "idx_player_deaths_unjustified_kills"

local countsCache = {}
local indexChecked = false

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function warnMissingUnjustifiedKillsIndex()
	local message = "[CustomUnjustifiedPoints] Missing index `" .. UNJUSTIFIED_KILLS_INDEX ..
		"` on `player_deaths`. Create it during migration/startup admin maintenance for faster refreshes."
	if logger and logger.warn then
		logger.warn(message)
	else
		print(message)
	end
end

local function ensureUnjustifiedKillsIndex()
	if indexChecked then
		return
	end

	local resultId = db.storeQuery(
		"SELECT COUNT(*) AS `count` FROM `information_schema`.`STATISTICS` " ..
		"WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'player_deaths' AND `INDEX_NAME` = " ..
		db.escapeString(UNJUSTIFIED_KILLS_INDEX)
	)
	if not resultId then
		return
	end

	local exists = result.getDataInt(resultId, "count") > 0
	result.free(resultId)
	indexChecked = true
	if not exists then
		warnMissingUnjustifiedKillsIndex()
	end
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

local function getKillLimit(player)
	local skull = player:getSkull()
	if skull == SKULL_RED or skull == SKULL_BLACK then
		return math.max(1, configManager.getNumber(configKeys.KILLS_TO_BLACK))
	end
	return math.max(1, configManager.getNumber(configKeys.KILLS_TO_RED))
end

local function getCacheKey(player)
	return player:getGuid()
end

local function setCachedCounts(player, dayKills, weekKills, monthKills, expiresAt)
	countsCache[getCacheKey(player)] = {
		dayKills = dayKills or 0,
		weekKills = weekKills or 0,
		monthKills = monthKills or 0,
		expiresAt = expiresAt
	}
end

local function getCachedCounts(player, now)
	local cached = countsCache[getCacheKey(player)]
	if cached and cached.expiresAt > now then
		return cached.dayKills, cached.weekKills, cached.monthKills
	end
	return nil
end

local function refreshUnjustifiedKillCaches(players)
	ensureUnjustifiedKillsIndex()

	local now = os.time()
	local expiresAt = now + COUNT_CACHE_SECONDS
	local escapedNames = {}
	local playersByName = {}

	for _, player in ipairs(players) do
		if supportsCustomNetwork(player) and not getCachedCounts(player, now) then
			local name = player:getName()
			escapedNames[#escapedNames + 1] = db.escapeString(name)
			playersByName[name] = playersByName[name] or {}
			playersByName[name][#playersByName[name] + 1] = player
			setCachedCounts(player, 0, 0, 0, expiresAt)
		end
	end

	if #escapedNames == 0 then
		return
	end

	local dayStart = now - PERIOD_DAY
	local weekStart = now - PERIOD_WEEK
	local monthStart = now - PERIOD_MONTH
	local resultId = db.storeQuery(
		"SELECT `killed_by`, " ..
			"SUM(CASE WHEN `time` >= " .. dayStart .. " THEN 1 ELSE 0 END) AS `day_kills`, " ..
			"SUM(CASE WHEN `time` >= " .. weekStart .. " THEN 1 ELSE 0 END) AS `week_kills`, " ..
			"COUNT(*) AS `month_kills` " ..
		"FROM `player_deaths` WHERE `killed_by` IN (" .. table.concat(escapedNames, ", ") ..
		") AND `is_player` = 1 AND `unjustified` = 1 AND `time` >= " .. monthStart .. " GROUP BY `killed_by`"
	)
	if not resultId then
		return
	end

	repeat
		local killedBy = result.getDataString(resultId, "killed_by")
		local matchedPlayers = playersByName[killedBy]
		if matchedPlayers then
			local dayKills = result.getDataInt(resultId, "day_kills") or 0
			local weekKills = result.getDataInt(resultId, "week_kills") or 0
			local monthKills = result.getDataInt(resultId, "month_kills") or 0
			for _, player in ipairs(matchedPlayers) do
				setCachedCounts(player, dayKills, weekKills, monthKills, expiresAt)
			end
		end
	until not result.next(resultId)
	result.free(resultId)
end

local function getUnjustifiedKillCounts(player)
	local now = os.time()
	local dayKills, weekKills, monthKills = getCachedCounts(player, now)
	if dayKills then
		return dayKills, weekKills, monthKills
	end

	refreshUnjustifiedKillCaches({ player })
	dayKills, weekKills, monthKills = getCachedCounts(player, now)
	return dayKills or 0, weekKills or 0, monthKills or 0
end

local function getPeriodData(kills, limit)
	local percent = clamp(math.floor((kills * 100) / limit), 0, 100)
	local remaining = clamp(limit - kills, 0, 255)
	return percent, remaining
end

local function getActivePvpSituations(player)
	local skullTime = math.max(0, player:getSkullTime() or 0)
	local fragTime = math.max(1, configManager.getNumber(configKeys.FRAG_TIME))
	return clamp(math.ceil(skullTime / fragTime), 0, 255)
end

local function sendUnjustifiedPoints(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local dayKills, weekKills, monthKills = getUnjustifiedKillCounts(player)
	local limit = getKillLimit(player)
	local dayPercent, dayRemaining = getPeriodData(dayKills, limit)
	local weekPercent, weekRemaining = getPeriodData(weekKills, limit)
	local monthPercent, monthRemaining = getPeriodData(monthKills, limit)
	local skullTime = math.max(0, player:getSkullTime() or 0)

	local msg = NetworkMessage(player)
	msg:addByte(OPCODE_UNJUSTIFIED_SEND)
	msg:addByte(dayPercent)
	msg:addByte(dayRemaining)
	msg:addByte(weekPercent)
	msg:addByte(weekRemaining)
	msg:addByte(monthPercent)
	msg:addByte(monthRemaining)
	msg:addU32(skullTime)
	msg:addByte(getActivePvpSituations(player))
	msg:addByte(player:getSkull())
	return msg:sendToPlayer(player)
end

local handler = PacketHandler(OPCODE_UNJUSTIFIED_REQUEST)

function handler.onReceive(player, msg)
	if not supportsCustomNetwork(player) then
		return true
	end
	if not NetworkGuard.cooldown(player, "unjustified-points", 1000) then
		return true
	end

	local action = NetworkGuard.readByte(msg)
	if not action then
		return true
	end

	if action == ACTION_REFRESH then
		sendUnjustifiedPoints(player)
	end
	return true
end

handler:register()

local updater = GlobalEvent("CustomUnjustifiedPointsUpdater")

function updater.onThink(interval)
	local players = Game.getPlayers()
	refreshUnjustifiedKillCaches(players)
	for _, player in ipairs(players) do
		sendUnjustifiedPoints(player)
	end
	return true
end

updater:interval(REFRESH_INTERVAL)
updater:register()

CustomUnjustifiedPoints = {
	send = sendUnjustifiedPoints
}
