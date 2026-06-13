-- Weekly Tasks logic: kill/delivery task generation, tracking, weekly reset, soulseal rewards.
-- Uses KV store for simple values, DB table for complex data.

local WeeklyTasks = {}

local protocol -- set by init.lua

-- Difficulty constants
local DIFFICULTY_BEGINNER = 0
local DIFFICULTY_ADEPT = 1
local DIFFICULTY_EXPERT = 2
local DIFFICULTY_MASTER = 3

-- HTP per kill task by difficulty
local HTP_PER_KILL = {
	[DIFFICULTY_BEGINNER] = 25,
	[DIFFICULTY_ADEPT] = 50,
	[DIFFICULTY_EXPERT] = 100,
	[DIFFICULTY_MASTER] = 110,
}

-- HTP multiplier based on completed task count
local SOULSEALS_PER_TASK = 1
local HTP_PER_DELIVERY = 75
local DELIVERY_EXP_BASE = 75

-- Any creature totals by difficulty
local ANY_CREATURE_TOTALS = {
	[DIFFICULTY_BEGINNER] = 1000,
	[DIFFICULTY_ADEPT] = 2000,
	[DIFFICULTY_EXPERT] = 3000,
	[DIFFICULTY_MASTER] = 4000,
}

-- Kill requirements by difficulty
local KILL_REQUIREMENTS = {
	[DIFFICULTY_BEGINNER] = { min = 50, max = 150 },
	[DIFFICULTY_ADEPT] = { min = 100, max = 250 },
	[DIFFICULTY_EXPERT] = { min = 200, max = 350 },
	[DIFFICULTY_MASTER] = { min = 250, max = 500 },
}

-- Task counts
local KILL_TASKS_NORMAL = 5
local KILL_TASKS_EXPANSION = 8
local DELIVERY_TASKS_NORMAL = 6
local DELIVERY_TASKS_EXPANSION = 9

-- Weekday constants (Lua: 1=Sunday, 2=Monday, ..., 7=Saturday)
local DEFAULT_RESET_DAY = 1 -- Sunday

local LEGACY_DELIVERY_ITEM_IDS = {
	[DIFFICULTY_BEGINNER] = {
		[2461] = 811, [2464] = 813, [2465] = 818, [2378] = 819, [2398] = 820,
		[2416] = 821, [2413] = 824, [2672] = 825, [2229] = 826, [2643] = 827,
	},
	[DIFFICULTY_ADEPT] = {
		[2488] = 828, [2502] = 829, [2438] = 830, [2458] = 3234, [2426] = 3269,
		[2424] = 3275, [2672] = 3291, [5925] = 3292, [5908] = 3301, [2472] = 3306,
	},
	[DIFFICULTY_EXPERT] = {
		[2472] = 3307, [2490] = 3316, [2498] = 3318, [2516] = 3320, [2430] = 3322,
		[5877] = 3330, [5908] = 3333, [5958] = 3337, [2535] = 3342, [2487] = 3346,
	},
	[DIFFICULTY_MASTER] = {
		[2493] = 3364, [2509] = 3371, [2400] = 3373, [2523] = 3413, [5904] = 3415,
		[5958] = 3421, [2476] = 3429, [2475] = 3509, [2518] = 3557, [5925] = 3575,
	},
}

local function clamp(value, minValue, maxValue)
	value = tonumber(value) or minValue
	if value < minValue then return minValue end
	if value > maxValue then return maxValue end
	return value
end

local function getPlayerGuid(player)
	return player:getGuid()
end

local function syncSoulsealBalance(player, data)
	local balance = player:getSoulsealsPoints()
	local legacyBalance = tonumber(data.soulsealsPoints) or 0
	if balance == 0 and legacyBalance > 0 then
		player:setSoulsealsPoints(legacyBalance)
		balance = legacyBalance
	end
	data.soulsealsPoints = balance
	return balance
end

local function getRewardMultiplier(completedTasks)
	if completedTasks >= 17 then return 8 end
	if completedTasks >= 13 then return 5 end
	if completedTasks >= 9 then return 3 end
	if completedTasks >= 5 then return 2 end
	return 1
end

