-- Including the Advanced NPC System
dofile('data/npc/lib/npcsystem/npcsystem.lua')
dofile("data/npc/lib/revnpcsys/npc.lua")

NPC_SHOP_BUY_EXHAUST_MS = NPC_SHOP_BUY_EXHAUST_MS or 500
NPC_SHOP_EXHAUST_MESSAGE = NPC_SHOP_EXHAUST_MESSAGE or "Please wait before buying again."

function msgcontains(message, keyword)
	local message, keyword = message:lower(), keyword:lower()
	if message == keyword then return true end

	return message:find(keyword) and not message:find('(%w+)' .. keyword)
end

local npcShopExhaust = {}
local npcShopExhaustCleanupAt = 0

local function getNpcShopTimeMs()
	if os.mtime then
		return os.mtime()
	end
	if os.clock then
		return math.floor(os.clock() * 1000)
	end
	return os.time() * 1000
end

local function getNpcShopPlayerKey(player)
	if player.getGuid then
		local ok, guid = pcall(function()
			return player:getGuid()
		end)
		if ok and guid and guid > 0 then
			return guid
		end
	end
	return player:getId()
end

local function cleanupNpcShopExhaust(now)
	if now < npcShopExhaustCleanupAt then
		return
	end
	npcShopExhaustCleanupAt = now + 60000
	for key, expiresAt in pairs(npcShopExhaust) do
		if expiresAt <= now then
			npcShopExhaust[key] = nil
		end
	end
end

function checkNpcShopBuyExhaust(player, delayMs)
	delayMs = tonumber(delayMs or NPC_SHOP_BUY_EXHAUST_MS) or 0
	if delayMs <= 0 then
		return true
	end

	local now = getNpcShopTimeMs()
	cleanupNpcShopExhaust(now)

	local key = getNpcShopPlayerKey(player)
	if (npcShopExhaust[key] or 0) > now then
		player:sendCancelMessage(NPC_SHOP_EXHAUST_MESSAGE)
		return false
	end

	npcShopExhaust[key] = now + delayMs
	return true
end

local function getNpcShopItemWeight(itemType, amount, subType, inBackpacks, backpack)
	local weight = 0
	if inBackpacks then
		local backpackType = ItemType(backpack)
		if not backpackType or backpackType:getId() == 0 then
			return nil
		end

		local backpackCapacity = math.max(1, backpackType:getCapacity())
		local stacks = itemType:isStackable() and math.ceil(amount / math.max(1, itemType:getStackSize())) or amount
		weight = weight + backpackType:getWeight(math.ceil(stacks / backpackCapacity))
	end

	if itemType:isStackable() then
		return weight + itemType:getWeight(amount)
	end
	return weight + (itemType:getWeight(subType > 0 and subType or 1) * amount)
end

local function rollbackCreatedShopItems(items)
	for i = #items, 1, -1 do
		if items[i] then
			items[i]:remove()
		end
	end
end

