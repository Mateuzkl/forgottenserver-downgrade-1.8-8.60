-- Lifecycle, leak and scalability coverage for the MiniBot server service.
--
-- The point of this file is to prove, without a live server, that the cost of the
-- MiniBot feature is proportional to the players actually using it and that every
-- registry it keeps returns to its baseline. Run from the repository root with
-- Lua 5.1+ or LuaJIT:  lua tests/minibot_lifecycle_smoke.lua

local root = (arg and arg[1]) or "."

local clock = 1700000000
local realTime = os.time
os.time = function()
	return clock
end

CreatureIconCategory_Quests = 0
CreatureIconQuests_Dove = 11

json = {
	encode = function(value)
		return value
	end,
	decode = function(value)
		if type(value) ~= "string" then
			error("json.decode expects a string")
		end
		local decoder = load("return " .. value)
		if not decoder then
			error("invalid payload")
		end
		return decoder()
	end,
}

local function expect(value, message)
	if not value then
		error(message, 2)
	end
end

--------------------------------------------------------------------------------
-- Scheduler mock. addEvent/stopEvent must exist as globals before the helper is
-- loaded, because AstraHelper resolves them once at load time.
--------------------------------------------------------------------------------

local scheduler = {
	events = {},
	nextId = 0,
	pending = 0,
	fired = 0,
	scheduled = 0,
	cancelled = 0,
}

function addEvent(callback, delay, ...)
	expect(type(callback) == "function", "addEvent received a non function callback")
	for index = 1, select("#", ...) do
		local argument = select(index, ...)
		expect(type(argument) == "number" or type(argument) == "string" or type(argument) == "boolean",
			"a scheduled MiniBot callback received a non scalar argument (userdata must never be captured)")
	end

	scheduler.nextId = scheduler.nextId + 1
	local id = scheduler.nextId
	scheduler.events[id] = {
		at = clock + (delay / 1000),
		callback = callback,
		args = table.pack(...),
	}
	scheduler.pending = scheduler.pending + 1
	scheduler.scheduled = scheduler.scheduled + 1
	return id
end

function stopEvent(id)
	if id and scheduler.events[id] then
		scheduler.events[id] = nil
		scheduler.pending = scheduler.pending - 1
		scheduler.cancelled = scheduler.cancelled + 1
	end
end

local function runDueEvents()
	local ran = true
	while ran do
		ran = false
		local dueId, due
		for id, event in pairs(scheduler.events) do
			if event.at <= clock and (not due or event.at < due.at or (event.at == due.at and id < dueId)) then
				dueId, due = id, event
			end
		end
		if due then
			scheduler.events[dueId] = nil
			scheduler.pending = scheduler.pending - 1
			scheduler.fired = scheduler.fired + 1
			due.callback(table.unpack(due.args, 1, due.args.n))
			ran = true
		end
	end
end

local function advance(seconds, step)
	step = step or 1
	local target = clock + seconds
	while clock < target do
		clock = math.min(target, clock + step)
		runDueEvents()
	end
end

--------------------------------------------------------------------------------
-- Player mock and the online registry backing the global Player(id) constructor.
--------------------------------------------------------------------------------

local online = {}

