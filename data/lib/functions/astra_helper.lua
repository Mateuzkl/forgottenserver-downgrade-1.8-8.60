AstraHelper = AstraHelper or {}

AstraHelper.OPCODES = {
	Cavebot = 210,
	CastOnFoot = 211,
	SmartFollow = 212,
	MiniBotState = 213,
	BotCheckAlert = 230,
}

AstraHelper.STORAGES = {
	Cavebot = 99997,
	SmartFollow = 99998,
	MehahClient = 99999,
	MiniBotTimeLeft = 100020,
	MiniBotTotalTime = 100021,
	MiniBotStartedAt = 100022,
	MiniBotTask = 100023,
	MiniBotRenewals = 100024,
	MiniBotBannedUntil = 100025,
	MiniBotAfkPauseUntil = 100026,
	MiniBotAfkAvailableAt = 100027,
}

-- These values mirror the recorder UI delivered with game_minibot. Keeping them
-- together makes the server policy explicit and avoids embedding economy rules in
-- the client.
AstraHelper.MINIBOT = {
	ProtocolVersion = 1,
	DefaultTime = 3 * 60 * 60,
	RenewTime = 60 * 60,
	MinimumTimeUsedToRenew = 15 * 60,
	RenewBasePrice = 5000000,
	RenewPriceStep = 5000000,
	AfkPauseDuration = 5 * 60,
	AfkPauseCooldown = 2 * 60 * 60,
	TaskExperienceMultiplier = 0.20,
	TaskLootMultiplier = 0.20,
	MaxPayloadBytes = 4096,
	MaxRequestsPerSecond = 8,
	MaxSafeInteger = 9007199254740991,
}

local miniBotRequestWindows = {}
local miniBotCheckSessions = {}
local MINIBOT_AFK_ICON_KEY = "minibot-afk-pause"

local function readStorage(player, key, defaultValue)
	local value = player:getStorageValue(key)
	if type(value) ~= "number" or value < 0 then
		return defaultValue
	end
	return value
end

local function writeStorage(player, key, value)
	player:setStorageValue(key, math.max(0, math.floor(tonumber(value) or 0)))
end

local function ensureMiniBotState(player)
	local total = readStorage(player, AstraHelper.STORAGES.MiniBotTotalTime, 0)
	if total <= 0 then
		total = AstraHelper.MINIBOT.DefaultTime
		writeStorage(player, AstraHelper.STORAGES.MiniBotTotalTime, total)
	end

	local timeLeft = readStorage(player, AstraHelper.STORAGES.MiniBotTimeLeft, -1)
	if timeLeft < 0 then
		timeLeft = total
		writeStorage(player, AstraHelper.STORAGES.MiniBotTimeLeft, timeLeft)
	elseif timeLeft > total then
		timeLeft = total
		writeStorage(player, AstraHelper.STORAGES.MiniBotTimeLeft, timeLeft)
	end

	return timeLeft, total
end


function AstraHelper.getMiniBotRenewPrice(player)
	local renewals = readStorage(player, AstraHelper.STORAGES.MiniBotRenewals, 0)
	return math.min(2000000000,
		AstraHelper.MINIBOT.RenewBasePrice + renewals * AstraHelper.MINIBOT.RenewPriceStep)
end

function AstraHelper.syncMiniBotTime(player, timestamp)
	if not player then
		return 0, AstraHelper.MINIBOT.DefaultTime
	end

	local now = timestamp or os.time()
	local timeLeft, total = ensureMiniBotState(player)
	local enabled = player:getStorageValue(AstraHelper.STORAGES.Cavebot) == 1
	local taskMode = player:getStorageValue(AstraHelper.STORAGES.MiniBotTask) == 1
	local startedAt = readStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, 0)
	local bannedUntil = readStorage(player, AstraHelper.STORAGES.MiniBotBannedUntil, 0)

	if enabled and not taskMode then
		if startedAt <= 0 or startedAt > now then
			startedAt = now
		end

		local elapsed = math.max(0, now - startedAt)
		if elapsed > 0 then
			timeLeft = math.max(0, timeLeft - elapsed)
			writeStorage(player, AstraHelper.STORAGES.MiniBotTimeLeft, timeLeft)
		end
		writeStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, now)
	elseif startedAt ~= 0 then
		writeStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, 0)
	end

	if enabled and (timeLeft <= 0 or bannedUntil > now) then
		player:setStorageValue(AstraHelper.STORAGES.Cavebot, 0)
		writeStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, 0)
	end

	return timeLeft, total
