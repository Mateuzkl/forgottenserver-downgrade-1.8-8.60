local talkaction = TalkAction("/bot")

local function send(player, message)
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, message)
end

local function parseAddAutoSpawn(value)
	value = tostring(value or ""):lower()
	return value == "auto" or BotCore.parseToggle(value)
end

local function showHelp(player)
	player:popupFYI(table.concat({
		"/bot add name[, auto[, vocation]]",
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

	if not BotSystem or not BotCore then
		send(player, "Bot system libraries are not loaded.")
		return false
	end

	local action, args = BotCore.parseCommand(param)
	if action == "" or action == "help" then
		showHelp(player)
		return false
	end

	if action == "add" or action == "register" then
		local ok, message = BotSystem.register(args[1], args[2] and parseAddAutoSpawn(args[2]), args[3])
		send(player, message)
		return false
	end

	if action == "remove" or action == "unregister" then
		local ok, message = BotSystem.unregister(args[1])
		send(player, message)
		return false
	end

	if action == "enable" or action == "disable" then
		local ok, message = BotSystem.setEnabled(args[1], action == "enable")
		send(player, message)
		return false
	end

	if action == "autospawn" then
		local ok, message = BotSystem.setAutoSpawn(args[1], BotCore.parseToggle(args[2]))
		send(player, message)
		return false
	end

	if action == "spawn" then
		local broadcast = args[2] and (args[2]:lower() == "cast" or BotCore.parseToggle(args[2]))
		local ok, message = BotSystem.spawn(args[1], broadcast, true)
		send(player, message)
		return false
	end

	if action == "despawn" or action == "kick" then
		local ok, message = BotSystem.despawn(args[1], true)
		send(player, message)
		return false
	end

	if action == "cast" then
		local ok, message = BotSystem.setCast(args[1], BotCore.parseToggle(args[2]))
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
talkaction:accountType(ACCOUNT_TYPE_GOD)
talkaction:access(true)
talkaction:register()
