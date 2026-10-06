-- Exercise the production server-save script, including an already-closed server.
local script = arg[1] or 'data/scripts/globalevents/serversave.lua'
local scheduled, event
local state, saves, globals, maps, shutdowns
local options = {}
GAME_STATE_NORMAL, GAME_STATE_CLOSED, GAME_STATE_SHUTDOWN = 1, 2, 3
configKeys = {
    SERVER_SAVE_SHUTDOWN = 'shutdown', SERVER_SAVE_CLOSE = 'close',
    SERVER_SAVE_CLEAN_MAP = 'clean', SERVER_SAVE_NOTIFY_MESSAGE = 'notify',
    SERVER_SAVE_NOTIFY_DURATION = 'duration'
}
configManager = {
    getBoolean = function(key) return options[key] == true end,
    getNumber = function() return 0 end
}
Game = {
    getGameState = function() return state end,
    setGameState = function(nextState)
        if state == nextState then return end
        state = nextState
        if nextState == GAME_STATE_CLOSED then
            globals, maps, saves = globals + 1, maps + 1, saves + 1
        elseif nextState == GAME_STATE_SHUTDOWN then
            shutdowns = shutdowns + 1
        end
    end
}
function saveServer() globals, maps, saves = globals + 1, maps + 1, saves + 1 end
function cleanMap() end
function addEvent(callback) scheduled = callback end
function GlobalEvent()
    event = {time = function() end, register = function() end}
    return event
end
assert(loadfile(script))()
for _, close in ipairs({false, true}) do
    for _, initiallyClosed in ipairs({false, true}) do
        options = {close = close}
        state = initiallyClosed and GAME_STATE_CLOSED or GAME_STATE_NORMAL
        saves, globals, maps, shutdowns = 0, 0, 0, 0
        event.onTime(0)
        assert(scheduled)
        scheduled()
        assert(saves == 1 and globals == 1 and maps == 1)
        assert(state == (initiallyClosed and GAME_STATE_CLOSED or GAME_STATE_NORMAL))
    end
end
options = {shutdown = true, close = true}
state = GAME_STATE_NORMAL
saves, globals, maps, shutdowns = 0, 0, 0, 0
event.onTime(0)
scheduled()
assert(shutdowns == 1 and saves == 0)
print('Production server-save close/already-closed/shutdown regression passed')
