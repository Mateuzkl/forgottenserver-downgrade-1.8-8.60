-- Run with: lua .github/tests/market_save_regressions.lua [repository root]
local root = arg[1] or "."
local function read(path)
    local file = assert(io.open(root .. "/" .. path, "rb"))
    local source = file:read("*a")
    file:close()
    return source
end

-- Execute the real schedule with minimal API stubs. CLOSED and SHUTDOWN own
-- their C++ save/hook call; an additional Lua saveServer would be a duplicate.
for _, mode in ipairs({"open", "close", "shutdown"}) do
    local saves, hooks, scheduled, clean, reopened = 0, 0, nil, 0, false
    local environment = setmetatable({}, {__index = _G})
    environment.configKeys = setmetatable({}, {__index = function(_, key) return key end})
    environment.configManager = {
        getBoolean = function(key)
            return (key == "SERVER_SAVE_CLOSE" and mode == "close") or
                (key == "SERVER_SAVE_SHUTDOWN" and mode == "shutdown") or key == "SERVER_SAVE_CLEAN_MAP"
        end,
        getNumber = function() return 0 end
    }
    environment.GAME_STATE_CLOSED, environment.GAME_STATE_SHUTDOWN, environment.GAME_STATE_NORMAL = 1, 2, 3
    environment.Game = {setGameState = function(state)
        if state == 3 then reopened = true
        else saves = saves + 1; hooks = hooks + 1 end
    end}
    environment.saveServer = function(reason)
        assert(reason == "periodic")
        saves = saves + 1; hooks = hooks + 1
    end
    environment.cleanMap = function() clean = clean + 1 end
    environment.addEvent = function(callback) scheduled = callback end
    local event = {time = function() end, register = function() end}
    environment.GlobalEvent = function() return event end
    assert(load(read("data/scripts/globalevents/serversave.lua"), "serversave", "t", environment))()
    event.onTime(0)
    assert(scheduled, "periodic save was not scheduled")
    scheduled()
    assert(saves == 1 and hooks == 1, mode .. ": duplicate save or missing hooks")
    assert(reopened == (mode == "close"))
    assert(clean == (mode == "shutdown" and 0 or 1))
end

-- Execute the real delivery event: syntax alone would miss a removed register().
do
    local registered, interval, calls = false, nil, 0
    local event = {
        interval = function(_, value) interval = value end,
        register = function() registered = true end
    }
    local environment = setmetatable({
        GlobalEvent = function(name) assert(name == "MarketDeliveries"); return event end,
        Game = {processMarketDeliveries = function() calls = calls + 1 end}
    }, {__index = _G})
    assert(load(read("data/scripts/globalevents/market_deliveries.lua"), "deliveries", "t", environment))()
    assert(registered and interval == 5000)
    assert(event.onThink(5000) == true and calls == 1)
end

-- The legacy command must open existing rights through the native depot check,
-- without silently moving or issuing replacement items.
do
    local opened, registered, messages = 0, false, {}
    local event = {
        separator = function(_, separator) assert(separator == " ") end,
        register = function() registered = true end
    }
    local allowed = true
    local environment = setmetatable({
        MESSAGE_EVENT_ADVANCE = 19,
        TalkAction = function(name) assert(name == "!marketstatus"); return event end,
        Game = {
            openLegacyMarketInbox = function(guid) assert(guid == 42); opened = opened + 1; return allowed end,
            getTowns = function() return {} end
        }
    }, {__index = _G})
    local player = {
        getGuid = function() return 42 end,
        getInbox = function(_, id) assert(id == 0); return {getSize = function() return 3 end} end,
        sendTextMessage = function(_, kind, message) assert(kind == 19); messages[#messages + 1] = message end
    }
    assert(load(read("data/scripts/talkactions/player/misc/marketstatus.lua"), "marketstatus", "t", environment))()
    assert(registered)
    assert(event.onSay(player, "!marketstatus", "legacy"))
    assert(opened == 1 and messages[1]:find("now open", 1, true))
    allowed = false
    assert(event.onSay(player, "!marketstatus", "legacy"))
    assert(opened == 2 and messages[2]:find("next to a depot", 1, true))
    assert(event.onSay(player, "!marketstatus", ""))
    assert(opened == 2 and messages[3]:find("Legacy Market Inbox: 3", 1, true))
end

local source = read("data/scripts/network/market/market.lua")
local first = assert(source:find("local function removePlayerMarketMoney", 1, true))
local last = assert(source:find("local function normalizeDepotId", first, true))
local calls, restores, allow = 0, {}, true
local environment = setmetatable({Game = {
    reserveMarketMoney = function(guid, amount)
        assert(guid == 42 and amount == 25)
        calls = calls + 1
        return allow
    end,
    finishMarketMoney = function(guid, restore)
        assert(guid == 42)
        restores[#restores + 1] = restore
        return true
    end
}}, {__index = _G})
local remove, refund, finish = assert(load(source:sub(first, last - 1) ..
    "\nreturn removePlayerMarketMoney, refundPlayerMarketMoney, finishPlayerMarketMoney", "payment", "t", environment))()
local player = {getGuid = function() return 42 end}
local payment = remove(player, 25)
assert(payment)
refund(player, payment)
assert(restores[1] == true)
finish(player, payment, false)
assert(restores[2] == false) -- success / unknown commit never compensates
allow = false
assert(remove(player, 25) == nil)
refund(player, nil)
assert(calls == 2 and #restores == 2)
print("PASS: single scheduled save; delivery event registration/tick; legacy inbox command; exact payment helper delegation")