local function recalculateRewards(data)
	local completedKills = data.completedKillTasks or 0
	local completedDeliveries = data.completedDeliveryTasks or 0
	local totalCompleted = completedKills + completedDeliveries
	local basePoints =
		(completedKills * (HTP_PER_KILL[data.difficulty] or HTP_PER_KILL[DIFFICULTY_BEGINNER])) +
		(completedDeliveries * HTP_PER_DELIVERY)

	data.rewardHTP = basePoints * getRewardMultiplier(totalCompleted)
	data.rewardSoulseals = totalCompleted * SOULSEALS_PER_TASK
	data.needsReward = totalCompleted > 0
end

local function getCurrentWeek()
	return os.date("%Y-%U")
end

-- ============================================
-- DATA HELPERS
-- ============================================

local weeklyCache = {}

local function invalidateCache(playerGuid)
	weeklyCache[playerGuid] = nil
end

function WeeklyTasks.invalidateCache(playerGuid)
	invalidateCache(playerGuid)
end

local function loadWeeklyData(playerGuid)
	local cached = weeklyCache[playerGuid]
	if cached then return cached end

	local data = {
		difficulty = DIFFICULTY_BEGINNER,
		hasExpansion = false,
		anyCreatureTotal = 0,
		anyCreatureCurrent = 0,
		killTasks = {},
		deliveryTasks = {},
		completedKillTasks = 0,
		completedDeliveryTasks = 0,
		killTaskRewardExp = 0,
		deliveryTaskRewardExp = 0,
		rewardHTP = 0,
		rewardSoulseals = 0,
		soulsealsPoints = 0,
		lastWeek = nil,
		needsReward = false,
		weeklyProgressFinished = 0,
		lastItemNotify = 0,
		lastWeek = nil,
	}

	local resultId = db.storeQuery("SELECT * FROM `player_weekly_tasks` WHERE `player_id` = " .. playerGuid)
	if resultId ~= false then
		data.hasExpansion = result.getDataInt(resultId, "has_expansion") ~= 0
		data.difficulty = result.getDataInt(resultId, "difficulty")
		data.anyCreatureTotal = result.getDataInt(resultId, "any_creature_total")
		data.anyCreatureCurrent = result.getDataInt(resultId, "any_creature_current")
		data.completedKillTasks = result.getDataInt(resultId, "completed_kill_tasks")
		data.completedDeliveryTasks = result.getDataInt(resultId, "completed_delivery_tasks")
		data.killTaskRewardExp = result.getDataInt(resultId, "kill_task_reward_exp")
		data.deliveryTaskRewardExp = result.getDataInt(resultId, "delivery_task_reward_exp")
		data.rewardHTP = result.getDataInt(resultId, "reward_hunting_points")
		data.rewardSoulseals = result.getDataInt(resultId, "reward_soulseals")
		data.soulsealsPoints = result.getDataInt(resultId, "soulseals_points")
		data.needsReward = result.getDataInt(resultId, "needs_reward") ~= 0
		data.weeklyProgressFinished = result.getDataInt(resultId, "weekly_progress_finished")
		data.lastItemNotify = result.getDataLong(resultId, "last_item_notify")
	data.lastWeek = result.getDataString(resultId, "last_week") or nil

		-- Parse kill tasks
		local ktStr = result.getDataString(resultId, "kill_tasks") or "[]"
		local ktSuccess, ktData = pcall(function() return json.decode(ktStr) end)
		data.killTasks = (ktSuccess and type(ktData) == "table") and ktData or {}

		-- Parse delivery tasks
		local dtStr = result.getDataString(resultId, "delivery_tasks") or "[]"
		local dtSuccess, dtData = pcall(function() return json.decode(dtStr) end)
		data.deliveryTasks = (dtSuccess and type(dtData) == "table") and dtData or {}
		local legacyIds = LEGACY_DELIVERY_ITEM_IDS[data.difficulty] or {}
		for _, task in ipairs(data.deliveryTasks) do
			task.itemId = legacyIds[tonumber(task.itemId)] or task.itemId
		end

		result.free(resultId)
	end

	weeklyCache[playerGuid] = data
	return data
end

