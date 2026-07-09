local talkaction = TalkAction("/bot")

local function send(player, message)
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, message)
end

local function splitCommand(param)
	param = tostring(param or ""):gsub("^%s+", ""):gsub("%s+$", "")
	if param == "" then
		return "", ""
	end

	local action, rest = param:match("^(%S+)%s*(.*)$")
	return (action or ""):lower(), rest or ""
end

local function splitArgs(rest)
	return rest:splitTrimmed(",")
end

local function parseToggle(value)
	value = tostring(value or ""):lower()
	return value == "on" or value == "true" or value == "1" or value == "yes"
end

local function showHelp(player)
	player:popupFYI(table.concat({
		"/bot add name[, auto]",
		"/bot remove name",
		"/bot enable name",
		"/bot disable name",
		"/bot autospawn name, on|off",
		"/bot spawn name[, cast]",
		"/bot despawn name",
		"/bot cast name, on|off",
		"/bot list",
		"/bot online"
	}, "\n"))
end

function talkaction.onSay(player, words, param)
	logCommand(player, words, param)

	local action, rest = splitCommand(param)
	if action == "" or action == "help" then
		showHelp(player)
		return false
	end

	if action == "add" or action == "register" then
		local args = splitArgs(rest)
		local ok, message = BotSystem.register(args[1], args[2] and parseToggle(args[2]))
		send(player, message)
		return false
	end

	if action == "remove" or action == "unregister" then
		local ok, message = BotSystem.unregister(rest)
		send(player, message)
		return false
	end

	if action == "enable" or action == "disable" then
		local ok, message = BotSystem.setEnabled(rest, action == "enable")
		send(player, message)
		return false
	end

	if action == "autospawn" then
		local args = splitArgs(rest)
		local ok, message = BotSystem.setAutoSpawn(args[1], parseToggle(args[2]))
		send(player, message)
		return false
	end

	if action == "spawn" then
		local args = splitArgs(rest)
		local broadcast = args[2] and (args[2]:lower() == "cast" or parseToggle(args[2]))
		local ok, message = BotSystem.spawn(args[1], broadcast, true)
		send(player, message)
		return false
	end

	if action == "despawn" or action == "kick" then
		local ok, message = BotSystem.despawn(rest, true)
		send(player, message)
		return false
	end

	if action == "cast" then
		local args = splitArgs(rest)
		local ok, message = BotSystem.setCast(args[1], parseToggle(args[2]))
		send(player, message)
		return false
	end

	if action == "list" then
		local rows = BotSystem.getRegistered()
		if #rows == 0 then
			send(player, "No registered bots.")
			return false
		end

		local lines = { "Registered bots:" }
		for _, bot in ipairs(rows) do
			lines[#lines + 1] = string.format("%s [%d] id=%d enabled=%s auto=%s",
				bot.name, bot.level, bot.playerId, bot.enabled and "yes" or "no", bot.autoSpawn and "yes" or "no")
		end
		player:popupFYI(table.concat(lines, "\n"))
		return false
	end

	if action == "online" then
		local bots = Game.getBots()
		if #bots == 0 then
			send(player, "No bots online.")
			return false
		end

		local names = {}
		for _, bot in ipairs(bots) do
			names[#names + 1] = string.format("%s [%d]", bot:getName(), bot:getLevel())
		end
		send(player, #bots .. " bot(s) online: " .. table.concat(names, ", "))
		return false
	end

	showHelp(player)
	return false
end

talkaction:separator(" ")
talkaction:accountType(6)
talkaction:access(true)
talkaction:register()
