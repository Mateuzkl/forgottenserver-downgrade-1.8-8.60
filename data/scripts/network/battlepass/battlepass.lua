BattlePassSystem = BattlePassSystem or {}

local BATTLEPASS_OPCODE = 225
local RESOURCE_BALANCE_OPCODE = 0xEE
local RESOURCE_BANK = 0
local RESOURCE_INVENTORY = 1
local EXTENDED_OPCODE_MAX_STRING_LENGTH = 8192
local REWARD_STEPS_PER_CHUNK = 20

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local DAY_SECONDS = 24 * 60 * 60
local WEEK_SECONDS = 7 * DAY_SECONDS

local config = {
	seasonAnchor = os.time({ year = 2024, month = 1, day = 1, hour = 10, min = 0, sec = 0 }),
	seasonWeeks = 5,
	maxStep = 50,
	pointsPerStep = 100,
	dailyRerollBasePrice = 1000,
	deluxePrice = 250,
}

local REQUEST_COOLDOWN_SECONDS = 1
local rateLimitedActions = {
	getMissions = true,
	getRewards = true,
}
local lastRequest = {}

local freeRewardSteps = {
	[3] = true, [6] = true, [9] = true, [12] = true, [15] = true, [18] = true,
	[21] = true, [24] = true, [27] = true, [30] = true, [33] = true, [36] = true,
	[39] = true, [42] = true, [45] = true, [48] = true, [49] = true, [50] = true,
}

local dailyMissionPool = {
	{ id = "daily_any_50", name = "Daily Hunt", description = "Kill 50 creatures.", rewardPoints = 50, maxProgress = 50, targets = "*" },
	{ id = "daily_any_100", name = "Daily Grinder", description = "Kill 100 creatures.", rewardPoints = 75, maxProgress = 100, targets = "*" },
	{ id = "daily_rotworm", name = "Rotworm Cleanup", description = "Kill 40 rotworms.", rewardPoints = 50, maxProgress = 40, targets = { "rotworm", "carrion worm" } },
	{ id = "daily_troll", name = "Troll Patrol", description = "Kill 40 trolls.", rewardPoints = 50, maxProgress = 40, targets = { "troll", "swamp troll", "frost troll" } },
	{ id = "daily_orc", name = "Orc Skirmish", description = "Kill 40 orcs.", rewardPoints = 60, maxProgress = 40, targets = { "orc", "orc spearman", "orc warrior", "orc berserker", "orc leader" } },
	{ id = "daily_cyclops", name = "One-Eyed Trouble", description = "Kill 20 cyclops.", rewardPoints = 60, maxProgress = 20, targets = { "cyclops", "cyclops smith", "cyclops drone" } },
	{ id = "daily_dragon", name = "Dragon Pressure", description = "Kill 10 dragons or dragon lords.", rewardPoints = 75, maxProgress = 10, targets = { "dragon", "dragon lord" } },
}