local function saveWeeklyData(playerGuid)
	local data = weeklyCache[playerGuid]
	if not data then return end

	local ktJson = json.encode(data.killTasks or {})
	local dtJson = json.encode(data.deliveryTasks or {})

	db.query(
		"INSERT INTO `player_weekly_tasks` (`player_id`, `has_expansion`, `difficulty`, " ..
		"`any_creature_total`, `any_creature_current`, `completed_kill_tasks`, `completed_delivery_tasks`, " ..
		"`kill_task_reward_exp`, `delivery_task_reward_exp`, `reward_hunting_points`, `reward_soulseals`, " ..
		"`soulseals_points`, `needs_reward`, `weekly_progress_finished`, " ..
		"`kill_tasks`, `delivery_tasks`, `last_week`, `last_item_notify`) " ..
		"VALUES (" .. playerGuid .. ", " .. (data.hasExpansion and 1 or 0) .. ", " .. data.difficulty .. ", " ..
		data.anyCreatureTotal .. ", " .. data.anyCreatureCurrent .. ", " ..
		data.completedKillTasks .. ", " .. data.completedDeliveryTasks .. ", " ..
		data.killTaskRewardExp .. ", " .. data.deliveryTaskRewardExp .. ", " ..
		data.rewardHTP .. ", " .. data.rewardSoulseals .. ", " ..
		data.soulsealsPoints .. ", " .. (data.needsReward and 1 or 0) .. ", " .. data.weeklyProgressFinished .. ", " ..
		db.escapeString(ktJson) .. ", " .. db.escapeString(dtJson) .. ", " .. db.escapeString(data.lastWeek or "") .. ", " .. data.lastItemNotify .. ") " ..
		"ON DUPLICATE KEY UPDATE `has_expansion` = VALUES(`has_expansion`), `difficulty` = VALUES(`difficulty`), " ..
		"`any_creature_total` = VALUES(`any_creature_total`), `any_creature_current` = VALUES(`any_creature_current`), " ..
		"`completed_kill_tasks` = VALUES(`completed_kill_tasks`), `completed_delivery_tasks` = VALUES(`completed_delivery_tasks`), " ..
		"`kill_task_reward_exp` = VALUES(`kill_task_reward_exp`), `delivery_task_reward_exp` = VALUES(`delivery_task_reward_exp`), " ..
		"`reward_hunting_points` = VALUES(`reward_hunting_points`), `reward_soulseals` = VALUES(`reward_soulseals`), " ..
		"`soulseals_points` = VALUES(`soulseals_points`), `needs_reward` = VALUES(`needs_reward`), " ..
		"`weekly_progress_finished` = VALUES(`weekly_progress_finished`), " ..
		"`kill_tasks` = VALUES(`kill_tasks`), `delivery_tasks` = VALUES(`delivery_tasks`), `last_week` = VALUES(`last_week`), " ..
		"`last_item_notify` = VALUES(`last_item_notify`)"
	)
end

-- ============================================
-- WEEKLY RESET LOGIC
-- ============================================

function WeeklyTasks.shouldReset(playerGuid)
	local data = weeklyCache[playerGuid]
	if not data then data = loadWeeklyData(playerGuid) end

	-- Compare current week identifier against stored week
	local currentWeek = getCurrentWeek()
	if not data.lastWeek or data.lastWeek ~= currentWeek then
		return true
	end
	return false
end

