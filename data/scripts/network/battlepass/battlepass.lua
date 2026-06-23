if not configManager.getBoolean(configKeys.BATTLEPASS_SYSTEM_ENABLED) then
	BattlePassSystem = nil
	return
end

BattlePassSystem = BattlePassSystem or {}

local BATTLEPASS_REQUEST_OPCODE = 0x36
local BATTLEPASS_SEND_OPCODE = 0x37
local RESOURCE_BALANCE_OPCODE = 0xEE
local RESOURCE_BANK = 0
local RESOURCE_INVENTORY = 1
local REWARD_STEPS_PER_CHUNK = 20

local REQUEST_GET_MISSIONS = 1
local REQUEST_GET_REWARDS = 2
local REQUEST_REROLL = 3
local REQUEST_REDEEM = 4
local REQUEST_BUY_PREMIUM = 5
local REQUEST_GET_SHOP = 6
local REQUEST_BUY_SHOP = 7

local RESPONSE_MISSIONS = 1
local RESPONSE_REWARDS = 2
local RESPONSE_ERROR = 3
local RESPONSE_SHOP = 4

local function supportsCustomNetwork(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local DAY_SECONDS = 24 * 60 * 60
local WEEK_SECONDS = 7 * DAY_SECONDS

local config = BattlePassConfig
if type(config) ~= "table" then
	error("Battle Pass configuration was not loaded. Check data/scripts/lib/reward_battlepass.lua")
end

local REQUEST_COOLDOWN_SECONDS = 1
local rateLimitedActions = {
	getMissions = true,
	getRewards = true,
	getShop = true,
	buyShop = true,
}
local lastRequest = {}

local dailyFreeMissions = config.dailyMissions and config.dailyMissions.free or {}
local dailyDeluxeMissions = config.dailyMissions and config.dailyMissions.deluxe or {}
local generalMissions = config.generalMissions or {}
local missionById = {}
local function registerMission(mission, poolName)
	if type(mission) ~= "table" or not mission.id then
		error(string.format("[Battle Pass] Malformed mission in %s. Every mission must be a table with an id.", poolName))
	end
	if missionById[mission.id] then
		error(string.format("[Battle Pass] Duplicate mission id '%s' in %s.", tostring(mission.id), poolName))
	end
	missionById[mission.id] = mission
end

for _, mission in ipairs(dailyFreeMissions) do
	registerMission(mission, "daily free missions")
end
for _, mission in ipairs(dailyDeluxeMissions) do
	registerMission(mission, "daily deluxe missions")
end
for _, mission in ipairs(generalMissions) do
	registerMission(mission, "general missions")
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

local function getSeasonStore()
	return kv.scoped("battlepass"):scoped("season")
end

local function getSeason()
	local seasonConfig = config.season or {}
	local store = getSeasonStore()
	local configuredId = tostring(seasonConfig.id or "season")
	local activeConfigId = store:get("configId")
	local epoch = tonumber(Game.getStorageValue(GlobalStorageKeys.battlePassSeasonEpoch)) or -1
	local beginTime = tonumber(Game.getStorageValue(GlobalStorageKeys.battlePassSeasonStartedAt)) or -1

	if epoch < 1 or beginTime < 1 then
		epoch = math.max(1, epoch + 1)
		beginTime = tonumber(seasonConfig.startsAt) or 0
		if beginTime <= 0 then
			beginTime = os.time()
		end
		Game.setStorageValue(GlobalStorageKeys.battlePassSeasonEpoch, epoch)
		Game.setStorageValue(GlobalStorageKeys.battlePassSeasonStartedAt, beginTime)
	end

	if activeConfigId ~= configuredId then
		store:set("configId", configuredId)
	end

	local durationDays = math.max(1, tonumber(seasonConfig.durationDays) or 35)
	return {
		id = configuredId .. ":" .. epoch,
		epoch = epoch,
		beginTime = beginTime,
		endTime = beginTime + durationDays * DAY_SECONDS,
	}
end

local function getDailyWindow()
	local now = os.time()
	local date = os.date("*t", now)
	local beginTime = os.time({ year = date.year, month = date.month, day = date.day, hour = tonumber(config.season.resetHour) or 10, min = 0, sec = 0 })
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
	state.shopPurchases = type(state.shopPurchases) == "table" and state.shopPurchases or {}
	state.points = clamp(state.points, 0, config.season.maxStep * config.season.pointsPerStep)
	state.shopPoints = clamp(state.shopPoints, 0, 0xFFFFFFFF)
	state.rerollCounter = tonumber(state.rerollCounter) or 0
	state.premium = state.premium == true
	state.completed = state.points >= config.season.maxStep * config.season.pointsPerStep
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
		shopPurchases = {},
		shopPoints = 0,
		rerollCounter = 0,
		completed = false,
	}
end

local function loadState(player)
	local store = getStore(player)
	local season = getSeason()
	local daily = getDailyWindow()
	local state = store:get("state")

	if type(state) ~= "table" or state.seasonId ~= season.id then
		state = resetStateForSeason(season)
		player:setStorageValue(PlayerStorageKeys.battlePassSeasonEpoch, season.epoch)
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
	local maxPoints = config.season.maxStep * config.season.pointsPerStep
	state.points = clamp((tonumber(state.points) or 0) + amount, 0, maxPoints)
	state.completed = state.points >= maxPoints
end

local function addShopPoints(state, amount)
	state.shopPoints = clamp((tonumber(state.shopPoints) or 0) + amount, 0, 0xFFFFFFFF)
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
		rewardPoints = mission.points,
	}
end

