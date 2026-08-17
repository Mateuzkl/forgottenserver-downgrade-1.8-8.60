AstraHelper = AstraHelper or {}

AstraHelper.OPCODES = {
	Cavebot = 210,
	CastOnFoot = 211,
	SmartFollow = 212,
	MiniBotState = 213,
	BotCheckAlert = 230,
}

-- PlayerStorageKeys (data/lib/core/storages.lua) is the single source of truth for
-- the numeric keys and is loaded before this file. The literals below are only the
-- fallback used by the standalone smoke tests, which load this file in isolation.
local STORAGE_KEYS = rawget(_G, "PlayerStorageKeys") or {}

AstraHelper.STORAGES = {
	Cavebot = STORAGE_KEYS.astraHelperCavebot or 99997,
	SmartFollow = STORAGE_KEYS.astraHelperSmartFollow or 99998,
	MehahClient = STORAGE_KEYS.astraHelperMehahClient or 99999,
	MiniBotTimeLeft = STORAGE_KEYS.miniBotTimeLeft or 100020,
	MiniBotTotalTime = STORAGE_KEYS.miniBotTotalTime or 100021,
	MiniBotStartedAt = STORAGE_KEYS.miniBotStartedAt or 100022,
	MiniBotTask = STORAGE_KEYS.miniBotTask or 100023,
	MiniBotRenewals = STORAGE_KEYS.miniBotRenewals or 100024,
	MiniBotBannedUntil = STORAGE_KEYS.miniBotBannedUntil or 100025,
	MiniBotAfkPauseUntil = STORAGE_KEYS.miniBotAfkPauseUntil or 100026,
	MiniBotAfkAvailableAt = STORAGE_KEYS.miniBotAfkAvailableAt or 100027,
}

-- These values mirror the recorder UI delivered with game_minibot. Keeping them
-- together makes the server policy explicit and avoids embedding economy rules in
-- the client. TaskLootMultiplier must equal MINIBOT_TASK_LOOT_MULTIPLIER in
-- src/player.h; tests/minibot_task_rewards_smoke.lua asserts that.
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
	-- The recorder UI renders the remaining time with minute granularity, so a
	-- running clock only has to be republished when that display changes. Nothing
	-- is published while the clock is not running (Task mode, disabled, paused).
	ClockPublishInterval = 60,
	-- Guards a double-clicked or replayed renewal from charging twice.
	RenewCooldown = 3,
}

local S = AstraHelper.STORAGES
local MINIBOT = AstraHelper.MINIBOT
local MINIBOT_AFK_ICON_KEY = "minibot-afk-pause"
local EMPTY_OPTIONS = {}

-- Engine globals resolved once. They are absent when the smoke tests load this
-- file standalone, and every use below is guarded.
local addEventFn = rawget(_G, "addEvent")
local stopEventFn = rawget(_G, "stopEvent")
local playerConstructor = rawget(_G, "Player")

-- Session registry.
--
-- Only players that actually interact with MiniBot get an entry, and an entry
-- never holds Player userdata: the runtime id is the key and the GUID is stored
-- next to it so a recycled id can never inherit another character's session.
-- Every scheduled callback re-resolves the player from (id, guid).
local sessions = {}
local sessionCount = 0

local stats = {
	statePacketsSent = 0,
	statePacketsSkipped = 0,
	storageWrites = 0,
	clockEventsScheduled = 0,
	clockEventsFired = 0,
	rateLimitedRequests = 0,
	sessionsCreated = 0,
	sessionsReleased = 0,
}

local function readStorage(player, key, defaultValue)
	local value = player:getStorageValue(key)
	if type(value) ~= "number" or value < 0 then
		return defaultValue
	end
	return value
end

-- Persists a MiniBot storage only when the stored value really changes.
-- setStorageValue is not free: it dispatches Creature:onUpdateStorage into Lua on
-- every call (src/creature.cpp), so rewriting an identical value is pure waste.
-- An unset key (-1) is equivalent to 0 for every MiniBot storage, so clearing a
-- key that was never written is a no-op as well.
local function writeStorage(player, key, value)
	local normalized = math.max(0, math.floor(tonumber(value) or 0))
	local current = player:getStorageValue(key)
	local currentIsUnset = type(current) ~= "number" or current < 0
	if current == normalized or (normalized == 0 and currentIsUnset) then
		return false
	end
	player:setStorageValue(key, normalized)
	stats.storageWrites = stats.storageWrites + 1
	return true