end

function AstraHelper.setMiniBotCavebotEnabled(player, enabled)
	if not player then
		return false
	end

	local now = os.time()
	-- Lua is the sole authority for opcode 210. Synchronize while the previous
	-- state is still intact so disabling also accounts for the final interval.
	local timeLeft = AstraHelper.syncMiniBotTime(player, now)
	local bannedUntil = readStorage(player, AstraHelper.STORAGES.MiniBotBannedUntil, 0)
	local botCheckActive = AstraHelper.isMiniBotCheckActive and AstraHelper.isMiniBotCheckActive(player)
	local allowed = enabled and timeLeft > 0 and bannedUntil <= now and not botCheckActive

	player:setStorageValue(AstraHelper.STORAGES.Cavebot, allowed and 1 or 0)
	if allowed and player:getStorageValue(AstraHelper.STORAGES.MiniBotTask) ~= 1 then
		writeStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, now)
	else
		writeStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, 0)
	end
	return allowed
end

function AstraHelper.setMiniBotTaskMode(player, enabled)
	if not player then
		return false
	end

	local now = os.time()
	AstraHelper.syncMiniBotTime(player, now)
	player:setStorageValue(AstraHelper.STORAGES.MiniBotTask, enabled and 1 or 0)
	if player:getStorageValue(AstraHelper.STORAGES.Cavebot) == 1 and not enabled then
		writeStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, now)
	else
		writeStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, 0)
	end
	return true
end

function AstraHelper.isMiniBotTaskMode(player)
	if not player then
		return false
	end

	-- This predicate is intentionally pure: XP/loot hot paths must never write
	-- storages, publish packets, or advance the MiniBot clock.
	if player:getStorageValue(AstraHelper.STORAGES.Cavebot) ~= 1 or
		player:getStorageValue(AstraHelper.STORAGES.MiniBotTask) ~= 1 then
		return false
	end

	local now = os.time()
	return readStorage(player, AstraHelper.STORAGES.MiniBotTimeLeft, 0) > 0 and
		readStorage(player, AstraHelper.STORAGES.MiniBotBannedUntil, 0) <= now
end

function AstraHelper.getMiniBotExperienceMultiplier(player)
	return AstraHelper.isMiniBotTaskMode(player) and AstraHelper.MINIBOT.TaskExperienceMultiplier or 1
end

function AstraHelper.getMiniBotLootMultiplier(player)
	return AstraHelper.isMiniBotTaskMode(player) and AstraHelper.MINIBOT.TaskLootMultiplier or 1
end

function AstraHelper.isMiniBotCheckActive(player)
	if not player then
		return false
	end

	local session = miniBotCheckSessions[player:getId()]
	return session ~= nil and session.playerGuid == player:getGuid()
end

function AstraHelper.refreshMiniBotAfkIndicator(player, timestamp)
	if not player then
		return false
	end

	local now = timestamp or os.time()
	local pauseUntil = readStorage(player, AstraHelper.STORAGES.MiniBotAfkPauseUntil, 0)
	if pauseUntil > now then
		local minutesLeft = math.max(1, math.ceil((pauseUntil - now) / 60))
		player:setIcon(MINIBOT_AFK_ICON_KEY, CreatureIconCategory_Quests, CreatureIconQuests_Dove, minutesLeft)
		return true
	end

	if pauseUntil ~= 0 then
		writeStorage(player, AstraHelper.STORAGES.MiniBotAfkPauseUntil, 0)
	end
	player:removeIcon(MINIBOT_AFK_ICON_KEY)
	return false
