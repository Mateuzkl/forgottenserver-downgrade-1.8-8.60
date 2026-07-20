local miniBotAdmin = TalkAction("/minibotadmin")

local MAX_BAN_MINUTES = 365 * 24 * 60
local USAGE = "Usage: /minibotadmin check-start|check-stop|ban|unban|status, player name[, ban minutes]"

local function reply(player, message, errorMessage)
	player:sendTextMessage(errorMessage and MESSAGE_STATUS_CONSOLE_RED or MESSAGE_STATUS_CONSOLE_BLUE, message)
end

local function formatTimestamp(timestamp, requireFuture)
	if not timestamp or timestamp <= 0 or (requireFuture and timestamp <= os.time()) then
		return "none"
	end
	return os.date("%Y-%m-%d %H:%M:%S", timestamp)
end

function miniBotAdmin.onSay(player, words, param)
	-- Keep the authorization check here as defense in depth in addition to the
	-- TalkAction registration below.
	if not player:getGroup():getAccess() or player:getAccountType() < ACCOUNT_TYPE_GOD then
		return true
	end
	if not AstraHelper then
		reply(player, "MiniBot server service is unavailable.", true)
		return false
	end

	local params = param:splitTrimmed(",")
	local action = params[1] and params[1]:lower() or ""
	local targetName = params[2] or ""
	if action == "" or targetName == "" then
		reply(player, USAGE, true)
		return false
	end

	local target = Player(targetName)
	if not target then
		reply(player, "Player must be online.", true)
		return false
	end

	if action == "check-start" then
		local started, reason = AstraHelper.startMiniBotCheck(target, player)
		if not started then
			reply(player, "Could not start MiniBot bot-check for " .. target:getName() .. ": " .. tostring(reason) .. ".", true)
			return false
		end
		reply(player, "MiniBot bot-check started for " .. target:getName() .. ".")
		return false
	end

	if action == "check-stop" then
		local stopped, reason = AstraHelper.stopMiniBotCheck(target)
		if not stopped then
			reply(player, "Could not stop MiniBot bot-check for " .. target:getName() .. ": " .. tostring(reason) .. ".", true)
			return false
		end
		reply(player, "MiniBot bot-check stopped for " .. target:getName() .. ".")
		return false
	end

	if action == "ban" then
		local minutes = tonumber(params[3])
		if not minutes or minutes ~= math.floor(minutes) or minutes < 1 or minutes > MAX_BAN_MINUTES then
			reply(player, "Ban minutes must be an integer from 1 to " .. MAX_BAN_MINUTES .. ".", true)
			return false
		end

		local bannedUntil = os.time() + minutes * 60
		local banned, reason = AstraHelper.setMiniBotBan(target, bannedUntil)
		if not banned then
			reply(player, "Could not apply MiniBot ban to " .. target:getName() .. ": " .. tostring(reason) .. ".", true)
			return false
		end
		target:sendTextMessage(MESSAGE_STATUS_WARNING,
			"Your MiniBot access is suspended until " .. formatTimestamp(bannedUntil, true) .. ".")
		reply(player, "MiniBot ban applied to " .. target:getName() .. " until " .. formatTimestamp(bannedUntil, true) .. ".")
		return false
	end

	if action == "unban" then
		local cleared, reason = AstraHelper.clearMiniBotBan(target)
		if not cleared then
			reply(player, "Could not clear MiniBot ban for " .. target:getName() .. ": " .. tostring(reason) .. ".", true)
			return false
		end
		target:sendTextMessage(MESSAGE_STATUS_WARNING, "Your MiniBot suspension has been removed.")
		reply(player, "MiniBot ban cleared for " .. target:getName() .. ".")
		return false
	end

	if action == "status" then
		local state = AstraHelper.getMiniBotState(target)
		local session = AstraHelper.getMiniBotCheckSession(target)
		local checkStatus = session and
			("active since " .. formatTimestamp(session.startedAt, false) .. " by " .. session.startedByName) or "inactive"
		reply(player, string.format(
			"%s: enabled=%s, task=%s, timeLeft=%d, bannedUntil=%s, afkPauseUntil=%s, botCheck=%s.",
			target:getName(), tostring(state.enabled), tostring(state.task), state.timeLeft,
			formatTimestamp(state.bannedUntil, true), formatTimestamp(state.afkPauseUntil, true), checkStatus))
		return false
	end

	reply(player, USAGE, true)
	return false
end

miniBotAdmin:separator(" ")
miniBotAdmin:accountType(ACCOUNT_TYPE_GOD)
miniBotAdmin:access(true)
miniBotAdmin:register()