end

local function releaseSession(playerId)
	local session = sessions[playerId]
	if not session then
		return false
	end
	if session.clockEvent and stopEventFn then
		stopEventFn(session.clockEvent)
	end
	session.clockEvent = nil
	sessions[playerId] = nil
	sessionCount = sessionCount - 1
	stats.sessionsReleased = stats.sessionsReleased + 1
	return true
end

-- Returns the session for this exact character, or nil. Never creates one, so a
-- player who does not use MiniBot keeps costing nothing.
local function findSession(player)
	if not player then
		return nil
	end
	local session = sessions[player:getId()]
	if not session then
		return nil
	end
	if session.guid ~= player:getGuid() then
		-- The runtime id was recycled for another character.
		releaseSession(player:getId())
		return nil
	end
	return session
end

local function acquireSession(player)
	local session = findSession(player)
	if session then
		return session
	end
	session = { guid = player:getGuid() }
	sessions[player:getId()] = session
	sessionCount = sessionCount + 1
	stats.sessionsCreated = stats.sessionsCreated + 1
	return session
end

-- Pure read of the persisted allowance. Returns the effective values without
-- creating any storage, so querying a player who never opened MiniBot leaves no
-- trace on their character.
local function readMiniBotState(player)
	local total = readStorage(player, S.MiniBotTotalTime, 0)
	if total <= 0 then
		total = MINIBOT.DefaultTime
	end

	local stored = player:getStorageValue(S.MiniBotTimeLeft)
	local timeLeft
	if type(stored) ~= "number" or stored < 0 then
		timeLeft = total
	else
		timeLeft = math.min(stored, total)
	end
	return timeLeft, total
end

-- Materialises the allowance on disk. Only called from real MiniBot interactions
-- (first query, enabling the cavebot, renewing, moderation), never from login.
local function ensureMiniBotState(player)
	local timeLeft, total = readMiniBotState(player)
	writeStorage(player, S.MiniBotTotalTime, total)
	writeStorage(player, S.MiniBotTimeLeft, timeLeft)
	return timeLeft, total
end

local function isTaskModeActive(player, now)
	if player:getStorageValue(S.Cavebot) ~= 1 or player:getStorageValue(S.MiniBotTask) ~= 1 then
		return false
	end
	return readMiniBotState(player) > 0 and readStorage(player, S.MiniBotBannedUntil, 0) <= now
end

-- Mirrors the Task predicate into the engine so the XP, loot and reward boss hot
-- paths answer it with a single boolean read instead of four storage lookups.
local function applyRuntimeFlag(player, active)
	if not player.setMiniBotTaskRestricted then
		return
	end
	player:setMiniBotTaskRestricted(active and true or false)
end

local function refreshRuntimeFlag(player, now)
	applyRuntimeFlag(player, isTaskModeActive(player, now or os.time()))
end

function AstraHelper.getMiniBotRenewPrice(player)
	local renewals = readStorage(player, S.MiniBotRenewals, 0)
	return math.min(2000000000, MINIBOT.RenewBasePrice + renewals * MINIBOT.RenewPriceStep)
end

-- Derives the remaining time from (baseTimeLeft, startedAt, now) instead of
-- writing a heartbeat. Storage is only touched when `persist` is requested by a
-- real transition, or when the allowance ran out / a ban kicked in and the
-- cavebot has to be switched off.
function AstraHelper.syncMiniBotTime(player, timestamp, persist)
	if not player then
		return 0, MINIBOT.DefaultTime
	end

	local now = timestamp or os.time()
	local timeLeft, total = readMiniBotState(player)
	local enabled = player:getStorageValue(S.Cavebot) == 1
	local taskMode = player:getStorageValue(S.MiniBotTask) == 1
	local startedAt = readStorage(player, S.MiniBotStartedAt, 0)
	local bannedUntil = readStorage(player, S.MiniBotBannedUntil, 0)
	local running = enabled and not taskMode

	if running then
		if startedAt <= 0 or startedAt > now then
			startedAt = now
		end
		local elapsed = now - startedAt
		if elapsed > 0 then
			timeLeft = math.max(0, timeLeft - elapsed)
		end
	end

	local exhausted = enabled and (timeLeft <= 0 or bannedUntil > now)

	if persist or exhausted then
		writeStorage(player, S.MiniBotTotalTime, total)
		writeStorage(player, S.MiniBotTimeLeft, timeLeft)
		writeStorage(player, S.MiniBotStartedAt, (running and not exhausted) and now or 0)
	end

	if exhausted then
		writeStorage(player, S.Cavebot, 0)
		writeStorage(player, S.MiniBotStartedAt, 0)
		applyRuntimeFlag(player, false)
	end

	return timeLeft, total
