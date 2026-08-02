CurrencyConversion = CurrencyConversion or {}
CurrencyConversion.SPECTRAL_GOLD_WORTH = 1000000

local MAX_ITEM_COUNT = 2147483647
local legacyNpcExchangeState = {}
local exchangeFailureMessages = {
	invalid_amount = "The currency amount is invalid.",
	not_enough = "You do not have enough coins.",
	remove_failed = "The required coins could not be removed.",
	add_failed = "There is not enough room for the converted coins.",
	rollback_failed = "The currency exchange failed and could not be rolled back. Please contact an administrator."
}

local function getObjectId(object)
	if type(object) == "number" then
		return object
	end
	return object and object.getId and object:getId() or nil
end

local function clearLegacyNpcExchangeState(npcId, playerId)
	local transactions = legacyNpcExchangeState[npcId]
	if not transactions then
		return
	end

	transactions[playerId] = nil
	if next(transactions) == nil then
		legacyNpcExchangeState[npcId] = nil
	end
end

local function isValidCount(count)
	return type(count) == "number" and count > 0 and count <= MAX_ITEM_COUNT and count == math.floor(count)
end

local function isValidItemId(itemId)
	return type(itemId) == "number" and itemId > 0 and itemId <= 65535 and itemId == math.floor(itemId)
end

local function formatCurrencyAmount(count, label)
	if count == 1 then
		label = label:gsub("s$", "")
	end
	return count .. " " .. label
end

local function getExchangeConfirmation(prefix, sourceCount, sourceLabel, resultCount, resultLabel, remainderCount)
	local confirmation = prefix .. formatCurrencyAmount(sourceCount, sourceLabel) ..
		" into " .. formatCurrencyAmount(resultCount, resultLabel)
	if remainderCount > 0 then
		confirmation = confirmation .. " and keep the remaining " ..
			formatCurrencyAmount(remainderCount, sourceLabel)
	end
	return confirmation .. "?"
end

local function addExact(player, itemId, count, canDropOnMap)
	local countBefore = player:getItemCount(itemId)
	player:addItem(itemId, count, canDropOnMap)
	local addedCount = player:getItemCount(itemId) - countBefore
	return addedCount == count, math.max(0, addedCount)
end

local function restoreItems(player, itemId, count)
	local countBefore = player:getItemCount(itemId)
	local restored, restoredCount = addExact(player, itemId, count, false)
	if restored then
		return true
	end

	local missingCount = count - restoredCount
	if missingCount > 0 then
		player:addItem(itemId, missingCount, true)
	end
	return player:getItemCount(itemId) - countBefore == count
end

local function rollbackAddedItems(player, itemId, count)
	return count <= 0 or player:removeItem(itemId, count)
end

local function exchangeWithRollback(player, sourceId, sourceCount, resultId, resultCount, removeSource)
	if not removeSource() then
		return false, "remove_failed"
	end

	local added, addedCount = addExact(player, resultId, resultCount, false)
	if added then
		return true
	end

	local resultRolledBack = rollbackAddedItems(player, resultId, addedCount)
	local sourceRestored = restoreItems(player, sourceId, sourceCount)
	if not resultRolledBack or not sourceRestored then
		logger.error("[CurrencyConversion] Rollback failed for player %s.", player:getName())
		return false, "rollback_failed"
	end
	return false, "add_failed"
end

function CurrencyConversion.parseAmount(message)
	if type(message) ~= "string" then
		return nil
	end

	local digits = message:match("^%s*(%d+)%s*$")
	local amount = digits and tonumber(digits) or nil
	if not isValidCount(amount) then
		return nil
	end
	return amount
end

function CurrencyConversion.multiply(amount, multiplier)
	if not isValidCount(amount) or not isValidCount(multiplier) or amount > math.floor(MAX_ITEM_COUNT / multiplier) then
		return nil
	end
	return amount * multiplier
end

function CurrencyConversion.divideExact(amount, divisor)
	if not isValidCount(amount) or not isValidCount(divisor) or amount % divisor ~= 0 then
		return nil
	end
	return amount // divisor
end

