local clock = 1700000000
os.time = function()
	return clock
end

CreatureIconCategory_Quests = 0
CreatureIconQuests_Dove = 11
json = {
	encode = function(value)
		return value
	end,
	decode = function()
		error("json.decode is not used by this smoke test")
	end,
}

local function expect(value, message)
	if not value then
		error(message, 2)
	end
end

local function newPlayer(id, guid, name)
	local player = {
		id = id,
		guid = guid,
		name = name,
		storage = {},
		writes = 0,
		packets = {},
		icons = {},
		removedIcons = {},
		money = 0,
		bank = 0,
		payments = {},
		usingAstraClient = true,
	}

	function player:getId()
		return self.id
	end

	function player:getGuid()
		return self.guid
	end

	function player:getName()
		return self.name
	end

	function player:isUsingAstraClient()
		return self.usingAstraClient
	end

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
		return true
	end

	function player:removeIcon(key)
		self.icons[key] = nil
		self.removedIcons[key] = true
		return true
	end

	function player:sendExtendedOpcode(opcode, buffer)
		self.packets[#self.packets + 1] = { opcode = opcode, buffer = buffer }
		return true
	end

	function player:getBankBalance()
		return self.bank
	end

	function player:getMoney()
		return self.money
	end

	function player:removeTotalMoney(amount)
		if self.money + self.bank < amount then
			return false
		end

		local inventoryPaid = math.min(self.money, amount)
		self.money = self.money - inventoryPaid
		self.bank = self.bank - (amount - inventoryPaid)
		self.payments[#self.payments + 1] = amount
		return true
	end

	return player
end

local testSource = debug.getinfo(1, "S").source:gsub("^@", ""):gsub("\\", "/")
local testDirectory = testSource:match("^(.*)/[^/]+$") or "."
local repositoryRoot = testDirectory == "tests" and "." or testDirectory:gsub("/tests$", "")
dofile(repositoryRoot .. "/data/lib/functions/astra_helper.lua")

local S = AstraHelper.STORAGES
local moderator = newPlayer(900, 9000, "Admin")

local unsupportedPlayer = newPlayer(901, 9001, "Other OTC")
unsupportedPlayer.usingAstraClient = false
local unsupportedStarted, unsupportedReason = AstraHelper.startMiniBotCheck(unsupportedPlayer, moderator)
expect(not unsupportedStarted and unsupportedReason == "unsupported-client",
	"Bot-check falsely reported support for a non-Astra OTClient")

-- Task is a read-only predicate on the XP/loot hot path.
local taskPlayer = newPlayer(1, 101, "Task")
taskPlayer.storage[S.Cavebot] = 1
taskPlayer.storage[S.MiniBotTask] = 1
taskPlayer.storage[S.MiniBotTimeLeft] = 100
taskPlayer.storage[S.MiniBotTotalTime] = 100
taskPlayer.storage[S.MiniBotBannedUntil] = 0
taskPlayer.writes = 0
local packetCount = #taskPlayer.packets
expect(AstraHelper.isMiniBotTaskMode(taskPlayer), "Task predicate should be true")
expect(taskPlayer.writes == 0, "Task predicate wrote a storage")
expect(#taskPlayer.packets == packetCount, "Task predicate published a packet")

local richPlayer = newPlayer(7, 107, "Rich")
richPlayer.bank = AstraHelper.MINIBOT.MaxSafeInteger * 2
richPlayer.money = AstraHelper.MINIBOT.MaxSafeInteger * 2
local richState = AstraHelper.getMiniBotState(richPlayer)
expect(richState.bankBalance == AstraHelper.MINIBOT.MaxSafeInteger and
	richState.inventoryBalance == AstraHelper.MINIBOT.MaxSafeInteger,
	"State balances exceeded the client's exact JSON integer range")

-- Lua owns opcode 210, accepts only 0/1, and charges the final disable interval.
local strictPlayer = newPlayer(2, 102, "Strict")
strictPlayer.storage[S.MiniBotTimeLeft] = 100
strictPlayer.storage[S.MiniBotTotalTime] = 100
strictPlayer.storage[S.MiniBotBannedUntil] = 0
expect(AstraHelper.handleMiniBotCavebotOpcode(strictPlayer, "1"), "Canonical enable failed")
expect(strictPlayer.storage[S.Cavebot] == 1, "Canonical enable did not persist")
expect(not AstraHelper.handleMiniBotCavebotOpcode(strictPlayer, "true"), "Non-canonical opcode 210 payload was accepted")
expect(strictPlayer.storage[S.Cavebot] == 1, "Invalid opcode 210 payload mutated authoritative state")
expect(strictPlayer.packets[#strictPlayer.packets].buffer.error == "invalid-cavebot-state",
	"Invalid opcode 210 did not publish the authoritative error state")
clock = clock + 9
expect(AstraHelper.handleMiniBotCavebotOpcode(strictPlayer, "0"), "Canonical disable failed")
expect(strictPlayer.storage[S.Cavebot] == 0, "Canonical disable did not persist")
expect(strictPlayer.storage[S.MiniBotTimeLeft] == 91, "Final disable interval was not consumed")

-- Time expiry and an active ban both force the switch off.
strictPlayer.storage[S.Cavebot] = 1
strictPlayer.storage[S.MiniBotTask] = 0
strictPlayer.storage[S.MiniBotTimeLeft] = 5
strictPlayer.storage[S.MiniBotStartedAt] = clock - 10
AstraHelper.syncMiniBotTime(strictPlayer, clock)
expect(strictPlayer.storage[S.MiniBotTimeLeft] == 0, "Elapsed MiniBot time did not expire")
expect(strictPlayer.storage[S.Cavebot] == 0, "Expired MiniBot remained enabled")

strictPlayer.storage[S.Cavebot] = 1
strictPlayer.storage[S.MiniBotTask] = 1
strictPlayer.storage[S.MiniBotTimeLeft] = 100
strictPlayer.storage[S.MiniBotBannedUntil] = clock + 60
AstraHelper.syncMiniBotTime(strictPlayer, clock)
expect(strictPlayer.storage[S.Cavebot] == 0, "Banned MiniBot remained enabled")
expect(not AstraHelper.isMiniBotTaskMode(strictPlayer), "Ban did not suppress Task mode")

-- Opcode 210 shares the per-player server-side request window.
local limitedPlayer = newPlayer(3, 103, "Limited")
limitedPlayer.storage[S.MiniBotTimeLeft] = 100
limitedPlayer.storage[S.MiniBotTotalTime] = 100
limitedPlayer.storage[S.MiniBotBannedUntil] = 0
for request = 1, AstraHelper.MINIBOT.MaxRequestsPerSecond do
	expect(AstraHelper.handleMiniBotCavebotOpcode(limitedPlayer, "1"),
		"Canonical opcode 210 request was rejected before the limit")
end
local packetsAtLimit = #limitedPlayer.packets
expect(not AstraHelper.handleMiniBotCavebotOpcode(limitedPlayer, "1"),
	"Opcode 210 rate limit did not reject the excess request")
expect(#limitedPlayer.packets == packetsAtLimit,
	"Rate-limited opcode 210 request still performed response work")

-- Bot-check state lives on the server. AFK pause cannot interrupt a running
-- check, suppresses new checks, and has a replicated spectator icon.
local checkedPlayer = newPlayer(4, 104, "Checked")
checkedPlayer.storage[S.MiniBotTimeLeft] = 100
checkedPlayer.storage[S.MiniBotTotalTime] = 100
checkedPlayer.storage[S.MiniBotBannedUntil] = 0
checkedPlayer.storage[S.Cavebot] = 1
checkedPlayer.storage[S.MiniBotStartedAt] = clock
expect(AstraHelper.startMiniBotCheck(checkedPlayer, moderator), "Bot-check did not start")
expect(AstraHelper.isMiniBotCheckActive(checkedPlayer), "Bot-check session was not retained server-side")
expect(checkedPlayer.storage[S.Cavebot] == 0, "Bot-check start did not force Cavebot off")
expect(not AstraHelper.handleMiniBotCavebotOpcode(checkedPlayer, "1"),
	"A modified client re-enabled Cavebot during an active bot-check")
expect(checkedPlayer.storage[S.Cavebot] == 0, "Rejected bot-check enable mutated Cavebot state")
expect(checkedPlayer.packets[#checkedPlayer.packets].buffer.error == "bot-check-active",
	"Rejected bot-check enable did not publish its authoritative reason")
local paused, pauseReason = AstraHelper.requestMiniBotAfkPause(checkedPlayer)
expect(not paused and pauseReason == "bot-check-active", "AFK pause bypassed an active bot-check")
expect(AstraHelper.stopMiniBotCheck(checkedPlayer), "Bot-check did not stop")
expect(not AstraHelper.isMiniBotCheckActive(checkedPlayer), "Stopped bot-check session remained active")
expect(AstraHelper.requestMiniBotAfkPause(checkedPlayer), "AFK pause was not granted")
local afkIcon = checkedPlayer.icons["minibot-afk-pause"]
expect(afkIcon and afkIcon.iconId == CreatureIconQuests_Dove and afkIcon.count == 5,
	"AFK spectator icon was not installed")
local checkStarted, checkReason = AstraHelper.startMiniBotCheck(checkedPlayer, moderator)
expect(not checkStarted and checkReason == "afk-paused", "AFK pause did not suppress a new bot-check")
clock = clock + AstraHelper.MINIBOT.AfkPauseDuration + 1
expect(not AstraHelper.refreshMiniBotAfkIndicator(checkedPlayer, clock), "Expired AFK indicator remained active")
expect(checkedPlayer.icons["minibot-afk-pause"] == nil and checkedPlayer.removedIcons["minibot-afk-pause"],
	"Expired AFK icon was not removed")
expect(checkedPlayer.storage[S.MiniBotAfkPauseUntil] == 0, "Expired AFK storage was not cleared")

-- Ban/clear APIs disable immediately, close a check session, persist, and publish.
local bannedPlayer = newPlayer(5, 105, "Banned")
bannedPlayer.storage[S.Cavebot] = 1
bannedPlayer.storage[S.MiniBotTask] = 1
bannedPlayer.storage[S.MiniBotTimeLeft] = 100
bannedPlayer.storage[S.MiniBotTotalTime] = 100
bannedPlayer.storage[S.MiniBotBannedUntil] = 0
expect(AstraHelper.startMiniBotCheck(bannedPlayer, moderator), "Pre-ban bot-check did not start")
local bannedUntil = clock + 600
expect(AstraHelper.setMiniBotBan(bannedPlayer, bannedUntil), "Ban API failed")
expect(bannedPlayer.storage[S.Cavebot] == 0, "Ban API did not disable MiniBot")
expect(bannedPlayer.storage[S.MiniBotBannedUntil] == bannedUntil, "Ban API did not persist its expiry")
expect(not AstraHelper.isMiniBotCheckActive(bannedPlayer), "Ban API did not close the bot-check session")
expect(AstraHelper.clearMiniBotBan(bannedPlayer), "Clear-ban API failed")
expect(bannedPlayer.storage[S.MiniBotBannedUntil] == 0, "Clear-ban API did not clear storage")

-- Renewal enforces minimum use, charges server-side, restores a complete hour,
-- and increments the price counter.
local renewalPlayer = newPlayer(6, 106, "Renewal")
renewalPlayer.storage[S.Cavebot] = 0
renewalPlayer.storage[S.MiniBotTask] = 0
renewalPlayer.storage[S.MiniBotTotalTime] = AstraHelper.MINIBOT.DefaultTime
renewalPlayer.storage[S.MiniBotTimeLeft] = AstraHelper.MINIBOT.DefaultTime -
	AstraHelper.MINIBOT.MinimumTimeUsedToRenew + 1
renewalPlayer.storage[S.MiniBotRenewals] = 0
renewalPlayer.money = AstraHelper.MINIBOT.RenewBasePrice
local renewed, renewReason = AstraHelper.renewMiniBotTime(renewalPlayer)
expect(not renewed and renewReason == "minimum-use", "Renewal ignored its minimum-use threshold")
expect(#renewalPlayer.payments == 0, "Rejected renewal charged the player")

renewalPlayer.storage[S.MiniBotTimeLeft] = AstraHelper.MINIBOT.DefaultTime - AstraHelper.MINIBOT.RenewTime
renewalPlayer.money = 2000000
renewalPlayer.bank = AstraHelper.MINIBOT.RenewBasePrice - renewalPlayer.money
expect(AstraHelper.renewMiniBotTime(renewalPlayer), "Eligible renewal failed")
expect(renewalPlayer.storage[S.MiniBotTimeLeft] == AstraHelper.MINIBOT.DefaultTime,
	"Renewal did not restore one complete hour")
expect(renewalPlayer.payments[1] == AstraHelper.MINIBOT.RenewBasePrice, "Renewal charged the wrong price")
expect(renewalPlayer.storage[S.MiniBotRenewals] == 1, "Renewal counter was not incremented")
expect(AstraHelper.getMiniBotRenewPrice(renewalPlayer) ==
	AstraHelper.MINIBOT.RenewBasePrice + AstraHelper.MINIBOT.RenewPriceStep,
	"Renewal price did not advance")

print("minibot server state smoke: OK")