end

-- Seconds until the next moment the published state actually changes, or nil when
-- nothing is pending. A Task-mode session never consumes time, so it schedules
-- nothing at all.
local function nextClockDelay(player, now)
	local delay

	if player:getStorageValue(S.Cavebot) == 1 and player:getStorageValue(S.MiniBotTask) ~= 1 then
		local timeLeft = AstraHelper.syncMiniBotTime(player, now)
		if timeLeft > 0 then
			delay = math.min(timeLeft, MINIBOT.ClockPublishInterval)
		end
	end

	local pauseUntil = readStorage(player, S.MiniBotAfkPauseUntil, 0)
	if pauseUntil > now then
		local pauseDelay = math.min(pauseUntil - now, MINIBOT.ClockPublishInterval)
		delay = delay and math.min(delay, pauseDelay) or pauseDelay
	end

	if not delay then
		return nil
	end
	return math.max(1, math.floor(delay))
end

local function cancelClock(session)
	if session and session.clockEvent then
		if stopEventFn then
			stopEventFn(session.clockEvent)
		end
		session.clockEvent = nil
	end
end

-- Schedules the single pending MiniBot event for this session. The callback is a
-- module function receiving (playerId, guid) so no Player reference is captured
-- and no delayed callback can keep a character alive or touch a recycled id.
local function scheduleClock(player, session, now)
	cancelClock(session)
	if not addEventFn then
		return
	end

	local delay = nextClockDelay(player, now or os.time())
	if not delay then
		return
	end

	session.clockEvent = addEventFn(AstraHelper.onMiniBotClockTick, delay * 1000, player:getId(), session.guid)
	stats.clockEventsScheduled = stats.clockEventsScheduled + 1
end

local function sessionHasPendingWork(player, session, now)
	if session.check then
		return true
	end
	if player:getStorageValue(S.Cavebot) == 1 then
		return true
	end
	return readStorage(player, S.MiniBotAfkPauseUntil, 0) > now
end

-- Re-arms the session clock after any transition and drops sessions that have
-- nothing left to do, so the registry stays proportional to the number of players
-- actually using MiniBot.
local function rearmSession(player, now)
	local session = findSession(player)
	if not session then
		return
	end

	now = now or os.time()
	if not sessionHasPendingWork(player, session, now) then
		cancelClock(session)
		return
	end
	scheduleClock(player, session, now)
end

function AstraHelper.isMiniBotCheckActive(player)
	local session = findSession(player)
	return session ~= nil and session.check ~= nil
end

function AstraHelper.setMiniBotCavebotEnabled(player, enabled)
	if not player then
		return false
	end

	local now = os.time()
	-- Lua is the sole authority for opcode 210. Synchronize while the previous
	-- state is still intact so disabling also accounts for the final interval.
	local timeLeft = AstraHelper.syncMiniBotTime(player, now, true)
	local bannedUntil = readStorage(player, S.MiniBotBannedUntil, 0)
	local allowed = enabled and timeLeft > 0 and bannedUntil <= now and not AstraHelper.isMiniBotCheckActive(player)

	if allowed then
		ensureMiniBotState(player)
	end

	writeStorage(player, S.Cavebot, allowed and 1 or 0)
	if allowed and player:getStorageValue(S.MiniBotTask) ~= 1 then
		writeStorage(player, S.MiniBotStartedAt, now)
	else
		writeStorage(player, S.MiniBotStartedAt, 0)
	end

	refreshRuntimeFlag(player, now)
	if allowed then
		acquireSession(player)
	end
	rearmSession(player, now)
	return allowed
end

function AstraHelper.setMiniBotTaskMode(player, enabled)
	if not player then
		return false
	end

	local now = os.time()
	AstraHelper.syncMiniBotTime(player, now, true)
	writeStorage(player, S.MiniBotTask, enabled and 1 or 0)
	if player:getStorageValue(S.Cavebot) == 1 and not enabled then
		writeStorage(player, S.MiniBotStartedAt, now)
	else
		writeStorage(player, S.MiniBotStartedAt, 0)
	end

	refreshRuntimeFlag(player, now)
	rearmSession(player, now)
	return true