local generalMissions = {
	{ id = "season_any_150", name = "Fresh Start", description = "Kill 150 creatures during this season.", rewardPoints = 100, maxProgress = 150, targets = "*" },
	{ id = "season_rotworm_120", name = "Tunnel Sweep", description = "Kill 120 rotworms.", rewardPoints = 100, maxProgress = 120, targets = { "rotworm", "carrion worm" } },
	{ id = "season_troll_120", name = "Troll Breaker", description = "Kill 120 trolls.", rewardPoints = 100, maxProgress = 120, targets = { "troll", "swamp troll", "frost troll" } },
	{ id = "season_goblin_120", name = "Goblin Control", description = "Kill 120 goblins.", rewardPoints = 100, maxProgress = 120, targets = { "goblin", "goblin assassin", "goblin leader", "goblin scavenger" } },
	{ id = "season_minotaur_120", name = "Maze Breaker", description = "Kill 120 minotaurs.", rewardPoints = 100, maxProgress = 120, targets = { "minotaur", "minotaur archer", "minotaur guard", "minotaur mage" } },
	{ id = "season_orc_150", name = "Orc Campaign", description = "Kill 150 orcs.", rewardPoints = 100, maxProgress = 150, targets = { "orc", "orc spearman", "orc warrior", "orc berserker", "orc leader", "orc warlord" } },
	{ id = "season_dwarf_120", name = "Dwarf Advance", description = "Kill 120 dwarves.", rewardPoints = 100, maxProgress = 120, targets = { "dwarf", "dwarf soldier", "dwarf guard", "dwarf geomancer" } },
	{ id = "season_amazon_100", name = "Amazon Trail", description = "Kill 100 amazons or valkyries.", rewardPoints = 100, maxProgress = 100, targets = { "amazon", "valkyrie" } },
	{ id = "season_undead_150", name = "Restless Dead", description = "Kill 150 undead creatures.", rewardPoints = 100, maxProgress = 150, targets = { "skeleton", "ghoul", "crypt shambler", "mummy", "vampire", "lich" } },
	{ id = "season_larva_120", name = "Desert Nest", description = "Kill 120 larvas or scarabs.", rewardPoints = 100, maxProgress = 120, targets = { "larva", "scarab", "ancient scarab" } },
	{ id = "season_slime_80", name = "Slime Splitter", description = "Kill 80 slimes.", rewardPoints = 100, maxProgress = 80, targets = { "slime" } },

	{ id = "season_any_350", name = "Battle Routine", description = "Kill 350 creatures during this season.", rewardPoints = 200, maxProgress = 350, targets = "*" },
	{ id = "season_cyclops_100", name = "Cyclops Camp", description = "Kill 100 cyclops.", rewardPoints = 200, maxProgress = 100, targets = { "cyclops", "cyclops smith", "cyclops drone" } },
	{ id = "season_dragon_80", name = "Dragon Hunter", description = "Kill 80 dragons.", rewardPoints = 200, maxProgress = 80, targets = { "dragon" } },
	{ id = "season_gs_40", name = "Web Cleaner", description = "Kill 40 giant spiders.", rewardPoints = 200, maxProgress = 40, targets = { "giant spider" } },
	{ id = "season_vampire_60", name = "Night Watch", description = "Kill 60 vampires.", rewardPoints = 200, maxProgress = 60, targets = { "vampire", "vampire bride", "vampire viscount" } },
	{ id = "season_necro_60", name = "Necromancer Hunt", description = "Kill 60 necromancers or priests.", rewardPoints = 200, maxProgress = 60, targets = { "necromancer", "priestess", "blood priest" } },
	{ id = "season_hero_50", name = "Hero Trial", description = "Kill 50 heroes or black knights.", rewardPoints = 200, maxProgress = 50, targets = { "hero", "black knight" } },
	{ id = "season_beholder_80", name = "Evil Eyes", description = "Kill 80 beholders.", rewardPoints = 200, maxProgress = 80, targets = { "beholder", "elder beholder", "bonelord", "elder bonelord" } },
	{ id = "season_dragon_lord_40", name = "Dragon Lord Hunt", description = "Kill 40 dragon lords.", rewardPoints = 200, maxProgress = 40, targets = { "dragon lord" } },
	{ id = "season_hydra_30", name = "Hydra Heads", description = "Kill 30 hydras.", rewardPoints = 200, maxProgress = 30, targets = { "hydra" } },
	{ id = "season_serpent_30", name = "Serpent Strike", description = "Kill 30 serpent spawns.", rewardPoints = 200, maxProgress = 30, targets = { "serpent spawn" } },

	{ id = "season_any_800", name = "Season Veteran", description = "Kill 800 creatures during this season.", rewardPoints = 300, maxProgress = 800, targets = "*" },
	{ id = "season_demon_25", name = "Demon Contract", description = "Kill 25 demons.", rewardPoints = 300, maxProgress = 25, targets = { "demon" } },
	{ id = "season_dragon_family_200", name = "Wyrm Scale", description = "Kill 200 dragons, dragon lords or wyrms.", rewardPoints = 300, maxProgress = 200, targets = { "dragon", "dragon lord", "wyrm" } },
	{ id = "season_strong_120", name = "Stronghold Breaker", description = "Kill 120 strong creatures.", rewardPoints = 300, maxProgress = 120, targets = { "warlock", "demon", "hydra", "serpent spawn", "frost dragon", "behemoth" } },
}

