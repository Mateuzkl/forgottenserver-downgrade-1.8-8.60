-- Bounty Tasks logic: creature generation, selection, reroll, kill tracking, reward claiming.
-- Uses TaskBoardProtocol for byte serialization and KV store for persistence.

local BountyTasks = {}

local protocol -- set by init.lua after loading

-- Difficulty constants
local DIFFICULTY_BEGINNER = 0
local DIFFICULTY_ADEPT = 1
local DIFFICULTY_EXPERT = 2
local DIFFICULTY_MASTER = 3

-- State constants
local STATE_NONE = 0
local STATE_SELECTION = 1
local STATE_ACTIVE = 2
local STATE_COMPLETED = 3

-- Grade constants
local GRADE_NORMAL = 0
local GRADE_SILVER = 1
local GRADE_GOLD = 2

-- Claim state
local CLAIM_SELECT_TASK = 0
local CLAIM_REWARD_NO_CLICK = 1
local CLAIM_REWARD_CLICKED = 2

-- Reroll mode
local REROLL_DAILY_CLAIMABLE = 0
local REROLL_TIMER_RUNNING = 1
local REROLL_LIMIT_REACHED = 2

-- Talisman paths
local TALISMAN_DAMAGE = 0
local TALISMAN_LIFELEECH = 1
local TALISMAN_LOOT = 2
local TALISMAN_BESTIARY = 3

-- Bounty config
local MAX_CREATURES = 3
local MAX_PREFERRED_SLOTS = 5
local MAX_REROLL_TOKENS = 10
local INITIAL_REROLL_TOKENS = 3
local BOUNTY_REWARD_REROLL_TOKENS = 1
local KILL_SAVE_INTERVAL = 5
local ITEM_BOUNTY_TALISMAN = 51978
local FREE_REROLL_COOLDOWN = 20 * 60 * 60 -- 20 hours in seconds
local PREFERRED_SLOT_COSTS = { 0, 300, 600, 900, 1200 }
local PREFERRED_CLEAR_COST = 10

-- Kill ranges by difficulty (min, max)
local KILL_RANGES = {
	[DIFFICULTY_BEGINNER] = { min = 50, max = 100 },
	[DIFFICULTY_ADEPT] = { min = 100, max = 200 },
	[DIFFICULTY_EXPERT] = { min = 200, max = 300 },
	[DIFFICULTY_MASTER] = { min = 300, max = 600 },
}

-- Bestiary star filters by difficulty
local STAR_FILTERS = {
	[DIFFICULTY_BEGINNER] = { min = 0, max = 1 },
	[DIFFICULTY_ADEPT] = { min = 0, max = 3 },
	[DIFFICULTY_EXPERT] = { min = 2, max = 5 },
	[DIFFICULTY_MASTER] = { min = 4, max = 6 },
}

local TALISMAN_STANDARD_MAX_LEVEL = 166
local TALISMAN_BESTIARY_MAX_LEVEL = 180

local function clamp(value, minValue, maxValue)
	value = tonumber(value) or minValue
	if value < minValue then return minValue end
	if value > maxValue then return maxValue end
	return value
end

local function getPlayerGuid(player)
	return player:getGuid()
end

local function syncBountyBalance(player, data)
	local balance = player:getBountyPoints()
	data.bountyPoints = balance
	return balance
end

local function sendRerollBalance(player, data)
	if protocol and protocol.RESOURCE_BOUNTY_REROLL_POINTS then
		protocol.sendResourceBalance(player, protocol.RESOURCE_BOUNTY_REROLL_POINTS, data.rerollTokens or 0)
	end
end

local function sendBountyBalances(player, data)
	if not protocol then
		return
	end

	syncBountyBalance(player, data)
	if protocol.RESOURCE_BOUNTY_POINTS then
		protocol.sendResourceBalance(player, protocol.RESOURCE_BOUNTY_POINTS, data.bountyPoints or 0)
	end
	sendRerollBalance(player, data)
end

local function shouldPersistKillProgress(previousKills, currentKills, completedNow)
	if completedNow then
		return true
	end
	if KILL_SAVE_INTERVAL <= 1 then
		return true
	end
	return math.floor(previousKills / KILL_SAVE_INTERVAL) ~= math.floor(currentKills / KILL_SAVE_INTERVAL)
end

local function isBountyTalismanEquipped(player)
	if not player or not player.getInventoryItem then
		return false
	end

	local item = player:getInventoryItem(CONST_SLOT_AMMO)
	return item and item:getId() == ITEM_BOUNTY_TALISMAN
end

-- ============================================
-- DATA HELPERS
-- ============================================

local bountyCache = {}

local function invalidateCache(playerGuid)
	bountyCache[playerGuid] = nil