end

function AstraHelper.isMiniBotTaskMode(player)
	if not player then
		return false
	end

	-- This predicate is intentionally pure: XP/loot hot paths must never write
	-- storages, publish packets, or advance the MiniBot clock. The engine mirror
	-- makes it a single boolean read; the storage form is the fallback used by the
	-- standalone tests.
	if player.isMiniBotTaskRestricted then
		return player:isMiniBotTaskRestricted() == true
	end
	return isTaskModeActive(player, os.time())
end

function AstraHelper.getMiniBotExperienceMultiplier(player)
	return AstraHelper.isMiniBotTaskMode(player) and MINIBOT.TaskExperienceMultiplier or 1
end

function AstraHelper.getMiniBotLootMultiplier(player)
	return AstraHelper.isMiniBotTaskMode(player) and MINIBOT.TaskLootMultiplier or 1
end

-- Retained for compatibility with callers that used to drive the removed global
-- ticker. It answers whether this character has any live MiniBot session at all.
function AstraHelper.needsMiniBotStateTick(player)
	local session = findSession(player)
	if not session then
		return false
	end
	return sessionHasPendingWork(player, session, os.time())
end

function AstraHelper.refreshMiniBotAfkIndicator(player, timestamp)
	if not player then
		return false
	end

	local now = timestamp or os.time()
	local session = findSession(player)
	local pauseUntil = readStorage(player, S.MiniBotAfkPauseUntil, 0)

	if pauseUntil > now then
		local minutesLeft = math.max(1, math.ceil((pauseUntil - now) / 60))
		if not session or session.afkIconMinutes ~= minutesLeft then
			player:setIcon(MINIBOT_AFK_ICON_KEY, CreatureIconCategory_Quests, CreatureIconQuests_Dove, minutesLeft)
			if session then
				session.afkIconMinutes = minutesLeft
			end
		end
		return true
	end

	if pauseUntil ~= 0 then
		writeStorage(player, S.MiniBotAfkPauseUntil, 0)
	end

	-- Only clear an icon this session actually installed. Without a session the
	-- removal still runs once so a server reload cannot strand the indicator.
	if not session then
		player:removeIcon(MINIBOT_AFK_ICON_KEY)
	elseif session.afkIconMinutes then
		player:removeIcon(MINIBOT_AFK_ICON_KEY)
		session.afkIconMinutes = nil
	end
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
	local availableAt = readStorage(player, S.MiniBotAfkAvailableAt, 0)
	if availableAt > now then
		return false, "pause-cooldown"
	end

	acquireSession(player)
	writeStorage(player, S.MiniBotAfkPauseUntil, now + MINIBOT.AfkPauseDuration)
	writeStorage(player, S.MiniBotAfkAvailableAt, now + MINIBOT.AfkPauseCooldown)
	AstraHelper.refreshMiniBotAfkIndicator(player, now)
	rearmSession(player, now)
	return true
end

function AstraHelper.isMiniBotAfkPaused(player, timestamp)
	if not player then
		return false
	end
	return readStorage(player, S.MiniBotAfkPauseUntil, 0) > (timestamp or os.time())
end

-- Mutates state only. Publication is the caller's responsibility so a single
-- moderator action never emits the same packet twice.
local function stopMiniBotCheckSession(player)
	local session = findSession(player)
	if not session or not session.check then
		return false
	end
	session.check = nil
	return true
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

	AstraHelper.syncMiniBotTime(player, now, true)
	ensureMiniBotState(player)
	writeStorage(player, S.MiniBotBannedUntil, bannedUntil)
	writeStorage(player, S.Cavebot, 0)
	writeStorage(player, S.MiniBotStartedAt, 0)
	applyRuntimeFlag(player, false)

	local hadCheck = stopMiniBotCheckSession(player)
	if hadCheck then
		AstraHelper.sendMiniBotCheckOpcode(player, false)
	end

	rearmSession(player, now)
	AstraHelper.sendMiniBotState(player, nil, { force = true })
	return true
end

function AstraHelper.clearMiniBotBan(player)
	if not player then
		return false, "invalid-player"
	end

	local now = os.time()
	writeStorage(player, S.MiniBotBannedUntil, 0)
	refreshRuntimeFlag(player, now)
	AstraHelper.sendMiniBotState(player, nil, { force = true })
	return true
end

