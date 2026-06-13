local talkaction = TalkAction("/luagc")

local function formatMemory(kb)
	if kb >= 1048576 then
		return string.format("%.2f GB", kb / 1048576)
	elseif kb >= 1024 then
		return string.format("%.2f MB", kb / 1024)
	end
	return string.format("%d KB", kb)
end

function talkaction.onSay(player, words, param)
	local subCommand = param and param:trim():lower()

	if subCommand == "collect" then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, ">> Running full Lua GC collect...")
		Game.collectLuaGarbage()
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, ">> Collection complete. Current memory: " ..
			formatMemory(Game.getLuaMemoryUsage()))
		return false
	end

	if subCommand == "step" then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, ">> Running Lua GC step...")
		Game.stepLuaGarbage()
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, ">> Step complete. Current memory: " ..
			formatMemory(Game.getLuaMemoryUsage()))
		return false
	end

	if subCommand == "memory" then
		local before = Game.getLuaMemoryUsage()
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, ">> Before GC: " .. formatMemory(before))
		Game.collectLuaGarbage()
		local after = Game.getLuaMemoryUsage()
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
			">> After GC: " .. formatMemory(after) .. " (freed " .. formatMemory(before - after) .. ")")
		return false
	end

	if subCommand == "mode" then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, ">> Lua GC mode: " .. (luaGcMode or "unknown"))
		return false
	end

	local mem = Game.getLuaMemoryUsage()
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, ">> Lua GC Status:")
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "   Memory: " .. formatMemory(mem))
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "   Mode: " .. (luaGcMode or "unknown"))
	local function enabledStr(v)
		if v == nil then return "UNKNOWN" end
		return v and "ON" or "OFF"
	end
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "   Step: " .. enabledStr(luaGcStepEnabled) ..
		" (interval: " .. (luaGcStepInterval or "?") .. "ms, size: " .. (luaGcStepSize or "?") .. ")")
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "   Log: " .. enabledStr(luaGcLogEnabled) ..
		" (interval: " .. (luaGcLogInterval or "?") .. "ms)")
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "   Subcommands: collect, step, memory, mode")
	return false
end

talkaction:separator(" ")
talkaction:accountType(6)
talkaction:access(true)
talkaction:register()
