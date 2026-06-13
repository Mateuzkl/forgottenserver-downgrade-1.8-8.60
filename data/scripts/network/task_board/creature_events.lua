-- Creature Events for Task Board: onKill, onLogin, onLogout hooks.

-- Guard: only register if Task Hunting system is enabled
if not configManager or not configManager.getBoolean or not configManager.getBoolean(configKeys.TASK_HUNTING_SYSTEM_ENABLED) then
	return
end

local bountyEnabled = configManager.getBoolean(configKeys.BOUNTY_TASKS_ENABLED)
local weeklyEnabled = configManager.getBoolean(configKeys.WEEKLY_TASKS_ENABLED)
local soulsealsEnabled = configManager.getBoolean(configKeys.SOULSEALS_SYSTEM_ENABLED)

-- Use globals set by init.lua (single module instance, protocol already wired).
-- Do NOT reload modules via dofile — that creates a second copy with nil protocol
-- and a separate cache, causing nil crashes and stale progress in UI.
local function getBountyModule()
	return _TASK_BOARD_BOUNTY_MODULE
end

local function getWeeklyModule()
	return _TASK_BOARD_WEEKLY_MODULE
end

local function getResourceBalance()
	return TaskBoardResourceBalance
end

-- ============================================
-- ON KILL
-- ============================================

local taskBoardKill = CreatureEvent("TaskBoardKill")

function taskBoardKill.onKill(player, target, lastHit)
	if not player or not target then
		return true
	end

	-- Get monster race ID from target
	local monster = Monster(target)
	if not monster then
		return true
	end

	local monsterType = monster:getType()
	if not monsterType then
		return true
	end

	local raceId = monsterType:raceId()

	-- Bounty task kill tracking
	if bountyEnabled then
		local bounty = getBountyModule()
		if bounty then
			bounty.onKill(player, raceId)
		end
	end

	-- Weekly task kill tracking
	if weeklyEnabled then
		local weekly = getWeeklyModule()
		if weekly then
			weekly.onKill(player, raceId)
		end
	end

	return true
end

taskBoardKill:type("kill")
taskBoardKill:register()

-- ============================================
-- ON LOGIN
-- ============================================

local taskBoardLogin = CreatureEvent("TaskBoardLogin")

function taskBoardLogin.onLogin(player)
	player:registerEvent("TaskBoardKill")
	local playerGuid = player:getGuid()

	-- All point fields loaded by iologindata.cpp from the players table.
	-- Distribute pending weekly rewards if any (this loads weekly data lazily).
	if weeklyEnabled then
		local weekly = getWeeklyModule()
		if weekly then
			weekly.checkRewardsOnLogin(player)
		end
	end

	-- Send resource balances (use GUID to re-acquire player after delay)
	local rb = getResourceBalance()
	if rb then
		addEvent(function()
			local p = Player(playerGuid)
			if p then
				rb.sendAll(p)
			end
		end, 1000) -- delay 1s for client to be ready
	end

	return true
end

taskBoardLogin:type("login")
taskBoardLogin:register()

-- ============================================
-- ON LOGOUT
-- ============================================

local taskBoardLogout = CreatureEvent("TaskBoardLogout")

function taskBoardLogout.onLogout(player)
	if bountyEnabled then
		local bounty = getBountyModule()
		if bounty and bounty.saveOnLogout then
			bounty.saveOnLogout(player)
		end
	end

	if weeklyEnabled then
		local weekly = getWeeklyModule()
		if weekly and weekly.saveOnLogout then
			weekly.saveOnLogout(player)
		end
	end

	return true
end

taskBoardLogout:type("logout")
taskBoardLogout:register()
