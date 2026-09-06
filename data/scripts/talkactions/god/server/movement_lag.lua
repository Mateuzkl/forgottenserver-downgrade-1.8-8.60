local talkaction = TalkAction("/movementlag")

function talkaction.onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return false
	end

	local trimmed = param:match("^%s*(.-)%s*$") or ""
	local split = {}
	for word in trimmed:gmatch("%S+") do
		table.insert(split, word:lower())
	end

	local command = split[1] or ""

	if command == "" or command == "report" or command == "status" then
		local enabled = isMovementDiagnosticsEnabled()
		local statusStr = enabled and "ENABLED" or "DISABLED"
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format("[Movement Diagnostics] Status: %s", statusStr))
		local report = getMovementDiagnosticsReport()
		for line in report:gmatch("[^\r\n]+") do
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, line)
		end
		return false
	elseif command == "on" or command == "enable" then
		setMovementDiagnosticsEnabled(true)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "[Movement Diagnostics] Movement latency telemetry ENABLED.")
		return false
	elseif command == "off" or command == "disable" then
		setMovementDiagnosticsEnabled(false)
		stopMovementDiagnosticsStress()
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "[Movement Diagnostics] Movement latency telemetry DISABLED.")
		return false
	elseif command == "reset" or command == "clear" then
		resetMovementDiagnostics()
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "[Movement Diagnostics] Movement metrics and ring buffers RESET.")
		return false
	elseif command == "stress" then
		local sub = split[2] or ""
		if sub == "stop" or sub == "off" or sub == "0" then
			stopMovementDiagnosticsStress()
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "[Movement Diagnostics] Stress test STOPPED.")
		else
			local level = tonumber(sub) or 1
			if level < 1 then level = 1 end
			if level > 3 then level = 3 end
			local ok = startMovementDiagnosticsStress(level)
			if ok then
				player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format("[Movement Diagnostics] Stress test level %d STARTED (running 10s synthetic load).", level))
			else
				player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "[Movement Diagnostics] Failed to start stress test or already running.")
			end
		end
		return false
	else
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Usage: /movementlag [on | off | reset | report | stress <1|2|3|stop>]")
		return false
	end
end

talkaction:separator(" ")
talkaction:accountType(6)
talkaction:access(true)
talkaction:register()
