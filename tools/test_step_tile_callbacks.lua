-- Standalone contract check. Does not connect to the server or database.
local timers, calls, currentPlayer = {}, 0, nil
Player = setmetatable({}, {__call = function(_, id)
    if currentPlayer and currentPlayer:getId() == id then return currentPlayer end
end})
hasEvent, Event = {}, {}
function addEvent(callback, delay, ...)
    assert(delay == 0)
    timers[#timers + 1] = {callback = callback, arguments = {...}}
    return #timers
end
local player = setmetatable({id = 42, guid = 7}, {__index = Player})
function player:getId() return self.id end
function player:getGuid() return self.guid end
currentPlayer = player
dofile(assert(arg[1], "Pass data/events/scripts/player.lua"))
local function step() return player:onStepTile({}, {}) end
local function runLast()
    local timer = assert(timers[#timers])
    timer.callback((table.unpack or unpack)(timer.arguments))
end

CustomMarket = nil
for _ = 1, 700 do assert(step() == true) end
assert(#timers == 0, "Missing market must not create 700 no-op timers")
CustomMarket = {}
assert(step() == true and #timers == 0)
CustomMarket.checkAccess = function(p) assert(p == currentPlayer); calls = calls + 1 end
local marketOpen = false
CustomMarket.isOpen = function(p) assert(p == currentPlayer); return marketOpen end
for _ = 1, 700 do assert(step() == true) end
assert(#timers == 0, "Players with a closed market must not create no-op timers")
marketOpen = true
assert(step() == true and #timers == 1)
runLast()
assert(calls == 1)

step()
currentPlayer = nil
runLast()
assert(calls == 1, "Logout must cancel the pending check")
currentPlayer = player
step()
local oldGuid = player.guid
player.guid = 99
runLast()
assert(calls == 1, "A recycled runtime id must not receive another player's callback")
player.guid = oldGuid
step()
CustomMarket = nil
runLast()
assert(calls == 1, "Module removal before the timer must be safe")

hasEvent.onStepTile = true
Event.onStepTile = function(p, from, to)
    assert(p == player and from and to)
    return false
end
assert(step() == false, "Normal step event veto must be preserved")
print("PASS: no timers with absent/closed market, active market callback, logout, runtime-id reuse, module removal and step veto")