end

function BountyTasks.invalidateCache(playerGuid)
	invalidateCache(playerGuid)
end

local function loadBountyData(playerGuid)
	local cached = bountyCache[playerGuid]
	if cached then
		return cached
	end

	local data = {
		state = STATE_NONE,
		difficulty = DIFFICULTY_BEGINNER,
		bountyPoints = 0,
		rerollTokens = INITIAL_REROLL_TOKENS,
		freeRerollTimestamp = os.time(), -- new players can claim immediately
		activeTask = nil,
		creaturesList = {},
		talismans = {
			[TALISMAN_DAMAGE + 1] = { tier = 0, upgrade = 0 },
			[TALISMAN_LIFELEECH + 1] = { tier = 0, upgrade = 0 },
			[TALISMAN_LOOT + 1] = { tier = 0, upgrade = 0 },
			[TALISMAN_BESTIARY + 1] = { tier = 0, upgrade = 0 },
		},
		preferredLists = {},
		rerollMode = REROLL_DAILY_CLAIMABLE,
		upgrade = 0,
	}

	-- Load from DB
	local resultId = db.storeQuery("SELECT * FROM `player_bounty_tasks` WHERE `player_id` = " .. playerGuid)
	if resultId ~= false then
		data.state = result.getDataInt(resultId, "state")
		data.difficulty = result.getDataInt(resultId, "difficulty")
		data.bountyPoints = result.getDataInt(resultId, "bounty_points")
		data.rerollTokens = result.getDataInt(resultId, "reroll_tokens")
		data.freeRerollTimestamp = result.getDataLong(resultId, "free_reroll")
		data.rerollMode = result.getDataInt(resultId, "reroll_mode")
		data.upgrade = result.getDataInt(resultId, "upgrade")

		-- Active task
		local activeRaceId = result.getDataInt(resultId, "active_raceid")
		if activeRaceId > 0 then
			data.activeTask = {
				raceId = activeRaceId,
				requiredKills = result.getDataInt(resultId, "active_required"),
				currentKills = result.getDataInt(resultId, "active_kills"),
				rewardExp = result.getDataInt(resultId, "active_reward_exp"),
				rewardBountyPoints = result.getDataInt(resultId, "active_reward_pts"),
				grade = result.getDataInt(resultId, "active_grade"),
				difficulty = result.getDataInt(resultId, "active_difficulty"),
				taskIndex = result.getDataInt(resultId, "active_index"),
				claimState = result.getDataInt(resultId, "active_claim_state"),
			}
			if data.state == STATE_COMPLETED and data.activeTask.claimState == CLAIM_REWARD_CLICKED then
				data.activeTask.claimState = CLAIM_REWARD_NO_CLICK
			end
		end

		-- Talismans
		data.talismans[TALISMAN_DAMAGE + 1].tier = result.getDataInt(resultId, "talisman_damage")
		data.talismans[TALISMAN_LIFELEECH + 1].tier = result.getDataInt(resultId, "talisman_lifeleech")
		data.talismans[TALISMAN_LOOT + 1].tier = result.getDataInt(resultId, "talisman_loot")
		data.talismans[TALISMAN_BESTIARY + 1].tier = result.getDataInt(resultId, "talisman_bestiary")
		data.talismans[TALISMAN_DAMAGE + 1].upgrade = result.getDataInt(resultId, "talisman_damage_upgrade")
		data.talismans[TALISMAN_LIFELEECH + 1].upgrade = result.getDataInt(resultId, "talisman_lifeleech_upgrade")
		data.talismans[TALISMAN_LOOT + 1].upgrade = result.getDataInt(resultId, "talisman_loot_upgrade")
		data.talismans[TALISMAN_BESTIARY + 1].upgrade = result.getDataInt(resultId, "talisman_bestiary_upgrade")

		-- Preferred lists
		local prefSuccess, prefData = pcall(function()
			return json.decode(result.getDataString(resultId, "preferred_lists") or "[]")
		end)
		data.preferredLists = (prefSuccess and type(prefData) == "table") and prefData or {}

		-- Creatures list
		local clSuccess, clData = pcall(function()
			return json.decode(result.getDataString(resultId, "creatures_list") or "[]")
		end)
		data.creaturesList = (clSuccess and type(clData) == "table") and clData or {}

		result.free(resultId)
	end

	-- Ensure preferred lists have 5 slots
	while #data.preferredLists < MAX_PREFERRED_SLOTS do
		data.preferredLists[#data.preferredLists + 1] = {
			active = false,
			preferredRaceId = 0,
			unwantedRaceId = 0,
		}
	end

	local hasActivePreferredSlot = false
	for _, slot in ipairs(data.preferredLists) do
		if slot.active then
			hasActivePreferredSlot = true
			break
		end
	end
	if not hasActivePreferredSlot then
		data.preferredLists[1].active = true
	end

	-- Initialize talisman defaults
	for i = 1, 4 do
		if not data.talismans[i] then
			data.talismans[i] = { tier = 0, upgrade = 0 }
		end
	end

	bountyCache[playerGuid] = data
	return data