local function newPlayer(id, guid, name)
	local player = {
		id = id,
		guid = guid,
		name = name or ("Player" .. id),
		storage = {},
		writes = 0,
		packets = 0,
		lastPacket = nil,
		iconSets = 0,
		iconRemovals = 0,
		icons = {},
		moneyReads = 0,
		bankReads = 0,
		money = 10000000000,
		bank = 10000000000,
		taskRestricted = false,
		usingAstraClient = true,
	}

	function player:getId() return self.id end
	function player:getGuid() return self.guid end
	function player:getName() return self.name end
	function player:isUsingAstraClient() return self.usingAstraClient end

	function player:getStorageValue(key)
		local value = self.storage[key]
		return value == nil and -1 or value
	end

	function player:setStorageValue(key, value)
		self.storage[key] = value
		self.writes = self.writes + 1
	end

	function player:setIcon(key, category, iconId, count)
		self.icons[key] = { category = category, iconId = iconId, count = count }
		self.iconSets = self.iconSets + 1
		return true
	end

	function player:removeIcon(key)
		self.icons[key] = nil
		self.iconRemovals = self.iconRemovals + 1
		return true
	end

	function player:sendExtendedOpcode(opcode, buffer)
		self.packets = self.packets + 1
		self.lastPacket = { opcode = opcode, buffer = buffer }
		return true
	end

	function player:getMoney()
		self.moneyReads = self.moneyReads + 1
		return self.money
	end

	function player:getBankBalance()
		self.bankReads = self.bankReads + 1
		return self.bank
	end

	function player:removeTotalMoney(amount)
		if self.money + self.bank < amount then
			return false
		end
		local paid = math.min(self.money, amount)
		self.money = self.money - paid
		self.bank = self.bank - (amount - paid)
		return true
	end

	function player:isMiniBotTaskRestricted() return self.taskRestricted end
	function player:setMiniBotTaskRestricted(value) self.taskRestricted = value and true or false end

	return player
end

function Player(id)
	return online[id]
end

--------------------------------------------------------------------------------

dofile(root .. "/data/lib/functions/astra_helper.lua")

local S = AstraHelper.STORAGES
local MINIBOT = AstraHelper.MINIBOT

local function login(player)
	online[player.id] = player
	AstraHelper.onMiniBotLogin(player)
end

local function logout(player)
	AstraHelper.onMiniBotLogout(player)
	online[player.id] = nil
end

local function stats()
	return AstraHelper.getMiniBotDebugStats()
end