end

function AstraHelper.requestMiniBotAfkPause(player)
	if not player then
		return false, "invalid-player"
	end
	if AstraHelper.isMiniBotCheckActive(player) then
		return false, "bot-check-active"
	end

	local now = os.time()
	local availableAt = readStorage(player, AstraHelper.STORAGES.MiniBotAfkAvailableAt, 0)
	if availableAt > now then
		return false, "pause-cooldown"
	end

	writeStorage(player, AstraHelper.STORAGES.MiniBotAfkPauseUntil, now + AstraHelper.MINIBOT.AfkPauseDuration)
	writeStorage(player, AstraHelper.STORAGES.MiniBotAfkAvailableAt, now + AstraHelper.MINIBOT.AfkPauseCooldown)
	AstraHelper.refreshMiniBotAfkIndicator(player, now)
	return true
end

function AstraHelper.isMiniBotAfkPaused(player)
	if not player then
		return false
	end
	return readStorage(player, AstraHelper.STORAGES.MiniBotAfkPauseUntil, 0) > os.time()
end

function AstraHelper.setMiniBotBan(player, bannedUntil)
	if not player then
		return false, "invalid-player"
	end

	local now = os.time()
	bannedUntil = tonumber(bannedUntil)
	if not bannedUntil or bannedUntil ~= math.floor(bannedUntil) or bannedUntil <= now then
		return false, "invalid-expiry"
	end

	AstraHelper.syncMiniBotTime(player, now)
	writeStorage(player, AstraHelper.STORAGES.MiniBotBannedUntil, bannedUntil)
	player:setStorageValue(AstraHelper.STORAGES.Cavebot, 0)
	writeStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, 0)
	if AstraHelper.stopMiniBotCheck then
		AstraHelper.stopMiniBotCheck(player)
	end
	if AstraHelper.sendMiniBotState then
		AstraHelper.sendMiniBotState(player)
	end
	return true
end

function AstraHelper.clearMiniBotBan(player)
	if not player then
		return false, "invalid-player"
	end

	writeStorage(player, AstraHelper.STORAGES.MiniBotBannedUntil, 0)
	if AstraHelper.sendMiniBotState then
		AstraHelper.sendMiniBotState(player)
	end
	return true
end

function AstraHelper.renewMiniBotTime(player)
	if not player then
		return false, "invalid-player"
	end

	local now = os.time()
	local timeLeft, total = AstraHelper.syncMiniBotTime(player, now)
	if total - timeLeft < AstraHelper.MINIBOT.MinimumTimeUsedToRenew then
		return false, "minimum-use"
	end

	local price = AstraHelper.getMiniBotRenewPrice(player)
	if player:getMoney() + player:getBankBalance() < price then
		return false, "not-enough-money"
	end
	if not player:removeTotalMoney(price) then
		return false, "payment-failed"
	end

	-- The original UI explicitly warns that payment is still consumed when less
	-- than a full hour has been used. Only a complete used hour is replenished.
	if total - timeLeft >= AstraHelper.MINIBOT.RenewTime then
		timeLeft = math.min(total, timeLeft + AstraHelper.MINIBOT.RenewTime)
		writeStorage(player, AstraHelper.STORAGES.MiniBotTimeLeft, timeLeft)
	end

	local renewals = readStorage(player, AstraHelper.STORAGES.MiniBotRenewals, 0)
	writeStorage(player, AstraHelper.STORAGES.MiniBotRenewals, renewals + 1)
	if player:getStorageValue(AstraHelper.STORAGES.Cavebot) == 1 and
		player:getStorageValue(AstraHelper.STORAGES.MiniBotTask) ~= 1 then
		writeStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, now)
	end
	return true
end

