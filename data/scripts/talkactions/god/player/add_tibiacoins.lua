local MAX_TIBIA_COINS = 4294967295
local COINS_USAGE = "Usage: /coins add, player name, amount or /coins remove, player name, amount"

local function shopHistoryExists()
	if db.tableExists then
		return db.tableExists("shop_history")
	end
	return true
end

local function addCoinHistory(accountId, playerGuid, title, amount, adminName)
	if not shopHistoryExists() then
		return
	end

	db.query("INSERT INTO `shop_history` (`account`, `player`, `date`, `title`, `price`, `costSecond`, `count`, `target`) VALUES (" ..
		accountId .. ", " ..
		playerGuid .. ", NOW(), " ..
		db.escapeString(title) .. ", " ..
		amount .. ", 0, 1, " ..
		db.escapeString(adminName) .. ")")
end

local function normalizeAction(action)
	action = (action or ""):lower():gsub("[%s_%-]+", "")
	if action == "add" or action == "adicionar" or action == "+" then
		return "add"
	end

	if action == "remove" or action == "remover" or action == "rem" or action == "del" or action == "-" then
		return "remove"
	end
end

local function parseCoinsParam(param)
	local split = param:splitTrimmed(",")
	if split[1] and split[2] and split[3] then
		return normalizeAction(split[1]), split[2], split[3]
	end

	local tokens = {}
	for token in param:gmatch("%S+") do
		tokens[#tokens + 1] = token
	end

	if #tokens < 3 then
		return nil
	end

	local amount = tokens[#tokens]
	local action = table.remove(tokens, 1)
	table.remove(tokens, #tokens)
	return normalizeAction(action), table.concat(tokens, " "), amount
end

local function updatePlayerCoins(admin, action, targetName, amountText)
	if (action ~= "add" and action ~= "remove") or not targetName or targetName == "" then
		admin:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, COINS_USAGE)
		return false
	end

	local amount = math.floor(tonumber(amountText) or 0)
	if amount <= 0 then
		admin:sendCancelMessage("Amount must be a positive number.")
		return false
	end

	local resultId = db.storeQuery("SELECT p.`id`, p.`account_id`, p.`name`, a.`tibia_coins` FROM `players` p INNER JOIN `accounts` a ON a.`id` = p.`account_id` WHERE LOWER(p.`name`) = LOWER(" .. db.escapeString(targetName) .. ") LIMIT 1")
	if resultId == false then
		admin:sendCancelMessage("Player not found.")
		return false
	end

	local targetGuid = result.getDataInt(resultId, "id")
	local accountId = result.getDataInt(resultId, "account_id")
	local storedName = result.getDataString(resultId, "name")
	local currentCoins = result.getDataInt(resultId, "tibia_coins")
	result.free(resultId)

	local newBalance
	local changedAmount
	local title
	local targetMessage

	if action == "add" then
		if currentCoins >= MAX_TIBIA_COINS then
			admin:sendCancelMessage(storedName .. " already has the maximum Tibia Coins.")
			return false
		end

		newBalance = math.min(currentCoins + amount, MAX_TIBIA_COINS)
		changedAmount = newBalance - currentCoins
		title = "God Add Tibia Coins"
		targetMessage = "You received " .. changedAmount .. " Tibia Coins."
	elseif action == "remove" then
		if currentCoins < amount then
			admin:sendCancelMessage(storedName .. " only has " .. currentCoins .. " Tibia Coins.")
			return false
		end

		newBalance = currentCoins - amount
		changedAmount = amount
		title = "God Remove Tibia Coins"
		targetMessage = changedAmount .. " Tibia Coins were removed from your account."
	end

	local target = Player(storedName)
	if target then
		target:setTibiaCoins(newBalance)
	elseif not db.query("UPDATE `accounts` SET `tibia_coins` = " .. newBalance .. " WHERE `id` = " .. accountId) then
		admin:sendCancelMessage("Could not update Tibia Coins.")
		return false
	end

	addCoinHistory(accountId, targetGuid, title, action == "add" and changedAmount or -changedAmount, admin:getName())

	local verb = action == "add" and "Added" or "Removed"
	local preposition = action == "add" and "to" or "from"
	admin:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, verb .. " " .. changedAmount .. " Tibia Coins " .. preposition .. " " .. storedName .. ". New balance: " .. newBalance .. ".")

	if target then
		target:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, targetMessage)
	end
	return false
end

local coins = TalkAction("/coins")
function coins.onSay(player, words, param)
	local action, targetName, amount = parseCoinsParam(param)
	return updatePlayerCoins(player, action, targetName, amount)
end
coins:separator(" ")
coins:accountType(6)
coins:access(true)
coins:register()

local add = TalkAction("/add")
function add.onSay(player, words, param)
	local split = param:splitTrimmed(",")
	if not split[1] or not split[2] or not split[3] then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Usage: /add tibiacoins, player name, amount")
		return false
	end

	local currency = split[1]:lower():gsub("[%s_%-]+", "")
	if currency ~= "tibiacoins" and currency ~= "tc" and currency ~= "coins" then
		player:sendCancelMessage("Unknown add type. Use: /add tibiacoins, player name, amount")
		return false
	end

	return updatePlayerCoins(player, "add", split[2], split[3])
end
add:separator(" ")
add:accountType(6)
add:access(true)
add:register()