function AstraHelper.renewMiniBotTime(player)
	if not player then
		return false, "invalid-player"
	end

	local now = os.time()
	local session = acquireSession(player)
	-- Economic action: reject a replayed or double-clicked request before any
	-- money is touched. The generic request window is far too permissive here.
	if session.renewAt and now < session.renewAt then
		return false, "renew-cooldown"
	end

	local timeLeft, total = AstraHelper.syncMiniBotTime(player, now, true)
	if total - timeLeft < MINIBOT.MinimumTimeUsedToRenew then
		return false, "minimum-use"
	end

	local price = AstraHelper.getMiniBotRenewPrice(player)
	if player:getMoney() + player:getBankBalance() < price then
		return false, "not-enough-money"
	end
	if not player:removeTotalMoney(price) then
		return false, "payment-failed"
	end
	session.renewAt = now + MINIBOT.RenewCooldown

	-- The original UI explicitly warns that payment is still consumed when less
	-- than a full hour has been used. Only a complete used hour is replenished.
	if total - timeLeft >= MINIBOT.RenewTime then
		timeLeft = math.min(total, timeLeft + MINIBOT.RenewTime)
		writeStorage(player, S.MiniBotTimeLeft, timeLeft)
	end

	local renewals = readStorage(player, S.MiniBotRenewals, 0)
	writeStorage(player, S.MiniBotRenewals, renewals + 1)
	if player:getStorageValue(S.Cavebot) == 1 and player:getStorageValue(S.MiniBotTask) ~= 1 then
		writeStorage(player, S.MiniBotStartedAt, now)
	end

	session.balanceAt = nil
	rearmSession(player, now)
	return true
end

-- Player:getMoney() walks the inventory and every nested container
-- (src/player.cpp), so it is only ever read when the answer is actually needed:
-- the client asked for it, or a payment just changed it. Everything else - the
-- session clock in particular - reuses the value cached on the session, which is
-- what keeps the inventory scan out of every periodic publication.
local function readBalances(player, now, forceRefresh)
	local session = findSession(player)
	if session and not forceRefresh and session.balanceAt then
		return session.bank, session.inventory
	end

	local cap = MINIBOT.MaxSafeInteger
	local bank = math.min(cap, math.max(0, player:getBankBalance()))
	local inventory = math.min(cap, math.max(0, player:getMoney()))
	if session then
		session.bank = bank
		session.inventory = inventory
		session.balanceAt = now
	end
	return bank, inventory
end

function AstraHelper.getMiniBotState(player, options)
	options = options or EMPTY_OPTIONS
	local now = options.timestamp or os.time()
	local timeLeft, total = AstraHelper.syncMiniBotTime(player, now)
	local enabled = player:getStorageValue(S.Cavebot) == 1
	local task = player:getStorageValue(S.MiniBotTask) == 1
	local bank, inventory = readBalances(player, now, options.refreshBalance)

	return {
		v = MINIBOT.ProtocolVersion,
		action = "state",
		enabled = enabled,
		timeLeft = timeLeft,
		total = total,
		task = task,
		renewPrice = AstraHelper.getMiniBotRenewPrice(player),
		bannedUntil = readStorage(player, S.MiniBotBannedUntil, 0),
		afkPauseUntil = readStorage(player, S.MiniBotAfkPauseUntil, 0),
		afkAvailableAt = readStorage(player, S.MiniBotAfkAvailableAt, 0),
		afkPaused = AstraHelper.isMiniBotAfkPaused(player, now),
		botCheckActive = AstraHelper.isMiniBotCheckActive(player),
		bankBalance = bank,
		inventoryBalance = inventory,
		-- Authoritative clock reference. The server still owns expiry; these two
		-- fields let a client count down locally without any server heartbeat.
		serverTime = now,
		running = enabled and not task,
	}
end

local function buildStateSignature(state)
	return table.concat({
		state.enabled and 1 or 0,
		state.task and 1 or 0,
		state.timeLeft,
		state.total,
		state.renewPrice,
		state.bannedUntil,
		state.afkPauseUntil,
		state.afkAvailableAt,
		state.botCheckActive and 1 or 0,
		state.bankBalance,
		state.inventoryBalance,
	}, ":")
end

