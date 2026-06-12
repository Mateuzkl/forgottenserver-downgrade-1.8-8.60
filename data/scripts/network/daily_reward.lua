local OPCODE_REWARD_OPEN = 0xB4
local OPCODE_REWARD_HISTORY = 0xB5
local OPCODE_REWARD_SELECT = 0xB6

local SERVER_PACKET_OPEN_REWARD_WALL = 0xE2
local SERVER_PACKET_DAILY_REWARD_BASIC = 0xE4
local SERVER_PACKET_DAILY_REWARD_HISTORY = 0xE5

local DAILY_STATE_ALREADY_CLAIMED = 0
local DAILY_STATE_MISSED = 1
local DAILY_STATE_AVAILABLE = 2

local REWARD_TYPE_ITEM = 1
local REWARD_TYPE_SYSTEM = 2
local SYSTEM_REWARD_PREY_REROLL = 2

local HISTORY_LIMIT = 50
local MAX_SELECTED_ITEMS = 32

local STORAGES = {
	lastDay = PlayerStorageKeys.dailyRewardLastDay,
	rewardIndex = PlayerStorageKeys.dailyRewardIndex,
	streak = PlayerStorageKeys.dailyRewardStreak,
	jokerTokens = PlayerStorageKeys.dailyRewardJokerTokens,
}

local POTION_REWARDS = { 266, 268, 236, 237, 238, 239, 7643, 23373, 23375 }
local EXERCISE_REWARDS = { 28552, 28553, 28554, 28555, 28556, 28557, 44065, 50293 }

local DAILY_REWARDS = {
	{ type = "item", freeAmount = 5, premiumAmount = 10, items = POTION_REWARDS },
	{ type = "prey", freeAmount = 1, premiumAmount = 2 },
	{ type = "item", freeAmount = 10, premiumAmount = 20, items = POTION_REWARDS },
	{ type = "prey", freeAmount = 1, premiumAmount = 2 },
	{ type = "item", freeAmount = 1, premiumAmount = 2, items = EXERCISE_REWARDS },
	{ type = "item", freeAmount = 15, premiumAmount = 25, items = POTION_REWARDS },
	{ type = "prey", freeAmount = 2, premiumAmount = 3 },
}

local STREAK_DESCRIPTIONS = {
	"Reward streak 2: resting area bonuses are kept active.",
	"Reward streak 3: daily reward progress is protected for regular play.",
	"Reward streak 4: resting area regeneration bonuses are improved.",
	"Reward streak 5: daily reward streak bonuses become stronger.",
	"Reward streak 6: resting area bonuses are extended.",
	"Reward streak 7: maximum daily reward streak bonus is active.",
}

local schemaChecked = false

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function clamp(value, minValue, maxValue)
	value = tonumber(value) or minValue
	return math.min(math.max(value, minValue), maxValue)
end

local function getStorageNumber(player, key, default)
	local value = tonumber(player:getStorageValue(key)) or -1
	if value < 0 then
		return default or 0
	end
	return value
end

local function setStorageNumber(player, key, value)
	player:setStorageValue(key, math.max(0, math.floor(tonumber(value) or 0)))
end

local function currentDailyDay()
	return math.floor(os.time() / 86400)
end

local function nextDailyReset()
	return (currentDailyDay() + 1) * 86400
end

local function isPremiumPlayer(player)
	return player and player.isPremium and player:isPremium()
end

local function getRewardAmount(reward, premium)
	return premium and (reward.premiumAmount or reward.freeAmount or 1) or (reward.freeAmount or 1)
end