function CurrencyConversion.getExchangeAmounts(message, operation)
	local requestedSourceCount = CurrencyConversion.parseAmount(message)
	if not requestedSourceCount then
		return nil
	end

	if operation == "divide" then
		local resultCount = math.floor(requestedSourceCount / 100)
		if not isValidCount(resultCount) then
			return nil
		end

		local sourceCount = resultCount * 100
		return sourceCount, resultCount, requestedSourceCount - sourceCount, requestedSourceCount
	end

	if operation == "multiply" then
		local resultCount = CurrencyConversion.multiply(requestedSourceCount, 100)
		if not resultCount then
			return nil
		end
		return requestedSourceCount, resultCount, 0, requestedSourceCount
	end

	return nil
end

function CurrencyConversion.getItemIdByWorth(worth)
	for _, currency in pairs(Game.getCurrencyItems()) do
		if currency:getWorth() == worth then
			return currency:getId()
		end
	end
	return nil
end

function CurrencyConversion.getExchangeFailureMessage(reason)
	return exchangeFailureMessages[reason] or "The currency exchange could not be completed."
end

function CurrencyConversion.exchangePlayerItems(player, sourceId, sourceCount, resultId, resultCount)
	if not isValidItemId(sourceId) or not isValidItemId(resultId) or sourceId == resultId or
		not isValidCount(sourceCount) or not isValidCount(resultCount) then
		return false, "invalid_amount"
	end

	if player:getItemCount(sourceId) < sourceCount then
		return false, "not_enough"
	end

	return exchangeWithRollback(player, sourceId, sourceCount, resultId, resultCount, function()
		return player:removeItem(sourceId, sourceCount)
	end)
end

function CurrencyConversion.exchangeItem(player, sourceItem, sourceCount, resultId, resultCount)
	if not sourceItem or not isValidCount(sourceCount) or not isValidCount(resultCount) then
		return false, "invalid_amount"
	end

	local sourceId = sourceItem:getId()
	if not isValidItemId(sourceId) or not isValidItemId(resultId) or sourceId == resultId or
		sourceItem:getCount() < sourceCount then
		return false, "not_enough"
	end

	return exchangeWithRollback(player, sourceId, sourceCount, resultId, resultCount, function()
		return sourceItem:remove(sourceCount)
	end)
end

function CurrencyConversion.registerNpcExchange(parent, words, prompt, sourceId, resultId, sourceLabel, resultLabel, operation)
	local exchange = parent:keyword(words)
	exchange:respond(prompt)

	local answer = exchange:onAnswer()
	function answer:callback(npc, player, message, handler)
		local sourceCount, resultCount, remainderCount, requestedSourceCount =
			CurrencyConversion.getExchangeAmounts(message, operation)

		if not sourceCount or not resultCount then
			return false, operation == "divide"
				and "Please enter a positive whole amount within the safe limit. At least 100 coins are required."
				or "Please enter a positive whole amount within the safe limit."
		end

		if player:getItemCount(sourceId) < requestedSourceCount then
			return false, "You don't have enough " .. sourceLabel .. "."
		end

		handler:addData(player, "currencySourceId", sourceId)
		handler:addData(player, "currencySourceCount", sourceCount)
		handler:addData(player, "currencyResultId", resultId)
		handler:addData(player, "currencyResultCount", resultCount)
		handler:addData(player, "currencySourceLabel", sourceLabel)
		handler:addData(player, "currencyResultLabel", resultLabel)
		return true, getExchangeConfirmation("You want to change ", sourceCount, sourceLabel,
			resultCount, resultLabel, remainderCount)
	end

	local accept = answer:keyword({ "yes" })
	function accept:callback(npc, player, message, handler)
		local sourceId = handler:getData(player, "currencySourceId")
		local sourceCount = handler:getData(player, "currencySourceCount")
		local resultId = handler:getData(player, "currencyResultId")
		local resultCount = handler:getData(player, "currencyResultCount")
		local sourceLabel = handler:getData(player, "currencySourceLabel")
		local resultLabel = handler:getData(player, "currencyResultLabel")

		if not sourceId or not sourceCount or not resultId or not resultCount then
			handler:resetData(player)
			return false, "The exchange request is no longer valid."
		end

		local success, reason = CurrencyConversion.exchangePlayerItems(
			player, sourceId, sourceCount, resultId, resultCount)
		handler:resetData(player)
		if not success then
			if reason == "not_enough" or reason == "remove_failed" then
				return false, "You don't have enough " .. sourceLabel .. "."
			end
			return false, "The exchange could not be completed. Make sure you have enough inventory space."
		end

		return true, "You have changed " .. formatCurrencyAmount(sourceCount, sourceLabel) ..
			" into " .. formatCurrencyAmount(resultCount, resultLabel) .. "."
	end

	local decline = answer:keyword({ "no" })
	function decline:callback(npc, player, message, handler)
		handler:resetData(player)
		return true, "Ok then, not."
	end

	return exchange