end

local function saveBountyData(playerGuid)
	local data = bountyCache[playerGuid]
	if not data then return end

	local activeRaceId = 0
	local activeKills = 0
	local activeRequired = 0
	local activeRewardExp = 0
	local activeRewardPts = 0
	local activeGrade = 0
	local activeDifficulty = 0
	local activeIndex = 0
	local activeClaimState = 0

	if data.activeTask then
		activeRaceId = data.activeTask.raceId or 0
		activeKills = data.activeTask.currentKills or 0
		activeRequired = data.activeTask.requiredKills or 0
		activeRewardExp = data.activeTask.rewardExp or 0
		activeRewardPts = data.activeTask.rewardBountyPoints or 0
		activeGrade = data.activeTask.grade or 0
		activeDifficulty = data.activeTask.difficulty or 0
		activeIndex = data.activeTask.taskIndex or 0
		activeClaimState = data.activeTask.claimState or 0
	end

	local preferredJson = json.encode(data.preferredLists or {})
	local creaturesJson = json.encode(data.creaturesList or {})

	db.query(
		"INSERT INTO `player_bounty_tasks` (`player_id`, `state`, `difficulty`, `bounty_points`, `reroll_tokens`, " ..
		"`free_reroll`, `active_raceid`, `active_kills`, `active_required`, `active_reward_exp`, `active_reward_pts`, " ..
		"`active_grade`, `active_difficulty`, `active_index`, `active_claim_state`, " ..
		"`talisman_damage`, `talisman_lifeleech`, `talisman_loot`, `talisman_bestiary`, " ..
		"`talisman_damage_upgrade`, `talisman_lifeleech_upgrade`, `talisman_loot_upgrade`, `talisman_bestiary_upgrade`, " ..
		"`preferred_lists`, `creatures_list`, `reroll_mode`, `upgrade`) " ..
		"VALUES (" .. playerGuid .. ", " .. data.state .. ", " .. data.difficulty .. ", " .. data.bountyPoints .. ", " ..
		data.rerollTokens .. ", " .. data.freeRerollTimestamp .. ", " .. activeRaceId .. ", " .. activeKills .. ", " ..
		activeRequired .. ", " .. activeRewardExp .. ", " .. activeRewardPts .. ", " .. activeGrade .. ", " ..
		activeDifficulty .. ", " .. activeIndex .. ", " .. activeClaimState .. ", " ..
		(data.talismans[TALISMAN_DAMAGE + 1].tier or 0) .. ", " ..
		(data.talismans[TALISMAN_LIFELEECH + 1].tier or 0) .. ", " ..
		(data.talismans[TALISMAN_LOOT + 1].tier or 0) .. ", " ..
		(data.talismans[TALISMAN_BESTIARY + 1].tier or 0) .. ", " ..
		(data.talismans[TALISMAN_DAMAGE + 1].upgrade or 0) .. ", " ..
		(data.talismans[TALISMAN_LIFELEECH + 1].upgrade or 0) .. ", " ..
		(data.talismans[TALISMAN_LOOT + 1].upgrade or 0) .. ", " ..
		(data.talismans[TALISMAN_BESTIARY + 1].upgrade or 0) .. ", " ..
		db.escapeString(preferredJson) .. ", " .. db.escapeString(creaturesJson) .. ", " ..
		data.rerollMode .. ", " .. data.upgrade .. ") " ..
		"ON DUPLICATE KEY UPDATE `state` = VALUES(`state`), `difficulty` = VALUES(`difficulty`), " ..
		"`bounty_points` = VALUES(`bounty_points`), `reroll_tokens` = VALUES(`reroll_tokens`), " ..
		"`free_reroll` = VALUES(`free_reroll`), `active_raceid` = VALUES(`active_raceid`), " ..
		"`active_kills` = VALUES(`active_kills`), `active_required` = VALUES(`active_required`), " ..
		"`active_reward_exp` = VALUES(`active_reward_exp`), `active_reward_pts` = VALUES(`active_reward_pts`), " ..
		"`active_grade` = VALUES(`active_grade`), `active_difficulty` = VALUES(`active_difficulty`), " ..
		"`active_index` = VALUES(`active_index`), `active_claim_state` = VALUES(`active_claim_state`), " ..
		"`talisman_damage` = VALUES(`talisman_damage`), `talisman_lifeleech` = VALUES(`talisman_lifeleech`), " ..
		"`talisman_loot` = VALUES(`talisman_loot`), `talisman_bestiary` = VALUES(`talisman_bestiary`), " ..
		"`talisman_damage_upgrade` = VALUES(`talisman_damage_upgrade`), `talisman_lifeleech_upgrade` = VALUES(`talisman_lifeleech_upgrade`), " ..
		"`talisman_loot_upgrade` = VALUES(`talisman_loot_upgrade`), `talisman_bestiary_upgrade` = VALUES(`talisman_bestiary_upgrade`), " ..
		"`preferred_lists` = VALUES(`preferred_lists`), `creatures_list` = VALUES(`creatures_list`), " ..
		"`reroll_mode` = VALUES(`reroll_mode`), `upgrade` = VALUES(`upgrade`)"
	)
