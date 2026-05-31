local promote = TalkAction("/promote")

local function getPromotion(vocation)
	if not vocation then
		return nil
	end

	return vocation:getPromotion()
end

local function promoteOnlinePlayer(player, target)
	local vocation = target:getVocation()
	local promotion = getPromotion(vocation)
	if not promotion then
		player:sendCancelMessage(target:getName() .. " is already promoted or cannot be promoted.")
		return false
	end

	target:setVocation(promotion)
	target:setStorageValue(PlayerStorageKeys.promotion, 1)
	target:save()
	target:getPosition():sendMagicEffect(CONST_ME_FIREWORK_RED)

	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You promoted " .. target:getName() .. " from " .. vocation:getName() .. " to " .. promotion:getName() .. ".")
	target:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You have been promoted to " .. promotion:getName() .. " by " .. player:getName() .. ".")
	return false
end

local function promoteOfflinePlayer(player, name)
	local resultId = db.storeQuery("SELECT `id`, `name`, `vocation` FROM `players` WHERE `name` = " .. db.escapeString(name))
	if not resultId then
		player:sendCancelMessage("Player " .. name .. " not found.")
		return false
	end

	local playerId = result.getNumber(resultId, "id")
	local playerName = result.getString(resultId, "name")
	local vocationId = result.getNumber(resultId, "vocation")
	result.free(resultId)

	local vocation = Vocation(vocationId)
	local promotion = getPromotion(vocation)
	if not promotion then
		player:sendCancelMessage(playerName .. " is already promoted or cannot be promoted.")
		return false
	end

	db.query("UPDATE `players` SET `vocation` = " .. promotion:getId() .. " WHERE `id` = " .. playerId)
	db.query("INSERT INTO `player_storage` (`player_id`, `key`, `value`) VALUES (" .. playerId .. ", " .. PlayerStorageKeys.promotion .. ", 1) ON DUPLICATE KEY UPDATE `value` = 1")

	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You promoted offline player " .. playerName .. " from " .. vocation:getName() .. " to " .. promotion:getName() .. ".")
	return false
end

function promote.onSay(player, words, param)
	local name = param:trim()
	if name == "" then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Usage: /promote <player name>")
		return false
	end

	local target = Player(name)
	if target then
		return promoteOnlinePlayer(player, target)
	end

	return promoteOfflinePlayer(player, name)
end

promote:separator(" ")
promote:accountType(ACCOUNT_TYPE_GAMEMASTER)
promote:access(true)
promote:register()