-- Publishes the authoritative state. An unchanged snapshot is dropped: only a
-- real transition, an explicit answer to a client query, or an error is worth a
-- packet.
function AstraHelper.sendMiniBotState(player, errorCode, options)
	if not player or not player.sendExtendedOpcode then
		return false
	end

	options = options or EMPTY_OPTIONS
	local session = findSession(player)
	local state = AstraHelper.getMiniBotState(player, options)
	local signature = buildStateSignature(state)

	if session and not errorCode and not options.force and session.signature == signature then
		stats.statePacketsSkipped = stats.statePacketsSkipped + 1
		return true
	end

	state.error = errorCode
	if session then
		session.signature = signature
	end
	stats.statePacketsSent = stats.statePacketsSent + 1
	return player:sendExtendedOpcode(AstraHelper.OPCODES.MiniBotState, json.encode(state))
end

-- The single scheduled MiniBot callback. It resolves the player from the runtime
-- id and confirms the GUID before touching anything, so a recycled id or a
-- logged-out character is a no-op.
function AstraHelper.onMiniBotClockTick(playerId, guid)
	stats.clockEventsFired = stats.clockEventsFired + 1

	local session = sessions[playerId]
	if not session or session.guid ~= guid then
		return
	end
	session.clockEvent = nil

	local player = playerConstructor and playerConstructor(playerId)
	if not player or player:getGuid() ~= guid then
		releaseSession(playerId)
		return
	end

	local now = os.time()
	-- Persist on the tick that switches the cavebot off; a running clock keeps
	-- deriving its value from (baseTimeLeft, startedAt, now).
	AstraHelper.syncMiniBotTime(player, now)
	AstraHelper.refreshMiniBotAfkIndicator(player, now)
	AstraHelper.sendMiniBotState(player)

	if sessionHasPendingWork(player, session, now) then
		scheduleClock(player, session, now)
	else
		releaseSession(playerId)
	end
end

function AstraHelper.allowMiniBotRequest(player)
	if not player then
		return false
	end

	local session = acquireSession(player)
	local now = os.time()
	if session.requestSecond ~= now then
		session.requestSecond = now
		session.requestCount = 1
		return true
	end

	session.requestCount = session.requestCount + 1
	if session.requestCount <= MINIBOT.MaxRequestsPerSecond then
		return true
	end
	stats.rateLimitedRequests = stats.rateLimitedRequests + 1
	return false
end

function AstraHelper.handleMiniBotCavebotOpcode(player, buffer)
	if not player then
		return false
	end
	if not AstraHelper.allowMiniBotRequest(player) then
		-- Drop excess traffic without JSON work, storage writes, or a response.
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
		elseif readStorage(player, S.MiniBotBannedUntil, 0) > now then
			errorCode = "banned"
		else
			errorCode = "time-expired"
		end
	end

	AstraHelper.sendMiniBotState(player, errorCode)
	return errorCode == nil
end

function AstraHelper.handleMiniBotOpcode(player, buffer)
	if not player then
		return false
	end
	if type(buffer) ~= "string" or #buffer == 0 or #buffer > MINIBOT.MaxPayloadBytes then
		return false
	end
	if not AstraHelper.allowMiniBotRequest(player) then
		return false
	end

	local decoded, payload = pcall(json.decode, buffer)
	if not decoded or type(payload) ~= "table" or payload.v ~= MINIBOT.ProtocolVersion then
		AstraHelper.sendMiniBotState(player, "invalid-payload", { force = true })
		return false
	end

	local action = payload.action
	local errorCode
	local publishOptions

	if action == "query" then
		-- First contact materialises the allowance and re-arms the session, which
		-- also restores the AFK indicator and the clock after a server restart.
		-- The answer is always sent, even when the snapshot did not change.
		local now = os.time()
		ensureMiniBotState(player)
		AstraHelper.refreshMiniBotAfkIndicator(player, now)
		refreshRuntimeFlag(player, now)
		rearmSession(player, now)
		publishOptions = { force = true, refreshBalance = true }
	elseif action == "pause" then
		local paused, reason = AstraHelper.requestMiniBotAfkPause(player)
		if not paused then
			errorCode = reason
		end
	elseif action == "task" and type(payload.enabled) == "boolean" then
		ensureMiniBotState(player)
		AstraHelper.setMiniBotTaskMode(player, payload.enabled)
	elseif action == "renew" then
		local renewed, reason = AstraHelper.renewMiniBotTime(player)
		if not renewed then
			errorCode = reason
		end
		publishOptions = { force = true, refreshBalance = true }
	else
		errorCode = "unknown-action"
	end

	AstraHelper.sendMiniBotState(player, errorCode, publishOptions)
	return errorCode == nil