end

-- ============================================
-- CREATURE GENERATION
-- ============================================

-- Get eligible creatures for bounty tasks based on difficulty
local function getEligibleRaceIds(difficulty)
	if not CustomBestiary or not CustomBestiary.monstersByRaceId then
		return {}
	end

	local starFilter = STAR_FILTERS[difficulty]
	local eligible = {}

	for raceId, entry in pairs(CustomBestiary.monstersByRaceId) do
		local stars = entry.stars or 0
		if stars >= starFilter.min and stars <= starFilter.max then
			-- Match Crystal: task creatures must grant experience.
			if entry.name and entry.name ~= "" and (tonumber(entry.experience) or 0) > 0 then
				eligible[#eligible + 1] = raceId
			end
		end
	end

	return eligible
end

local function getPreferredCreatureRaceIds()
	if not CustomBestiary or not CustomBestiary.monstersByRaceId then
		return {}
	end

	local raceIds = {}
	for raceId, entry in pairs(CustomBestiary.monstersByRaceId) do
		local numericRaceId = tonumber(raceId)
		if numericRaceId and numericRaceId > 0 and numericRaceId <= 0xFFFF and
			entry.name and entry.name ~= "" and (tonumber(entry.experience) or 0) > 0 then
			raceIds[#raceIds + 1] = numericRaceId
		end
	end
	table.sort(raceIds)
	return raceIds
end

-- Check if a raceId is in unwanted list
local function isUnwanted(data, raceId)
	for _, slot in ipairs(data.preferredLists) do
		if slot.active and slot.unwantedRaceId == raceId then
			return true
		end
	end
	return false
end

-- Check if a raceId is in a preferred slot
local function isPreferredMatch(data, raceId)
	for _, slot in ipairs(data.preferredLists) do
		if slot.active and slot.preferredRaceId == raceId then
			return true
		end
	end
	return false
end

-- Pick creatures respecting preferred/unwanted
local function pickCreatures(difficulty, data, count)
	local eligible = getEligibleRaceIds(difficulty)
	if #eligible == 0 then
		return {}
	end

	-- First pass: try to include preferred creatures
	local picked = {}
	local pickedRaceIds = {}

	-- Gather preferred raceIds that are eligible
	for _, slot in ipairs(data.preferredLists) do
		if slot.active and slot.preferredRaceId > 0 then
			local raceId = slot.preferredRaceId
			if not pickedRaceIds[raceId] then
				-- Check if eligible
				for _, eligibleId in ipairs(eligible) do
					if eligibleId == raceId then
						picked[#picked + 1] = raceId
						pickedRaceIds[raceId] = true
						break
					end
				end
			end
		end
	end

	-- Second pass: fill remaining slots randomly
	local shuffled = {}
	for i = 1, #eligible do
		shuffled[i] = eligible[i]
	end
	-- Fisher-Yates shuffle
	for i = #shuffled, 2, -1 do
		local j = math.random(i)
		shuffled[i], shuffled[j] = shuffled[j], shuffled[i]
	end

	for i = 1, #shuffled do
		if #picked >= count then break end
		local raceId = shuffled[i]
		if not pickedRaceIds[raceId] and not isUnwanted(data, raceId) then
			picked[#picked + 1] = raceId
			pickedRaceIds[raceId] = true
		end
	end

	return picked
end

-- Generate creature list data for bounty
local function generateCreatureList(data)
	local difficulty = data.difficulty or DIFFICULTY_BEGINNER
	local killRange = KILL_RANGES[difficulty]
	local creatures = {}

	local raceIds = pickCreatures(difficulty, data, MAX_CREATURES)

	for i, raceId in ipairs(raceIds) do
		local entry = CustomBestiary and CustomBestiary.getMonster(raceId)
		local requiredKills = math.random(killRange.min, killRange.max)

		-- Grade calculation (~8% silver, ~2% gold)
		local gradeRoll = math.random(100)
		local grade = GRADE_NORMAL
		if gradeRoll <= 2 then
			grade = GRADE_GOLD
		elseif gradeRoll <= 10 then
			grade = GRADE_SILVER
		end

		-- Grade multiplier: silver=2x, gold=4x
		local gradeMult = 1
		if grade == GRADE_SILVER then gradeMult = 2
		elseif grade == GRADE_GOLD then gradeMult = 4 end

		local baseExp = entry and (entry.experience or 0) * requiredKills or 0
		local rewardExp = math.min(math.floor((baseExp * 0.15) * gradeMult), 65535) -- 15% of total exp, U16 max
		local bountyPts = math.min(math.floor(requiredKills / 10) * gradeMult, 65535)

		creatures[#creatures + 1] = {
			raceId = raceId,
			kills = 0,
			required = requiredKills,
			reward = rewardExp,
			bountyPts = bountyPts,
			grade = grade,
			claimState = CLAIM_SELECT_TASK,
			index = i - 1,
		}
	end

	-- Pad to 3 entries with empty slots
	while #creatures < MAX_CREATURES do
		creatures[#creatures + 1] = {
			raceId = 0,
			kills = 0,
			required = 0,
			reward = 0,
			bountyPts = 0,
			grade = 0,
			claimState = 0,
			index = #creatures,
		}
	end

	return creatures
end

-- ============================================
-- BOUNTY TASK ACTIONS
-- ============================================

function BountyTasks.openBounty(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)

	-- Preserve active/completed states — only generate new list when idle or finished claiming
	if data.state == STATE_SELECTION or data.state == STATE_ACTIVE or data.state == STATE_COMPLETED then
		return BountyTasks.sendBountyData(player)
	end

	-- Generate new creature list (only when state is STATE_NONE)
	data.state = STATE_SELECTION
	data.creaturesList = generateCreatureList(data)

	saveBountyData(playerGuid)
	return BountyTasks.sendBountyData(player)
end

function BountyTasks.onLogin(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)

	sendBountyBalances(player, data)

	-- Do not generate a new selection list on login just for the tracker.
	-- Only push live state that already exists, so Kill Tracker can show it
	-- without requiring the player to reopen the Task Hunt window.
	if data.state == STATE_ACTIVE or data.state == STATE_COMPLETED then
		return BountyTasks.sendBountyData(player)
	end

	return true
end

function BountyTasks.preload(player)
	if not player then
		return false
	end
	loadBountyData(getPlayerGuid(player))
	return true
end

function BountyTasks.changeDifficulty(player, difficulty)
	if difficulty == nil or difficulty < 0 or difficulty > 3 then
		return false
	end

	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)

	-- Can only change if not in active state
	if data.state == STATE_ACTIVE then
		return false
	end

	data.difficulty = difficulty
	data.state = STATE_NONE -- Reset to generate new list

	saveBountyData(playerGuid)
	return BountyTasks.openBounty(player)
end

function BountyTasks.rerollTasks(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)

	if data.state == STATE_ACTIVE then
		return false -- Can't reroll while hunting
	end

	-- Check for free daily reroll
	local now = os.time()
	if data.freeRerollTimestamp > 0 and now >= data.freeRerollTimestamp then
		-- Free reroll is available
		data.freeRerollTimestamp = now + FREE_REROLL_COOLDOWN
		data.rerollMode = REROLL_TIMER_RUNNING
	elseif data.rerollTokens > 0 then
		data.rerollTokens = data.rerollTokens - 1
		if data.rerollTokens <= 0 then
			data.rerollMode = REROLL_LIMIT_REACHED
		end
	else
		return false -- No rerolls available
	end

	data.state = STATE_NONE
	data.creaturesList = generateCreatureList(data)

	saveBountyData(playerGuid)
	sendRerollBalance(player, data)
	return BountyTasks.openBounty(player)
end

function BountyTasks.claimDailyReroll(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)

	local now = os.time()
	if data.freeRerollTimestamp > 0 and now >= data.freeRerollTimestamp then
		data.freeRerollTimestamp = now + FREE_REROLL_COOLDOWN
		data.rerollMode = REROLL_TIMER_RUNNING
		data.rerollTokens = math.min(data.rerollTokens + 1, MAX_REROLL_TOKENS)
		saveBountyData(playerGuid)
		sendRerollBalance(player, data)
		return BountyTasks.sendBountyData(player)
	end

	return false
end

function BountyTasks.selectTask(player, taskIndex)
	if taskIndex == nil or taskIndex < 0 or taskIndex >= MAX_CREATURES then
		return false
	end

	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)

	if data.state ~= STATE_SELECTION then
		return false
	end

	local creature = data.creaturesList[taskIndex + 1]
	if not creature or creature.raceId == 0 then
		return false
	end

	-- Set as active task
	data.activeTask = {
		raceId = creature.raceId,
		requiredKills = creature.required,
		currentKills = 0,
		rewardExp = creature.reward,
		rewardBountyPoints = creature.bountyPts,
		grade = creature.grade,
		difficulty = data.difficulty,
		taskIndex = taskIndex,
		claimState = CLAIM_REWARD_NO_CLICK,
	}
	data.state = STATE_ACTIVE
	data.creaturesList = {}

	saveBountyData(playerGuid)
	return BountyTasks.sendBountyData(player)
end

function BountyTasks.claimReward(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)
	syncBountyBalance(player, data)

	if data.state ~= STATE_ACTIVE and data.state ~= STATE_COMPLETED then return false end

	local active = data.activeTask
	if not active then return false end

	if active.currentKills < active.requiredKills then
		return false -- Not done yet
	end

	-- Give rewards
	local rewardExp = active.rewardExp or 0
	local rewardBountyPts = active.rewardBountyPoints or 0
	local rerollsBefore = data.rerollTokens or 0
	local rerollsAfter = math.min(rerollsBefore + BOUNTY_REWARD_REROLL_TOKENS, MAX_REROLL_TOKENS)
	local rewardRerolls = rerollsAfter - rerollsBefore

	if rewardExp > 0 then
		player:addExperience(rewardExp, true)
	end
	if rewardBountyPts > 0 then
		player:addBountyPoints(rewardBountyPts)
	end
	data.rerollTokens = rerollsAfter
	if data.rerollTokens >= MAX_REROLL_TOKENS then
		data.rerollMode = REROLL_LIMIT_REACHED
	elseif data.rerollTokens > 0 and data.rerollMode == REROLL_LIMIT_REACHED then
		data.rerollMode = REROLL_TIMER_RUNNING
	end
	data.bountyPoints = player:getBountyPoints()

	player:sendTextMessage(MESSAGE_STATUS_DEFAULT, string.format(
		"You have claimed your bounty task reward! (+%d exp, +%d bounty points, +%d reroll token%s)",
		rewardExp,
		rewardBountyPts,
		rewardRerolls,
		rewardRerolls == 1 and "" or "s"))

	-- Reset state
	data.state = STATE_NONE
	data.activeTask = nil

	saveBountyData(playerGuid)

	-- Notify client of new bounty points
	sendBountyBalances(player, data)

	-- Generate new creature list immediately (like Crystal Server)
	BountyTasks.openBounty(player)

	return true
end

function BountyTasks.onKill(player, raceId)
	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)

	if data.state ~= STATE_ACTIVE then return false end
	if not data.activeTask then return false end
	if data.activeTask.raceId ~= raceId then return false end

	local killMultiplier = TaskBoard.getBountyKillMultiplier(player)
	local previousKills = data.activeTask.currentKills or 0
	local requiredKills = data.activeTask.requiredKills or 0
	local currentKills = math.min(previousKills + killMultiplier, requiredKills)
	if currentKills <= previousKills then
		return false
	end

	data.activeTask.currentKills = currentKills

	local completedNow = previousKills < requiredKills and currentKills >= requiredKills
	if completedNow then
		data.activeTask.claimState = CLAIM_REWARD_NO_CLICK
		data.state = STATE_COMPLETED
	end

	if shouldPersistKillProgress(previousKills, currentKills, completedNow) then
		saveBountyData(playerGuid)
	end

	if protocol and protocol.sendBountyKillUpdate then
		protocol.sendBountyKillUpdate(player, raceId, currentKills, requiredKills, completedNow)
	elseif completedNow then
		BountyTasks.sendBountyData(player)
	end
	return true
end

-- ============================================
-- PREFERRED / UNWANTED LISTS
-- ============================================

function BountyTasks.unlockPreferredSlot(player, _)
	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)
	syncBountyBalance(player, data)

	local slot = nil
	for i = 1, MAX_PREFERRED_SLOTS do
		if data.preferredLists[i] and not data.preferredLists[i].active then
			slot = i
			break
		end
	end
	if not slot then
		return false
	end

	local cost = PREFERRED_SLOT_COSTS[slot] or 0
	if cost > 0 then
		if not player:removeBountyPoints(cost) then
			return false
		end
		protocol.sendResourceBalance(player, protocol.RESOURCE_BOUNTY_POINTS, player:getBountyPoints())
	end

	data.preferredLists[slot].active = true
	data.bountyPoints = player:getBountyPoints()
	saveBountyData(playerGuid)
	BountyTasks.sendBountyData(player)

	return true
