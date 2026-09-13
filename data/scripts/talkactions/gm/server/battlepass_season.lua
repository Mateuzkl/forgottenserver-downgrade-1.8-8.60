local talk = TalkAction("/battlepass")

local function usage(player)
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
		"Usage: /battlepass status | newseason | reset <player> | sync <player> | unlockshop <player> | shopcoins|coins <player> <amount> | test [player] [shop points]")
end

function talk.onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end
	if not BattlePassSystem then
		player:sendCancelMessage("Battle Pass system is disabled.")
		return false
	end

	local action, targetName = param:match("^(%S+)%s*(.*)$")
	action = (action or "status"):lower()
	targetName = (targetName or ""):trim()
	local isShopCoinsAction = action == "shopcoins" or action == "coins" or action == "addcoins"
	local isUnlockShopAction = action == "unlockshop" or action == "completeshop"
	local isTestAction = action == "test" or action == "testplayer"

	if action == "status" then
		local season = BattlePassSystem.getSeasonInfo()
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
			string.format("Battle Pass: %s (epoch %d), starts %s, ends %s.", season.id, season.epoch,
				os.date("%Y-%m-%d %H:%M", season.beginTime), os.date("%Y-%m-%d %H:%M", season.endTime)))
		return false
	end

	if action == "newseason" then
		local season = BattlePassSystem.startNewSeason()
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
			string.format("New Battle Pass season started: %s.", season.id))
		return false
	end

	if action == "reset" or action == "sync" or isShopCoinsAction or isUnlockShopAction or isTestAction then
		if targetName == "" and not isTestAction then
			usage(player)
			return false
		end

		local amount
		if isShopCoinsAction then
			targetName, amount = targetName:match("^(.-)%s+(%d+)$")
			targetName = (targetName or ""):trim()
			amount = tonumber(amount)
			if targetName == "" or not amount then
				usage(player)
				return false
			end
		elseif isTestAction then
			if targetName == "" then
				amount = 10000
			elseif targetName:match("^%d+$") then
				amount = tonumber(targetName)
				targetName = ""
			else
				local parsedName, parsedAmount = targetName:match("^(.-)%s+(%d+)$")
				if parsedName then
				targetName = parsedName:trim()
				amount = tonumber(parsedAmount)
				else
					amount = 10000
				end
			end
		end

		local target = isTestAction and targetName == "" and player or Player(targetName)
		if not target then
			player:sendCancelMessage("Player must be online.")
			return false
		end

		if action == "reset" then
			local ok, errorMessage = BattlePassSystem.resetPlayer(target)
			if not ok then
				player:sendCancelMessage(errorMessage or "Could not reset Battle Pass state.")
				return false
			end
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Battle Pass state reset for " .. target:getName() .. ".")
		elseif action == "sync" then
			local missionsSent = BattlePassSystem.sendMissions(target)
			local rewardsSent = missionsSent and BattlePassSystem.sendRewards(target)
			local shopSent = rewardsSent and BattlePassSystem.sendShop(target)
			if missionsSent and rewardsSent and shopSent then
				player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Battle Pass sent to " .. target:getName() .. ".")
			else
				player:sendCancelMessage("Could not send Battle Pass to " .. target:getName() .. ".")
			end
		elseif isShopCoinsAction then
			local ok, balanceOrError = BattlePassSystem.addShopPoints(target, amount)
			if not ok then
				player:sendCancelMessage(balanceOrError or "Could not add Battle Pass shop points.")
				return false
			end
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
				string.format("Added %d Battle Pass shop points to %s. New balance: %d.", amount, target:getName(), balanceOrError))
		elseif isTestAction then
			local ok, balanceOrError = BattlePassSystem.prepareTestPlayer(target, amount)
			if not ok then
				player:sendCancelMessage(balanceOrError or "Could not prepare the Battle Pass test state.")
				return false
			end
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
				string.format("Battle Pass test state enabled for %s: level 50, Deluxe, shop unlocked, balance %d.", target:getName(), balanceOrError))
		else
			local ok, errorMessage = BattlePassSystem.unlockShop(target)
			if not ok then
				player:sendCancelMessage(errorMessage or "Could not unlock Battle Pass shop.")
				return false
			end
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Battle Pass shop unlocked for " .. target:getName() .. ".")
		end
		return false
	end

	usage(player)
	return false
end

talk:separator(" ")
talk:access(true)
talk:register()