end

function AstraHelper.sendMiniBotCheckOpcode(player, enabled)
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
	if not AstraHelper.sendMiniBotCheckOpcode(player, true) then
		return false, "unsupported-client"
	end

	local session = acquireSession(player)
	session.check = {
		startedAt = os.time(),
		startedByGuid = moderator and moderator:getGuid() or 0,
		startedByName = moderator and moderator:getName() or "server",
	}
	-- The server, not the client alarm callback, owns this safety transition.
	-- A modified client therefore cannot keep or re-enable Cavebot while the
	-- moderator's check session is active.
	AstraHelper.setMiniBotCavebotEnabled(player, false)
	AstraHelper.sendMiniBotState(player, nil, { force = true })
	return true
end

function AstraHelper.stopMiniBotCheck(player)
	if not player then
		return false, "invalid-player"
	end

	local wasActive = stopMiniBotCheckSession(player)
	local sent = AstraHelper.sendMiniBotCheckOpcode(player, false)
	AstraHelper.sendMiniBotState(player, nil, { force = wasActive })
	if wasActive then
		rearmSession(player, os.time())
	end
	if not wasActive then
		return false, "not-active"
	end
	if not sent then
		return false, "unsupported-client"
	end
	return true
end

function AstraHelper.getMiniBotCheckSession(player)
	local session = findSession(player)
	if not session or not session.check then
		return nil
	end

	local check = session.check
	return {
		playerGuid = session.guid,
		startedAt = check.startedAt,
		startedByGuid = check.startedByGuid,
		startedByName = check.startedByName,
	}
end

function AstraHelper.onMiniBotLogin(player)
	if not player then
		return
	end

	-- A runtime id is reused by the engine, so anything left under it belongs to a
	-- previous character and must go before this one touches MiniBot.
	releaseSession(player:getId())

	-- Lazy by design: no MiniBot storage is created here and no packet is sent.
	-- The client asks for its state with an opcode 213 query when the module
	-- opens, so a player who never uses MiniBot pays nothing for it. The three
	-- writes below are no-ops unless a crash left a runtime flag behind, and they
	-- guarantee that a disconnect never consumes offline time.
	writeStorage(player, S.Cavebot, 0)
	writeStorage(player, S.SmartFollow, 0)
	writeStorage(player, S.MiniBotStartedAt, 0)
end

function AstraHelper.onMiniBotLogout(player)
	if not player then
		return
	end

	local playerId = player:getId()
	local session = findSession(player)
	if not session then
		-- Never touched MiniBot this session: nothing to persist or clean up.
		releaseSession(playerId)
		return
	end

	AstraHelper.syncMiniBotTime(player, os.time(), true)
	writeStorage(player, S.Cavebot, 0)
	writeStorage(player, S.MiniBotStartedAt, 0)
	applyRuntimeFlag(player, false)
	if session.afkIconMinutes then
		player:removeIcon(MINIBOT_AFK_ICON_KEY)
	end
	releaseSession(playerId)
end

function AstraHelper.isCavebotEnabled(player)
	return player and player:getStorageValue(S.Cavebot) == 1
end

function AstraHelper.isSmartFollowEnabled(player)
	return player and player:getStorageValue(S.SmartFollow) == 1
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

-- Observability for the leak/performance regression tests and for /minibotadmin.
function AstraHelper.getMiniBotDebugStats()
	local checkSessions = 0
	local scheduledEvents = 0
	for _, session in pairs(sessions) do
		if session.check then
			checkSessions = checkSessions + 1
		end
		if session.clockEvent then
			scheduledEvents = scheduledEvents + 1
		end
	end

	return {
		activeSessions = sessionCount,
		checkSessions = checkSessions,
		scheduledExpiryEvents = scheduledEvents,
		statePacketsSent = stats.statePacketsSent,
		statePacketsSkipped = stats.statePacketsSkipped,
		storageWrites = stats.storageWrites,
		clockEventsScheduled = stats.clockEventsScheduled,
		clockEventsFired = stats.clockEventsFired,
		rateLimitedRequests = stats.rateLimitedRequests,
		sessionsCreated = stats.sessionsCreated,
		sessionsReleased = stats.sessionsReleased,
	}
end

function AstraHelper.resetMiniBotDebugStats()
	for key in pairs(stats) do
		stats[key] = 0
	end
end