local missionById = {}
for _, mission in ipairs(dailyMissionPool) do
	missionById[mission.id] = mission
end
for _, mission in ipairs(generalMissions) do
	missionById[mission.id] = mission
end

local function normalizeName(name)
	return tostring(name or ""):lower()
end

local function clamp(value, minValue, maxValue)
	value = tonumber(value) or 0
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function getSeason()
	local now = os.time()
	local seasonLength = config.seasonWeeks * WEEK_SECONDS
	local seasonIndex = 1

	if now >= config.seasonAnchor then
		seasonIndex = math.floor((now - config.seasonAnchor) / seasonLength) + 1
	end

	local beginTime = config.seasonAnchor + ((seasonIndex - 1) * seasonLength)
	return {
		id = "astra-season-" .. seasonIndex,
		beginTime = beginTime,
		endTime = beginTime + seasonLength,
	}
end

local function getDailyWindow()
	local now = os.time()
	local date = os.date("*t", now)
	local beginTime = os.time({ year = date.year, month = date.month, day = date.day, hour = 10, min = 0, sec = 0 })
	if now < beginTime then
		beginTime = beginTime - DAY_SECONDS
	end

	return {
		key = os.date("%Y%m%d", beginTime),
		beginTime = beginTime,
		endTime = beginTime + DAY_SECONDS,
	}
end

local function getStore(player)
	return player:kv():scoped("battlepass")
end

local function ensureStateTables(state)
	state.generalProgress = type(state.generalProgress) == "table" and state.generalProgress or {}
	state.generalAwarded = type(state.generalAwarded) == "table" and state.generalAwarded or {}
	state.dailyProgress = type(state.dailyProgress) == "table" and state.dailyProgress or {}
	state.dailyAwarded = type(state.dailyAwarded) == "table" and state.dailyAwarded or {}
	state.dailySlots = type(state.dailySlots) == "table" and state.dailySlots or {}
	state.claimed = type(state.claimed) == "table" and state.claimed or {}
	state.points = clamp(state.points, 0, config.maxStep * config.pointsPerStep)
	state.rerollCounter = tonumber(state.rerollCounter) or 0
	state.premium = state.premium == true
end

local function resetStateForSeason(season)
	return {
		seasonId = season.id,
		points = 0,
		premium = false,
		generalProgress = {},
		generalAwarded = {},
		dailyKey = "",
		dailySlots = {},
		dailyProgress = {},
		dailyAwarded = {},
		claimed = {},
		rerollCounter = 0,
	}
end

local function loadState(player)
	local store = getStore(player)
	local season = getSeason()
	local daily = getDailyWindow()
	local state = store:get("state")

	if type(state) ~= "table" or state.seasonId ~= season.id then
		state = resetStateForSeason(season)
	else
		ensureStateTables(state)
	end

	if state.dailyKey ~= daily.key then
		state.dailyKey = daily.key
		state.dailySlots = {}
		state.dailyProgress = {}
		state.dailyAwarded = {}
	end

	ensureStateTables(state)
	return state, store, season, daily
end

local function saveState(store, state)
	store:set("state", state)
end

local function getMissionProgress(state, mission, daily)
	local source = daily and state.dailyProgress or state.generalProgress
	return clamp(source[mission.id], 0, mission.maxProgress)
end

local function setMissionProgress(state, mission, progress, daily)
	local source = daily and state.dailyProgress or state.generalProgress
	source[mission.id] = clamp(progress, 0, mission.maxProgress)
