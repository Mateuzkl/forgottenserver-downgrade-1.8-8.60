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

local function formatMessage(message, ...)
	local text = tostring(message or "")
	local argCount = select("#", ...)
	if argCount == 0 then
		return text
	end

	if text:find("{}", 1, true) then
		local args = {...}
		local index = 0
		text = text:gsub("{}", function()
			index = index + 1
			if index <= argCount then
				return tostring(args[index])
			end
			return "{}"
		end)
		return text
	end

	local ok, formatted = pcall(string.format, text, ...)
	if ok then
		return formatted
	end
	return text
end

function logger.info(message, ...)
	local formatted = formatMessage(message, ...)
	if logInfo then
		logInfo(formatted)
	else
		print(formatted)
	end
end

function logger.warn(message, ...)
	local formatted = formatMessage(message, ...)
	if logWarning then
		logWarning(formatted)
	else
		print(formatted)
	end
end

function logger.error(message, ...)
	local formatted = formatMessage(message, ...)
	if logError then
		logError(formatted)
	else
		print(formatted)
	end
end

function logger.debug(message, ...)
	local formatted = formatMessage(message, ...)
	if logDebug then
		logDebug(formatted)
	elseif logInfo then
		logInfo(formatted)
	else
		print(formatted)
	end
end
