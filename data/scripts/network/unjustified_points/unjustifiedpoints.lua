local OPCODE_UNJUSTIFIED_REQUEST = 0x2E
local OPCODE_UNJUSTIFIED_SEND = 0x2F

local ACTION_REFRESH = 1
local REFRESH_INTERVAL = 30000

local PERIOD_DAY = 24 * 60 * 60
local PERIOD_WEEK = 7 * 24 * 60 * 60
local PERIOD_MONTH = 30 * 24 * 60 * 60

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
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

local function getUnjustifiedKillCounts(player)
	local now = os.time()
	local dayStart = now - PERIOD_DAY
	local weekStart = now - PERIOD_WEEK
	local monthStart = now - PERIOD_MONTH
	local resultId = db.storeQuery(
		"SELECT " ..
			"SUM(CASE WHEN `time` >= " .. dayStart .. " THEN 1 ELSE 0 END) AS `day_kills`, " ..
			"SUM(CASE WHEN `time` >= " .. weekStart .. " THEN 1 ELSE 0 END) AS `week_kills`, " ..
			"COUNT(*) AS `month_kills` " ..
		"FROM `player_deaths` WHERE `killed_by` = " ..
		db.escapeString(player:getName()) .. " AND `is_player` = 1 AND `unjustified` = 1 AND `time` >= " .. monthStart
	)

	local dayKills, weekKills, monthKills = 0, 0, 0
	if resultId then
		dayKills = result.getDataInt(resultId, "day_kills") or 0
		weekKills = result.getDataInt(resultId, "week_kills") or 0
		monthKills = result.getDataInt(resultId, "month_kills") or 0
		result.free(resultId)
	end
	return dayKills, weekKills, monthKills
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
	for _, player in ipairs(Game.getPlayers()) do
		sendUnjustifiedPoints(player)
	end
	return true
end

updater:interval(REFRESH_INTERVAL)
updater:register()

CustomUnjustifiedPoints = {
	send = sendUnjustifiedPoints
}