end

local function wasMissionAwarded(state, mission, daily)
	local source = daily and state.dailyAwarded or state.generalAwarded
	return source[mission.id] == true
end

local function setMissionAwarded(state, mission, daily)
	local source = daily and state.dailyAwarded or state.generalAwarded
	source[mission.id] = true
end

local function addBattlePassPoints(state, amount)
	local maxPoints = config.maxStep * config.pointsPerStep
	state.points = clamp((tonumber(state.points) or 0) + amount, 0, maxPoints)
end

local function missionMatches(mission, monsterName)
	if mission.targets == "*" then
		return true
	end

	local normalized = normalizeName(monsterName)
	for _, targetName in ipairs(mission.targets or {}) do
		if normalized == normalizeName(targetName) then
			return true
		end
	end
	return false
end

local function getMissionPayload(state, mission, daily)
	local progress = getMissionProgress(state, mission, daily)
	return {
		missionId = mission.id,
		missionName = mission.name,
		missionDescription = mission.description,
		currentProgress = progress,
		maxProgress = mission.maxProgress,
		rewardPoints = mission.rewardPoints,
	}
end

local function getDailyMissionBySlot(state, slot, dailyKey)
	if not state.dailySlots[tostring(slot)] then
		local daySeed = tonumber(dailyKey) or math.floor(os.time() / DAY_SECONDS)
		local firstIndex = (daySeed % #dailyMissionPool) + 1
		local secondIndex = ((firstIndex + 3) % #dailyMissionPool) + 1

		state.dailySlots["1"] = dailyMissionPool[firstIndex].id
		state.dailySlots["2"] = dailyMissionPool[secondIndex].id
	end

	return missionById[state.dailySlots[tostring(slot)]]
end

local function getActiveDailyMissions(state, dailyKey)
	return {
		getDailyMissionBySlot(state, 1, dailyKey),
		getDailyMissionBySlot(state, 2, dailyKey),
	}
end

local function getPlayerOutfitPayload(player)
	local outfit = player:getOutfit()
	return {
		type = outfit.lookType or outfit.type or 0,
		head = outfit.lookHead or outfit.head or 0,
		body = outfit.lookBody or outfit.body or 0,
		legs = outfit.lookLegs or outfit.legs or 0,
		feet = outfit.lookFeet or outfit.feet or 0,
		addons = outfit.lookAddons or outfit.addons or 0,
	}
end

local function isPremiumActive(state)
	return state.premium == true
end

local function getCurrentRewardStep(points)
	return math.min(config.maxStep, math.floor((tonumber(points) or 0) / config.pointsPerStep))
end

local function buildMissionsPayload(player, state, season, daily)
	local currentRewardStep = getCurrentRewardStep(state.points)
	local nextStepPoints = math.min((currentRewardStep + 1) * config.pointsPerStep, config.maxStep * config.pointsPerStep)

	local dailyMissions = {}
	for _, mission in ipairs(getActiveDailyMissions(state, daily.key)) do
		if mission then
			table.insert(dailyMissions, getMissionPayload(state, mission, true))
		end
	end

	local generalPayload = {}
	for _, mission in ipairs(generalMissions) do
		table.insert(generalPayload, getMissionPayload(state, mission, false))
	end

	return {
		playerOutfit = getPlayerOutfitPayload(player),
		beginTime = season.beginTime,
		endTime = season.endTime,
		points = state.points,
		rerollPrice = config.dailyRerollBasePrice,
		deluxePrice = config.deluxePrice,
		battlePassActive = isPremiumActive(state),
		currentRewardStep = currentRewardStep,
		nextStepPoints = nextStepPoints,
		dailyBeginTime = daily.beginTime,
		dailyEndTime = daily.endTime,
		dailyMissions = dailyMissions,
		generalMissions = generalPayload,
	}
end

local function sendOpcode(player, action, data)
	if not player or not player.sendExtendedOpcode then
		return false
	end

	local ok, buffer = pcall(json.encode, {
		action = action,
		data = data or {},
	})
	if not ok or type(buffer) ~= "string" then
		return false
	end

	if #buffer > EXTENDED_OPCODE_MAX_STRING_LENGTH then
		print(string.format("[Battle Pass] Refusing oversized extended opcode payload (%s, %d bytes).", action, #buffer))
		return false
	end

	return player:sendExtendedOpcode(BATTLEPASS_OPCODE, buffer)
end

local function sendResourceBalance(player, resourceType, value)
	if not supportsCustomNetwork(player) then
		return false
	end

	local msg = NetworkMessage(player)
	msg:addByte(RESOURCE_BALANCE_OPCODE)
	msg:addByte(resourceType)
	msg:addU64(math.max(0, tonumber(value) or 0))
	return msg:sendToPlayer(player)
end

local function sendMoneyResources(player)
	local bankSent = sendResourceBalance(player, RESOURCE_BANK, player:getBankBalance())
	local inventorySent = sendResourceBalance(player, RESOURCE_INVENTORY, player:getMoney())
	return bankSent and inventorySent
end

local function sendMissionState(player, state, season, daily)
	local payload = buildMissionsPayload(player, state, season, daily)
	sendMoneyResources(player)
	return sendOpcode(player, "missions", payload)
end

function BattlePassSystem.sendMissions(player)
	local state, store, season, daily = loadState(player)
	saveState(store, state)
	return sendMissionState(player, state, season, daily)
end

local function makeReward(step, freeReward)
	local rewardId = step * 10 + (freeReward and 1 or 2)
	local count = freeReward and math.max(1, math.floor(step / 6)) or math.max(1, math.floor(step / 3))

	if step >= 45 then
		count = count + (freeReward and 2 or 5)
	elseif step >= 30 then
		count = count + (freeReward and 1 or 3)
	end

	return {
		rewardId = rewardId,
		rewardType = 1,
		freeReward = freeReward,
		-- TODO: Replace the placeholder Crystal Coin rewards with the final season reward table.
		itemId = 3043,
		count = count,
		charges = 0,
		stuck = false,
	}
end

local function setRewardClaimState(reward, state)
	local claimed = state.claimed[tostring(reward.rewardId)] == true
	reward.hasClaimedReward = claimed
	reward.hasClamedReward = claimed
end

local function buildRewardSteps(state)
	local steps = {}
	for step = 1, config.maxStep do
		local rewards = {}

		if freeRewardSteps[step] then
			local freeReward = makeReward(step, true)
			setRewardClaimState(freeReward, state)
			table.insert(rewards, freeReward)
		end

		local premiumReward = makeReward(step, false)
		setRewardClaimState(premiumReward, state)
		table.insert(rewards, premiumReward)

		table.insert(steps, {
			stepId = step,
			rewards = rewards,
		})
	end
	return steps
end

function BattlePassSystem.sendRewards(player)
	local state, store = loadState(player)
	local rewards = buildRewardSteps(state)
	saveState(store, state)

	if #rewards == 0 then
		return sendOpcode(player, "rewards", rewards)
	end

	local sent = false
	for first = 1, #rewards, REWARD_STEPS_PER_CHUNK do
		local steps = {}
		for index = first, math.min(first + REWARD_STEPS_PER_CHUNK - 1, #rewards) do
			table.insert(steps, rewards[index])
		end

		sent = sendOpcode(player, "rewards", {
			chunk = true,
			first = first,
			total = #rewards,
			steps = steps,
		}) or sent
	end
	return sent
end

local function findReward(step, rewardId)
	step = tonumber(step) or 0
	rewardId = tonumber(rewardId) or 0
	if step < 1 or step > config.maxStep then
		return nil
	end

	if freeRewardSteps[step] then
		local freeReward = makeReward(step, true)
		if freeReward.rewardId == rewardId then
			return freeReward
		end
	end

	local premiumReward = makeReward(step, false)
	if premiumReward.rewardId == rewardId then
		return premiumReward
	end
	return nil
end

local function deliverReward(player, reward, objectId)
	if reward.rewardType == 1 then
		local added = player:addItem(reward.itemId, reward.count, true)
		if not added then
			return false, "Failed to deliver item. Check your inventory."
		end
		return true
	end

	return false, "Unsupported reward type."
end

function BattlePassSystem.redeemReward(player, data)
	local step = tonumber(data and data.index) or 0
	local rewardId = tonumber(data and data.rewardId) or 0
	local objectId = tonumber(data and data.objectId) or -1

	local state, store = loadState(player)
	local reward = findReward(step, rewardId)
	if not reward then
		player:sendCancelMessage("[Battle Pass] Reward not found.")
		return false
	end

	if getCurrentRewardStep(state.points) < step then
		player:sendCancelMessage("[Battle Pass] This reward is still locked.")
		return false
	end

	if not reward.freeReward and not isPremiumActive(state) then
		player:sendCancelMessage("[Battle Pass] Deluxe Battle Pass is required for this reward.")
		return false
	end

	local claimedKey = tostring(reward.rewardId)
	if state.claimed[claimedKey] == true then
		player:sendCancelMessage("[Battle Pass] This reward was already claimed.")
		return false
	end

	local delivered, errorMessage = deliverReward(player, reward, objectId)
	if not delivered then
		player:sendCancelMessage("[Battle Pass] " .. (errorMessage or "Could not deliver reward."))
		return false
	end

	state.claimed[claimedKey] = true
	saveState(store, state)
	player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "[Battle Pass] Reward claimed.")
	sendMoneyResources(player)
	BattlePassSystem.sendRewards(player)
	return true
end

function BattlePassSystem.rerollDailyMission(player, data)
	local missionId = tostring(data and data.missionId or "")
	if missionId == "" then
		player:sendCancelMessage("[Battle Pass] Invalid mission.")
		return false
	end

	local state, store, season, daily = loadState(player)
	getActiveDailyMissions(state, daily.key)

	local slotKey = nil
	for index = 1, 2 do
		if state.dailySlots[tostring(index)] == missionId then
			slotKey = tostring(index)
			break
		end
	end

	if not slotKey then
		player:sendCancelMessage("[Battle Pass] This daily mission is not active.")
		return false
	end

	local cost = config.dailyRerollBasePrice * player:getLevel()
	if cost > 0 and not player:removeMoneyBank(cost) then
		player:sendCancelMessage("[Battle Pass] You do not have enough gold for this reroll.")
		return false
	end

	state.rerollCounter = (tonumber(state.rerollCounter) or 0) + 1
	local used = {}
	for _, activeMissionId in pairs(state.dailySlots) do
		used[activeMissionId] = true
	end

	local startIndex = ((state.rerollCounter + tonumber(slotKey)) % #dailyMissionPool) + 1
	for offset = 0, #dailyMissionPool - 1 do
		local index = ((startIndex + offset - 1) % #dailyMissionPool) + 1
		local candidate = dailyMissionPool[index]
		if candidate and not used[candidate.id] then
			state.dailySlots[slotKey] = candidate.id
			state.dailyProgress[missionId] = nil
			state.dailyAwarded[missionId] = nil
			state.dailyProgress[candidate.id] = nil
			state.dailyAwarded[candidate.id] = nil
			break
		end
	end

	saveState(store, state)
	sendMissionState(player, state, season, daily)
	return true
end

local function updateMissionProgress(player, state, mission, daily, monsterName)
	if not mission or not missionMatches(mission, monsterName) then
		return false
	end

	local previous = getMissionProgress(state, mission, daily)
	if previous >= mission.maxProgress then
		return false
	end

	local current = math.min(previous + 1, mission.maxProgress)
	setMissionProgress(state, mission, current, daily)

	if current >= mission.maxProgress and not wasMissionAwarded(state, mission, daily) then
		setMissionAwarded(state, mission, daily)
		addBattlePassPoints(state, mission.rewardPoints)
		player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "[Battle Pass] Mission completed: " .. mission.name .. " (+" .. mission.rewardPoints .. " points).")
	end

	return true
end

function BattlePassSystem.onKill(player, target)
	if not player or not target or not target:isMonster() then
		return true
	end

	if target:getMaster() then
		return true
	end

	local state, store, season, daily = loadState(player)
	if os.time() >= season.endTime then
		return true
	end

	local monsterName = target:getName()
	local changed = false
	local previousStep = getCurrentRewardStep(state.points)

	for _, mission in ipairs(getActiveDailyMissions(state, daily.key)) do
		changed = updateMissionProgress(player, state, mission, true, monsterName) or changed
	end

	for _, mission in ipairs(generalMissions) do
		changed = updateMissionProgress(player, state, mission, false, monsterName) or changed
	end

	if changed then
		saveState(store, state)
		sendMissionState(player, state, season, daily)
		if getCurrentRewardStep(state.points) > previousStep then
			BattlePassSystem.sendRewards(player)
		end
	end
	return true
end

function BattlePassSystem.purchasePremium(player, skipCoinCharge)
	local state, store, season, daily = loadState(player)
	if isPremiumActive(state) then
		return "You already have the Deluxe Battle Pass for this season."
	end

	if not skipCoinCharge and not player:removeTibiaCoins(config.deluxePrice) then
		return "Not enough Tibia Coins."
	end

	state.premium = true
	saveState(store, state)
	player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "[Battle Pass] Deluxe Battle Pass purchased.")
	sendMissionState(player, state, season, daily)
	BattlePassSystem.sendRewards(player)
	return nil
end

local function isRateLimited(player, action)
	if not rateLimitedActions[action] then
		return false
	end

	local guid = player:getGuid()
	local requests = lastRequest[guid]
	if not requests then
		requests = {}
		lastRequest[guid] = requests
	end

	local now = os.time()
	local last = requests[action]
	if last and now - last < REQUEST_COOLDOWN_SECONDS then
		return true
	end

	requests[action] = now
	return false
end

function BattlePassSystem.onExtendedOpcode(player, buffer)
	local ok, payload = pcall(json.decode, buffer)
	if not ok or type(payload) ~= "table" then
		return true
	end

	local action = payload.action
	local data = type(payload.data) == "table" and payload.data or {}
	if isRateLimited(player, action) then
		return true
	end

	if action == "getMissions" then
		BattlePassSystem.sendMissions(player)
	elseif action == "getRewards" then
		BattlePassSystem.sendRewards(player)
	elseif action == "reroll" then
		BattlePassSystem.rerollDailyMission(player, data)
	elseif action == "redeem" then
		BattlePassSystem.redeemReward(player, data)
	elseif action == "buyPremium" or action == "buyDeluxe" or action == "purchasePremium" then
		local errorMessage = BattlePassSystem.purchasePremium(player)
		if errorMessage then
			player:sendCancelMessage("[Battle Pass] " .. errorMessage)
			BattlePassSystem.sendMissions(player)
		end
	end
	return true
end

local killEvent = CreatureEvent("BattlePassKill")
function killEvent.onKill(player, target)
	return BattlePassSystem.onKill(player, target)
end
killEvent:register()

local logoutEvent = CreatureEvent("BattlePassLogout")
function logoutEvent.onLogout(player)
	lastRequest[player:getGuid()] = nil
	return true
end
logoutEvent:register()

local loginEvent = CreatureEvent("BattlePassLogin")
function loginEvent.onLogin(player)
	player:registerEvent("BattlePassKill")
	player:registerEvent("BattlePassLogout")
	return true
end
loginEvent:register()