end

function BountyTasks.clearPreferred(player, slot)
	if slot == nil or slot < 1 or slot > MAX_PREFERRED_SLOTS then return false end

	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)
	syncBountyBalance(player, data)

	if not data.preferredLists[slot] or not data.preferredLists[slot].active then
		return false
	end
	if (data.preferredLists[slot].preferredRaceId or 0) <= 0 then
		return false
	end
	if not player:removeBountyPoints(PREFERRED_CLEAR_COST) then
		return false
	end
	protocol.sendResourceBalance(player, protocol.RESOURCE_BOUNTY_POINTS, player:getBountyPoints())

	data.preferredLists[slot].preferredRaceId = 0
	data.bountyPoints = player:getBountyPoints()
	saveBountyData(playerGuid)
	return BountyTasks.sendBountyData(player)
end

function BountyTasks.clearUnwanted(player, slot)
	if slot == nil or slot < 1 or slot > MAX_PREFERRED_SLOTS then return false end

	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)
	syncBountyBalance(player, data)

	if not data.preferredLists[slot] or not data.preferredLists[slot].active then
		return false
	end
	if (data.preferredLists[slot].unwantedRaceId or 0) <= 0 then
		return false
	end
	if not player:removeBountyPoints(PREFERRED_CLEAR_COST) then
		return false
	end
	protocol.sendResourceBalance(player, protocol.RESOURCE_BOUNTY_POINTS, player:getBountyPoints())

	data.preferredLists[slot].unwantedRaceId = 0
	data.bountyPoints = player:getBountyPoints()
	saveBountyData(playerGuid)
	return BountyTasks.sendBountyData(player)