function AstraHelper.getMiniBotState(player)
	local now = os.time()
	local timeLeft, total = AstraHelper.syncMiniBotTime(player, now)
	local afkPaused = AstraHelper.refreshMiniBotAfkIndicator(player, now)
	return {
		v = AstraHelper.MINIBOT.ProtocolVersion,
		action = "state",
		enabled = player:getStorageValue(AstraHelper.STORAGES.Cavebot) == 1,
		timeLeft = timeLeft,
		total = total,
		task = player:getStorageValue(AstraHelper.STORAGES.MiniBotTask) == 1,
		renewPrice = AstraHelper.getMiniBotRenewPrice(player),
		bannedUntil = readStorage(player, AstraHelper.STORAGES.MiniBotBannedUntil, 0),
		afkPauseUntil = readStorage(player, AstraHelper.STORAGES.MiniBotAfkPauseUntil, 0),
		afkAvailableAt = readStorage(player, AstraHelper.STORAGES.MiniBotAfkAvailableAt, 0),
		afkPaused = afkPaused,
		botCheckActive = AstraHelper.isMiniBotCheckActive(player),
		bankBalance = math.min(AstraHelper.MINIBOT.MaxSafeInteger, math.max(0, player:getBankBalance())),
		inventoryBalance = math.min(AstraHelper.MINIBOT.MaxSafeInteger, math.max(0, player:getMoney())),
	}
end

function AstraHelper.sendMiniBotState(player, errorCode)
	if not player or not player.sendExtendedOpcode then
		return false
	end
	local state = AstraHelper.getMiniBotState(player)
	state.error = errorCode
	return player:sendExtendedOpcode(AstraHelper.OPCODES.MiniBotState, json.encode(state))
end

function AstraHelper.allowMiniBotRequest(player)
	if not player then
		return false
	end
	local playerId = player:getId()
	local now = os.time()
	local window = miniBotRequestWindows[playerId]
	if not window or window.second ~= now then
		miniBotRequestWindows[playerId] = { second = now, count = 1 }
		return true
	end
	window.count = window.count + 1
	return window.count <= AstraHelper.MINIBOT.MaxRequestsPerSecond
end

function AstraHelper.handleMiniBotCavebotOpcode(player, buffer)
	if not player then
		return false
	end
	if not AstraHelper.allowMiniBotRequest(player) then
		-- Drop excess traffic without JSON work, storage writes, or a response;
		-- the periodic ticker will still publish the authoritative state.
		return false
	end
	if type(buffer) ~= "string" or (buffer ~= "0" and buffer ~= "1") then
		AstraHelper.sendMiniBotState(player, "invalid-cavebot-state")
		return false
	end

	local requestedEnabled = buffer == "1"
	local enabled = AstraHelper.setMiniBotCavebotEnabled(player, requestedEnabled)
	local errorCode
	if requestedEnabled and not enabled then
		local now = os.time()
		if AstraHelper.isMiniBotCheckActive(player) then
			errorCode = "bot-check-active"
		elseif readStorage(player, AstraHelper.STORAGES.MiniBotBannedUntil, 0) > now then
			errorCode = "banned"
		else
			errorCode = "time-expired"
		end
	end

	AstraHelper.sendMiniBotState(player, errorCode)
	return errorCode == nil
end

function AstraHelper.handleMiniBotOpcode(player, buffer)
	if type(buffer) ~= "string" or #buffer == 0 or #buffer > AstraHelper.MINIBOT.MaxPayloadBytes then
		return false
	end
	if not AstraHelper.allowMiniBotRequest(player) then
		return false
	end

	local decoded, payload = pcall(json.decode, buffer)
	if not decoded or type(payload) ~= "table" or payload.v ~= AstraHelper.MINIBOT.ProtocolVersion then
		AstraHelper.sendMiniBotState(player, "invalid-payload")
		return false
	end

	local action = payload.action
	local errorCode
	if action == "query" then
		-- State is sent below.
	elseif action == "pause" then
		local paused, reason = AstraHelper.requestMiniBotAfkPause(player)
		if not paused then
			errorCode = reason
		end
	elseif action == "task" and type(payload.enabled) == "boolean" then
		AstraHelper.setMiniBotTaskMode(player, payload.enabled)
	elseif action == "renew" then
		local renewed, reason = AstraHelper.renewMiniBotTime(player)
		if not renewed then
			errorCode = reason
		end
	else
		errorCode = "unknown-action"
	end

	AstraHelper.sendMiniBotState(player, errorCode)
	return errorCode == nil