local function getDailyMissionBySlot(state, slot, dailyKey)
	local pool = slot == 1 and dailyFreeMissions or dailyDeluxeMissions
	if #pool == 0 then
		return nil
	end

	local key = tostring(slot)
	if not state.dailySlots[key] then
		local daySeed = tonumber(dailyKey) or math.floor(os.time() / DAY_SECONDS)
		local index = ((daySeed + slot * 7) % #pool) + 1
		state.dailySlots[key] = pool[index].id
	end

	return missionById[state.dailySlots[key]]
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
	return math.min(config.season.maxStep, math.floor((tonumber(points) or 0) / config.season.pointsPerStep))
end

local function getCurrentWeek(season)
	if os.time() < season.beginTime then
		return 0
	end
	return math.max(1, math.min(math.ceil((os.time() - season.beginTime + 1) / WEEK_SECONDS), math.ceil((season.endTime - season.beginTime) / WEEK_SECONDS)))
end

local function isGeneralMissionUnlocked(mission, season)
	return tonumber(mission.unlockWeek) == nil or tonumber(mission.unlockWeek) <= getCurrentWeek(season)
end

local function buildMissionsPayload(player, state, season, daily)
	local currentRewardStep = getCurrentRewardStep(state.points)
	local nextStepPoints = math.min((currentRewardStep + 1) * config.season.pointsPerStep, config.season.maxStep * config.season.pointsPerStep)

	local dailyMissions = {}
	for _, mission in ipairs(getActiveDailyMissions(state, daily.key)) do
		if mission then
			table.insert(dailyMissions, getMissionPayload(state, mission, true))
		end
	end

	local generalPayload = {}
	for _, mission in ipairs(generalMissions) do
		if isGeneralMissionUnlocked(mission, season) then
			table.insert(generalPayload, getMissionPayload(state, mission, false))
		end
	end

	return {
		playerOutfit = getPlayerOutfitPayload(player),
		beginTime = season.beginTime,
		endTime = season.endTime,
		points = state.points,
		rerollPrice = (tonumber(config.reroll.goldPerLevel) or 800) * player:getLevel(),
		deluxePrice = config.deluxe.price,
		battlePassActive = isPremiumActive(state),
		currentRewardStep = currentRewardStep,
		nextStepPoints = nextStepPoints,
		dailyBeginTime = daily.beginTime,
		dailyEndTime = daily.endTime,
		dailyMissions = dailyMissions,
		generalMissions = generalPayload,
	}
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

local function writeString(out, value)
	out:addString(tostring(value or ""))
end

local function writeBool(out, value)
	out:addByte(value and 1 or 0)
end

local function writeU16(out, value)
	out:addU16(clamp(value, 0, 0xFFFF))
end

local function writeU32(out, value)
	out:addU32(clamp(value, 0, 0xFFFFFFFF))
end

local function writeOutfit(out, outfit)
	outfit = outfit or {}
	writeU16(out, outfit.type)
	out:addByte(clamp(outfit.head, 0, 0xFF))
	out:addByte(clamp(outfit.body, 0, 0xFF))
	out:addByte(clamp(outfit.legs, 0, 0xFF))
	out:addByte(clamp(outfit.feet, 0, 0xFF))
	out:addByte(clamp(outfit.addons, 0, 0xFF))
end

local function writeMission(out, mission)
	writeString(out, mission.missionId)
	writeString(out, mission.missionName)
	writeString(out, mission.missionDescription)
	writeU32(out, mission.currentProgress)
	writeU32(out, mission.maxProgress)
	writeU16(out, mission.rewardPoints)
end

local function writeMissionList(out, missions)
	missions = type(missions) == "table" and missions or {}
	writeU16(out, #missions)
	for index = 1, math.min(#missions, 0xFFFF) do
		writeMission(out, missions[index])
	end
end

local function getItemTypeInfo(itemId)
	itemId = tonumber(itemId) or 0
	if itemId <= 0 then
		return 0, ""
	end

	local itemType = ItemType(itemId)
	if not itemType then
		return itemId, ""
	end

	local clientId = tonumber(itemType:getClientId()) or 0
	if clientId <= 0 then
		clientId = itemId
	end

	return clientId, itemType:getName() or ""
end

local function makeItemThingValue(itemId, itemName)
	local clientId, resolvedName = getItemTypeInfo(itemId)
	itemName = tostring(itemName or "")
	if itemName ~= "" then
		resolvedName = itemName
	end

	return {
		thingId = clientId,
		thingName = resolvedName,
	}
end

local function makeItemThingValues(items)
	local values = {}
	items = type(items) == "table" and items or {}
	for index = 1, math.min(#items, 0xFFFF) do
		local item = items[index] or {}
		if type(item) == "table" then
			values[index] = makeItemThingValue(item.itemId or item.thingId, item.itemName or item.thingName)
		else
			values[index] = makeItemThingValue(item)
		end
	end
	return values
end

local shopTypes = {
	item = 1,
	mount = 2,
	outfit = 3,
	prey = 4,
	charms = 5,
}

local function getShopEntries()
	local shop = config.shop
	return type(shop) == "table" and type(shop.items) == "table" and shop.items or {}
end

local function getShopEntry(shopId)
	shopId = tonumber(shopId) or 0
	for _, entry in ipairs(getShopEntries()) do
		if tonumber(entry.id) == shopId then
			return entry
		end
	end
	return nil
end

local function getShopOutfit(player, entry)
	local outfits = player:getSex() == PLAYERSEX_FEMALE and entry.female or entry.male
	if type(outfits) ~= "table" or #outfits == 0 then
		outfits = entry.male or entry.female or {}
	end
	return outfits[1]
end

local function isShopEntryPurchased(player, state, entry)
	if entry.repeatable == true then
		return false
	end

	local shopId = tostring(tonumber(entry.id) or 0)
	if state.shopPurchases[shopId] == true then
		return true
	end
	if entry.type == "mount" and tonumber(entry.mountId) and player:hasMount(tonumber(entry.mountId)) then
		return true
	end
	if entry.type == "outfit" then
		local outfit = getShopOutfit(player, entry)
		if outfit and outfit.looktype and player:hasOutfit(outfit.looktype, tonumber(entry.addons) or 0) then
			return true
		end
	end
	return false
end

local function writeThingValues(out, values)
	values = type(values) == "table" and values or {}
	local count = math.min(#values, 0xFFFF)
	writeU16(out, count)
	for index = 1, count do
		local value = values[index] or {}
		local thingId = tonumber(value.thingId) or 0
		writeU16(out, thingId)
		writeString(out, value.thingName or "")
	end
end

local function writeOutfitGroups(out, groups)
	groups = type(groups) == "table" and groups or {}
	local groupIds = {}
	for key, outfits in pairs(groups) do
		local groupId = tonumber(key)
		if groupId and groupId >= 0 and groupId <= 0xFF and type(outfits) == "table" and #outfits > 0 then
			groupIds[#groupIds + 1] = groupId
		end
	end
	table.sort(groupIds)

	local groupCount = math.min(#groupIds, 0xFF)
	out:addByte(groupCount)
	for index = 1, groupCount do
		local groupId = groupIds[index]
		local outfits = groups[groupId] or groups[tostring(groupId)] or {}
		local outfitCount = math.min(#outfits, 0xFF)
		out:addByte(groupId)
		out:addByte(outfitCount)
		for outfitIndex = 1, outfitCount do
			local outfit = outfits[outfitIndex] or {}
			writeU16(out, outfit.looktype or outfit.thingId or outfit.type)
			writeString(out, outfit.name or outfit.thingName)
		end
	end
end

local function writeRewardItems(out, items)
	items = type(items) == "table" and items or {}
	local count = math.min(#items, 0xFFFF)
	writeU16(out, count)
	for index = 1, count do
		local item = items[index]
		if type(item) == "table" then
			writeU16(out, item.itemId or item.thingId)
			writeU16(out, item.count)
			writeBool(out, item.stuck)
		else
			writeU16(out, item)
			writeU16(out, 1)
			writeBool(out, false)
		end
	end
end

local function sendBattlePassMessage(player, response, writer)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(BATTLEPASS_SEND_OPCODE)
	out:addByte(response)
	if writer then
		writer(out)
	end
	return out:sendToPlayer(player)
end

local function sendBattlePassError(player, message)
	return sendBattlePassMessage(player, RESPONSE_ERROR, function(out)
		writeString(out, message)
	end)
end

local function sendMoneyResources(player)
	local bankSent = sendResourceBalance(player, RESOURCE_BANK, player:getBankBalance())
	local inventorySent = sendResourceBalance(player, RESOURCE_INVENTORY, player:getMoney())
	return bankSent and inventorySent
end

local function sendMissionState(player, state, season, daily)
	local payload = buildMissionsPayload(player, state, season, daily)
	sendMoneyResources(player)
	return sendBattlePassMessage(player, RESPONSE_MISSIONS, function(out)
		writeOutfit(out, payload.playerOutfit)
		writeU32(out, payload.beginTime)
		writeU32(out, payload.endTime)
		writeU32(out, payload.points)
		writeU32(out, payload.rerollPrice)
		writeU32(out, payload.deluxePrice)
		writeBool(out, payload.battlePassActive)
		writeU16(out, payload.currentRewardStep)
		writeU32(out, payload.nextStepPoints)
		writeU32(out, payload.dailyBeginTime)
		writeU32(out, payload.dailyEndTime)
		writeMissionList(out, payload.dailyMissions)
		writeMissionList(out, payload.generalMissions)
	end)
end

local function getRequirementError(player)
	local requirements = config.requirements or {}
	if player:getLevel() < (tonumber(requirements.minimumLevel) or 8) then
		return string.format("You need level %d to use the Battle Pass.", tonumber(requirements.minimumLevel) or 8)
	end
	if requirements.requireVocation then
		local vocation = player:getVocation()
		if not vocation or vocation:getId() <= 0 then
			return "You need to choose a vocation to use the Battle Pass."
		end
	end
	return nil
end

function BattlePassSystem.sendShop(player)
	local requirementError = getRequirementError(player)
	if requirementError then
		return sendBattlePassError(player, requirementError)
	end

	local state, store = loadState(player)
	local entries = getShopEntries()
	saveState(store, state)

	return sendBattlePassMessage(player, RESPONSE_SHOP, function(out)
		writeU32(out, state.shopPoints)
		writeBool(out, state.completed)
		writeU16(out, #entries)
		for _, entry in ipairs(entries) do
			local itemClientId = 0
			if entry.type == "item" then
				itemClientId = select(1, getItemTypeInfo(entry.itemId))
			end
			local outfit = entry.type == "outfit" and getShopOutfit(player, entry) or nil
			writeU16(out, entry.id)
			writeString(out, entry.title or entry.name or "Battle Pass Offer")
			writeString(out, entry.description or "")
			writeU32(out, entry.price)
			out:addByte(clamp(shopTypes[entry.type] or 0, 0, 0xFF))
			writeBool(out, entry.repeatable == true)
			writeBool(out, isShopEntryPurchased(player, state, entry))
			writeU16(out, itemClientId)
			writeU16(out, entry.looktype or (outfit and outfit.looktype) or 0)
			out:addByte(clamp(entry.addons, 0, 0xFF))
		end
	end)
end

function BattlePassSystem.sendMissions(player)
	local requirementError = getRequirementError(player)
	if requirementError then
		return sendBattlePassError(player, requirementError)
	end
	local state, store, season, daily = loadState(player)
	saveState(store, state)
	return sendMissionState(player, state, season, daily)
end

local rewardTypes = {
	item = 1,
	randomItem = 2,
	randomMount = 3,
	exercise = 4,
	doubleSkill = 5,
	level = 6,
	prey = 7,
	xpBoost = 8,
	regeneration = 9,
	overloadForge = 10,
	instantReward = 11,
	boostedExercise = 12,
	charms = 13,
	outfit = 14,
	extraSkill = 15,
	elementalOutfit = 16,
	multiItem = 17,
	choiceItem = 18,
}

local function makeMountValues(mounts)
	local values = {}
	for index, mount in ipairs(mounts or {}) do
		values[index] = {
			thingId = tonumber(mount.looktype) or 0,
			thingName = tostring(mount.name or "Mount"),
		}
	end
	return values
end

local function makeOutfitValues(outfits)
	local values = {}
	for index, outfit in ipairs(outfits or {}) do
		values[index] = {
			thingId = tonumber(outfit.looktype) or 0,
			thingName = tostring(outfit.name or "Outfit"),
		}
	end
	return values
end

local function makeReward(step, freeReward, definition)
	if type(definition) ~= "table" then
		return nil
	end

	local rewardType = rewardTypes[definition.type]
	if not rewardType then
		print(string.format("[Battle Pass] Invalid reward type '%s' at step %d.", tostring(definition.type), step))
		return nil
	end

	local reward = {
		rewardId = step * 10 + (freeReward and 1 or 2),
		rewardType = rewardType,
		freeReward = freeReward,
		itemId = tonumber(definition.itemId) or 0,
		count = math.max(1, tonumber(definition.count) or 1),
		charges = math.max(0, tonumber(definition.charges) or 0),
		stuck = definition.stuck == true,
		hasClaimedReward = false,
		durationTime = math.max(0, tonumber(definition.durationHours) or 0),
		addons = math.max(0, tonumber(definition.addons) or 0),
		randomValues = {},
		choosableValues = {},
		maleOutfit = definition.maleOutfit or {},
		femaleOutfit = definition.femaleOutfit or {},
		items = definition.items or {},
		definition = definition,
	}

	if definition.type == "randomMount" then
		reward.randomValues = makeMountValues(definition.mounts)
	elseif definition.type == "outfit" then
		reward.randomValues = makeOutfitValues(definition.male)
		if #reward.randomValues == 0 then
			reward.randomValues = makeOutfitValues(definition.female)
		end
	elseif definition.type == "elementalOutfit" then
		reward.maleOutfit = definition.maleOutfit or {}
		reward.femaleOutfit = definition.femaleOutfit or {}
	end

	return reward
end

local function setRewardClaimState(reward, state)
	local claimed = state.claimed[tostring(reward.rewardId)] == true
	reward.hasClaimedReward = claimed
end

local function buildRewardSteps(state)
	local steps = {}
	for step = 1, config.season.maxStep do
		local rewards = {}
		local configuredStep = config.rewards[step] or {}

		if configuredStep.free then
			local freeReward = makeReward(step, true, configuredStep.free)
			if freeReward then
			setRewardClaimState(freeReward, state)
			table.insert(rewards, freeReward)
			end
		end

		if configuredStep.deluxe then
			local premiumReward = makeReward(step, false, configuredStep.deluxe)
			if premiumReward then
				setRewardClaimState(premiumReward, state)
				table.insert(rewards, premiumReward)
			end
		end

		table.insert(steps, {
			stepId = step,
			rewards = rewards,
		})
	end
	return steps
end

function BattlePassSystem.sendRewards(player)
	local requirementError = getRequirementError(player)
	if requirementError then
		return sendBattlePassError(player, requirementError)
	end
	local state, store = loadState(player)
	local rewards = buildRewardSteps(state)
	saveState(store, state)

	if #rewards == 0 then
		return sendBattlePassMessage(player, RESPONSE_REWARDS, function(out)
			writeBool(out, false)
			writeU16(out, 1)
			writeU16(out, 0)
			writeU16(out, 0)
		end)
	end

	local sent = false
	for first = 1, #rewards, REWARD_STEPS_PER_CHUNK do
		local steps = {}
		for index = first, math.min(first + REWARD_STEPS_PER_CHUNK - 1, #rewards) do
			table.insert(steps, rewards[index])
		end

		sent = sendBattlePassMessage(player, RESPONSE_REWARDS, function(out)
			writeBool(out, true)
			writeU16(out, first)
			writeU16(out, #rewards)
			writeU16(out, #steps)
			for _, step in ipairs(steps) do
				writeU16(out, step.stepId)
				out:addByte(math.min(#step.rewards, 0xFF))
				for index = 1, math.min(#step.rewards, 0xFF) do
					local reward = step.rewards[index]
					local randomValues = reward.randomValues
					local choosableValues = reward.choosableValues
					if reward.rewardType == 1 then
						randomValues = {makeItemThingValue(reward.itemId, reward.itemName)}
					elseif reward.rewardType == 2 or reward.rewardType == 4 or reward.rewardType == 12 then
						randomValues = makeItemThingValues(reward.definition.items or reward.definition.choices)
					elseif reward.rewardType == 17 then
						randomValues = makeItemThingValues(reward.items)
					elseif reward.rewardType == 18 then
						choosableValues = makeItemThingValues(reward.definition.choices)
					end
					writeU32(out, reward.rewardId)
					out:addByte(clamp(reward.rewardType, 0, 0xFF))
					writeBool(out, reward.freeReward)
					writeU16(out, reward.itemId)
					writeU16(out, reward.count)
					writeU16(out, reward.charges)
					writeBool(out, reward.stuck)
					writeBool(out, reward.hasClaimedReward)
					writeU32(out, reward.durationTime)
					out:addByte(clamp(reward.addons, 0, 0xFF))
					writeThingValues(out, randomValues)
					writeThingValues(out, choosableValues)
					writeOutfitGroups(out, reward.maleOutfit)
					writeOutfitGroups(out, reward.femaleOutfit)
					writeRewardItems(out, reward.items)
				end
			end
		end) or sent
	end
	return sent
end

local function findReward(step, rewardId)
	step = tonumber(step) or 0
	rewardId = tonumber(rewardId) or 0
	if step < 1 or step > config.season.maxStep then
		return nil
	end

	local configuredStep = config.rewards[step] or {}
	if configuredStep.free then
		local freeReward = makeReward(step, true, configuredStep.free)
		if freeReward and freeReward.rewardId == rewardId then
			return freeReward
		end
	end

	if configuredStep.deluxe then
		local premiumReward = makeReward(step, false, configuredStep.deluxe)
		if premiumReward and premiumReward.rewardId == rewardId then
			return premiumReward
		end
	end
	return nil
end

local function resolveItemChoice(itemIds, objectId)
	for _, itemId in ipairs(itemIds or {}) do
		local clientId = select(1, getItemTypeInfo(itemId))
		if objectId == itemId or objectId == clientId then
			return itemId
		end
	end
	return nil
end

local function addItemsToBattlePassInbox(player, items)
	local inbox = player:getStoreInbox()
	if not inbox then
		return false, "Your Battle Pass inbox is not available."
	end

	local neededSlots = 0
	for _, entry in ipairs(items) do
		local itemType = ItemType(entry.itemId)
		if not itemType or itemType:getId() ~= entry.itemId then
			return false, "This season has an invalid reward item configured."
		end
		neededSlots = neededSlots + (itemType:isStackable() and 1 or entry.count)
	end
	if inbox:getEmptySlots() < neededSlots then
		return false, "Your Battle Pass inbox does not have enough room for this reward."
	end

	for _, entry in ipairs(items) do
		local itemType = ItemType(entry.itemId)
		local deliveries = itemType:isStackable() and 1 or entry.count
		local amount = itemType:isStackable() and entry.count or 1
		for _ = 1, deliveries do
			local item = Game.createItem(entry.itemId, amount)
			if not item then
				return false, "Could not create the reward item."
			end
			if entry.charges and entry.charges > 0 then
				item:setAttribute(ITEM_ATTRIBUTE_CHARGES, entry.charges)
			end
			if inbox:addItemEx(item, INDEX_WHEREEVER, FLAG_NOLIMIT) ~= RETURNVALUE_NOERROR then
				item:remove()
				return false, "Your Battle Pass inbox does not have enough room for this reward."
			end
		end
	end
	return true
end

local function rewardSkillParameter(skillId)
	local parameters = {
		[0] = CONDITION_PARAM_SKILL_FIST,
		[1] = CONDITION_PARAM_SKILL_CLUB,
		[2] = CONDITION_PARAM_SKILL_SWORD,
		[3] = CONDITION_PARAM_SKILL_AXE,
		[4] = CONDITION_PARAM_SKILL_DISTANCE,
		[5] = CONDITION_PARAM_SKILL_SHIELD,
		[13] = CONDITION_PARAM_STAT_MAGICPOINTS,
	}
	return parameters[skillId]
end

local function deliverReward(player, reward, objectId)
	local definition = reward.definition
	if definition.type == "item" then
		return addItemsToBattlePassInbox(player, { { itemId = reward.itemId, count = reward.count, charges = reward.charges } })
	elseif definition.type == "randomItem" then
		local items = definition.items or {}
		if #items == 0 then
			return false, "This season has an empty random-item reward."
		end
		local itemId = items[math.random(#items)]
		return addItemsToBattlePassInbox(player, { { itemId = itemId, count = reward.count, charges = reward.charges } })
	elseif definition.type == "exercise" or definition.type == "boostedExercise" or definition.type == "choiceItem" then
		local itemId = resolveItemChoice(definition.choices, objectId)
		if not itemId then
			return false, "Choose a valid reward item first."
		end
		return addItemsToBattlePassInbox(player, { { itemId = itemId, count = reward.count, charges = reward.charges } })
	elseif definition.type == "multiItem" then
		return addItemsToBattlePassInbox(player, definition.items)
	elseif definition.type == "randomMount" then
		local available = {}
		for _, mount in ipairs(definition.mounts or {}) do
			if mount.id and not player:hasMount(mount.id) then
				table.insert(available, mount)
			end
		end
		if #available == 0 then
			return false, "You already own every mount in this reward."
		end
		if not player:addMount(available[math.random(#available)].id) then
			return false, "Could not add the selected mount."
		end
		return true
	elseif definition.type == "outfit" then
		local primaryOutfits = player:getSex() == PLAYERSEX_FEMALE and definition.female or definition.male
		local fallbackOutfits = player:getSex() == PLAYERSEX_FEMALE and definition.male or definition.female
		local outfits = primaryOutfits or fallbackOutfits or {}
		if #outfits == 0 and fallbackOutfits and fallbackOutfits ~= outfits then
			outfits = fallbackOutfits
		end
		if #outfits == 0 then
			return false, "This season has an empty outfit reward."
		end
		for _, outfit in ipairs(outfits) do
			local delivered
			if definition.addons and definition.addons > 0 then
				delivered = player:addOutfitAddon(outfit.looktype, definition.addons)
			else
				delivered = player:addOutfit(outfit.looktype)
			end
			if delivered == false then
				return false, "Could not add this outfit."
			end
		end
		return true
	elseif definition.type == "level" then
		return player:addLevel(reward.count), "Could not add the level reward."
	elseif definition.type == "prey" then
		if not PreySystem or not PreySystem.addWildcards or not PreySystem.addWildcards(player, reward.count) then
			return false, "Prey System is not available."
		end
		return true
	elseif definition.type == "charms" then
		if not db.query("UPDATE `players` SET `charmpoints` = `charmpoints` + " .. reward.count .. " WHERE `id` = " .. player:getGuid()) then
			return false, "Could not add charm points."
		end
		return true
	elseif definition.type == "xpBoost" then
		if not player.getXpBoostTime or not player.setXpBoostTime or not player.setXpBoostPercent then
			return false, "XP Boost is not available."
		end
		local duration = math.min(65535, reward.durationTime * 60 * 60)
		player:setXpBoostPercent(math.max(player:getXpBoostPercent(), tonumber(definition.percent) or 50))
		player:setXpBoostTime(math.min(65535, player:getXpBoostTime() + duration))
		return true
	elseif definition.type == "regeneration" then
		local vocation = player:getVocation()
		if not vocation then
			return false, "You need a vocation to receive this reward."
		end
		local condition = Condition(CONDITION_REGENERATION, CONDITIONID_DEFAULT)
		condition:setParameter(CONDITION_PARAM_SUBID, 43000 + reward.rewardId)
		condition:setParameter(CONDITION_PARAM_TICKS, reward.durationTime * 60 * 60 * 1000)
		condition:setParameter(CONDITION_PARAM_HEALTHGAIN, vocation:getHealthGainAmount())
		condition:setParameter(CONDITION_PARAM_HEALTHTICKS, vocation:getHealthGainTicks())
		condition:setParameter(CONDITION_PARAM_MANAGAIN, vocation:getManaGainAmount())
		condition:setParameter(CONDITION_PARAM_MANATICKS, vocation:getManaGainTicks())
		return player:addCondition(condition), "Could not add the regeneration reward."
	elseif definition.type == "doubleSkill" then
		local now = os.time()
		local currentUntil = tonumber(player:getStorageValue(PlayerStorageKeys.battlePassDoubleSkillUntil)) or 0
		player:setStorageValue(PlayerStorageKeys.battlePassDoubleSkillUntil, math.max(now, currentUntil) + reward.durationTime * 60 * 60)
		return true
	elseif definition.type == "extraSkill" then
		local skillParameter = rewardSkillParameter(objectId)
		if not skillParameter then
			return false, "Choose a valid skill first."
		end
		local condition = Condition(CONDITION_ATTRIBUTES, CONDITIONID_DEFAULT)
		condition:setParameter(CONDITION_PARAM_SUBID, 44000 + reward.rewardId)
		condition:setParameter(CONDITION_PARAM_TICKS, reward.durationTime * 60 * 60 * 1000)
		condition:setParameter(skillParameter, reward.count)
		return player:addCondition(condition), "Could not add the skill reward."
	end

	return false, "Unsupported reward type."
end

local function deliverShopEntry(player, entry)
	if entry.type == "item" then
		return addItemsToBattlePassInbox(player, {
			{ itemId = tonumber(entry.itemId) or 0, count = math.max(1, tonumber(entry.count) or 1), charges = math.max(0, tonumber(entry.charges) or 0) },
		})
	elseif entry.type == "mount" then
		local mountId = tonumber(entry.mountId) or 0
		if mountId <= 0 or player:hasMount(mountId) then
			return false, "You already own this mount."
		end
		return player:addMount(mountId), "Could not add this mount."
	elseif entry.type == "outfit" then
		local outfit = getShopOutfit(player, entry)
		if not outfit or not outfit.looktype then
			return false, "This season has an invalid outfit configured."
		end
		local looktype = tonumber(outfit.looktype) or 0
		if looktype <= 0 then
			return false, "This season has an invalid outfit configured."
		end
		local addons = math.max(0, tonumber(entry.addons) or 0)
		local delivered
		if addons > 0 then
			delivered = player:addOutfitAddon(looktype, addons)
		else
			delivered = player:addOutfit(looktype)
		end
		return delivered, "Could not add this outfit."
	elseif entry.type == "prey" then
		local count = math.max(1, tonumber(entry.count) or 1)
		if not PreySystem or not PreySystem.addWildcards or not PreySystem.addWildcards(player, count) then
			return false, "Prey System is not available."
		end
		return true
	elseif entry.type == "charms" then
		local count = math.max(1, tonumber(entry.count) or 1)
		if not db.query("UPDATE `players` SET `charmpoints` = `charmpoints` + " .. count .. " WHERE `id` = " .. player:getGuid()) then
			return false, "Could not add charm points."
		end
		return true
	end

	return false, "Unsupported Battle Pass shop offer."
end

function BattlePassSystem.purchaseShopEntry(player, shopId)
	local requirementError = getRequirementError(player)
	if requirementError then
		return false, requirementError
	end

	local state, store, season = loadState(player)
	if os.time() < season.beginTime or os.time() >= season.endTime then
		return false, "The Battle Pass season is not active."
	end
	if not state.completed then
		return false, string.format("Complete Battle Pass level %d before using the shop.", config.season.maxStep)
	end

	local entry = getShopEntry(shopId)
	if not entry then
		return false, "Battle Pass shop offer not found."
	end
	if isShopEntryPurchased(player, state, entry) then
		return false, "You already own this offer."
	end

	local price = math.max(0, tonumber(entry.price) or 0)
	if state.shopPoints < price then
		return false, "You do not have enough Battle Pass shop points."
	end

	local delivered, errorMessage = deliverShopEntry(player, entry)
	if not delivered then
		return false, errorMessage or "Could not deliver this offer."
	end

	state.shopPoints = state.shopPoints - price
	if entry.repeatable ~= true then
		state.shopPurchases[tostring(tonumber(entry.id) or 0)] = true
	end
	saveState(store, state)
	player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "[Battle Pass] Shop offer purchased.")
	BattlePassSystem.sendShop(player)
	return true
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
	if os.time() < season.beginTime or os.time() >= season.endTime then
		player:sendCancelMessage("[Battle Pass] This season is not accepting mission progress.")
		return false
	end
	local requirementError = getRequirementError(player)
	if requirementError then
		player:sendCancelMessage("[Battle Pass] " .. requirementError)
		return false
	end
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
	if slotKey == "2" and not isPremiumActive(state) then
		player:sendCancelMessage("[Battle Pass] Deluxe Battle Pass is required for this mission.")
		return false
	end

	local pool = slotKey == "1" and dailyFreeMissions or dailyDeluxeMissions
	if #pool < 2 then
		player:sendCancelMessage("[Battle Pass] This mission cannot be rerolled right now.")
		return false
	end

	local nextRerollCounter = (tonumber(state.rerollCounter) or 0) + 1
	local used = {}
	for _, activeMissionId in pairs(state.dailySlots) do
		used[activeMissionId] = true
	end

	local replacement = nil
	local startIndex = ((nextRerollCounter + tonumber(slotKey)) % #pool) + 1
	for offset = 0, #pool - 1 do
		local index = ((startIndex + offset - 1) % #pool) + 1
		local candidate = pool[index]
		if candidate and not used[candidate.id] then
			replacement = candidate
			break
		end
	end
	if not replacement then
		player:sendCancelMessage("[Battle Pass] There is no different mission available to reroll.")
		return false
	end

	local cost = (tonumber(config.reroll.goldPerLevel) or 800) * player:getLevel()
	if cost > 0 and not player:removeMoneyBank(cost) then
		player:sendCancelMessage("[Battle Pass] You do not have enough gold for this reroll.")
		return false
	end

	state.rerollCounter = nextRerollCounter
	state.dailySlots[slotKey] = replacement.id
	state.dailyProgress[missionId] = nil
	state.dailyAwarded[missionId] = nil
	state.dailyProgress[replacement.id] = nil
	state.dailyAwarded[replacement.id] = nil

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
		if state.completed and daily then
			local shopPoints = math.max(0, tonumber(mission.shopPoints) or tonumber(mission.points) or 0)
			addShopPoints(state, shopPoints)
			player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "[Battle Pass] Daily mission completed: " .. mission.name .. " (+" .. shopPoints .. " shop points).")
		else
			local battlePassPoints = math.max(0, tonumber(mission.points) or 0)
			addBattlePassPoints(state, battlePassPoints)
			player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "[Battle Pass] Mission completed: " .. mission.name .. " (+" .. battlePassPoints .. " points).")
		end
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
	if getRequirementError(player) then
		return true
	end

	local state, store, season, daily = loadState(player)
	if os.time() < season.beginTime or os.time() >= season.endTime then
		return true
	end

	local monsterName = target:getName()
	local changed = false
	local previousStep = getCurrentRewardStep(state.points)
	local wasCompleted = state.completed
	local previousShopPoints = state.shopPoints

	local dailyMissions = getActiveDailyMissions(state, daily.key)
	if dailyMissions[1] then
		changed = updateMissionProgress(player, state, dailyMissions[1], true, monsterName) or changed
	end
	if isPremiumActive(state) and dailyMissions[2] then
		changed = updateMissionProgress(player, state, dailyMissions[2], true, monsterName) or changed
	end

	for _, mission in ipairs(generalMissions) do
		if not state.completed and isGeneralMissionUnlocked(mission, season) then
			changed = updateMissionProgress(player, state, mission, false, monsterName) or changed
		end
	end

	if changed then
		saveState(store, state)
		sendMissionState(player, state, season, daily)
		if getCurrentRewardStep(state.points) > previousStep then
			BattlePassSystem.sendRewards(player)
		end
		if (not wasCompleted and state.completed) or state.shopPoints ~= previousShopPoints then
			BattlePassSystem.sendShop(player)
		end
	end
	return true
end

function BattlePassSystem.purchasePremium(player, skipCoinCharge)
	local requirementError = getRequirementError(player)
	if requirementError then
		return requirementError
	end
	local state, store, season, daily = loadState(player)
	if os.time() < season.beginTime or os.time() >= season.endTime then
		return "The Battle Pass season is not active."
	end
	if isPremiumActive(state) then
		return "You already have the Deluxe Battle Pass for this season."
	end

	if not skipCoinCharge and not player:removeTibiaCoins(config.deluxe.price) then
		return "Not enough Tibia Coins."
	end

	state.premium = true
	saveState(store, state)
	player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "[Battle Pass] Deluxe Battle Pass purchased.")
	sendMissionState(player, state, season, daily)
	BattlePassSystem.sendRewards(player)
	return nil
end

function BattlePassSystem.resetPlayer(player)
	if not player then
		return false, "Player not found."
	end
	getStore(player):remove("state")
	player:setStorageValue(PlayerStorageKeys.battlePassSeasonEpoch, -1)
	local state, store, season, daily = loadState(player)
	saveState(store, state)
	if supportsCustomNetwork(player) then
		sendMissionState(player, state, season, daily)
		BattlePassSystem.sendRewards(player)
		BattlePassSystem.sendShop(player)
	end
	return true
end

function BattlePassSystem.addShopPoints(player, amount)
	if not player then
		return false, "Player not found."
	end

	amount = math.floor(tonumber(amount) or 0)
	if amount <= 0 then
		return false, "Amount must be greater than zero."
	end

	local state, store, season, daily = loadState(player)
	if not state.completed then
		return false, string.format("Complete Battle Pass level %d before adding shop points. Use /battlepass unlockshop <player> for testing.", config.season.maxStep)
	end
	addShopPoints(state, amount)
	saveState(store, state)
	if supportsCustomNetwork(player) then
		sendMissionState(player, state, season, daily)
		BattlePassSystem.sendRewards(player)
		BattlePassSystem.sendShop(player)
	end
	return true, state.shopPoints
end

function BattlePassSystem.unlockShop(player)
	if not player then
		return false, "Player not found."
	end

	local state, store, season, daily = loadState(player)
	state.points = config.season.maxStep * config.season.pointsPerStep
	ensureStateTables(state)
	saveState(store, state)
	if supportsCustomNetwork(player) then
		sendMissionState(player, state, season, daily)
		BattlePassSystem.sendRewards(player)
		BattlePassSystem.sendShop(player)
	end
	return true
end

function BattlePassSystem.startNewSeason()
	local seasonStore = getSeasonStore()
	local currentEpoch = tonumber(Game.getStorageValue(GlobalStorageKeys.battlePassSeasonEpoch)) or 0
	Game.setStorageValue(GlobalStorageKeys.battlePassSeasonEpoch, math.max(1, currentEpoch + 1))
	Game.setStorageValue(GlobalStorageKeys.battlePassSeasonStartedAt, os.time())
	seasonStore:set("configId", tostring(config.season.id or "season"))

	for _, onlinePlayer in ipairs(Game.getPlayers()) do
		BattlePassSystem.resetPlayer(onlinePlayer)
		onlinePlayer:sendTextMessage(MESSAGE_EVENT_ADVANCE, "[Battle Pass] A new season has started. Your Battle Pass progress was reset.")
	end
	return getSeason()
end

function BattlePassSystem.getSeasonInfo()
	return getSeason()
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

local function handleBattlePassRequest(player, action, data)
	if isRateLimited(player, action) then
		return true
	end

	if action == "getMissions" then
		BattlePassSystem.sendMissions(player)
	elseif action == "getRewards" then
		BattlePassSystem.sendRewards(player)
	elseif action == "getShop" then
		BattlePassSystem.sendShop(player)
	elseif action == "reroll" then
		BattlePassSystem.rerollDailyMission(player, data)
	elseif action == "redeem" then
		BattlePassSystem.redeemReward(player, data)
	elseif action == "buyShop" then
		local success, errorMessage = BattlePassSystem.purchaseShopEntry(player, data.shopId)
		if not success then
			player:sendCancelMessage("[Battle Pass] " .. errorMessage)
			sendBattlePassError(player, errorMessage)
			BattlePassSystem.sendShop(player)
		end
	elseif action == "buyPremium" or action == "buyDeluxe" or action == "purchasePremium" then
		local errorMessage = BattlePassSystem.purchasePremium(player)
		if errorMessage then
			player:sendCancelMessage("[Battle Pass] " .. errorMessage)
			sendBattlePassError(player, errorMessage)
			BattlePassSystem.sendMissions(player)
		end
	end
	return true
end

local battlePassHandler = PacketHandler(BATTLEPASS_REQUEST_OPCODE)
function battlePassHandler.onReceive(player, msg)
	if not supportsCustomNetwork(player) then
		return true
	end

	local request = NetworkGuard.readByte(msg)
	if not request then
		return true
	end

	if request == REQUEST_GET_MISSIONS then
		return handleBattlePassRequest(player, "getMissions", {})
	elseif request == REQUEST_GET_REWARDS then
		return handleBattlePassRequest(player, "getRewards", {})
	elseif request == REQUEST_GET_SHOP then
		return handleBattlePassRequest(player, "getShop", {})
	elseif request == REQUEST_REROLL then
		local missionId = NetworkGuard.readString(msg, 128)
		if not missionId then
			sendBattlePassError(player, "Invalid mission.")
			return true
		end
		return handleBattlePassRequest(player, "reroll", { missionId = missionId })
	elseif request == REQUEST_REDEEM then
		local index = NetworkGuard.readU16(msg)
		local rewardId = NetworkGuard.readU32(msg)
		local objectId = NetworkGuard.readU32(msg)
		if not index or not rewardId or not objectId then
			sendBattlePassError(player, "Invalid reward.")
			return true
		end
		if objectId == 0 then
			objectId = -1
		end
		return handleBattlePassRequest(player, "redeem", {
			index = index,
			rewardId = rewardId,
			objectId = objectId,
		})
	elseif request == REQUEST_BUY_SHOP then
		local shopId = NetworkGuard.readU16(msg)
		if not shopId or shopId <= 0 then
			sendBattlePassError(player, "Invalid Battle Pass shop offer.")
			return true
		end
		return handleBattlePassRequest(player, "buyShop", { shopId = shopId })
	elseif request == REQUEST_BUY_PREMIUM then
		return handleBattlePassRequest(player, "buyPremium", {})
	end

	sendBattlePassError(player, "Unknown request.")
	return true
end
battlePassHandler:register()

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