function WeeklyTasks.performWeeklyReset(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadWeeklyData(playerGuid)
	data.hasExpansion = player:hasWeeklyExpansion()

	-- Distribute pending rewards first
	if data.needsReward then
		WeeklyTasks.distributeRewards(player)
		data = loadWeeklyData(playerGuid) -- Reload after reward
	end

	-- Reset all progress
	data.killTasks = {}
	data.deliveryTasks = {}
	data.completedKillTasks = 0
	data.completedDeliveryTasks = 0
	data.anyCreatureCurrent = 0
	data.anyCreatureTotal = 0
	data.killTaskRewardExp = 0
	data.deliveryTaskRewardExp = 0
	data.rewardHTP = 0
	data.rewardSoulseals = 0
	data.needsReward = false
	data.weeklyProgressFinished = 0
	data.lastWeek = getCurrentWeek()

	saveWeeklyData(playerGuid)
	return true
end

function WeeklyTasks.distributeRewards(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadWeeklyData(playerGuid)
	syncSoulsealBalance(player, data)
	recalculateRewards(data)

	if not data.needsReward then return false end
	if data.rewardHTP <= 0 and data.rewardSoulseals <= 0 then
		data.needsReward = false
		saveWeeklyData(playerGuid)
		return false
	end

	-- Give hunting task points
	if data.rewardHTP > 0 then
		player:addTaskHuntingPoints(data.rewardHTP)
		protocol.sendResourceBalance(player, protocol.RESOURCE_TASK_HUNTING, player:getTaskHuntingPoints())
	end

	-- Give soulseals
	if data.rewardSoulseals > 0 then
		player:addSoulsealsPoints(data.rewardSoulseals)
		data.soulsealsPoints = player:getSoulsealsPoints()
		protocol.sendResourceBalance(player, protocol.RESOURCE_SOULSEALS_POINTS, data.soulsealsPoints)
	end

	data.needsReward = false
	saveWeeklyData(playerGuid)
	return true
end

-- ============================================
-- TASK GENERATION
-- ============================================

function WeeklyTasks.selectDifficulty(player, difficulty)
	if difficulty == nil or difficulty < 0 or difficulty > 3 then return false end

	local playerGuid = getPlayerGuid(player)
	local data = loadWeeklyData(playerGuid)
	data.hasExpansion = player:hasWeeklyExpansion()

	-- Can only set once per week; block if progress is already finished
	if data.weeklyProgressFinished == 1 then
		return false
	end

	data.difficulty = difficulty
	WeeklyTasks.generateTasks(player)
	return WeeklyTasks.sendWeeklyData(player)
end

function WeeklyTasks.generateTasks(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadWeeklyData(playerGuid)
	data.hasExpansion = player:hasWeeklyExpansion()

	local difficulty = data.difficulty or DIFFICULTY_BEGINNER
	local hasExpansion = data.hasExpansion

	local killCount = hasExpansion and KILL_TASKS_EXPANSION or KILL_TASKS_NORMAL
	local deliveryCount = hasExpansion and DELIVERY_TASKS_EXPANSION or DELIVERY_TASKS_NORMAL

	-- Set any creature total
	data.anyCreatureTotal = ANY_CREATURE_TOTALS[difficulty]
	data.anyCreatureCurrent = 0

	-- Generate kill tasks
	data.killTasks = {}
	if CustomBestiary and CustomBestiary.monstersByRaceId then
		local eligible = {}
		for raceId, _ in pairs(CustomBestiary.monstersByRaceId) do
			eligible[#eligible + 1] = raceId
		end

		-- Shuffle and pick
		for i = #eligible, 2, -1 do
			local j = math.random(i)
			eligible[i], eligible[j] = eligible[j], eligible[i]
		end

		local killReq = KILL_REQUIREMENTS[difficulty]
		for i = 1, math.min(killCount, #eligible) do
			local raceId = eligible[i]
			local required = math.random(killReq.min, killReq.max)
			data.killTasks[#data.killTasks + 1] = {
				raceId = raceId,
				kills = 0,
				required = required,
				grade = 0,
			}
		end
	end

	-- Calculate kill exp reward
	data.killTaskRewardExp = killCount * (HTP_PER_KILL[difficulty] * 10)

	-- Generate delivery tasks
	data.deliveryTasks = {}
	if WeeklyTasks.deliveryItems then
		local items = WeeklyTasks.deliveryItems[difficulty] or {}
		local shuffled = {}
		for i = 1, #items do shuffled[i] = items[i] end
		for i = #shuffled, 2, -1 do
			local j = math.random(i)
			shuffled[i], shuffled[j] = shuffled[j], shuffled[i]
		end

		for i = 1, math.min(deliveryCount, #shuffled) do
			local item = shuffled[i]
			local required = item.amount or 1
			local reduced = TaskBoard.hasWeeklyReducedItems(player)
			if reduced then
				required = math.max(1, math.ceil(required / 2))
			end
			data.deliveryTasks[#data.deliveryTasks + 1] = {
				index = i - 1,
				itemId = item.itemId,
				amount = required,
				required = required,
				available = 0,
				collectedItems = 0,
				delivered = 0,
				grade = 0,
				reduced = reduced,
			}
		end
	end

	data.deliveryTaskRewardExp = deliveryCount * DELIVERY_EXP_BASE
	data.lastWeek = getCurrentWeek()

	saveWeeklyData(playerGuid)
	return true
end

-- ============================================
-- KILL TRACKING
-- ============================================

function WeeklyTasks.onKill(player, raceId)
	local playerGuid = getPlayerGuid(player)
	local data = loadWeeklyData(playerGuid)

	if #data.killTasks == 0 then return false end

	local updated = false
	local matchedTask = false
	local killMultiplier = TaskBoard.getWeeklyKillMultiplier(player)

	-- Any creature counter
	local oldAnyCreatureCurrent = data.anyCreatureCurrent or 0
	data.anyCreatureCurrent = math.min(oldAnyCreatureCurrent + killMultiplier, data.anyCreatureTotal or 0)
	updated = data.anyCreatureCurrent ~= oldAnyCreatureCurrent
	if oldAnyCreatureCurrent < data.anyCreatureTotal and data.anyCreatureCurrent >= data.anyCreatureTotal then
		data.completedKillTasks = (data.completedKillTasks or 0) + 1
		matchedTask = true
		if data.killTaskRewardExp > 0 and #data.killTasks > 0 then
			player:addExperience(math.floor(data.killTaskRewardExp / #data.killTasks), true)
		end
	end

	-- Check kill tasks
	for _, kt in ipairs(data.killTasks) do
		if kt.raceId == raceId and kt.kills < kt.required then
			kt.kills = math.min(kt.kills + killMultiplier, kt.required)
			updated = true
			matchedTask = true

			if kt.kills >= kt.required then
				data.completedKillTasks = (data.completedKillTasks or 0) + 1
				kt.grade = 1 -- Mark completed
				-- Give kill exp
				if data.killTaskRewardExp > 0 then
					local expPerTask = math.floor(data.killTaskRewardExp / (#data.killTasks))
					player:addExperience(expPerTask, true)
				end
			end
			break
		end
	end

	if matchedTask then
		recalculateRewards(data)

		-- Check if all tasks done
		local allKillDone = (data.completedKillTasks or 0) >= (#data.killTasks + 1)
		local allDeliveryDone = (data.completedDeliveryTasks or 0) >= #data.deliveryTasks

		if allKillDone and allDeliveryDone then
			data.weeklyProgressFinished = 1
		end
	end

	if updated then
		saveWeeklyData(playerGuid)
		WeeklyTasks.sendWeeklyData(player)
	end

	return updated
end

-- ============================================
-- DELIVERY TASKS
-- ============================================

function WeeklyTasks.deliverTask(player, taskIndex)
	if taskIndex == nil then return false end

	local playerGuid = getPlayerGuid(player)
	local data = loadWeeklyData(playerGuid)

	local dt = data.deliveryTasks[taskIndex + 1]
	if not dt then return false end
	if dt.delivered == 1 then return false end

	-- Count items in player inventory
	local itemId = dt.itemId
	local required = dt.required
	local found = 0

	-- Quick count from inventory
	local countResult = player:getItemTypeCount(itemId)
	if countResult >= required then
		-- Remove items from player
		if player:removeItem(itemId, required) then
			dt.collectedItems = (dt.collectedItems or 0) + required
			dt.available = countResult - required
			dt.delivered = 1
			data.completedDeliveryTasks = (data.completedDeliveryTasks or 0) + 1

			-- Give delivery exp
			if data.deliveryTaskRewardExp > 0 then
				local expPerTask = math.floor(data.deliveryTaskRewardExp / (#data.deliveryTasks))
				player:addExperience(expPerTask, true)
			end

			recalculateRewards(data)

			-- Check completion
			local allKillDone = (data.completedKillTasks or 0) >= (#data.killTasks + 1)
			local allDeliveryDone = (data.completedDeliveryTasks or 0) >= #data.deliveryTasks

			if allKillDone and allDeliveryDone then
				data.weeklyProgressFinished = 1
			end

			saveWeeklyData(playerGuid)
			WeeklyTasks.sendWeeklyData(player)
			return true
		end
	end

	return false
end

-- ============================================
-- SHOP OFFERS
-- ============================================

function WeeklyTasks.setDeliveryItems(items)
	WeeklyTasks.deliveryItems = items
end

function WeeklyTasks.applyReducedItems(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadWeeklyData(playerGuid)
	local changed = false

	for _, task in ipairs(data.deliveryTasks) do
		if task.delivered ~= 1 and not task.reduced then
			task.required = math.max(1, math.ceil((task.required or task.amount or 1) / 2))
			task.amount = task.required
			task.reduced = true
			changed = true
		end
	end

	if changed then
		saveWeeklyData(playerGuid)
		WeeklyTasks.sendWeeklyData(player)
	end
	return changed
end

function WeeklyTasks.applyExpansion(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadWeeklyData(playerGuid)
	data.hasExpansion = player:hasWeeklyExpansion()
	saveWeeklyData(playerGuid)
	return WeeklyTasks.sendWeeklyData(player)
end

-- ============================================
-- SEND TO CLIENT
-- ============================================

function WeeklyTasks.sendWeeklyData(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadWeeklyData(playerGuid)
	if data.lastWeek and data.lastWeek ~= "" and data.lastWeek ~= getCurrentWeek() and #data.killTasks > 0 then
		WeeklyTasks.performWeeklyReset(player)
		data = loadWeeklyData(playerGuid)
	end
	data.hasExpansion = player:hasWeeklyExpansion()
	syncSoulsealBalance(player, data)
	TaskBoard.sendAll(player)

	-- Build kill tasks for protocol
	local killTasks = {}
	for _, kt in ipairs(data.killTasks) do
		killTasks[#killTasks + 1] = {
			raceId = kt.raceId,
			kills = kt.kills or 0,
			required = kt.required or 0,
			grade = kt.grade or 0,
		}
	end

	-- Build delivery tasks for protocol
	local deliveryTasks = {}
	for _, dt in ipairs(data.deliveryTasks) do
		local required = dt.required or dt.amount or 0
		local delivered = dt.delivered == 1
		local available = 0
		if dt.itemId then
			available = player:getItemCount(dt.itemId) or 0
		end

		deliveryTasks[#deliveryTasks + 1] = {
			itemId = dt.itemId,
			amount = delivered and required or 0,
			required = required,
			available = available,
			grade = dt.grade or 0,
		}
	end

	-- Use actual soulseals balance from C++ player object
	local soulsealsBalance = player:getSoulsealsPoints()

	local protocolData = {
		anyCreatureKills = data.anyCreatureCurrent or 0,
		anyCreatureTotal = data.anyCreatureTotal or 0,
		killTasks = killTasks,
		deliveryTasks = deliveryTasks,
		difficulty = data.difficulty,
		killExp = data.killTaskRewardExp or 0,
		deliveryExp = data.deliveryTaskRewardExp or 0,
		completedKills = data.completedKillTasks or 0,
		completedDeliveries = data.completedDeliveryTasks or 0,
		weeklyProgress = data.weeklyProgressFinished or 0,
		huntingPts = data.rewardHTP or 0,
		soulseals = data.rewardSoulseals or 0,
		soulsealsBalance = soulsealsBalance,
		needsReward = data.needsReward or false,
		hasExpansion = data.hasExpansion or false,
	}

	return protocol.sendWeeklyTaskData(player, protocolData)
end

function WeeklyTasks.setProtocol(protoModule)
	protocol = protoModule
end

function WeeklyTasks.saveOnLogout(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadWeeklyData(playerGuid)
	data.hasExpansion = player:hasWeeklyExpansion()
	syncSoulsealBalance(player, data)
	saveWeeklyData(playerGuid)
	invalidateCache(playerGuid)
end

-- Expose internal loader for C++ sync on login
function WeeklyTasks.loadWeeklyData(playerGuid)
	return loadWeeklyData(playerGuid)
end

-- Check pending rewards on login
function WeeklyTasks.checkRewardsOnLogin(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadWeeklyData(playerGuid)

	if data.lastWeek and data.lastWeek ~= "" and data.lastWeek ~= getCurrentWeek() and #data.killTasks > 0 then
		WeeklyTasks.performWeeklyReset(player)
	end
end

return WeeklyTasks
