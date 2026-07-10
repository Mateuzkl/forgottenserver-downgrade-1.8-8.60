-- Logger Lib
_G.logger = {}

local ANSI_RESET      = "\27[0m"
local ANSI_RED        = "\27[31m"
local ANSI_GREEN      = "\27[32m"
local ANSI_YELLOW     = "\27[33m"
local ANSI_BLUE       = "\27[34m"
local ANSI_MAGENTA    = "\27[35m"
local ANSI_CYAN       = "\27[36m"
local ANSI_WHITE      = "\27[97m"
local ANSI_BOLD       = "\27[1m"
local ANSI_LIME_GREEN = "\27[92m"  -- bright green
local ANSI_ORANGE_RED = "\27[91m"  -- bright red
local ANSI_BOLD_CYAN  = "\27[1;36m"
local ANSI_BOLD_WHITE = "\27[1;97m"

logger.colors = {
    reset      = ANSI_RESET,
    red        = ANSI_RED,
    green      = ANSI_GREEN,
    yellow     = ANSI_YELLOW,
    blue       = ANSI_BLUE,
    magenta    = ANSI_MAGENTA,
    cyan       = ANSI_CYAN,
    white      = ANSI_WHITE,
    bold       = ANSI_BOLD,
    lime_green = ANSI_LIME_GREEN,
    orange_red = ANSI_ORANGE_RED,
    bold_cyan  = ANSI_BOLD_CYAN,
    bold_white = ANSI_BOLD_WHITE,
}

local function getTimestamp()
    return os.date("%Y-%m-%d %H:%M:%S") .. string.format(".%03d", math.random(1, 999)) -- Emulating ms for visual consistency
end

function logger.info(message, ...)
    print(string.format("[%s] [%sinfo%s] " .. message, getTimestamp(), ANSI_GREEN, ANSI_RESET, ...))
end

function logger.warn(message, ...)
    print(string.format("[%s] [%swarning%s] " .. message, getTimestamp(), ANSI_YELLOW, ANSI_RESET, ...))
end

function logger.error(message, ...)
    print(string.format("[%s] [%serror%s] " .. message, getTimestamp(), ANSI_RED, ANSI_RESET, ...))
end

function logger.debug(message, ...)
    print(string.format("[%s] [%sdebug%s] " .. message, getTimestamp(), ANSI_CYAN, ANSI_RESET, ...))
end