end

function BountyTasks.assignPreferred(player, slot, raceId)
	if slot == nil or slot < 1 or slot > MAX_PREFERRED_SLOTS then return false end
	if raceId == nil or raceId <= 0 then return false end

	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)
	syncBountyBalance(player, data)

	if not data.preferredLists[slot] or not data.preferredLists[slot].active then
		return false
	end

	-- Validate raceId exists
	if CustomBestiary and not CustomBestiary.getMonster(raceId) then
		return false
	end

	data.preferredLists[slot].preferredRaceId = raceId
	saveBountyData(playerGuid)
	return BountyTasks.sendBountyData(player)
end

function BountyTasks.assignUnwanted(player, slot, raceId)
	if slot == nil or slot < 1 or slot > MAX_PREFERRED_SLOTS then return false end
	if raceId == nil or raceId <= 0 then return false end

	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)
	syncBountyBalance(player, data)

	if not data.preferredLists[slot] or not data.preferredLists[slot].active then
		return false
	end

	-- Validate raceId exists
	if CustomBestiary and not CustomBestiary.getMonster(raceId) then
		return false
	end

	data.preferredLists[slot].unwantedRaceId = raceId
	saveBountyData(playerGuid)
	return BountyTasks.sendBountyData(player)
end

-- ============================================
-- TALISMAN
-- ============================================