local function encodeRequest(payload)
	local parts = {}
	for key, value in pairs(payload) do
		if type(value) == "string" then
			parts[#parts + 1] = string.format("%s=%q", key, value)
		else
			parts[#parts + 1] = string.format("%s=%s", key, tostring(value))
		end
	end
	return "{" .. table.concat(parts, ",") .. "}"
end

--------------------------------------------------------------------------------
-- TEST A - a server full of players that never touch MiniBot pays nothing.
--------------------------------------------------------------------------------

AstraHelper.resetMiniBotDebugStats()
local idle = {}
for index = 1, 1000 do
	idle[index] = newPlayer(0x10000000 + index, 100000 + index)
	login(idle[index])
end

advance(600, 30)

local idleStats = stats()
expect(idleStats.activeSessions == 0, "idle players created MiniBot sessions")
expect(idleStats.scheduledExpiryEvents == 0, "idle players scheduled MiniBot events")
expect(idleStats.statePacketsSent == 0, "idle players received MiniBot state packets")
expect(idleStats.storageWrites == 0, "idle players had MiniBot storages written")
expect(scheduler.pending == 0, "idle players left events queued")

for index = 1, 1000 do
	local player = idle[index]
	expect(player.packets == 0, "an idle player received a MiniBot packet")
	expect(player.writes == 0, "an idle player had a storage written on login")
	expect(player.moneyReads == 0, "an idle player had getMoney() called")
	expect(next(player.storage) == nil, "an idle player was given MiniBot storages")
end

for index = 1, 1000 do
	logout(idle[index])
end
expect(stats().activeSessions == 0, "logging out idle players created sessions")

--------------------------------------------------------------------------------
-- TEST B - 1000 players online, 10 running MiniBot: work tracks the 10.
--------------------------------------------------------------------------------

AstraHelper.resetMiniBotDebugStats()
scheduler.fired, scheduler.scheduled, scheduler.cancelled = 0, 0, 0

local crowd = {}
for index = 1, 990 do
	crowd[index] = newPlayer(0x11000000 + index, 200000 + index)
	login(crowd[index])
end

local botters = {}
for index = 1, 10 do
	local player = newPlayer(0x12000000 + index, 300000 + index)
	login(player)
	botters[index] = player
	expect(AstraHelper.handleMiniBotOpcode(player, encodeRequest({v = 1, action = "query"})),
		"MiniBot query was rejected")
	expect(AstraHelper.handleMiniBotCavebotOpcode(player, "1"), "cavebot enable was rejected")
end

expect(stats().activeSessions == 10, "session count does not match the players using MiniBot")

local packetsBefore = stats().statePacketsSent
local window = 1800
advance(window, 5)

local activeStats = stats()
local maxTicks = 10 * (window / MINIBOT.ClockPublishInterval) + 10
expect(scheduler.fired <= maxTicks,
	"periodic MiniBot work grew beyond the players using it: " .. scheduler.fired .. " > " .. maxTicks)
expect(activeStats.statePacketsSent - packetsBefore <= maxTicks,
	"published more MiniBot states than there were clock transitions")
expect(activeStats.activeSessions == 10, "session count drifted while the clock was running")
expect(scheduler.pending == 10, "each running MiniBot session must hold exactly one pending event")

for index = 1, 990 do
	expect(crowd[index].packets == 0, "a player not using MiniBot received a state packet")
	expect(crowd[index].writes == 0, "a player not using MiniBot had a storage written")
	expect(crowd[index].moneyReads == 0, "a player not using MiniBot had getMoney() called")
end

-- getMoney() must never be driven by the clock: one read per query, cached for the
-- rest, and never once per tick.
for index = 1, 10 do
	expect(botters[index].moneyReads <= 2,
		"the MiniBot clock scanned the inventory for money: " .. botters[index].moneyReads .. " reads")
end

-- Task mode does not consume the allowance, so it must not schedule anything.
local taskPlayer = botters[1]
expect(AstraHelper.handleMiniBotOpcode(taskPlayer, encodeRequest({v = 1, action = "task", enabled = true})),
	"enabling Task mode failed")
expect(taskPlayer.taskRestricted == true, "Task mode did not reach the engine hot path flag")
expect(AstraHelper.needsMiniBotStateTick(taskPlayer), "a Task session must stay registered")
local pendingWithTask = scheduler.pending
advance(600, 30)
expect(scheduler.pending == pendingWithTask, "Task mode scheduled clock work despite not consuming time")
expect(taskPlayer.packets > 0, "the Task player never received any state")

expect(AstraHelper.handleMiniBotOpcode(taskPlayer, encodeRequest({v = 1, action = "task", enabled = false})),
	"disabling Task mode failed")
expect(taskPlayer.taskRestricted == false, "leaving Task mode did not clear the engine hot path flag")

for index = 1, 10 do
	logout(botters[index])
end
for index = 1, 990 do
	logout(crowd[index])
end
expect(stats().activeSessions == 0, "sessions survived logout")
expect(scheduler.pending == 0, "scheduled MiniBot events survived logout")

--------------------------------------------------------------------------------
-- TEST C - 10000 login/logout cycles return every registry to its baseline.
--------------------------------------------------------------------------------

AstraHelper.resetMiniBotDebugStats()
for cycle = 1, 10000 do
	-- The engine recycles runtime ids, so half of the cycles reuse one.
	local runtimeId = 0x13000000 + (cycle % 64)
	local player = newPlayer(runtimeId, 400000 + cycle)
	login(player)
	if cycle % 2 == 0 then
		AstraHelper.handleMiniBotOpcode(player, encodeRequest({v = 1, action = "query"}))
		AstraHelper.handleMiniBotCavebotOpcode(player, "1")
	end
	if cycle % 5 == 0 then
		AstraHelper.requestMiniBotAfkPause(player)
	end
	logout(player)
end

local cycledStats = stats()
expect(cycledStats.activeSessions == 0, "sessions leaked over 10000 login/logout cycles")
expect(cycledStats.checkSessions == 0, "bot-check sessions leaked over 10000 login/logout cycles")
expect(cycledStats.scheduledExpiryEvents == 0, "scheduled events leaked over 10000 login/logout cycles")
expect(scheduler.pending == 0, "the scheduler queue leaked over 10000 login/logout cycles")
expect(cycledStats.sessionsCreated == cycledStats.sessionsReleased,
	"every created MiniBot session must be released")

-- A recycled runtime id must never inherit the previous character's session.
local firstOwner = newPlayer(0x14000000, 500001)
login(firstOwner)
AstraHelper.handleMiniBotOpcode(firstOwner, encodeRequest({v = 1, action = "query"}))
AstraHelper.handleMiniBotCavebotOpcode(firstOwner, "1")
expect(stats().activeSessions == 1, "the first owner did not register a session")
online[firstOwner.id] = nil -- hard disconnect: the logout event never ran

local secondOwner = newPlayer(0x14000000, 500002)
login(secondOwner)
expect(stats().activeSessions == 0, "a recycled runtime id inherited the previous character's session")
expect(not AstraHelper.isMiniBotCheckActive(secondOwner), "a recycled runtime id inherited a bot-check")
logout(secondOwner)
expect(scheduler.pending == 0, "a recycled runtime id left an orphan event")

--------------------------------------------------------------------------------
-- TEST D - opcode 210/213 spam is bounded in both traffic and memory.
--------------------------------------------------------------------------------

AstraHelper.resetMiniBotDebugStats()
local spammer = newPlayer(0x15000000, 600001)
login(spammer)
AstraHelper.handleMiniBotOpcode(spammer, encodeRequest({v = 1, action = "query"}))

local accepted = 0
for request = 1, 10000 do
	if AstraHelper.handleMiniBotCavebotOpcode(spammer, request % 2 == 0 and "1" or "0") then
		accepted = accepted + 1
	end
	if AstraHelper.handleMiniBotOpcode(spammer, encodeRequest({v = 1, action = "query"})) then
		accepted = accepted + 1
	end
end

expect(accepted <= MINIBOT.MaxRequestsPerSecond,
	"the MiniBot request window did not bound a 20000 request burst: " .. accepted)
expect(stats().activeSessions == 1, "opcode spam created more than one session")
expect(stats().rateLimitedRequests > 0, "the rate limiter never reported a rejection")

-- Oversized and malformed payloads are dropped before any decoding work.
expect(not AstraHelper.handleMiniBotOpcode(spammer, string.rep("x", MINIBOT.MaxPayloadBytes + 1)),
	"an oversized opcode 213 payload was accepted")
expect(not AstraHelper.handleMiniBotOpcode(spammer, ""), "an empty opcode 213 payload was accepted")

clock = clock + 1
local renewed, renewReason = AstraHelper.renewMiniBotTime(spammer)
if renewed then
	local replayed, replayReason = AstraHelper.renewMiniBotTime(spammer)
	expect(not replayed and replayReason == "renew-cooldown",
		"a replayed renewal was not rejected: " .. tostring(replayReason))
else
	expect(renewReason == "minimum-use", "unexpected renewal rejection: " .. tostring(renewReason))
end

logout(spammer)
expect(stats().activeSessions == 0, "the spammer's session survived logout")
expect(scheduler.pending == 0, "the spammer left a scheduled event behind")

--------------------------------------------------------------------------------
-- TEST E - AFK pause never duplicates icon traffic and never orphans an event.
--------------------------------------------------------------------------------

AstraHelper.resetMiniBotDebugStats()
local afkPlayer = newPlayer(0x16000000, 700001)
login(afkPlayer)
AstraHelper.handleMiniBotOpcode(afkPlayer, encodeRequest({v = 1, action = "query"}))
expect(AstraHelper.handleMiniBotOpcode(afkPlayer, encodeRequest({v = 1, action = "pause"})),
	"the AFK pause request was rejected")
expect(afkPlayer.iconSets == 1, "the AFK pause installed more than one icon")

for _ = 1, 20 do
	AstraHelper.refreshMiniBotAfkIndicator(afkPlayer, clock)
end
expect(afkPlayer.iconSets == 1, "refreshing an unchanged AFK indicator re-sent the icon")
expect(afkPlayer.iconRemovals == 0, "refreshing an active AFK pause removed the icon")

-- The pause is denied while on cooldown and must not touch the icon at all.
local iconSetsBeforeDenied = afkPlayer.iconSets
local pausedAgain, pausedReason = AstraHelper.requestMiniBotAfkPause(afkPlayer)
expect(not pausedAgain and pausedReason == "pause-cooldown", "the AFK pause cooldown was not enforced")
expect(afkPlayer.iconSets == iconSetsBeforeDenied, "a denied AFK pause still touched the icon")

-- One icon update per displayed minute while the pause runs, then a single removal.
advance(MINIBOT.AfkPauseDuration + 5, 5)
expect(afkPlayer.icons["minibot-afk-pause"] == nil, "the AFK icon outlived the pause")
expect(afkPlayer.iconRemovals == 1, "the expired AFK icon was removed more than once")
expect(afkPlayer.iconSets <= math.ceil(MINIBOT.AfkPauseDuration / 60) + 1,
	"the AFK indicator was updated more often than the minute it displays")
expect(afkPlayer.storage[S.MiniBotAfkPauseUntil] == 0, "the expired AFK pause storage was not cleared")
expect(scheduler.pending == 0, "the expired AFK pause left an orphan event")
expect(stats().activeSessions == 0, "an idle session survived its last scheduled tick")

logout(afkPlayer)

--------------------------------------------------------------------------------
-- TEST F - bot-check start/stop/start/ban/logout leaves nothing alive.
--------------------------------------------------------------------------------

AstraHelper.resetMiniBotDebugStats()
local moderator = newPlayer(0x17000000, 800001, "God")
local suspect = newPlayer(0x17000001, 800002, "Suspect")
login(moderator)
login(suspect)
AstraHelper.handleMiniBotOpcode(suspect, encodeRequest({v = 1, action = "query"}))
AstraHelper.handleMiniBotCavebotOpcode(suspect, "1")

expect(AstraHelper.startMiniBotCheck(suspect, moderator), "the bot-check did not start")
expect(AstraHelper.isMiniBotCheckActive(suspect), "the bot-check session was not registered")
expect(stats().checkSessions == 1, "the bot-check was not counted")
expect(suspect.storage[S.Cavebot] == 0, "the bot-check did not force the cavebot off")

expect(AstraHelper.stopMiniBotCheck(suspect), "the bot-check did not stop")
expect(stats().checkSessions == 0, "the stopped bot-check stayed registered")

expect(AstraHelper.startMiniBotCheck(suspect, moderator), "the second bot-check did not start")
expect(stats().checkSessions == 1, "the second bot-check was not counted")

-- A ban closes the check and publishes exactly one state packet.
local packetsBeforeBan = suspect.packets
expect(AstraHelper.setMiniBotBan(suspect, clock + 600), "the MiniBot ban failed")
expect(suspect.packets - packetsBeforeBan == 2,
	"the ban published " .. (suspect.packets - packetsBeforeBan) .. " packets instead of one alert plus one state")
expect(stats().checkSessions == 0, "the ban did not close the bot-check session")
expect(suspect.taskRestricted == false, "the ban did not clear the engine hot path flag")

logout(suspect)
logout(moderator)
expect(stats().activeSessions == 0, "a bot-check session survived logout")
expect(stats().checkSessions == 0, "a bot-check survived logout")
expect(scheduler.pending == 0, "the bot-check left a scheduled event behind")

--------------------------------------------------------------------------------
-- TEST G - the reward boss combat path never reaches for storages.
--------------------------------------------------------------------------------

local function readSource(relativePath)
	local file = assert(io.open(root .. "/" .. relativePath, "rb"))
	local contents = assert(file:read("*a"))
	file:close()
	return contents
end

local gameSource = readSource("src/game.cpp")
local rewardHelper = gameSource:match("void recordMiniBotRewardMode.-\n}")
	or gameSource:match("inline void recordMiniBotRewardMode.-\n}")
expect(rewardHelper, "recordMiniBotRewardMode is no longer present in src/game.cpp")
expect(not rewardHelper:find("getStorageValue", 1, true),
	"the reward boss combat path reads storages on every hit")
expect(not rewardHelper:find("time(nullptr)", 1, true),
	"the reward boss combat path calls time() on every hit")
expect(rewardHelper:find("miniBotTaskRestricted", 1, true),
	"the reward boss combat path no longer latches the restricted flag")
expect(rewardHelper:find("!score.miniBotTaskRestricted", 1, true),
	"the reward boss combat path keeps working after the restricted flag is latched")
expect(not gameSource:find("STORAGE_ASTRA_HELPER", 1, true),
	"src/game.cpp still duplicates the MiniBot storage keys")

local gameHeader = readSource("src/game.h")
expect(gameHeader:find("bool miniBotTaskRestricted", 1, true),
	"PlayerScoreInfo should carry a binary MiniBot flag, not a floating multiplier")

-- The Task policy has exactly one value on each side and they have to agree.
local playerHeader = readSource("src/player.h")
local cppMultiplier = tonumber(playerHeader:match("MINIBOT_TASK_LOOT_MULTIPLIER%s*=%s*([%d%.]+)"))
expect(cppMultiplier, "MINIBOT_TASK_LOOT_MULTIPLIER is missing from src/player.h")
expect(math.abs(cppMultiplier - MINIBOT.TaskLootMultiplier) < 1e-9,
	"the C++ and Lua Task loot multipliers disagree")

-- The storage keys live in data/lib/core/storages.lua and nowhere else.
local storagesSource = readSource("data/lib/core/storages.lua")
for name, key in pairs({
	astraHelperCavebot = S.Cavebot,
	astraHelperSmartFollow = S.SmartFollow,
	astraHelperMehahClient = S.MehahClient,
	miniBotTimeLeft = S.MiniBotTimeLeft,
	miniBotTotalTime = S.MiniBotTotalTime,
	miniBotStartedAt = S.MiniBotStartedAt,
	miniBotTask = S.MiniBotTask,
	miniBotRenewals = S.MiniBotRenewals,
	miniBotBannedUntil = S.MiniBotBannedUntil,
	miniBotAfkPauseUntil = S.MiniBotAfkPauseUntil,
	miniBotAfkAvailableAt = S.MiniBotAfkAvailableAt,
}) do
	local declared = tonumber(storagesSource:match(name .. "%s*=%s*(%d+)"))
	expect(declared == key, "PlayerStorageKeys." .. name .. " disagrees with AstraHelper.STORAGES")
end

-- No global player scan may come back for the MiniBot clock. Comments are
-- stripped so documenting the removal does not trip the guard.
local function strippedCode(relativePath)
	local code = {}
	for line in (readSource(relativePath) .. "\n"):gmatch("(.-)\r?\n") do
		code[#code + 1] = line:gsub("%-%-.*$", "")
	end
	return table.concat(code, "\n")
end

local opcodeCode = strippedCode("data/scripts/creaturescripts/others/extendedopcode.lua")
expect(not opcodeCode:find("Game.getPlayers", 1, true),
	"the MiniBot extended opcode script scans every online player again")
expect(not opcodeCode:find("GlobalEvent", 1, true),
	"a MiniBot global ticker was reintroduced")

local helperCode = strippedCode("data/lib/functions/astra_helper.lua")
expect(not helperCode:find("Game.getPlayers", 1, true), "AstraHelper scans every online player")

os.time = realTime
print("minibot lifecycle smoke: OK")