local function getCurrentRewardIndex(player)
	return clamp(getStorageNumber(player, STORAGES.rewardIndex, 0), 0, #DAILY_REWARDS - 1)
end

local function getStreakLevel(player)
	return math.max(1, getStorageNumber(player, STORAGES.streak, 1))
end

local function resetMissedReward(player)
	setStorageNumber(player, STORAGES.rewardIndex, 0)
	setStorageNumber(player, STORAGES.streak, 1)
end

local function getDailyState(player)
	local today = currentDailyDay()
	local lastDay = getStorageNumber(player, STORAGES.lastDay, 0)
	if lastDay == today then
		return DAILY_STATE_ALREADY_CLAIMED
	end

	if lastDay > 0 and lastDay < today - 1 then
		resetMissedReward(player)
		return DAILY_STATE_MISSED
	end

	return DAILY_STATE_AVAILABLE
end

local function ensureDailyRewardSchema()
	if schemaChecked then
		return
	end

	db.query([[
		CREATE TABLE IF NOT EXISTS `daily_reward_history` (
			`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
			`player_id` INT NOT NULL,
			`daystreak` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
			`timestamp` INT UNSIGNED NOT NULL DEFAULT 0,
			`description` TEXT NOT NULL,
			PRIMARY KEY (`id`),
			KEY `idx_daily_reward_history_player` (`player_id`, `timestamp`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8
	]])
	schemaChecked = true
end

local function getValidRewardItems(reward)
	local items = {}
	for _, itemId in ipairs(reward.items or {}) do
		local itemType = ItemType(itemId)
		if itemType and itemType:getId() ~= 0 then
			items[#items + 1] = itemId
		end
	end

	if #items == 0 then
		items[1] = 266
	end
	return items
end

local function writeItemReward(out, reward, premium)
	local amount = getRewardAmount(reward, premium)
	local items = getValidRewardItems(reward)

	out:addByte(REWARD_TYPE_ITEM)
	out:addByte(math.min(amount, 0xFF))
	out:addByte(math.min(#items, 0xFF))
	for i = 1, math.min(#items, 0xFF) do
		local itemId = items[i]
		local itemType = ItemType(itemId)
		out:addU16(itemId)
		out:addString(itemType:getName())
		out:addU32(math.max(0, tonumber(itemType:getWeight(1)) or 0))
	end
end

local function writeSystemReward(out, reward, premium)
	out:addByte(REWARD_TYPE_SYSTEM)
	out:addByte(1)
	out:addByte(SYSTEM_REWARD_PREY_REROLL)
	out:addByte(math.min(getRewardAmount(reward, premium), 0xFF))
end

local function writeDailyReward(out, reward, premium)
	if reward.type == "item" then
		writeItemReward(out, reward, premium)
		return
	end

	writeSystemReward(out, reward, premium)
end

local function sendDailyReward(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(SERVER_PACKET_DAILY_REWARD_BASIC)
	out:addByte(#DAILY_REWARDS)
	for _, reward in ipairs(DAILY_REWARDS) do
		writeDailyReward(out, reward, false)
		writeDailyReward(out, reward, true)
	end

	out:addByte(#STREAK_DESCRIPTIONS)
	for i, description in ipairs(STREAK_DESCRIPTIONS) do
		out:addString(description)
		out:addByte(i + 1)
	end
	out:addByte(1)
	return out:sendToPlayer(player)
end

local function sendOpenRewardWall(player, fromShrine)
	if not supportsCustomNetwork(player) then
		return false
	end

	local dailyState = getDailyState(player)
	local taken = dailyState == DAILY_STATE_ALREADY_CLAIMED
	local jokerTokens = getStorageNumber(player, STORAGES.jokerTokens, 0)

	local out = NetworkMessage(player)
	out:addByte(SERVER_PACKET_OPEN_REWARD_WALL)
	out:addByte(fromShrine and 1 or 0)
	out:addU32(taken and nextDailyReset() or 0)
	out:addByte(getCurrentRewardIndex(player))
	out:addByte(taken and 1 or 0)
	if taken then
		out:addString("You already claimed your daily reward. Come back after the next server save.")
		if jokerTokens > 0 then
			out:addByte(1)
			out:addU16(math.min(jokerTokens, 0xFFFF))
		else
			out:addByte(0)
		end
	else
		out:addByte(dailyState)
		out:addU32(nextDailyReset())
		out:addU16(math.min(jokerTokens, 0xFFFF))
	end
	out:addU16(math.min(getStreakLevel(player), 0xFFFF))
	return out:sendToPlayer(player)
end

DailyRewardSystem = DailyRewardSystem or {}

function DailyRewardSystem.openRewardWall(player, fromShrine)
	if not supportsCustomNetwork(player) then
		return false
	end

	ensureDailyRewardSchema()
	sendDailyReward(player)
	return sendOpenRewardWall(player, fromShrine ~= false)
end

local function openRewardWall(player)
	return DailyRewardSystem.openRewardWall(player, true)
end

local function sendRewardHistory(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	ensureDailyRewardSchema()

	local entries = {}
	local resultId = db.storeQuery("SELECT `timestamp`, `daystreak`, `description` FROM `daily_reward_history` WHERE `player_id` = " ..
		player:getGuid() .. " ORDER BY `timestamp` DESC, `id` DESC LIMIT " .. HISTORY_LIMIT)
	if resultId ~= false then
		repeat
			entries[#entries + 1] = {
				timestamp = result.getNumber(resultId, "timestamp"),
				streak = result.getNumber(resultId, "daystreak"),
				description = result.getString(resultId, "description") or "",
			}
		until not result.next(resultId)
		result.free(resultId)
	end

	local out = NetworkMessage(player)
	out:addByte(SERVER_PACKET_DAILY_REWARD_HISTORY)
	out:addByte(math.min(#entries, 0xFF))
	for i = 1, math.min(#entries, 0xFF) do
		out:addU32(entries[i].timestamp)
		out:addByte(0)
		out:addString(entries[i].description)
		out:addU16(math.min(entries[i].streak, 0xFFFF))
	end
	return out:sendToPlayer(player)
end

local function addRewardHistory(player, streak, description)
	ensureDailyRewardSchema()
	db.query("INSERT INTO `daily_reward_history` (`player_id`, `daystreak`, `timestamp`, `description`) VALUES (" ..
		player:getGuid() .. ", " .. math.min(streak, 0xFFFF) .. ", " .. os.time() .. ", " .. db.escapeString(description or "") .. ")")
end

local function sendClaimError(player, message)
	player:sendCancelMessage(message)
	sendDailyReward(player)
	sendOpenRewardWall(player, true)
	return false
end

local function buildAllowedItemMap(reward)
	local allowed = {}
	for _, itemId in ipairs(getValidRewardItems(reward)) do
		allowed[itemId] = true
	end
	return allowed
end

local function giveSelectedItems(player, reward, premium, selectedItems)
	local amount = getRewardAmount(reward, premium)
	local allowedItems = buildAllowedItemMap(reward)
	local totalSelected = 0

	for itemId, count in pairs(selectedItems) do
		if not allowedItems[itemId] then
			return false, "Invalid reward item selected."
		end
		if count <= 0 or count > amount then
			return false, "Invalid reward amount selected."
		end
		totalSelected = totalSelected + count
	end

	if totalSelected ~= amount then
		return false, "Select exactly " .. amount .. " reward item" .. (amount == 1 and "." or "s.")
	end

	local inbox = player:getStoreInbox()
	if not inbox then
		return false, "Your store inbox is not available."
	end

	for itemId, count in pairs(selectedItems) do
		local itemType = ItemType(itemId)
		local deliveries = itemType:isStackable() and 1 or count
		local itemCount = itemType:isStackable() and count or 1
		for _ = 1, deliveries do
			local item = Game.createItem(itemId, itemCount)
			if not item then
				return false, "Could not create the selected reward."
			end

			if inbox:addItemEx(item, INDEX_WHEREEVER, FLAG_NOLIMIT) ~= RETURNVALUE_NOERROR then
				item:remove()
				return false, "Your store inbox does not have enough room for this reward."
			end
		end
	end

	return true, string.format("%dx selected reward item%s", amount, amount == 1 and "" or "s")
end

local function giveSystemReward(player, reward, premium)
	local amount = getRewardAmount(reward, premium)
	if reward.type == "prey" then
		if not PreySystem or not PreySystem.addWildcards then
			return false, "Prey System is not available."
		end

		if not PreySystem.addWildcards(player, amount) then
			return false, "Could not add Prey Wildcards."
		end
		return true, string.format("%dx Prey Wildcard%s", amount, amount == 1 and "" or "s")
	end

	return false, "Unsupported daily reward."
end

local function parseSelectedItems(msg)
	if not NetworkGuard.canRead(msg, 1) then
		return nil
	end

	local count = NetworkGuard.readByte(msg)
	if not count or count > MAX_SELECTED_ITEMS then
		return nil
	end

	local selected = {}
	for _ = 1, count do
		if not NetworkGuard.canRead(msg, 3) then
			return nil
		end

		local itemId = NetworkGuard.readU16(msg)
		local amount = NetworkGuard.readByte(msg)
		if not itemId or not amount then
			return nil
		end

		selected[itemId] = (selected[itemId] or 0) + amount
	end

	return selected
end

local function claimReward(player, msg)
	if not supportsCustomNetwork(player) then
		return false
	end
	if not NetworkGuard.cooldown(player, "daily-reward-claim", 500) then
		return false
	end

	if not NetworkGuard.canRead(msg, 1) then
		return sendClaimError(player, "Invalid daily reward request.")
	end
	local fromShrine = NetworkGuard.readByte(msg)
	-- Astra sends this shrine/shortcut flag before the selected item list.
	-- Claims currently use the same validation path either way, but the byte must be consumed.

	local selectedItems = parseSelectedItems(msg)
	if not selectedItems then
		return sendClaimError(player, "Invalid daily reward selection.")
	end

	if getDailyState(player) == DAILY_STATE_ALREADY_CLAIMED then
		return sendClaimError(player, "You already claimed your daily reward today.")
	end

	local rewardIndex = getCurrentRewardIndex(player)
	local reward = DAILY_REWARDS[rewardIndex + 1]
	if not reward then
		resetMissedReward(player)
		rewardIndex = 0
		reward = DAILY_REWARDS[1]
	end

	local premium = isPremiumPlayer(player)
	local ok, description
	if reward.type == "item" then
		ok, description = giveSelectedItems(player, reward, premium, selectedItems)
	else
		if next(selectedItems) ~= nil then
			return sendClaimError(player, "This reward does not use item selection.")
		end
		ok, description = giveSystemReward(player, reward, premium)
	end

	if not ok then
		return sendClaimError(player, description or "Could not claim your daily reward.")
	end

	local today = currentDailyDay()
	local lastDay = getStorageNumber(player, STORAGES.lastDay, 0)
	local streak = lastDay == today - 1 and (getStreakLevel(player) + 1) or 1

	setStorageNumber(player, STORAGES.lastDay, today)
	setStorageNumber(player, STORAGES.streak, streak)
	setStorageNumber(player, STORAGES.rewardIndex, (rewardIndex + 1) % #DAILY_REWARDS)
	addRewardHistory(player, streak, description)

	player:sendTextMessage(MESSAGE_INFO_DESCR, "You have claimed your daily reward: " .. description .. ".")
	player:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)

	sendDailyReward(player)
	return sendOpenRewardWall(player, true)
end

local openHandler = PacketHandler(OPCODE_REWARD_OPEN)
function openHandler.onReceive(player, msg)
	if not NetworkGuard.cooldown(player, "daily-reward-open", 500) then
		return
	end
	openRewardWall(player)
end
openHandler:register()

local historyHandler = PacketHandler(OPCODE_REWARD_HISTORY)
function historyHandler.onReceive(player, msg)
	if not NetworkGuard.cooldown(player, "daily-reward-history", 500) then
		return
	end
	sendRewardHistory(player)
end
historyHandler:register()

local claimHandler = PacketHandler(OPCODE_REWARD_SELECT)
function claimHandler.onReceive(player, msg)
	claimReward(player, msg)
end
claimHandler:register()