local function getTalismanUpgradeCost(pathIndex, data)
	local talisman = data.talismans[pathIndex + 1]
	local currentTier = talisman.tier or 0
	return 5 + (currentTier * 12)
end

local function getTalismanMaxLevel(pathIndex)
	return pathIndex == TALISMAN_BESTIARY and TALISMAN_BESTIARY_MAX_LEVEL or TALISMAN_STANDARD_MAX_LEVEL
end

local function getTalismanBonusHundredths(level, pathIndex)
	level = math.max(0, tonumber(level) or 0)
	if level == 0 then
		return 0
	end

	if pathIndex == TALISMAN_BESTIARY then
		if level <= 20 then
			return level * 100
		end
		return math.min(2000 + ((level - 20) * 50), 10000)
	end

	if level <= 26 then
		return 250 + ((level - 1) * 50)
	end
	return math.min(1500 + ((level - 26) * 25), 5000)
end

function BountyTasks.getTalismanBonus(player, raceId, pathIndex)
	pathIndex = tonumber(pathIndex)
	raceId = tonumber(raceId) or 0
	if not player or pathIndex == nil or pathIndex < 0 or pathIndex > 3 or raceId <= 0 then
		return 0
	end
	if not isBountyTalismanEquipped(player) then
		return 0
	end

	local data = loadBountyData(getPlayerGuid(player))
	local activeTask = data.activeTask
	if data.state ~= STATE_ACTIVE or not activeTask or activeTask.raceId ~= raceId then
		return 0
	end

	local talisman = data.talismans[pathIndex + 1]
	return getTalismanBonusHundredths(talisman and talisman.tier or 0, pathIndex)