function doNpcSellItem(cid, itemid, amount, subType, ignoreCap, inBackpacks,
                       backpack)
	local amount = amount or 1
	local subType = subType or 0
	local backpack = backpack or ITEM_BACKPACK
	local player = Player(cid)
	if not player or amount <= 0 then
		return 0, 0, "invalid"
	end

	local itemType = ItemType(itemid)
	if not itemType or itemType:getId() == 0 or itemType:getName() == "" then
		return 0, 0, "invalid"
	end

	local weight = getNpcShopItemWeight(itemType, amount, subType, inBackpacks, backpack)
	if not weight then
		return 0, 0, "invalid"
	end
	if player:getFreeCapacity() < weight then
		return 0, 0, "capacity"
	end

	local createdItems = {}
	local backpackCount = 0
	local stackSize = math.max(1, itemType:getStackSize())
	local itemSubType = subType > 0 and subType or 1

	if inBackpacks then
		local backpackType = ItemType(backpack)
		local backpackCapacity = math.max(1, backpackType:getCapacity())
		local remaining = amount

		while remaining > 0 do
			local container = Game.createItem(backpack, 1)
			if not container then
				rollbackCreatedShopItems(createdItems)
				return 0, 0, "invalid"
			end
			backpackCount = backpackCount + 1
			createdItems[#createdItems + 1] = container

			for _ = 1, backpackCapacity do
				if remaining <= 0 then
					break
				end

				local count = itemType:isStackable() and math.min(remaining, stackSize) or itemSubType
				local added = container:addItem(itemid, count)
				if not added then
					rollbackCreatedShopItems(createdItems)
					return 0, 0, "room"
				end
				remaining = remaining - (itemType:isStackable() and count or 1)
			end
		end
	elseif itemType:isStackable() then
		local remaining = amount
		while remaining > 0 do
			local count = math.min(remaining, stackSize)
			local item = Game.createItem(itemid, count)
			if not item then
				rollbackCreatedShopItems(createdItems)
				return 0, 0, "invalid"
			end
			createdItems[#createdItems + 1] = item
			remaining = remaining - count
		end
	else
		for _ = 1, amount do
			local item = Game.createItem(itemid, itemSubType)
			if not item then
				rollbackCreatedShopItems(createdItems)
				return 0, 0, "invalid"
			end
			createdItems[#createdItems + 1] = item
		end
	end

	for _, item in ipairs(createdItems) do
		local ret = player:addItemEx(item, false)
		if ret ~= RETURNVALUE_NOERROR then
			rollbackCreatedShopItems(createdItems)
			return 0, 0, ret == RETURNVALUE_NOTENOUGHCAPACITY and "capacity" or "room"
		end
	end
	return amount, backpackCount, nil
end

local func = function(cid, text, type, e, pcid)
	if Player(pcid) then
		local creature = Creature(cid)
		creature:say(text, type, false, pcid, creature:getPosition())
		e.done = true
	end
end

function doCreatureSayWithDelay(cid, text, type, delay, e, pcid)
	if Player(pcid) then
		e.done = false
		e.event =
			addEvent(func, delay < 1 and 1000 or delay, cid, text, type, e, pcid)
	end
end

function doPlayerSellItem(cid, itemid, count, cost)
	local player = Player(cid)
	if player and player:removeItem(itemid, count) then
		if not player:addMoney(cost) then
			error('Could not add money to ' .. player:getName() .. '(' .. cost .. 'gp)')
		end
		return true
	end
	return false
end

function doPlayerBuyItemContainer(cid, containerid, itemid, count, cost, charges)
	local player = Player(cid)
	if not player then return false end

	if player:getTotalMoney() < cost then return false end

	local containers = {}
	local weight = ItemType(containerid):getWeight(1) * count
	for i = 1, count do
		weight = weight + (ItemType(itemid):getWeight(charges or 1) * ItemType(containerid):getCapacity())
	end
	if player:getFreeCapacity() < weight then return false end

	for i = 1, count do
		local container = Game.createItem(containerid, 1)
		for x = 1, ItemType(containerid):getCapacity() do
			container:addItem(itemid, charges)
		end
		containers[#containers + 1] = container
	end

	for _, container in ipairs(containers) do
		if player:addItemEx(container, false) ~= RETURNVALUE_NOERROR then
			rollbackCreatedShopItems(containers)
			return false
		end
	end
	if not player:removeTotalMoney(cost) then
		rollbackCreatedShopItems(containers)
		return false
	end
	return true
end

function getCount(string)
	local b, e = string:find("%d+")
    if not b then
        return -1
    end
    local count = tonumber(string:sub(b, e))
    if count > 2 ^ 32 - 1 then
        print("Warning: Casting value to 32bit to prevent crash\n" .. debug.traceback())
    end
    return b and e and math.min(2 ^ 32 - 1, count) or -1
end

function Player.getTotalMoney(self)
	return self:getMoney() + self:getBankBalance()
end

function isValidMoney(money) return isNumber(money) and money > 0 end

function getMoneyCount(string)
	local b, e = string:find("%d+")
    if not b then
        return -1
    end
    local count = tonumber(string:sub(b, e))
    if count > 2 ^ 32 - 1 then
        print("Warning: Casting value to 32bit to prevent crash\n" .. debug.traceback())
    end
    local money = b and e and math.min(2 ^ 32 - 1, count) or -1
    if isValidMoney(money) then
        return money
    end
    return -1
end

function getMoneyWeight(money)
	local weight, currencyItems = 0, Game.getCurrencyItems()
	for index = #currencyItems, 1, -1 do
		local currency = currencyItems[index]
		local worth = currency:getWorth()
		local currencyCoins = math.floor(money / worth)
		if currencyCoins > 0 then
			money = money - (currencyCoins * worth)
			weight = weight + currency:getWeight(currencyCoins)
		end
	end
	return weight
end

do
	RevNpcCompat = RevNpcCompat or {
		handlerMoveCallback = 10050,
		lastSellResult = {},
		npcConfigs = {},
		shopExhaustion = {},
	}

	-- Reset last created handler for each NPC script load
	NpcHandler.__modernCompatLastCreated = nil

	local compat = RevNpcCompat

	MsgContains = MsgContains or msgcontains
	CALLBACK_ON_TRADE_REQUEST = CALLBACK_ON_TRADE_REQUEST or CALLBACK_ONTRADEREQUEST
	CALLBACK_SET_INTERACTION = CALLBACK_SET_INTERACTION or CALLBACK_ONADDFOCUS
	CALLBACK_REMOVE_INTERACTION = CALLBACK_REMOVE_INTERACTION or CALLBACK_ONRELEASEFOCUS
	CALLBACK_ON_MOVE = CALLBACK_ON_MOVE or compat.handlerMoveCallback

	local function getPlayerId(target)
		if type(target) == "number" then
			return target
		end

		local player = Player(target)
		if player then
			return player:getId()
		end

		local creature = Creature(target)
		if creature then
			return creature:getId()
		end

		return 0
	end

	local function getPlayerObject(target)
		if not target then
			return nil
		end

		return Player(target)
	end

	local function getCreatureObject(target)
		if not target then
			return nil
		end

		return Player(target) or Creature(target)
	end

	local function getNpcObject(target)
		if not target then
			return nil
		end

		if type(target) == "userdata" then
			return target
		end

		return Creature(target) -- Fallback
	end

	local function getCurrentNpc(handler)
		return Npc() or (handler and handler.__modernCompatNpcName and Npc(handler.__modernCompatNpcName)) or nil
	end

	local function callModernCallback(callback, ...)
		local result = callback(...)
		return result ~= false
	end

	local function resolveItemType(itemIdOrName, clientId)
		local itemType = nil
		if itemIdOrName then
			itemType = ItemType(itemIdOrName)
			if itemType and itemType:getId() ~= 0 then
				return itemType
			end
		end

		if clientId then
			itemType = Game.getItemTypeByClientId(clientId)
			if itemType and itemType:getId() ~= 0 then
				return itemType
			end
		end

		return nil
	end

	local function resolveCurrencyId(currency)
		if not currency then
			return 0
		end

		local itemType = resolveItemType(currency, currency)
		return itemType and itemType:getId() or currency
	end

	local function registerItemShopPrice(itemId, buy, sell, npcName)
		local itemType = ItemType(itemId)
		if not itemType or itemType:getId() == 0 or not itemType.setBuyPrice then
			return
		end

		buy = tonumber(buy) or 0
		sell = tonumber(sell) or 0

		-- Convert token prices to gold equivalent when NPC uses custom currency
		if npcName then
			local npcData = compat.npcConfigs[npcName] or compat.npcConfigs[npcName:lower()]
			if npcData and npcData.currency and npcData.currency ~= 0 then
				local currencyType = ItemType(npcData.currency)
				if currencyType and currencyType:getId() ~= 0 then
					local worth = currencyType:getWorth()
					if worth and worth > 0 then
						buy = buy * worth
						sell = sell * worth
					end
				end
			end
		end

		if buy > 0 then
			itemType:setBuyPrice(buy)
		end
		if sell > 0 then
			itemType:setSellPrice(sell)
		end
		if ItemPriceRegistry and ItemPriceRegistry.register then
			ItemPriceRegistry.register(itemId, buy, sell, npcName)
		end
	end

	local function normalizeShopItems(items, npcName)
		local normalized = {}
		if type(items) ~= "table" then
			return normalized
		end

		for _, item in ipairs(items) do
			local itemType = resolveItemType(item.id or item.itemId or item.itemid or item.itemName or item.itemname or item.name, item.clientId or item.clientid)
			if itemType and itemType:getId() ~= 0 then
				registerItemShopPrice(itemType:getId(), item.buy, item.sell, npcName)
				normalized[#normalized + 1] = {
					id = itemType:getId(),
					subType = item.subType or item.subtype or item.count or item.charges or (itemType:isFluidContainer() and 0 or 1),
					buy = item.buy or 0,
					sell = item.sell or 0,
					name = item.itemName or item.itemname or item.name or itemType:getName(),
				}
			end
		end

		return normalized
	end

	local function findShopItem(items, itemId, subType)
		for _, item in ipairs(items) do
			if item.id == itemId and (item.subType == subType or item.subType == 0 or subType == 0) then
				return item
			end
		end

		for _, item in ipairs(items) do
			if item.id == itemId then
				return item
			end
		end

		return nil
	end

	local function normalizeOutfit(outfit)
		if type(outfit) ~= "table" then
			return outfit
		end

		outfit.lookType = outfit.lookType or outfit.type or outfit.looktype
		outfit.lookTypeEx = outfit.lookTypeEx or outfit.typeEx or outfit.typeex or outfit.looktypeex
		outfit.lookAddons = outfit.lookAddons or outfit.addons or outfit.lookaddons
		return outfit
	end

	local function getNpcData(npcName)
		if not npcName then
			return nil
		end

		return compat.npcConfigs[npcName] or compat.npcConfigs[npcName:lower()]
	end

	local function getCategoryNames(itemsTable, selectedCategory)
		local categories = {}
		if type(itemsTable) ~= "table" then
			return categories
		end

		selectedCategory = selectedCategory and selectedCategory:lower() or nil
		for categoryName in pairs(itemsTable) do
			if type(categoryName) == "string" and categoryName:lower() ~= selectedCategory then
				categories[#categories + 1] = categoryName
			end
		end

		table.sort(categories)
		return categories
	end

	function GetFormattedShopCategoryNames(itemsTable)
		local categories = {}

		if type(itemsTable) == "table" then
			if #itemsTable > 0 and type(itemsTable[1]) == "string" then
				for index = 1, #itemsTable do
					categories[#categories + 1] = itemsTable[index]
				end
			else
				for categoryName in pairs(itemsTable) do
					if type(categoryName) == "string" then
						categories[#categories + 1] = categoryName
					end
				end
			end
		end

		table.sort(categories)
		for index = 1, #categories do
			categories[index] = "{" .. categories[index] .. "}"
		end

		if #categories == 0 then
			return "other categories"
		end
		if #categories == 1 then
			return categories[1]
		end
		if #categories == 2 then
			return categories[1] .. " and " .. categories[2]
		end

		return table.concat(categories, ", ", 1, #categories - 1) .. " and " .. categories[#categories]
	end

	if not compat.originalNpcHandlerNew then
		compat.originalNpcHandlerNew = NpcHandler.new

		function NpcHandler:new(keywordHandler)
			local handler = compat.originalNpcHandlerNew(self, keywordHandler)
			NpcHandler.__modernCompatLastCreated = handler
			return handler
		end
	end

	if not compat.originalNpcHandlerSay then
		compat.originalNpcHandlerSay = NpcHandler.say

		function NpcHandler:say(message, focus, publicize, shallDelay, delay)
			local actualFocus = focus
			local actualPublicize = publicize

			if type(focus) ~= "number" then
				if type(publicize) == "number" or type(publicize) == "userdata" then
					actualFocus = getPlayerId(publicize)
					actualPublicize = false
				else
					actualFocus = getPlayerId(focus)
				end
			end

			return compat.originalNpcHandlerSay(self, message, actualFocus, actualPublicize, shallDelay, delay)
		end
	end

	if not compat.originalNpcHandlerResetNpc then
		compat.originalNpcHandlerResetNpc = NpcHandler.resetNpc

		function NpcHandler:resetNpc(target)
			if target == nil then
				return compat.originalNpcHandlerResetNpc(self)
			end

			return compat.originalNpcHandlerResetNpc(self, getPlayerId(target))
		end
	end

	if not compat.originalNpcHandlerGreet then
		compat.originalNpcHandlerGreet = NpcHandler.greet

		function NpcHandler:greet(target, creature)
			return compat.originalNpcHandlerGreet(self, getPlayerId(creature or target))
		end
	end

	if not compat.originalNpcHandlerOnThink then
		compat.originalNpcHandlerOnThink = NpcHandler.onThink

		function NpcHandler:onThink(npc, interval)
			local result = compat.originalNpcHandlerOnThink(self)

			local npcObj = npc or Npc()
			if npcObj then
				local npcData = getNpcData(npcObj:getName())
				if npcData and npcData.respawnType and npcData.respawnType.period and npcData.respawnType.period ~= RESPAWNPERIOD_ALL then
					local worldTime = getWorldTime()
					local isNight = (worldTime >= 1080 or worldTime <= 360) 
					local isDay = not isNight

					local shouldBeHidden = false
					if npcData.respawnType.period == RESPAWNPERIOD_DAY and isNight then
						shouldBeHidden = true
					elseif npcData.respawnType.period == RESPAWNPERIOD_NIGHT and isDay then
						shouldBeHidden = true
					end

					if shouldBeHidden then
						local spawnName = npcObj:getName()
						local spawnPos = npcObj:getPosition()
						local spawnDir = npcObj:getDirection()
						local period = npcData.respawnType.period
						-- We must capture the master position so the NPC can move later
						-- Some Npcs created dynamically might lack getMasterPos, fallback to spawnPos
						local masterRadius = 2

						spawnPos:sendMagicEffect(CONST_ME_POFF)
						npcObj:remove()

						local function checkRespawn()
							local wt = getWorldTime()
							local night = (wt >= 1080 or wt <= 360)
							local day = not night

							local shouldSpawn = false
							if period == RESPAWNPERIOD_DAY and day then
								shouldSpawn = true
							elseif period == RESPAWNPERIOD_NIGHT and night then
								shouldSpawn = true
							end

							if shouldSpawn then
								local newNpc = Game.createNpc(spawnName, spawnPos)
								if newNpc then
									newNpc:setDirection(spawnDir)
									-- Master radius is essential for the NPC to move freely
									newNpc:setMasterPos(spawnPos, masterRadius)
									spawnPos:sendMagicEffect(CONST_ME_TELEPORT)
								else
									addEvent(checkRespawn, 10000)
								end
							else
								addEvent(checkRespawn, 10000)
							end
						end
						
						addEvent(checkRespawn, 10000)
						return result
					end
				end	
			end

			return result
		end
	end

	if not compat.originalNpcHandlerUnGreet then
		compat.originalNpcHandlerUnGreet = NpcHandler.unGreet

		function NpcHandler:unGreet(target, creature)
			return compat.originalNpcHandlerUnGreet(self, getPlayerId(creature or target))
		end
	end

	function NpcHandler:checkInteraction(npc, creature)
		local playerId = getPlayerId(creature or npc)
		return playerId ~= 0 and self:isFocused(playerId) and self:isInRange(playerId)
	end

	function NpcHandler:setTopic(target, value)
		self.topic[getPlayerId(target)] = value
	end

	function NpcHandler:getTopic(target)
		return self.topic[getPlayerId(target)] or 0
	end

	function NpcHandler:addInteraction(npc, creature)
		local playerId = getPlayerId(creature or npc)
		if playerId == 0 then
			return false
		end

		self:addFocus(playerId)
		return true
	end

	function NpcHandler:setInteraction(npc, creature)
		return self:addInteraction(npc, creature)
	end

	function NpcHandler:removeInteraction(npc, creature)
		local playerId = getPlayerId(creature or npc)
		if playerId == 0 then
			return false
		end

		self:releaseFocus(playerId)
		return true
	end

	function NpcHandler:onAppear(npc, creature)
		local target = getCreatureObject(creature or npc)
		if not target then return end
		return self:onCreatureAppear(target)
	end

	function NpcHandler:onDisappear(npc, creature)
		local target = getCreatureObject(creature or npc)
		if not target then return end
		return self:onCreatureDisappear(target)
	end

	function NpcHandler:onSay(npc, creature, messageType, message)
		local target = getCreatureObject(creature)
		if not target then return end
		return self:onCreatureSay(target, messageType, message)
	end

	if not compat.originalNpcHandlerOnCreatureSay then
		compat.originalNpcHandlerOnCreatureSay = NpcHandler.onCreatureSay

		function NpcHandler:onCreatureSay(creature, messageType, message)
			local playerId = getPlayerId(creature)
			if playerId == 0 then
				return compat.originalNpcHandlerOnCreatureSay(self, creature, messageType, message)
			end

			if not self:isFocused(playerId) then
				return compat.originalNpcHandlerOnCreatureSay(self, creature, messageType, message)
			end

			local callback = self:getCallback(CALLBACK_CREATURE_SAY)
			if callback ~= nil and not callback(playerId, messageType, message) then
				return false
			end

			if not self:processModuleCallback(CALLBACK_CREATURE_SAY, playerId, messageType, message) then
				return false
			end

			if not self:isInRange(playerId) then
				return false
			end

			if self.keywordHandler and self.keywordHandler:processMessage(playerId, message) then
				self.talkStart[playerId] = os.time()
				return true
			end

			local defaultCallback = self:getCallback(CALLBACK_MESSAGE_DEFAULT)
			if defaultCallback and defaultCallback(playerId, messageType, message) then
				self.talkStart[playerId] = os.time()
				return true
			end

			return false
		end
	end

	function NpcHandler:onCloseChannel(npc, creature)
		local target = getCreatureObject(creature or npc)
		if not target then return end
		return self:onPlayerCloseChannel(target)
	end

	function NpcHandler:onMove(npc, creature, fromPosition, toPosition)
		local callback = self:getCallback(CALLBACK_ON_MOVE)
		if not callback then
			return true
		end

		return callModernCallback(callback, getCurrentNpc(self) or npc, getCreatureObject(creature), fromPosition, toPosition)
	end

	-- Save original C++ Npc:openShopWindow once; never overwrite with our Lua wrapper
	if not compat.originalNpcOpenShopWindow then
		compat.originalNpcOpenShopWindow = Npc.openShopWindow
	end

	local fmt = string.format

	function Npc:sellItem(player, itemId, amount, subType, actionId, ignoreCap, inBackpacks)
		local playerId = getPlayerId(player)
		local soldAmount, backpackCount, reason = doNpcSellItem(playerId, itemId, amount, subType, false, inBackpacks, ITEM_BACKPACK)
		compat.lastSellResult[playerId] = {
			amount = soldAmount or 0,
			backpacks = backpackCount or 0,
			reason = reason,
		}
		return soldAmount == amount
	end

	function Npc:getCurrency()
		local npcData = getNpcData(self:getName())
		return npcData and npcData.currency or 0
	end

	function Npc:getRemainingShopCategories(selectedCategory, itemsTable)
		return GetFormattedShopCategoryNames(getCategoryNames(itemsTable, selectedCategory))
	end

	local function openCompatShopWindow(npc, player, itemsTable)
		local playerObject = getPlayerObject(player)
		if not playerObject then
			return false
		end

		local npcData = getNpcData(npc:getName())
		if not npcData then
			return false
		end

		local shopItems = normalizeShopItems(itemsTable, npc:getName())
		local currency = npcData.currency or 0
		local onBuyItem = npcData.onBuyItem
		local onSellItem = npcData.onSellItem

		return compat.originalNpcOpenShopWindow(npc, playerObject, shopItems,
			function(playerObj, itemId, subType, amount, ignoreCap, inBackpacks)
				local playerId = playerObj:getId()
				if not checkNpcShopBuyExhaust(playerObj) then
					return false
				end

				local shopItem = findShopItem(shopItems, itemId, subType)
				if not shopItem or shopItem.buy <= 0 then
					return false
				end

				local totalCost = shopItem.buy * amount
				if currency ~= 0 then
					if playerObj:getItemCount(currency) < totalCost then
						return false
					end
				elseif playerObj:getTotalMoney() < totalCost then
					return false
				end

				compat.lastSellResult[playerId] = nil
				if onBuyItem then
					onBuyItem(npc, playerObj, itemId, subType, amount, ignoreCap, inBackpacks, totalCost)
				else
					npc:sellItem(playerObj, itemId, amount, subType, 0, ignoreCap, inBackpacks)
				end

				local sellResult = compat.lastSellResult[playerId]
				if sellResult and sellResult.amount < amount then
					if sellResult.reason == "capacity" then
						playerObj:sendCancelMessage("You do not have enough capacity.")
					else
						playerObj:sendCancelMessage("You do not have enough room.")
					end
					return false
				end

				if currency ~= 0 then
					if not playerObj:removeItem(currency, totalCost) then
						return false
					end
				elseif not playerObj:removeTotalMoney(totalCost) then
					return false
				end

				npc:say(fmt("Bought %ix {%s} for {%i} gold.", amount, ItemType(itemId):getName(), totalCost), TALKTYPE_PRIVATE_NP, false, playerObj)
				return true
			end,
			function(playerObj, itemId, subType, amount, ignoreEquipped, _)
				local shopItem = findShopItem(shopItems, itemId, subType)
				if not shopItem or shopItem.sell <= 0 then
					return false
				end

				local removeSubType = subType
				if not ItemType(itemId):isFluidContainer() then
					removeSubType = -1
				end

				if not playerObj:removeItem(itemId, amount, removeSubType, ignoreEquipped) then
					return false
				end

				local totalCost = shopItem.sell * amount
				if currency ~= 0 then
					playerObj:addItem(currency, totalCost)
				else
					playerObj:addMoney(totalCost)
				end

				if onSellItem then
					onSellItem(npc, playerObj, itemId, subType, amount, ignoreEquipped, shopItem.name, totalCost)
				end

				npc:say(fmt("Sold %ix {%s} for {%i} gold.", amount, shopItem.name, totalCost), TALKTYPE_PRIVATE_NP, false, playerObj)
				return true
			end)
	end

	function Npc:openShopWindow(player, items, buyCallback, sellCallback)
		if type(items) == "table" then
			if buyCallback == nil and sellCallback == nil then
				return openCompatShopWindow(self, player, items)
			end
			return compat.originalNpcOpenShopWindow(self, player, items, buyCallback, sellCallback)
		end

		local npcData = getNpcData(self:getName())
		if not npcData then
			return false
		end

		return openCompatShopWindow(self, player, npcData.shopSource or npcData.shopItems)
	end

	function Npc:openShopWindowTable(player, itemsTable)
		return openCompatShopWindow(self, player, itemsTable)
	end

	local function wrapHandlerCallback(handler, callbackId, wrapperFactory)
		local callback = handler:getCallback(callbackId)
		if not callback then
			return
		end

		handler.__modernCompatWrappedCallbacks = handler.__modernCompatWrappedCallbacks or {}
		if handler.__modernCompatWrappedCallbacks[callbackId] then
			return
		end

		handler.__modernCompatWrappedCallbacks[callbackId] = true
		handler:setCallback(callbackId, wrapperFactory(callback))
	end

	local function registerTradeKeyword(handler)
		if handler.__modernCompatTradeKeyword or not handler.keywordHandler then
			return
		end

		handler.__modernCompatTradeKeyword = true
		handler.keywordHandler:addKeyword({ "trade" }, function(cid)
			local player = Player(cid)
			local npc = getCurrentNpc(handler)
			if not player or not npc or not handler:isFocused(cid) then
				return false
			end

			if not handler:onTradeRequest(cid) then
				return false
			end

			local npcData = getNpcData(npc:getName())
			if npcData then
				openCompatShopWindow(npc, player, npcData.shopSource or npcData.shopItems)
			end

			handler:say(handler:getMessage(MESSAGE_SENDTRADE), cid)
			return true
		end, {})
	end

	function NpcType:register(npcConfig)
		npcConfig = npcConfig or {}

		local npcName = npcConfig.name or self:name()
		local compatData = RevNpcTypeCompatGetData and RevNpcTypeCompatGetData(self) or {}
		local handler = NpcHandler.__modernCompatLastCreated
		local shopItems = normalizeShopItems(npcConfig.shop, npcName)
		local currency = resolveCurrencyId(npcConfig.currency)
		local speechBubble = npcConfig.speechBubble or npcConfig.speechbubble or 0

		if #shopItems > 0 then
			if speechBubble == 3 then
				speechBubble = 4
			elseif speechBubble == 0 then
				speechBubble = 2
			end
		end

		if npcConfig.health then
			self:health(npcConfig.health)
		end
		if npcConfig.maxHealth then
			self:maxHealth(npcConfig.maxHealth)
		end
		if npcConfig.walkInterval ~= nil then
			self:walkInterval(npcConfig.walkInterval)
		end
		if npcConfig.walkSpeed ~= nil then
			self:walkSpeed(npcConfig.walkSpeed)
		end
		if npcConfig.walkRadius ~= nil then
			self:spawnRadius(npcConfig.walkRadius)
		end
		if npcConfig.outfit then
			self:outfit(normalizeOutfit(npcConfig.outfit))
		end
		if speechBubble ~= nil then
			self:speechBubble(speechBubble)
		end
		if npcConfig.flags then
			if npcConfig.flags.floorchange ~= nil then
				self:floorChange(npcConfig.flags.floorchange)
			end
			if npcConfig.flags.attackable ~= nil then
				self:attackable(npcConfig.flags.attackable)
			end
			if npcConfig.flags.ignoreHeight ~= nil then
				self:ignoreHeight(npcConfig.flags.ignoreHeight)
			end
			if npcConfig.flags.ignoreheight ~= nil then
				self:ignoreHeight(npcConfig.flags.ignoreheight)
			end
			if npcConfig.flags.isIdle ~= nil then
				self:isIdle(npcConfig.flags.isIdle)
			end
			if npcConfig.flags.isidle ~= nil then
				self:isIdle(npcConfig.flags.isidle)
			end
			if npcConfig.flags.pushable ~= nil then
				self:pushable(npcConfig.flags.pushable)
			end
		end

		-- The native NpcType methods live in the metatable __index. Assigning
		-- self[luaField] alone only changes Lua state; it does not register the
		-- callback reference that the C++ engine calls during NPC ticks.
		local npcTypeMeta = getmetatable(self)
		local npcTypeIndex = npcTypeMeta and npcTypeMeta.__index or nil

		local function getNativeNpcTypeMethod(methodName)
			if type(npcTypeIndex) == "table" and type(npcTypeIndex[methodName]) == "function" then
				return npcTypeIndex[methodName]
			end

			return nil
		end

		-- Essential callbacks must exist on both sides. compatData tells us
		-- whether the RevScript already provided a real callback; when it did
		-- not, install a harmless stub and register that same stub natively.
		local essentialEvents = { "onSay", "onAppear", "onThink" }
		for _, luaField in ipairs(essentialEvents) do
			if type(compatData[luaField]) ~= "function" then
				local stub = function()
					return true
				end
				local nativeMethod = getNativeNpcTypeMethod(luaField)

				self[luaField] = stub
				self:eventType(luaField:gsub("on", ""):lower())
				if nativeMethod then
					nativeMethod(self, stub)
				end
			end
		end

		compat.npcConfigs[npcName] = {
			name = npcName,
			currency = currency,
			shopItems = shopItems,
			shopSource = npcConfig.shop or {},
			onBuyItem = compatData.onBuyItem,
			onSellItem = compatData.onSellItem,
			onCheckItem = compatData.onCheckItem,
			respawnType = npcConfig.respawnType,
		}
		compat.npcConfigs[npcName:lower()] = compat.npcConfigs[npcName]

		if handler then
			handler.__modernCompatNpcName = npcName

			wrapHandlerCallback(handler, CALLBACK_GREET, function(callback)
				return function(cid)
					return callModernCallback(callback, getCurrentNpc(handler), getCreatureObject(cid))
				end
			end)

			wrapHandlerCallback(handler, CALLBACK_MESSAGE_DEFAULT, function(callback)
				return function(cid, messageType, message)
					return callModernCallback(callback, getCurrentNpc(handler), getCreatureObject(cid), messageType, message)
				end
			end)

			wrapHandlerCallback(handler, CALLBACK_ONTRADEREQUEST, function(callback)
				return function(cid)
					return callModernCallback(callback, getCurrentNpc(handler), getCreatureObject(cid))
				end
			end)

			wrapHandlerCallback(handler, CALLBACK_ONADDFOCUS, function(callback)
				return function(cid)
					return callModernCallback(callback, getCurrentNpc(handler), getCreatureObject(cid))
				end
			end)

			wrapHandlerCallback(handler, CALLBACK_ONRELEASEFOCUS, function(callback)
				return function(cid)
					return callModernCallback(callback, getCurrentNpc(handler), getCreatureObject(cid))
				end
			end)

			if #shopItems > 0 then
				registerTradeKeyword(handler)
			end
		end

		if npcConfig.voices then
			local h = NpcsHandler(self)
			h:talk(npcConfig.voices, npcConfig.voices.interval)
		end

		return true
	end

	-- =====================================================================
	-- KeywordHandler Compatibility Methods
	-- =====================================================================
	-- Modern NPC scripts call keywordHandler:addGreetKeyword({...}, {npcHandler=..., text=...})
	-- These methods don't exist in the classic Jiddo NPC System.
	-- We add them here (in the NPC Lua environment) where KeywordHandler IS defined.

	if not KeywordHandler.__compatGreetKeywordAdded then
		KeywordHandler.__compatGreetKeywordAdded = true

		function KeywordHandler:addGreetKeyword(keywords, parameters, startCallback, ...)
			if type(keywords) ~= "table" then
				return nil
			end

			local params = parameters
			local callback = nil

			-- If the second argument is a function, it's a direct callback
			if type(parameters) == "function" then
				callback = parameters
				params = startCallback or {}
			end

			local handler = params and params.npcHandler or NpcHandler.__modernCompatLastCreated
			if not handler then
				return nil
			end

			local function parseText(cid, text)
				local player = Player(cid)
				local playerName = player and player:getName() or ""
				local parseInfo = { [TAG_PLAYERNAME] = playerName }
				if type(text) == "table" then
					local parsed = {}
					for index = 1, #text do
						parsed[index] = handler:parseMessage(text[index], parseInfo)
					end
					return parsed
				end
				return handler:parseMessage(text, parseInfo)
			end

			local function greet(cid, message)
				local player = Player(cid)
				if not player then
					return false
				end

				if type(startCallback) == "function" and not startCallback(player) then
					return false
				end

				if callback then
					local result = callback(getCurrentNpc(handler), player, message)
					if result == false then
						return false
					end
				elseif params and params.text then
					if not handler:isFocused(cid) then
						handler:addInteraction(getCurrentNpc(handler), player)
					end
					handler:say(
						parseText(cid, params.text),
						cid,
						params.publicize and true or false,
						nil,
						params.delay or params.interval
					)
				else
					handler:greet(cid)
				end

				if not handler:isFocused(cid) then
					handler:addInteraction(getCurrentNpc(handler), player)
				end
				handler.talkStart[cid] = os.time()
				return true
			end

			local lastNode = nil
			for _, keyword in ipairs(keywords) do
				lastNode = self:addKeyword({ keyword }, function(cid, message)
					return greet(cid, message)
				end, params)
			end
			return lastNode
		end

		function KeywordHandler:addCustomGreetKeyword(keywords, parameters, startCallback, ...)
			return self:addGreetKeyword(keywords, parameters, startCallback, ...)
		end

		function KeywordHandler:addFarewellKeyword(keywords, parameters, startCallback, ...)
			if type(keywords) ~= "table" then
				return nil
			end

			local params = parameters or {}
			local handler = params.npcHandler or NpcHandler.__modernCompatLastCreated
			if not handler then
				return nil
			end

			local function farewell(cid)
				local player = Player(cid)
				if not player then
					return false
				end

				if type(startCallback) == "function" and not startCallback(player) then
					return false
				end

				if params.text then
					local parseInfo = { [TAG_PLAYERNAME] = player:getName() }
					local text = params.text
					if type(text) == "table" then
						local parsed = {}
						for index = 1, #text do
							parsed[index] = handler:parseMessage(text[index], parseInfo)
						end
						text = parsed
					else
						text = handler:parseMessage(text, parseInfo)
					end
					handler:say(text, cid)
				else
					handler:unGreet(cid)
				end

				handler:releaseFocus(cid)
				return true
			end

			local lastNode = nil
			for _, keyword in ipairs(keywords) do
				lastNode = self:addKeyword({ keyword }, function(cid)
					return farewell(cid)
				end, params)
			end
			return lastNode
		end
	end

	-- DATA_DIRECTORY global used by some NPC scripts for dofile paths
	DATA_DIRECTORY = DATA_DIRECTORY or "data/"

	-- string.titleCase compatibility
	if not string.titleCase then
		function string:titleCase()
			return self:gsub("(%a)([%w_']*)", function(first, rest)
				return first:upper() .. rest:lower()
			end)
		end
	end

	-- TOWNS_LIST compatibility
	TOWNS_LIST = {
		AB_DENDRIEL = 1,
		ANKRAHMUN = 2,
		CARLIN = 3,
		DARASHIA = 4,
		EDRON = 5,
		FARMINE = 6,
		GRAY_BEACH = 7,
		KAZORDOON = 8,
		LIBERTY_BAY = 9,
		PORT_HOPE = 10,
		ROSHAMUUL = 11,
		SVARGROND = 12,
		THAIS = 13,
		VENORE = 14,
		YALAHAR = 15,
		RATHLETON = 16,
		DAWNPORT = 17,
		FEYRIST = 18,
	}
end
