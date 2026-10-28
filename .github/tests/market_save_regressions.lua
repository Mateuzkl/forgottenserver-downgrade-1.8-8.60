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
print("PASS: periodic open/close/shutdown each saves once; payment helper delegates exact restoration")