end

function BountyTasks.talismanUpgrade(player, pathIndex)
	if pathIndex == nil or pathIndex < 0 or pathIndex > 3 then
		return false
	end

	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)
	syncBountyBalance(player, data)

	local talisman = data.talismans[pathIndex + 1]
	if not talisman then return false end

	if talisman.tier >= getTalismanMaxLevel(pathIndex) then
		return false -- Already at cap
	end

	local cost = getTalismanUpgradeCost(pathIndex, data)
	if player:getBountyPoints() < cost then
		return false
	end

	if not player:removeBountyPoints(cost) then
		return false
	end
	protocol.sendResourceBalance(player, protocol.RESOURCE_BOUNTY_POINTS, player:getBountyPoints())

	data.bountyPoints = player:getBountyPoints()
	talisman.tier = talisman.tier + 1
	talisman.upgrade = 1

	saveBountyData(playerGuid)
	BountyTasks.sendBountyData(player)

	return true
end

-- ============================================
-- SEND TO CLIENT
-- ============================================

function BountyTasks.sendBountyData(player, includeAvailableCreatures)
	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)
	sendBountyBalances(player, data)

	-- Build creature list for protocol
	local creatures = {}
	if data.state == STATE_SELECTION and #data.creaturesList > 0 then
		for i = 1, MAX_CREATURES do
			local c = data.creaturesList[i]
			if c then
				creatures[i] = {
					raceId = c.raceId,
					kills = c.kills or 0,
					required = c.required or 0,
					reward = c.reward or 0,
					bountyPts = c.bountyPts or 0,
					grade = c.grade or 0,
					claimState = c.claimState or 0,
					index = c.index or (i - 1),
				}
			else
				creatures[i] = { raceId = 0, kills = 0, required = 0, reward = 0, bountyPts = 0, grade = 0, claimState = 0, index = i - 1 }
			end
		end
	elseif (data.state == STATE_ACTIVE or data.state == STATE_COMPLETED) and data.activeTask then
		-- Show only the active task
		for i = 1, MAX_CREATURES do
			if i == 1 then
				local a = data.activeTask
				creatures[i] = {
					raceId = a.raceId,
					kills = a.currentKills or 0,
					required = a.requiredKills or 0,
					reward = a.rewardExp or 0,
					bountyPts = a.rewardBountyPoints or 0,
					grade = a.grade or 0,
					claimState = a.claimState or 0,
					index = a.taskIndex or 0,
				}
			else
				creatures[i] = { raceId = 0, kills = 0, required = 0, reward = 0, bountyPts = 0, grade = 0, claimState = 0, index = i - 1 }
			end
		end
	else
		for i = 1, MAX_CREATURES do
			creatures[i] = { raceId = 0, kills = 0, required = 0, reward = 0, bountyPts = 0, grade = 0, claimState = 0, index = i - 1 }
		end
	end

	-- Build talisman data
	local talismans = {}
	for i = 1, 4 do
		local t = data.talismans[i] or { tier = 0, upgrade = 0 }
		local reachedCap = t.tier >= getTalismanMaxLevel(i - 1)

		talismans[i] = {
			tier1 = t.tier,
			tier2 = 0,
			upgrade = reachedCap and 0 or 1,
			ptsToUpgrade = reachedCap and 0 or getTalismanUpgradeCost(i - 1, data),
		}
	end

	-- Build protocol data
	local protocolData = {
		state = data.state,
		difficulty = data.difficulty,
		creatures = creatures,
		rerollTokens = data.rerollTokens,
		rerollMode = data.rerollMode,
		rerollTimestamp = data.freeRerollTimestamp,
		upgrade = data.upgrade or 0,
		talismans = talismans,
		preferredSlots = MAX_PREFERRED_SLOTS,
		preferred = data.preferredLists,
		preferredClearCost = PREFERRED_CLEAR_COST,
	}
	if includeAvailableCreatures then
		protocolData.availableRaceIds = getPreferredCreatureRaceIds()
	end

	return protocol.sendBountyTaskData(player, protocolData)
end

-- ============================================
-- SETUP / INIT
-- ============================================

function BountyTasks.setProtocol(protoModule)
	protocol = protoModule
end

function BountyTasks.saveOnLogout(player)
	local playerGuid = getPlayerGuid(player)
	local data = loadBountyData(playerGuid)
	syncBountyBalance(player, data)
	saveBountyData(playerGuid)
	invalidateCache(playerGuid)
end

-- Expose internal loader for C++ sync on login
function BountyTasks.loadBountyData(playerGuid)
	return loadBountyData(playerGuid)
end

return BountyTasks