end

function CurrencyConversion.registerBankerExchanges(change)
	CurrencyConversion.registerNpcExchange(change, "gold", "How many gold coins would you like to change?",
		ITEM_GOLD_COIN, ITEM_PLATINUM_COIN, "gold coins", "platinum coins", "divide")

	local platinum = change:keyword("platinum")
	platinum:respond("Would you like to change your platinum coins into {gold} coins or into {crystal} coins?")
	CurrencyConversion.registerNpcExchange(platinum, "gold",
		"How many platinum coins would you like to change into gold?",
		ITEM_PLATINUM_COIN, ITEM_GOLD_COIN, "platinum coins", "gold coins", "multiply")
	CurrencyConversion.registerNpcExchange(platinum, "crystal",
		"How many platinum coins would you like to change into crystal?",
		ITEM_PLATINUM_COIN, ITEM_CRYSTAL_COIN, "platinum coins", "crystal coins", "divide")

	local crystal = change:keyword("crystal")
	crystal:respond(
		"Would you like to change your crystal coins into {platinum} coins or into {spectral} gold nuggets?")
	CurrencyConversion.registerNpcExchange(crystal, "platinum",
		"How many crystal coins would you like to change into platinum?",
		ITEM_CRYSTAL_COIN, ITEM_PLATINUM_COIN, "crystal coins", "platinum coins", "multiply")

	local spectralGoldNuggetId = CurrencyConversion.getItemIdByWorth(CurrencyConversion.SPECTRAL_GOLD_WORTH)
	if spectralGoldNuggetId then
		CurrencyConversion.registerNpcExchange(crystal, { "spectral", "nugget" },
			"How many crystal coins would you like to change into spectral gold nuggets?",
			ITEM_CRYSTAL_COIN, spectralGoldNuggetId, "crystal coins", "spectral gold nuggets", "divide")
		CurrencyConversion.registerNpcExchange(change, { "spectral", "nugget" },
			"How many spectral gold nuggets would you like to change into crystal coins?",
			spectralGoldNuggetId, ITEM_CRYSTAL_COIN, "spectral gold nuggets", "crystal coins", "multiply")
	end
end

function CurrencyConversion.cleanupLegacyNpcExchange(npc, player)
	local npcId = getObjectId(npc)
	local playerId = getObjectId(player)
	if not npcId or not playerId then
		return false
	end

	clearLegacyNpcExchangeState(npcId, playerId)
	return true
end