end

local function sendMiniBotCheckOpcode(player, enabled)
	if not player or not player.sendExtendedOpcode then
		return false
	end
	return player:sendExtendedOpcode(AstraHelper.OPCODES.BotCheckAlert, enabled and "start" or "stop")
end

function AstraHelper.startMiniBotCheck(player, moderator)
	if not player then
		return false, "invalid-player"
	end
	if not player.isUsingAstraClient or not player:isUsingAstraClient() then
		return false, "unsupported-client"
	end
	if AstraHelper.isMiniBotAfkPaused(player) then
		return false, "afk-paused"
	end
	if AstraHelper.isMiniBotCheckActive(player) then
		return false, "already-active"
	end
	if not sendMiniBotCheckOpcode(player, true) then
		return false, "unsupported-client"
	end

	miniBotCheckSessions[player:getId()] = {
		playerGuid = player:getGuid(),
		startedAt = os.time(),
		startedByGuid = moderator and moderator:getGuid() or 0,
		startedByName = moderator and moderator:getName() or "server",
	}
	-- The server, not the client alarm callback, owns this safety transition.
	-- A modified client therefore cannot keep or re-enable Cavebot while the
	-- moderator's check session is active.
	AstraHelper.setMiniBotCavebotEnabled(player, false)
	AstraHelper.sendMiniBotState(player)
	return true
end

function AstraHelper.stopMiniBotCheck(player)
	if not player then
		return false, "invalid-player"
	end

	local wasActive = AstraHelper.isMiniBotCheckActive(player)
	miniBotCheckSessions[player:getId()] = nil
	local sent = sendMiniBotCheckOpcode(player, false)
	AstraHelper.sendMiniBotState(player)
	if not wasActive then
		return false, "not-active"
	end
	if not sent then
		return false, "unsupported-client"
	end
	return true
end

function AstraHelper.getMiniBotCheckSession(player)
	if not AstraHelper.isMiniBotCheckActive(player) then
		return nil
	end

	local session = miniBotCheckSessions[player:getId()]
	return {
		playerGuid = session.playerGuid,
		startedAt = session.startedAt,
		startedByGuid = session.startedByGuid,
		startedByName = session.startedByName,
	}
end

function AstraHelper.onMiniBotLogin(player)
	-- A disconnect/server restart must never consume the player's offline time.
	ensureMiniBotState(player)
	player:setStorageValue(AstraHelper.STORAGES.Cavebot, 0)
	player:setStorageValue(AstraHelper.STORAGES.SmartFollow, 0)
	writeStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, 0)
	miniBotRequestWindows[player:getId()] = nil
	miniBotCheckSessions[player:getId()] = nil
	AstraHelper.sendMiniBotState(player)
end

function AstraHelper.onMiniBotLogout(player)
	AstraHelper.syncMiniBotTime(player)
	player:setStorageValue(AstraHelper.STORAGES.Cavebot, 0)
	writeStorage(player, AstraHelper.STORAGES.MiniBotStartedAt, 0)
	miniBotRequestWindows[player:getId()] = nil
	miniBotCheckSessions[player:getId()] = nil
	player:removeIcon(MINIBOT_AFK_ICON_KEY)
end

function AstraHelper.isCavebotEnabled(player)
	return player and player:getStorageValue(AstraHelper.STORAGES.Cavebot) == 1
end

function AstraHelper.isSmartFollowEnabled(player)
	return player and player:getStorageValue(AstraHelper.STORAGES.SmartFollow) == 1
end

function AstraHelper.isHelperToolEnabled(player)
	return AstraHelper.isCavebotEnabled(player) or AstraHelper.isSmartFollowEnabled(player)
end

function AstraHelper.sendBotCheckAlert(player, enabled)
	if enabled then
		return AstraHelper.startMiniBotCheck(player)
	end
	return AstraHelper.stopMiniBotCheck(player)
end