function CurrencyConversion.handleLegacyNpcExchange(npcHandler, npc, creature, player, message)
	local playerId = player:getId()
	local topic = npcHandler:getTopic(playerId)
	local spectralGoldNuggetId = CurrencyConversion.getItemIdByWorth(CurrencyConversion.SPECTRAL_GOLD_WORTH)
	local npcId = npc:getId()
	local transactions = legacyNpcExchangeState[npcId]

	local function resetExchange()
		CurrencyConversion.cleanupLegacyNpcExchange(npc, player)
		npcHandler:setTopic(playerId, 0)
	end

	local function prepareExchange(sourceId, resultId, sourceLabel, resultLabel, operation, confirmationTopic)
		local sourceCount, resultCount, remainderCount, requestedSourceCount =
			CurrencyConversion.getExchangeAmounts(message, operation)

		if not sourceCount or not resultCount then
			npcHandler:say(operation == "divide"
				and "Please enter a positive whole amount within the safe limit. At least 100 coins are required."
				or "Please enter a positive whole amount within the safe limit.", npc, creature)
			clearLegacyNpcExchangeState(npcId, playerId)
			return
		end

		if player:getItemCount(sourceId) < requestedSourceCount then
			npcHandler:say("Sorry, you do not have enough " .. sourceLabel .. ".", npc, creature)
			resetExchange()
			return
		end

		if not transactions then
			transactions = {}
			legacyNpcExchangeState[npcId] = transactions
		end
		transactions[playerId] = {
			sourceId = sourceId,
			sourceCount = sourceCount,
			resultId = resultId,
			resultCount = resultCount,
			sourceLabel = sourceLabel,
			resultLabel = resultLabel
		}
		npcHandler:say(getExchangeConfirmation("So you would like me to change ", sourceCount, sourceLabel,
			resultCount, resultLabel, remainderCount), npc, creature)
		npcHandler:setTopic(playerId, confirmationTopic)
	end

	local function confirmExchange()
		if not MsgContains(message, "yes") then
			npcHandler:say("Well, can I help you with something else?", npc, creature)
			resetExchange()
			return
		end

		local transaction = transactions and transactions[playerId]
		if not transaction then
			npcHandler:say("The exchange request is no longer valid.", npc, creature)
			resetExchange()
			return
		end

		local success, reason = CurrencyConversion.exchangePlayerItems(player, transaction.sourceId,
			transaction.sourceCount, transaction.resultId, transaction.resultCount)
		if success then
			npcHandler:say("Here you are.", npc, creature)
		elseif reason == "not_enough" or reason == "remove_failed" then
			npcHandler:say("Sorry, you do not have enough " .. transaction.sourceLabel .. ".", npc, creature)
		else
			npcHandler:say("The exchange could not be completed. Make sure you have enough inventory space.",
				npc, creature)
		end
		resetExchange()
	end

	if MsgContains(message, "change gold") then
		npcHandler:say("How many gold coins would you like to change?", npc, creature)
		npcHandler:setTopic(playerId, 14)
	elseif topic == 14 then
		prepareExchange(ITEM_GOLD_COIN, ITEM_PLATINUM_COIN, "gold coins", "platinum coins", "divide", 15)
	elseif topic == 15 then
		confirmExchange()
	elseif MsgContains(message, "change platinum") then
		npcHandler:say("Would you like to change your platinum coins into gold or crystal?", npc, creature)
		npcHandler:setTopic(playerId, 16)
	elseif topic == 16 then
		if MsgContains(message, "gold") then
			npcHandler:say("How many platinum coins would you like to change into gold?", npc, creature)
			npcHandler:setTopic(playerId, 17)
		elseif MsgContains(message, "crystal") then
			npcHandler:say("How many platinum coins would you like to change into crystal?", npc, creature)
			npcHandler:setTopic(playerId, 19)
		else
			resetExchange()
		end
	elseif topic == 17 then
		prepareExchange(ITEM_PLATINUM_COIN, ITEM_GOLD_COIN, "platinum coins", "gold coins", "multiply", 18)
	elseif topic == 18 then
		confirmExchange()
	elseif topic == 19 then
		prepareExchange(ITEM_PLATINUM_COIN, ITEM_CRYSTAL_COIN, "platinum coins", "crystal coins", "divide", 20)
	elseif topic == 20 then
		confirmExchange()
	elseif MsgContains(message, "change crystal") then
		npcHandler:say("Would you like to change your crystal coins into platinum or spectral gold nuggets?",
			npc, creature)
		npcHandler:setTopic(playerId, 21)
	elseif topic == 21 then
		if MsgContains(message, "platinum") then
			npcHandler:say("How many crystal coins would you like to change into platinum?", npc, creature)
			npcHandler:setTopic(playerId, 22)
		elseif spectralGoldNuggetId and (MsgContains(message, "spectral") or MsgContains(message, "nugget")) then
			npcHandler:say("How many crystal coins would you like to change into spectral gold nuggets?",
				npc, creature)
			npcHandler:setTopic(playerId, 24)
		else
			resetExchange()
		end
	elseif topic == 22 then
		prepareExchange(ITEM_CRYSTAL_COIN, ITEM_PLATINUM_COIN, "crystal coins", "platinum coins", "multiply", 23)
	elseif topic == 23 then
		confirmExchange()
	elseif topic == 24 and spectralGoldNuggetId then
		prepareExchange(ITEM_CRYSTAL_COIN, spectralGoldNuggetId, "crystal coins", "spectral gold nuggets",
			"divide", 25)
	elseif topic == 25 then
		confirmExchange()
	elseif MsgContains(message, "change spectral") and spectralGoldNuggetId then
		npcHandler:say("How many spectral gold nuggets would you like to change into crystal coins?", npc, creature)
		npcHandler:setTopic(playerId, 26)
	elseif topic == 26 and spectralGoldNuggetId then
		prepareExchange(spectralGoldNuggetId, ITEM_CRYSTAL_COIN, "spectral gold nuggets", "crystal coins",
			"multiply", 27)
	elseif topic == 27 then
		confirmExchange()
	else
		return false
	end

	return true
end
