-- Custom Market PacketHandler
-- OTC sends 0xF4..0xF7 and 0xE0..0xE1. Server responds with 0xDB.
-- Item ids are traded directly, no wareId/ThingAttrMarket dependency.

if not configManager.getBoolean(configKeys.MARKET_SYSTEM_ENABLED) then
	CustomMarket = nil
	return
end

local MARKET_ITEM_ID = ITEM_MARKET or 12903

local OPCODE_MARKET_OPEN = 0xF4
local OPCODE_MARKET_LEAVE = 0xF5
local OPCODE_MARKET_BROWSE = 0xF6
local OPCODE_MARKET_CREATE = 0xF7
local OPCODE_MARKET_CANCEL = 0xE0
local OPCODE_MARKET_ACCEPT = 0xE1
local OPCODE_MARKET_SEND = 0xDB

local RESP_MESSAGE = 0x00
local RESP_ENTER = 0x01
local RESP_LEAVE = 0x02
local RESP_BROWSE = 0x03
local RESP_DETAIL = 0x04

local MARKET_ACTION_BUY = 0
local MARKET_ACTION_SELL = 1

local MARKET_DESC_ARMOR = 1
local MARKET_DESC_ATTACK = 2
local MARKET_DESC_CONTAINER = 3
local MARKET_DESC_DEFENSE = 4
local MARKET_DESC_GENERAL = 5
local MARKET_DESC_DECAY_TIME = 6
local MARKET_DESC_COMBAT = 7
local MARKET_DESC_MIN_LEVEL = 8
local MARKET_DESC_MIN_MAGIC_LEVEL = 9
local MARKET_DESC_VOCATION = 10
local MARKET_DESC_RUNE = 11
local MARKET_DESC_ABILITY = 12
local MARKET_DESC_CHARGES = 13
local MARKET_DESC_WEAPON = 14
local MARKET_DESC_WEIGHT = 15
local MARKET_DESC_IMBUEMENTS = 16
local MARKET_DESC_CLASSIFICATION = 17
local MARKET_DESC_TIER = 18

local MARKET_REQUEST_MY_OFFERS = 0xFFFE
local MARKET_REQUEST_MY_HISTORY = 0xFFFF

local MARKET_STATE_ACTIVE = 0
local MARKET_STATE_CANCELLED = 1
local MARKET_STATE_EXPIRED = 2
local MARKET_STATE_ACCEPTED = 3

local MARKET_MAX_OFFERS = 100
local MARKET_MAX_AMOUNT = 2000
local MARKET_MAX_AMOUNT_STACKABLE = 64000
local MARKET_MAX_PRICE = 999999999
local MARKET_MAX_PACKET_OFFERS = 250
local MARKET_CATALOG_CHUNK_SIZE = 350
local MARKET_ACTION_DELAY = 1
local MARKET_EXPIRE_CHECK_INTERVAL = 60
local MARKET_DEFAULT_DURATION = 30 * 24 * 60 * 60
local MARKET_STATISTICS_DAYS = 30
local MARKET_DEPOT_BOX_FIRST = 1
local MARKET_DEPOT_BOX_LAST = 15

local CATEGORY_ARMORS = 1
local CATEGORY_AMULETS = 2
local CATEGORY_BOOTS = 3
local CATEGORY_CONTAINERS = 4
local CATEGORY_FOOD = 6
local CATEGORY_HELMETS = 7
local CATEGORY_LEGS = 8
local CATEGORY_OTHERS = 9
local CATEGORY_POTIONS = 10
local CATEGORY_RINGS = 11
local CATEGORY_RUNES = 12
local CATEGORY_SHIELDS = 13
local CATEGORY_TOOLS = 14
local CATEGORY_VALUABLES = 15
local CATEGORY_AMMUNITION = 16
local CATEGORY_AXES = 17
local CATEGORY_CLUBS = 18
local CATEGORY_DISTANCE = 19
local CATEGORY_SWORDS = 20
local CATEGORY_WANDS = 21
local CATEGORY_GOLD = 30

local marketItems = {}
local marketItemsById = {}
local marketItemXmlAttributes = {}
local lastAction = {}
local marketDepotSessions = {}
local marketOpenSessions = {}
local lastExpireCheck = 0

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

-- ============================================================
-- ANTI-DUPE LOCK SYSTEM
-- Lua is single-threaded in TFS, so this table-based lock is
-- sufficient to prevent the same offer being processed twice.
-- Keys: "offer:<id>", "player:<guid>", "owner:<guid>"
-- ============================================================
local marketLocks = {}

local function acquireLock(key)
	if marketLocks[key] then
		return false
	end
	marketLocks[key] = true
	return true
end

local function releaseLock(key)
	marketLocks[key] = nil
end

-- Acquire multiple locks at once. Returns true only if ALL succeed.
-- If any fails, already-acquired ones are released automatically.
local function acquireMultipleLocks(keys)
	local acquired = {}
	for _, key in ipairs(keys) do
		if not acquireLock(key) then
			for _, k in ipairs(acquired) do
				releaseLock(k)
			end
			return false
		end
		acquired[#acquired + 1] = key
	end
	return true
end

local function releaseMultipleLocks(keys)
	for _, key in ipairs(keys) do
		releaseLock(key)
	end
end

local function hasSerializedAttributes(attributes)
	return type(attributes) == "string" and #attributes > 0
end

local function emptyAttributesSql()
	return db.escapeBlob and db.escapeBlob("", 0) or "''"
end

local function escapeAttributes(attributes)
	if hasSerializedAttributes(attributes) then
		return db.escapeBlob and db.escapeBlob(attributes, #attributes) or db.escapeString(attributes)
	end
	return emptyAttributesSql()
end

-- Rollback a DB-claimed offer (used when delivery fails after DB claim).
-- For full-amount offers (deleted): tries to re-INSERT with original data.
-- For partial offers (updated): restores the subtracted amount.
local function rollbackOfferClaim(offer, acceptedAmount)
	if acceptedAmount >= offer.amount then
		-- Full offer was DELETE'd — restore it
		return db.query(
			"INSERT INTO `market_offers` (`id`, `player_id`, `sale`, `itemtype`, `amount`, `created`, `anonymous`, `price`, `tier`, `attributes`) VALUES (" ..
			offer.id .. ", " .. offer.playerId .. ", " .. offer.sale .. ", " .. offer.itemId .. ", " ..
			offer.amount .. ", " .. offer.created .. ", " .. (offer.anonymous and 1 or 0) .. ", " .. offer.price .. ", " ..
			math.max(0, math.min(10, tonumber(offer.tier) or 0)) .. ", " ..
			escapeAttributes(offer.attributes) .. ")"
		)
	else
		-- Partial offer: restore subtracted amount
		return db.query(
			"UPDATE `market_offers` SET `amount` = `amount` + " .. acceptedAmount ..
			" WHERE `id` = " .. offer.id
		)
	end
end

local blockedItems = {}
for _, itemId in ipairs({
	_G.ITEM_GOLD_COIN,
	_G.ITEM_PLATINUM_COIN,
	_G.ITEM_CRYSTAL_COIN,
	_G.ITEM_GOLD_NUGGET,
	MARKET_ITEM_ID
}) do
	if itemId then
		blockedItems[itemId] = true
	end
end

local function logInfo(message)
	if logger and logger.info then
		logger.info(message)
	else
		print(message)
	end
end

local function logError(message)
	if logger and logger.error then
		logger.error(message)
	else
		print(message)
	end
end

local function clamp(value, minValue, maxValue)
	value = tonumber(value) or minValue
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local tableExistsCache = {}

local function tableExists(name)
	if tableExistsCache[name] ~= nil then
		return tableExistsCache[name]
	end

	local exists = true
	if db.tableExists then
		exists = db.tableExists(name)
	end
	tableExistsCache[name] = exists
	return exists
end

local function columnExists(tableName, columnName)
	local resultId = db.storeQuery("SHOW COLUMNS FROM `" .. tableName .. "` LIKE " .. db.escapeString(columnName))
	if resultId ~= false then
		result.free(resultId)
		return true
	end
	return false
end

local function ensureColumn(tableName, columnName, definition)
	if not tableExists(tableName) or columnExists(tableName, columnName) then
		return
	end
	db.query("ALTER TABLE `" .. tableName .. "` ADD COLUMN `" .. columnName .. "` " .. definition)
end

local function getResultAttributes(resultId, columnName)
	columnName = columnName or "attributes"
	if result.getStream then
		local attributes, size = result.getStream(resultId, columnName)
		if attributes and (tonumber(size) or 0) > 0 then
			return attributes
		end
		return nil
	end

	local attributes = result.getDataString(resultId, columnName)
	if hasSerializedAttributes(attributes) then
		return attributes
	end
	return nil
end

local function ensureTables()
	db.query([[
		CREATE TABLE IF NOT EXISTS `market_offers` (
			`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
			`player_id` INT NOT NULL,
			`sale` TINYINT(1) NOT NULL DEFAULT 0,
			`itemtype` SMALLINT UNSIGNED NOT NULL,
			`amount` SMALLINT UNSIGNED NOT NULL,
			`created` INT UNSIGNED NOT NULL,
			`anonymous` TINYINT(1) NOT NULL DEFAULT 0,
			`price` INT UNSIGNED NOT NULL DEFAULT 0,
			`tier` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`attributes` MEDIUMBLOB NULL,
			PRIMARY KEY (`id`),
			KEY `idx_market_offers_itemtype_sale` (`itemtype`, `sale`),
			KEY `idx_market_offers_player` (`player_id`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])

	ensureColumn("market_offers", "attributes", "MEDIUMBLOB NULL")
	ensureColumn("market_offers", "tier", "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `price`")

	db.query([[
		CREATE TABLE IF NOT EXISTS `market_history` (
			`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
			`player_id` INT NOT NULL,
			`sale` TINYINT(1) NOT NULL DEFAULT 0,
			`itemtype` SMALLINT UNSIGNED NOT NULL,
			`amount` SMALLINT UNSIGNED NOT NULL,
			`price` INT UNSIGNED NOT NULL DEFAULT 0,
			`tier` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`expires_at` INT UNSIGNED NOT NULL,
			`inserted` INT UNSIGNED NOT NULL,
			`state` TINYINT UNSIGNED NOT NULL,
			PRIMARY KEY (`id`),
			KEY `idx_market_history_player` (`player_id`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])

	ensureColumn("market_history", "tier", "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `price`")

	db.query([[
		CREATE TABLE IF NOT EXISTS `market_statistics` (
			`itemtype` SMALLINT UNSIGNED NOT NULL,
			`sale` TINYINT(1) NOT NULL DEFAULT 0,
			`day` INT UNSIGNED NOT NULL,
			`transactions` INT UNSIGNED NOT NULL DEFAULT 0,
			`total_price` BIGINT UNSIGNED NOT NULL DEFAULT 0,
			`highest_price` INT UNSIGNED NOT NULL DEFAULT 0,
			`lowest_price` INT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`itemtype`, `sale`, `day`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])
end

local function getOfferDuration()
	if configManager and configKeys and configKeys.MARKET_OFFER_DURATION then
		local ok, duration = pcall(function()
			return configManager.getNumber(configKeys.MARKET_OFFER_DURATION)
		end)
		duration = tonumber(ok and duration or nil)
		if duration and duration > 0 then
			return duration
		end
	end
	return MARKET_DEFAULT_DURATION
end

local function isOnCooldown(player)
	local pid = player:getId()
	local now = os.time()
	if lastAction[pid] and now - lastAction[pid] < MARKET_ACTION_DELAY then
		return true
	end
	lastAction[pid] = now
	return false
end

local function sendMarketMessage(player, message)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_MARKET_SEND)
	out:addByte(RESP_MESSAGE)
	out:addString(message)
	return out:sendToPlayer(player)
end

local function sendMarketLeave(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_MARKET_SEND)
	out:addByte(RESP_LEAVE)
	return out:sendToPlayer(player)
end

local function tileHasMarketAccess(tile)
	if not tile then
		return false
	end

	if tile:getItemByType(ITEM_TYPE_DEPOT) then
		return true
	end
	if tile.getItemById and tile:getItemById(MARKET_ITEM_ID) then
		return true
	end
	return false
end

local function hasMarketAccessAtPosition(player, position)
	if not player or not position then
		return false
	end

	local playerTile = Tile(position)
	if not playerTile or not playerTile:hasFlag(TILESTATE_PROTECTIONZONE) then
		return false
	end

	if tileHasMarketAccess(playerTile) then
		return true
	end

	for x = -1, 1 do
		for y = -1, 1 do
			if x ~= 0 or y ~= 0 then
				local tile = Tile(Position(position.x + x, position.y + y, position.z))
				if tileHasMarketAccess(tile) then
					return true
				end
			end
		end
	end
	return false
end

local function hasCurrentMarketAccess(player)
	return hasMarketAccessAtPosition(player, player:getPosition())
end

local function setMarketOpen(player)
	marketOpenSessions[player:getId()] = true
end

local function clearMarketOpen(player)
	marketOpenSessions[player:getId()] = nil
end

local function closeMarket(player, message)
	if not marketOpenSessions[player:getId()] then
		return false
	end
	clearMarketOpen(player)
	if message then
		sendMarketMessage(player, message)
	end
	sendMarketLeave(player)
	return true
end

local function ensureMarketAccess(player)
	if hasCurrentMarketAccess(player) then
		return true
	end
	if not closeMarket(player, "Market closed.") then
		sendMarketLeave(player)
	end
	return false
end

local function getItemCategory(itemType)
	if itemType:isRune() then
		return CATEGORY_RUNES
	end
	if itemType:isContainer() then
		return CATEGORY_CONTAINERS
	end
	local weaponType = itemType:getWeaponType()
	if weaponType == WEAPON_SWORD then
		return CATEGORY_SWORDS
	elseif weaponType == WEAPON_CLUB then
		return CATEGORY_CLUBS
	elseif weaponType == WEAPON_AXE then
		return CATEGORY_AXES
	elseif weaponType == WEAPON_DISTANCE then
		return CATEGORY_DISTANCE
	elseif weaponType == WEAPON_WAND then
		return CATEGORY_WANDS
	elseif weaponType == WEAPON_SHIELD then
		return CATEGORY_SHIELDS
	elseif weaponType == WEAPON_AMMO then
		return CATEGORY_AMMUNITION
	end
	if itemType:isHelmet() then
		return CATEGORY_HELMETS
	elseif itemType:isArmor() then
		return CATEGORY_ARMORS
	elseif itemType:isLegs() then
		return CATEGORY_LEGS
	elseif itemType:isBoots() then
		return CATEGORY_BOOTS
	elseif itemType:isNecklace() then
		return CATEGORY_AMULETS
	elseif itemType:isRing() then
		return CATEGORY_RINGS
	end
	if itemType:getWorth() > 0 then
		return CATEGORY_GOLD
	end

	local name = itemType:getName():lower()
	if name:find("potion", 1, true) then
		return CATEGORY_POTIONS
	elseif name:find("fish", 1, true) or name:find("meat", 1, true) or name:find("bread", 1, true) or name:find("ham", 1, true) then
		return CATEGORY_FOOD
	elseif name:find("rope", 1, true) or name:find("shovel", 1, true) or name:find("pick", 1, true) or name:find("machete", 1, true) then
		return CATEGORY_TOOLS
	elseif name:find("gem", 1, true) or name:find("crystal", 1, true) or name:find("pearl", 1, true) then
		return CATEGORY_VALUABLES
	end
	return CATEGORY_OTHERS
end

local function isMarketableItem(itemId)
	itemId = tonumber(itemId) or 0
	if itemId <= 0 or itemId > 0xFFFF or blockedItems[itemId] then
		return false
	end

	local itemType = ItemType(itemId)
	if not itemType or itemType:getId() == 0 then
		return false
	end

	local name = itemType:getName()
	if not name or name == "" then
		return false
	end

	if itemType:isCorpse() or itemType:isDoor() or itemType:isFluidContainer() or itemType:isMagicField() or itemType:isGroundTile() then
		return false
	end

	return itemType:isMovable() and itemType:isPickupable()
end

local function copyAttributes(attributes)
	if not attributes then
		return nil
	end

	local copy = {}
	for key, value in pairs(attributes) do
		copy[key] = value
	end
	return copy
end

local function readItemXmlAttributes(itemNode)
	local attributes = {}
	for attributeNode in itemNode:children() do
		if attributeNode:name() == "attribute" then
			local key = attributeNode:attribute("key")
			local value = attributeNode:attribute("value")
			if key and value then
				attributes[key:lower()] = value
			end
		end
	end
	return attributes
end

local function addMarketItem(itemId, xmlName, xmlAttributes)
	itemId = tonumber(itemId)
	if not itemId or marketItemsById[itemId] or not isMarketableItem(itemId) then
		return
	end

	local itemType = ItemType(itemId)
	local name = xmlName or itemType:getName()
	if not name or name == "" then
		return
	end

	local entry = {
		id = itemId,
		name = name,
		category = getItemCategory(itemType)
	}

	marketItemsById[itemId] = entry
	marketItemXmlAttributes[itemId] = copyAttributes(xmlAttributes)
	marketItems[#marketItems + 1] = entry
end

local function loadMarketCatalog()
	marketItems = {}
	marketItemsById = {}
	marketItemXmlAttributes = {}

	local xmlDoc = XMLDocument("data/items/items.xml")
	if not xmlDoc then
		logError("[CustomMarket] Could not load data/items/items.xml")
		return
	end

	local root = xmlDoc:child("items")
	if not root then
		logError("[CustomMarket] items.xml has no root items node")
		return
	end

	for itemNode in root:children() do
		if itemNode:name() == "item" then
			local id = tonumber(itemNode:attribute("id"))
			local fromId = tonumber(itemNode:attribute("fromid"))
			local toId = tonumber(itemNode:attribute("toid"))
			local name = itemNode:attribute("name")
			local attributes = readItemXmlAttributes(itemNode)

			if id then
				addMarketItem(id, name, attributes)
			elseif fromId and toId and toId >= fromId then
				for itemId = fromId, toId do
					addMarketItem(itemId, name, attributes)
				end
			end
		end
	end

	table.sort(marketItems, function(a, b)
		local nameA = a.name:lower()
		local nameB = b.name:lower()
		if nameA == nameB then
			return a.id < b.id
		end
		return nameA < nameB
	end)

	logInfo(string.format(">> Loading Custom Market System (%d tradeable items)", #marketItems))
end

local function getPlayerTotalMoney(player)
	local inventoryMoney = math.max(0, tonumber(player:getMoney()) or 0)
	local bankBalance = math.max(0, tonumber(player:getBankBalance()) or 0)
	return inventoryMoney + bankBalance
end

local function removePlayerMarketMoney(player, amount)
	amount = tonumber(amount) or 0
	if amount <= 0 then
		return { inventory = 0, bank = 0 }
	end

	local inventoryMoney = math.max(0, tonumber(player:getMoney()) or 0)
	local bankBalance = math.max(0, tonumber(player:getBankBalance()) or 0)
	if inventoryMoney + bankBalance < amount then
		return nil
	end

	local fromInventory = math.min(inventoryMoney, amount)
	if fromInventory > 0 and not player:removeMoney(fromInventory) then
		return nil
	end

	local fromBank = amount - fromInventory
	if fromBank > 0 then
		player:setBankBalance(bankBalance - fromBank)
	end

	return { inventory = fromInventory, bank = fromBank }
end

local function refundPlayerMarketMoney(player, payment)
	if not payment then
		return
	end
	if payment.inventory and payment.inventory > 0 then
		player:addMoney(payment.inventory)
	end
	if payment.bank and payment.bank > 0 then
		player:setBankBalance(player:getBankBalance() + payment.bank)
	end
end

local function normalizeDepotId(depotId)
	depotId = tonumber(depotId) or 0
	if depotId < 0 then
		return 0
	end
	if depotId > 0xFFFF then
		return 0xFFFF
	end
	return depotId
end

local function getPlayerLastDepotId(player)
	if player.getLastDepotId then
		local ok, depotId = pcall(function()
			return player:getLastDepotId()
		end)
		if ok then
			return normalizeDepotId(depotId)
		end
	end
	return 0
end

local function setMarketDepotId(player, depotId)
	marketDepotSessions[player:getId()] = normalizeDepotId(depotId)
end

local function getMarketDepotId(player)
	local depotId = marketDepotSessions[player:getId()]
	if depotId ~= nil then
		return depotId
	end
	return getPlayerLastDepotId(player)
end

local function getItemTradeCount(item, itemType)
	if itemType:isStackable() then
		return math.max(0, item:getCount() or 0)
	end
	return 1
end

local function getItemTier(item)
	if item and item.getTier then
		return math.max(0, math.min(10, tonumber(item:getTier()) or 0))
	end
	return 0
end

local function getDepotItemKey(itemId, tier)
	return tostring(itemId) .. ":" .. tostring(math.max(0, math.min(10, tonumber(tier) or 0)))
end

local function getMarketItemAttributes(item, itemType)
	if not item or not item.serializeAttributes then
		return nil
	end

	local attributes = item:serializeAttributes()
	if not hasSerializedAttributes(attributes) then
		return nil
	end

	-- Stackable market items are stored as quantity. Their normal count is
	-- serialized too, so only non-stackable instance data is kept per offer.
	if itemType and itemType:isStackable() then
		return nil
	end
	return attributes
end

local function createMarketItem(itemId, count, attributes)
	local item = Game.createItem(itemId, count)
	if not item then
		return nil
	end

	if hasSerializedAttributes(attributes) then
		if not item.unserializeAttributes or not item:unserializeAttributes(attributes) then
			item:remove()
			return nil
		end
	end
	return item
end

local function getAttributesTier(itemId, attributes)
	if not hasSerializedAttributes(attributes) then
		return 0
	end

	local item = createMarketItem(itemId, 1, attributes)
	if not item then
		return 0
	end

	local tier = 0
	if item.getTier then
		tier = tonumber(item:getTier()) or 0
	end
	item:remove()
	return math.max(0, math.min(10, tier))
end

local function getDepotBoxes(player)
	local boxes = {}
	local depotId = getMarketDepotId(player)
	for boxIndex = MARKET_DEPOT_BOX_FIRST, MARKET_DEPOT_BOX_LAST do
		local box = player:getDepotBox(depotId, boxIndex)
		if box then
			boxes[#boxes + 1] = box
		end
	end
	return boxes
end

local function getDepotItemAmount(player, itemId)
	local itemType = ItemType(itemId)
	if not itemType or itemType:getId() == 0 then
		return 0
	end

	local amount = 0
	for _, box in ipairs(getDepotBoxes(player)) do
		for _, item in ipairs(box:getItems(true)) do
			if item:getId() == itemId then
				amount = amount + getItemTradeCount(item, itemType)
				if amount >= 0xFFFF then
					return 0xFFFF
				end
			end
		end
	end
	return clamp(amount, 0, 0xFFFF)
end

local itemTypeStackableCache = {}

local function buildDepotItemMap(player)
	local depotMap = {}
	for _, box in ipairs(getDepotBoxes(player)) do
		for _, item in ipairs(box:getItems(true)) do
			local itemId = item:getId()
			local itemTypeInfo = itemTypeStackableCache[itemId]
			if itemTypeInfo == nil then
				local itemType = ItemType(itemId)
				itemTypeInfo = {
					isStackable = itemType and itemType:isStackable() or false,
					getId = itemType and itemType:getId() or 0
				}
				itemTypeStackableCache[itemId] = itemTypeInfo
			end
			if itemTypeInfo.getId ~= 0 then
				local count = itemTypeInfo.isStackable and math.max(0, item:getCount() or 0) or 1
				local tier = getItemTier(item)
				local totalAmount = (depotMap[itemId] or 0) + count
				local tierKey = getDepotItemKey(itemId, tier)
				local tierAmount = (depotMap[tierKey] or 0) + count
				depotMap[itemId] = math.min(totalAmount, 0xFFFF)
				depotMap[tierKey] = math.min(tierAmount, 0xFFFF)
			end
		end
	end
	return depotMap
end

local function buildMarketEnterEntries(depotMap)
	local entries = {}
	depotMap = depotMap or {}

	for _, entry in ipairs(marketItems) do
		entries[#entries + 1] = {
			id = entry.id,
			category = entry.category,
			name = entry.name,
			amount = depotMap[getDepotItemKey(entry.id, 0)] or 0,
			tier = 0
		}

		for tier = 1, 10 do
			local amount = depotMap[getDepotItemKey(entry.id, tier)] or 0
			if amount > 0 then
				entries[#entries + 1] = {
					id = entry.id,
					category = entry.category,
					name = entry.name,
					amount = amount,
					tier = tier
				}
			end
		end
	end

	return entries
end

local function addDepotItems(player, itemId, amount, attributes)
	local itemType = ItemType(itemId)
	if not itemType or itemType:getId() == 0 or amount <= 0 then
		return false
	end

	if hasSerializedAttributes(attributes) then
		if amount ~= 1 then
			return false
		end

		for _, box in ipairs(getDepotBoxes(player)) do
			local item = createMarketItem(itemId, 1, attributes)
			if item then
				local ret = box:addItemEx(item, INDEX_WHEREEVER, FLAG_NOLIMIT)
				if ret == RETURNVALUE_NOERROR then
					return true
				end
				item:remove()
			end
		end
		return false
	end

	local stackSize = math.max(1, itemType:getStackSize())
	local remaining = amount
	for _, box in ipairs(getDepotBoxes(player)) do
		while remaining > 0 do
			local count = itemType:isStackable() and math.min(remaining, stackSize) or 1
			local added = box:addItem(itemId, count)
			if not added then
				break
			end
			remaining = remaining - count
		end
		if remaining <= 0 then
			return true
		end
	end
	return false
end

local function collectDepotRemovals(player, itemId, amount, tier)
	local itemType = ItemType(itemId)
	if not itemType or itemType:getId() == 0 or amount <= 0 then
		return nil
	end

	local tierFilter = nil
	if tier ~= nil then
		tierFilter = math.max(0, math.min(10, tonumber(tier) or 0))
	end

	local removals = {}
	local found = 0
	for _, box in ipairs(getDepotBoxes(player)) do
		for _, item in ipairs(box:getItems(true)) do
			if item:getId() == itemId and (tierFilter == nil or getItemTier(item) == tierFilter) then
				local count = math.min(amount - found, getItemTradeCount(item, itemType))
				if count > 0 then
					removals[#removals + 1] = { item = item, count = count }
					found = found + count
					if found >= amount then
						return removals
					end
				end
			end
		end
	end
	return nil
end

local function removeDepotItemsWithAttributes(player, itemId, amount, tier)
	local removals = collectDepotRemovals(player, itemId, amount, tier)
	if not removals then
		return false, nil, tier ~= nil and "You do not have enough items for this tier." or nil
	end

	local itemType = ItemType(itemId)
	local storedAttributes = nil
	for _, entry in ipairs(removals) do
		local attributes = getMarketItemAttributes(entry.item, itemType)
		if hasSerializedAttributes(attributes) then
			if amount ~= 1 or entry.count ~= 1 then
				return false, nil, "Tiered/custom items must be traded one at a time."
			end
			storedAttributes = attributes
		end
	end

	local removed = 0
	for _, entry in ipairs(removals) do
		if not entry.item:remove(entry.count) then
			if removed > 0 then
				addDepotItems(player, itemId, removed)
			end
			return false, nil
		end
		removed = removed + entry.count
	end
	return true, storedAttributes
end

local function removeDepotItems(player, itemId, amount)
	local ok = removeDepotItemsWithAttributes(player, itemId, amount)
	return ok
end

local function getMarketOfferCount(playerId)
	if not tableExists("market_offers") then
		return 0
	end

	local resultId = db.storeQuery("SELECT COUNT(*) AS `total` FROM `market_offers` WHERE `player_id` = " .. playerId)
	if resultId == false then
		return 0
	end

	local total = result.getDataInt(resultId, "total")
	result.free(resultId)
	return total
end

local function calculateFee(price, amount)
	local fee = math.ceil((price * amount) / 100)
	if fee < 20 then
		return 20
	elseif fee > 1000 then
		return 1000
	end
	return fee
end

local function getNextInboxSid(playerId)
	local sid = 100
	local resultId = db.storeQuery("SELECT COALESCE(MAX(`sid`), 100) AS `sid` FROM `player_inboxitems` WHERE `player_id` = " .. playerId)
	if resultId ~= false then
		sid = result.getDataInt(resultId, "sid")
		result.free(resultId)
	end
	return sid + 1
end

local function insertInboxItem(playerId, itemId, amount, attributes)
	if not tableExists("player_inboxitems") then
		return false
	end

	local itemType = ItemType(itemId)
	if not itemType or itemType:getId() == 0 then
		return false
	end

	if hasSerializedAttributes(attributes) then
		if amount ~= 1 then
			return false
		end

		local sid = getNextInboxSid(playerId)
		local query = "INSERT INTO `player_inboxitems` (`player_id`, `sid`, `pid`, `itemtype`, `count`, `attributes`) VALUES (" ..
			playerId .. ", " .. sid .. ", 0, " .. itemId .. ", 1, " .. escapeAttributes(attributes) .. ")"
		return db.query(query)
	end

	local remaining = amount
	local sid = getNextInboxSid(playerId)
	while remaining > 0 do
		local count = 1
		if itemType:isStackable() then
			count = math.min(remaining, math.max(1, itemType:getStackSize()))
		end

		local attributesSql = emptyAttributesSql()
		local query = "INSERT INTO `player_inboxitems` (`player_id`, `sid`, `pid`, `itemtype`, `count`, `attributes`) VALUES (" ..
			playerId .. ", " .. sid .. ", 0, " .. itemId .. ", " .. count .. ", " .. attributesSql .. ")"
		if not db.query(query) then
			return false
		end

		remaining = remaining - count
		sid = sid + 1
	end
	return true
end

local function addItemToInbox(inbox, itemId, amount, attributes)
	local itemType = ItemType(itemId)
	if not inbox or not itemType or itemType:getId() == 0 or amount <= 0 then
		return false
	end

	if hasSerializedAttributes(attributes) then
		if amount ~= 1 then
			return false
		end

		local item = createMarketItem(itemId, 1, attributes)
		if not item then
			return false
		end

		local ret = inbox:addItemEx(item, INDEX_WHEREEVER, FLAG_NOLIMIT)
		if ret ~= RETURNVALUE_NOERROR then
			item:remove()
			return false
		end
		return true
	end

	local stackSize = math.max(1, itemType:getStackSize())
	local remaining = amount
	while remaining > 0 do
		local count = itemType:isStackable() and math.min(remaining, stackSize) or 1
		if not inbox:addItem(itemId, count, INDEX_WHEREEVER, FLAG_NOLIMIT) then
			return false
		end
		remaining = remaining - count
	end
	return true
end

local function itemMatchesAttributes(item, attributes)
	if not hasSerializedAttributes(attributes) then
		return true
	end
	return item and item.serializeAttributes and item:serializeAttributes() == attributes
end

local function removeInboxItems(player, itemId, amount, attributes)
	local inbox = player:getInbox()
	local itemType = ItemType(itemId)
	if not inbox or not itemType or itemType:getId() == 0 or amount <= 0 then
		return false
	end

	local removals = {}
	local found = 0
	for _, item in ipairs(inbox:getItems(true)) do
		if item:getId() == itemId and itemMatchesAttributes(item, attributes) then
			local count = math.min(amount - found, getItemTradeCount(item, itemType))
			if count > 0 then
				removals[#removals + 1] = { item = item, count = count }
				found = found + count
				if found >= amount then
					break
				end
			end
		end
	end

	if found < amount then
		return false
	end

	for _, entry in ipairs(removals) do
		if not entry.item:remove(entry.count) then
			return false
		end
	end
	return true
end

local function deliverItemToPlayer(playerId, playerName, itemId, amount, attributes)
	local target = playerName and Player(playerName) or nil
	if target then
		local inbox = target:getInbox()
		if addItemToInbox(inbox, itemId, amount, attributes) then
			return true
		end
	end
	return insertInboxItem(playerId, itemId, amount, attributes)
end

local function creditPlayerBank(playerId, playerName, amount)
	if amount <= 0 then
		return true
	end

	local target = playerName and Player(playerName) or nil
	if target then
		target:setBankBalance(target:getBankBalance() + amount)
		return true
	end

	return db.query("UPDATE `players` SET `balance` = `balance` + " .. amount .. " WHERE `id` = " .. playerId)
end

local function addHistory(playerId, sale, itemId, amount, price, state, created, tier)
	if not tableExists("market_history") then
		return
	end

	local now = os.time()
	local expiresAt = (created or now) + getOfferDuration()
	tier = math.max(0, math.min(10, tonumber(tier) or 0))
	db.query("INSERT INTO `market_history` (`player_id`, `sale`, `itemtype`, `amount`, `price`, `tier`, `expires_at`, `inserted`, `state`) VALUES (" ..
		playerId .. ", " .. sale .. ", " .. itemId .. ", " .. amount .. ", " .. price .. ", " .. tier .. ", " .. expiresAt .. ", " .. now .. ", " .. state .. ")")
end

local function fetchOfferById(offerId)
	local resultId = db.storeQuery("SELECT mo.`id`, mo.`player_id`, mo.`sale`, mo.`itemtype`, mo.`amount`, mo.`created`, mo.`anonymous`, mo.`price`, mo.`tier`, mo.`attributes`, p.`name` AS `player_name` FROM `market_offers` mo INNER JOIN `players` p ON p.`id` = mo.`player_id` WHERE mo.`id` = " .. offerId .. " LIMIT 1")
	if resultId == false then
		return nil
	end

	local offer = {
		id = result.getDataInt(resultId, "id"),
		playerId = result.getDataInt(resultId, "player_id"),
		sale = result.getDataInt(resultId, "sale"),
		itemId = result.getDataInt(resultId, "itemtype"),
		amount = result.getDataInt(resultId, "amount"),
		created = result.getDataInt(resultId, "created"),
		anonymous = result.getDataInt(resultId, "anonymous") ~= 0,
		price = result.getDataInt(resultId, "price"),
		tier = result.getDataInt(resultId, "tier"),
		attributes = getResultAttributes(resultId),
		playerName = result.getDataString(resultId, "player_name")
	}
	local attributesTier = getAttributesTier(offer.itemId, offer.attributes)
	if attributesTier > 0 then
		offer.tier = attributesTier
	end
	result.free(resultId)
	return offer
end

local function fetchOffers(query)
	local offers = {}
	local resultId = db.storeQuery(query)
	if resultId == false then
		return offers
	end

	repeat
		offers[#offers + 1] = {
			id = result.getDataInt(resultId, "id"),
			playerId = result.getDataInt(resultId, "player_id"),
			sale = result.getDataInt(resultId, "sale"),
			itemId = result.getDataInt(resultId, "itemtype"),
			amount = result.getDataInt(resultId, "amount"),
			created = result.getDataInt(resultId, "created"),
			anonymous = result.getDataInt(resultId, "anonymous") ~= 0,
			price = result.getDataInt(resultId, "price"),
			tier = result.getDataInt(resultId, "tier"),
			attributes = getResultAttributes(resultId),
			playerName = result.getDataString(resultId, "player_name"),
			state = MARKET_STATE_ACTIVE
		}
		local attributesTier = getAttributesTier(offers[#offers].itemId, offers[#offers].attributes)
		if attributesTier > 0 then
			offers[#offers].tier = attributesTier
		end
	until not result.next(resultId)

	result.free(resultId)
	return offers
end

local function fetchHistory(playerId)
	local offers = {}
	if not tableExists("market_history") then
		return offers
	end

	local resultId = db.storeQuery("SELECT `id`, `player_id`, `sale`, `itemtype`, `amount`, `price`, `tier`, `inserted`, `state` FROM `market_history` WHERE `player_id` = " .. playerId .. " ORDER BY `inserted` DESC LIMIT " .. MARKET_MAX_PACKET_OFFERS)
	if resultId == false then
		return offers
	end

	repeat
		offers[#offers + 1] = {
			id = result.getDataInt(resultId, "id"),
			playerId = result.getDataInt(resultId, "player_id"),
			sale = result.getDataInt(resultId, "sale"),
			itemId = result.getDataInt(resultId, "itemtype"),
			amount = result.getDataInt(resultId, "amount"),
			created = result.getDataInt(resultId, "inserted"),
			anonymous = false,
			price = result.getDataInt(resultId, "price"),
			tier = result.getDataInt(resultId, "tier"),
			playerName = "",
			state = result.getDataInt(resultId, "state")
		}
	until not result.next(resultId)

	result.free(resultId)
	return offers
end

local function writeOffer(out, offer)
	out:addU32(offer.id)
	out:addU32(offer.created)
	out:addU16(offer.itemId)
	out:addByte(math.max(0, math.min(10, tonumber(offer.tier) or 0)))
	out:addU16(clamp(offer.amount, 0, 0xFFFF))
	out:addU32(clamp(offer.price, 0, MARKET_MAX_PRICE))
	out:addString(offer.anonymous and "Anonymous" or (offer.playerName or ""))
	out:addByte(offer.state or MARKET_STATE_ACTIVE)
end

local function addDescription(descriptions, descriptionType, value)
	if value == nil or value == "" then
		return
	end

	descriptions[#descriptions + 1] = { type = descriptionType, text = tostring(value) }
end

local function formatWeight(weight)
	weight = tonumber(weight) or 0
	local oz = math.floor(weight / 100)
	local decimals = weight % 100
	return string.format("%d.%02d oz", oz, decimals)
end

local function formatDuration(seconds)
	seconds = tonumber(seconds) or 0
	if seconds <= 0 then
		return nil
	end
	if seconds % 60 == 0 then
		local minutes = seconds / 60
		if minutes % 60 == 0 then
			return string.format("%d hour%s", minutes / 60, minutes == 60 and "" or "s")
		end
		return string.format("%d minute%s", minutes, minutes == 1 and "" or "s")
	end
	return string.format("%d second%s", seconds, seconds == 1 and "" or "s")
end

local function getWeaponTypeName(weaponType)
	if weaponType == WEAPON_SWORD then
		return "Sword"
	elseif weaponType == WEAPON_CLUB then
		return "Club"
	elseif weaponType == WEAPON_AXE then
		return "Axe"
	elseif weaponType == WEAPON_SHIELD then
		return "Shield"
	elseif weaponType == WEAPON_DISTANCE then
		return "Distance"
	elseif weaponType == WEAPON_WAND then
		return "Wand/Rod"
	elseif weaponType == WEAPON_AMMO then
		return "Ammunition"
	elseif weaponType == WEAPON_QUIVER then
		return "Quiver"
	elseif weaponType == WEAPON_FIST then
		return "Fist"
	end
	return nil
end

local function getCombatTypeName(combatType)
	if COMBAT_PHYSICALDAMAGE and combatType == COMBAT_PHYSICALDAMAGE then
		return "physical"
	elseif COMBAT_ENERGYDAMAGE and combatType == COMBAT_ENERGYDAMAGE then
		return "energy"
	elseif COMBAT_EARTHDAMAGE and combatType == COMBAT_EARTHDAMAGE then
		return "earth"
	elseif COMBAT_FIREDAMAGE and combatType == COMBAT_FIREDAMAGE then
		return "fire"
	elseif COMBAT_ICEDAMAGE and combatType == COMBAT_ICEDAMAGE then
		return "ice"
	elseif COMBAT_HOLYDAMAGE and combatType == COMBAT_HOLYDAMAGE then
		return "holy"
	elseif COMBAT_DEATHDAMAGE and combatType == COMBAT_DEATHDAMAGE then
		return "death"
	end
	return nil
end

local function buildAbilityDescription(itemType)
	local abilities = itemType:getAbilities()
	if not abilities then
		return nil
	end

	local parts = {}
	if (abilities.speed or 0) ~= 0 then
		parts[#parts + 1] = string.format("speed %+d", abilities.speed)
	end
	if (abilities.healthGain or 0) > 0 then
		parts[#parts + 1] = string.format("health regeneration +%d", abilities.healthGain)
	end
	if (abilities.manaGain or 0) > 0 then
		parts[#parts + 1] = string.format("mana regeneration +%d", abilities.manaGain)
	end
	if abilities.manaShield then
		parts[#parts + 1] = "mana shield"
	end
	if abilities.invisible then
		parts[#parts + 1] = "invisibility"
	end
	if #parts == 0 then
		return nil
	end
	return table.concat(parts, ", ")
end

local function buildMarketDescriptions(itemId)
	local itemType = ItemType(itemId)
	local descriptions = {}
	if not itemType or itemType:getId() == 0 then
		return descriptions
	end
	local xmlAttributes = marketItemXmlAttributes[itemId] or {}

	local armor = tonumber(itemType:getArmor()) or 0
	if armor > 0 then
		addDescription(descriptions, MARKET_DESC_ARMOR, armor)
	end

	local attack = tonumber(itemType:getAttack()) or 0
	if attack > 0 then
		addDescription(descriptions, MARKET_DESC_ATTACK, attack)
	end

	local defense = tonumber(itemType:getDefense()) or 0
	local extraDefense = tonumber(itemType:getExtraDefense()) or 0
	if defense > 0 then
		local defenseText = tostring(defense)
		if extraDefense ~= 0 then
			defenseText = string.format("%d %+d", defense, extraDefense)
		end
		addDescription(descriptions, MARKET_DESC_DEFENSE, defenseText)
	end

	if itemType:isContainer() then
		addDescription(descriptions, MARKET_DESC_CONTAINER, string.format("%d slots", itemType:getCapacity()))
	end

	local description = xmlAttributes.description or itemType:getDescription()
	if description and description ~= "" then
		addDescription(descriptions, MARKET_DESC_GENERAL, description)
	end

	local classification = tonumber(xmlAttributes.classification) or tonumber(itemType:getClassification()) or 0
	if classification > 0 then
		addDescription(descriptions, MARKET_DESC_CLASSIFICATION, classification)
	end

	local tier = tonumber(xmlAttributes.tier) or tonumber(itemType:getTier()) or 0
	if tier > 0 then
		addDescription(descriptions, MARKET_DESC_TIER, tier)
	end

	local duration = formatDuration(math.max(tonumber(itemType:getDurationMin()) or 0, tonumber(itemType:getDurationMax()) or 0))
	if duration then
		addDescription(descriptions, MARKET_DESC_DECAY_TIME, duration)
	end

	local elementDamage = tonumber(itemType:getElementDamage()) or 0
	local elementType = itemType:getElementType()
	local elementName = getCombatTypeName(elementType)
	if elementDamage > 0 and elementName then
		addDescription(descriptions, MARKET_DESC_COMBAT, string.format("%s +%d", elementName, elementDamage))
	end

	local minLevel = tonumber(itemType:getMinReqLevel()) or 0
	if minLevel > 0 then
		addDescription(descriptions, MARKET_DESC_MIN_LEVEL, minLevel)
	end

	local minMagicLevel = tonumber(itemType:getMinReqMagicLevel()) or 0
	if minMagicLevel > 0 then
		addDescription(descriptions, MARKET_DESC_MIN_MAGIC_LEVEL, minMagicLevel)
	end

	local vocation = itemType:getVocationString()
	if vocation and vocation ~= "" then
		addDescription(descriptions, MARKET_DESC_VOCATION, vocation)
	end

	local runeSpell = itemType:getRuneSpellName()
	if runeSpell and runeSpell ~= "" then
		addDescription(descriptions, MARKET_DESC_RUNE, runeSpell)
	end

	addDescription(descriptions, MARKET_DESC_ABILITY, buildAbilityDescription(itemType))

	local charges = tonumber(itemType:getCharges()) or 0
	if charges > 0 then
		addDescription(descriptions, MARKET_DESC_CHARGES, charges)
	end

	local weaponName = getWeaponTypeName(itemType:getWeaponType())
	if weaponName then
		addDescription(descriptions, MARKET_DESC_WEAPON, weaponName)
	end

	local weight = tonumber(itemType:getWeight()) or 0
	if weight > 0 then
		addDescription(descriptions, MARKET_DESC_WEIGHT, formatWeight(weight))
	end

	local imbuementSlots = tonumber(itemType:getImbuementSlot()) or 0
	if imbuementSlots > 0 then
		addDescription(descriptions, MARKET_DESC_IMBUEMENTS, string.format("%d slot%s", imbuementSlots, imbuementSlots == 1 and "" or "s"))
	end

	return descriptions
end

local function fetchMarketStatistics(itemId, actionType)
	local stats = {}
	if not tableExists("market_statistics") then
		return stats
	end

	local since = os.time() - (MARKET_STATISTICS_DAYS * 24 * 60 * 60)
	local firstDay = math.floor(since / 86400) * 86400
	local query = "SELECT `day`, `transactions`, `total_price`, `highest_price`, `lowest_price` FROM `market_statistics` " ..
		"WHERE `itemtype` = " .. itemId .. " AND `sale` = " .. actionType .. " AND `day` >= " .. firstDay ..
		" ORDER BY `day` ASC LIMIT " .. MARKET_STATISTICS_DAYS

	local resultId = db.storeQuery(query)
	if resultId == false then
		return stats
	end

	repeat
		stats[#stats + 1] = {
			day = result.getDataInt(resultId, "day"),
			transactions = result.getDataInt(resultId, "transactions"),
			totalPrice = result.getDataLong(resultId, "total_price"),
			highestPrice = result.getDataInt(resultId, "highest_price"),
			lowestPrice = result.getDataInt(resultId, "lowest_price")
		}
	until not result.next(resultId)

	result.free(resultId)
	return stats
end

local function refreshMarketStatistics()
	if not tableExists("market_history") or not tableExists("market_statistics") then
		return false
	end

	local since = os.time() - (MARKET_STATISTICS_DAYS * 24 * 60 * 60)
	local firstDay = math.floor(since / 86400) * 86400
	db.query("DELETE FROM `market_statistics`")

	return db.query(
		"REPLACE INTO `market_statistics` (`itemtype`, `sale`, `day`, `transactions`, `total_price`, `highest_price`, `lowest_price`) " ..
		"SELECT `itemtype`, `sale`, FLOOR(`inserted` / 86400) * 86400 AS `stat_day`, COUNT(*) AS `transactions`, " ..
		"CASE WHEN SUM(`amount`) > 0 THEN FLOOR((SUM(`price` * `amount`) / SUM(`amount`)) * COUNT(*)) ELSE 0 END AS `total_price`, " ..
		"MAX(`price`) AS `highest_price`, MIN(`price`) AS `lowest_price` FROM `market_history` " ..
		"WHERE `state` = " .. MARKET_STATE_ACCEPTED .. " AND `inserted` >= " .. firstDay ..
		" GROUP BY `itemtype`, `sale`, FLOOR(`inserted` / 86400)"
	)
end

local function writeStatistics(out, stats)
	out:addByte(math.min(#stats, MARKET_STATISTICS_DAYS))
	for i = 1, math.min(#stats, MARKET_STATISTICS_DAYS) do
		local stat = stats[i]
		out:addU32(clamp(stat.day, 0, 0xFFFFFFFF))
		out:addU32(clamp(stat.transactions, 0, 0xFFFFFFFF))
		out:addU32(clamp(stat.totalPrice, 0, 0xFFFFFFFF))
		out:addU32(clamp(stat.highestPrice, 0, 0xFFFFFFFF))
		out:addU32(clamp(stat.lowestPrice, 0, 0xFFFFFFFF))
	end
end

local sendMarketDetail

local function sendMarketBrowse(player, browseId)
	if not supportsCustomNetwork(player) then
		return false
	end

	browseId = tonumber(browseId) or 0

	local buyOffers = {}
	local sellOffers = {}
	local playerId = player:getGuid()

	if browseId == MARKET_REQUEST_MY_OFFERS then
		local query = "SELECT mo.`id`, mo.`player_id`, mo.`sale`, mo.`itemtype`, mo.`amount`, mo.`created`, mo.`anonymous`, mo.`price`, mo.`tier`, mo.`attributes`, p.`name` AS `player_name` FROM `market_offers` mo INNER JOIN `players` p ON p.`id` = mo.`player_id` WHERE mo.`player_id` = " .. playerId .. " ORDER BY mo.`created` DESC LIMIT " .. MARKET_MAX_PACKET_OFFERS
		for _, offer in ipairs(fetchOffers(query)) do
			if offer.sale == MARKET_ACTION_BUY then
				buyOffers[#buyOffers + 1] = offer
			else
				sellOffers[#sellOffers + 1] = offer
			end
		end
	elseif browseId == MARKET_REQUEST_MY_HISTORY then
		for _, offer in ipairs(fetchHistory(playerId)) do
			if offer.sale == MARKET_ACTION_BUY then
				buyOffers[#buyOffers + 1] = offer
			else
				sellOffers[#sellOffers + 1] = offer
			end
		end
	elseif marketItemsById[browseId] then
		buyOffers = fetchOffers("SELECT mo.`id`, mo.`player_id`, mo.`sale`, mo.`itemtype`, mo.`amount`, mo.`created`, mo.`anonymous`, mo.`price`, mo.`tier`, mo.`attributes`, p.`name` AS `player_name` FROM `market_offers` mo INNER JOIN `players` p ON p.`id` = mo.`player_id` WHERE mo.`itemtype` = " .. browseId .. " AND mo.`sale` = " .. MARKET_ACTION_BUY .. " ORDER BY mo.`price` DESC, mo.`created` ASC LIMIT " .. MARKET_MAX_PACKET_OFFERS)
		sellOffers = fetchOffers("SELECT mo.`id`, mo.`player_id`, mo.`sale`, mo.`itemtype`, mo.`amount`, mo.`created`, mo.`anonymous`, mo.`price`, mo.`tier`, mo.`attributes`, p.`name` AS `player_name` FROM `market_offers` mo INNER JOIN `players` p ON p.`id` = mo.`player_id` WHERE mo.`itemtype` = " .. browseId .. " AND mo.`sale` = " .. MARKET_ACTION_SELL .. " ORDER BY mo.`price` ASC, mo.`created` ASC LIMIT " .. MARKET_MAX_PACKET_OFFERS)
	else
		sendMarketMessage(player, "This item cannot be traded on the market.")
		return
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_MARKET_SEND)
	out:addByte(RESP_BROWSE)
	out:addU16(browseId)
	out:addU16(#buyOffers)
	for _, offer in ipairs(buyOffers) do
		writeOffer(out, offer)
	end
	out:addU16(#sellOffers)
	for _, offer in ipairs(sellOffers) do
		writeOffer(out, offer)
	end
	local sent = out:sendToPlayer(player)

	if marketItemsById[browseId] then
		sendMarketDetail(player, browseId)
	end
	return sent
end

local offerCountCache = {}

local function sendMarketEnter(player, depotMap)
	if not supportsCustomNetwork(player) then
		return false
	end

	local balance = getPlayerTotalMoney(player)
	local playerGuid = player:getGuid()
	local offers = offerCountCache[playerGuid]
	if offers == nil then
		offers = clamp(getMarketOfferCount(playerGuid), 0, MARKET_MAX_OFFERS)
		offerCountCache[playerGuid] = offers
	end
	local chunkIndex = 0
	depotMap = depotMap or buildDepotItemMap(player)
	local enterEntries = buildMarketEnterEntries(depotMap)
	local totalItems = #enterEntries

	for offset = 1, math.max(totalItems, 1), MARKET_CATALOG_CHUNK_SIZE do
		local chunkEnd = math.min(offset + MARKET_CATALOG_CHUNK_SIZE - 1, totalItems)
		local chunkCount = totalItems == 0 and 0 or (chunkEnd - offset + 1)
		local out = NetworkMessage(player)
		out:addByte(OPCODE_MARKET_SEND)
		out:addByte(RESP_ENTER)
		out:addU64(balance)
		out:addU16(offers)
		out:addU16(chunkIndex)
		out:addByte(chunkEnd >= totalItems and 1 or 0)
		out:addU16(chunkCount)

		for i = offset, chunkEnd do
			local entry = enterEntries[i]
			out:addU16(entry.id)
			out:addByte(entry.category)
			out:addString(entry.name)
			out:addU16(math.min(entry.amount or 0, 0xFFFF))
			out:addByte(entry.tier or 0)
		end

		out:sendToPlayer(player)
		chunkIndex = chunkIndex + 1
	end
	return true
end

sendMarketDetail = function(player, itemId)
	if not supportsCustomNetwork(player) then
		return false
	end

	if not marketItemsById[itemId] then
		return false
	end

	local descriptions = buildMarketDescriptions(itemId)
	local purchaseStats = fetchMarketStatistics(itemId, MARKET_ACTION_BUY)
	local saleStats = fetchMarketStatistics(itemId, MARKET_ACTION_SELL)

	local out = NetworkMessage(player)
	out:addByte(OPCODE_MARKET_SEND)
	out:addByte(RESP_DETAIL)
	out:addU16(itemId)
	out:addByte(math.min(#descriptions, 0xFF))
	for i = 1, math.min(#descriptions, 0xFF) do
		out:addByte(descriptions[i].type)
		out:addString(descriptions[i].text)
	end
	writeStatistics(out, purchaseStats)
	writeStatistics(out, saleStats)
	return out:sendToPlayer(player)
end

local function refreshMarket(player, browseId, depotMap)
	sendMarketEnter(player, depotMap)
	if browseId then
		sendMarketBrowse(player, browseId)
	end
end

local function expireOffers()
	local now = os.time()
	if now - lastExpireCheck < MARKET_EXPIRE_CHECK_INTERVAL or not tableExists("market_offers") then
		return
	end
	lastExpireCheck = now

	local expiredBefore = now - getOfferDuration()
	local offers = fetchOffers("SELECT mo.`id`, mo.`player_id`, mo.`sale`, mo.`itemtype`, mo.`amount`, mo.`created`, mo.`anonymous`, mo.`price`, mo.`tier`, mo.`attributes`, p.`name` AS `player_name` FROM `market_offers` mo INNER JOIN `players` p ON p.`id` = mo.`player_id` WHERE mo.`created` <= " .. expiredBefore .. " ORDER BY mo.`created` ASC LIMIT 100")
	for _, offer in ipairs(offers) do
		local offerKey = "offer:" .. offer.id
		-- Skip offers currently being processed by another handler
		if not acquireLock(offerKey) then
			logInfo("[CustomMarket] Skipping expire for locked offer " .. offer.id)
		else
			-- Atomically claim the offer before returning goods
			local claimed = db.query("DELETE FROM `market_offers` WHERE `id` = " .. offer.id)
			if claimed then
				local returned = true
				if offer.sale == MARKET_ACTION_BUY then
					returned = creditPlayerBank(offer.playerId, offer.playerName, offer.price * offer.amount)
				else
					returned = deliverItemToPlayer(offer.playerId, offer.playerName, offer.itemId, offer.amount, offer.attributes)
				end

				if returned then
					offerCountCache[offer.playerId] = nil
					addHistory(offer.playerId, offer.sale, offer.itemId, offer.amount, offer.price, MARKET_STATE_EXPIRED, offer.created, offer.tier)
				else
					-- Delivery failed: restore the offer so it can be retried
					rollbackOfferClaim(offer, offer.amount)
					logError("[CustomMarket] Failed to return expired offer " .. offer.id .. " — restored to DB")
				end
			end
			releaseLock(offerKey)
		end
	end
end

local function validateOfferPayload(player, actionType, itemId, amount, price)
	if actionType ~= MARKET_ACTION_BUY and actionType ~= MARKET_ACTION_SELL then
		return false, "Invalid offer type."
	end

	if not marketItemsById[itemId] then
		return false, "This item cannot be traded on the market."
	end

	local itemType = ItemType(itemId)
	local maxAmount = itemType:isStackable() and MARKET_MAX_AMOUNT_STACKABLE or MARKET_MAX_AMOUNT
	if amount <= 0 or amount > maxAmount then
		return false, "Invalid amount."
	end

	if price <= 0 or price > MARKET_MAX_PRICE or price * amount > MARKET_MAX_PRICE then
		return false, "Invalid price."
	end

	if getMarketOfferCount(player:getGuid()) >= MARKET_MAX_OFFERS then
		return false, "You have too many active market offers."
	end

	return true
end

local openHandler = PacketHandler(OPCODE_MARKET_OPEN)
function openHandler.onReceive(player, msg)
	if not hasCurrentMarketAccess(player) then
		sendMarketMessage(player, "You need to be near a depot or market.")
		sendMarketLeave(player)
		return
	end

	setMarketDepotId(player, getPlayerLastDepotId(player))
	setMarketOpen(player)
	expireOffers()
	sendMarketEnter(player)
end
openHandler:register()

local leaveHandler = PacketHandler(OPCODE_MARKET_LEAVE)
function leaveHandler.onReceive(player, msg)
	clearMarketOpen(player)
	sendMarketLeave(player)
end
leaveHandler:register()

local browseHandler = PacketHandler(OPCODE_MARKET_BROWSE)
function browseHandler.onReceive(player, msg)
	if not ensureMarketAccess(player) then
		return
	end
	if msg:len() - msg:tell() < 2 then
		return
	end

	expireOffers()
	sendMarketBrowse(player, msg:getU16())
end
browseHandler:register()

local createHandler = PacketHandler(OPCODE_MARKET_CREATE)
function createHandler.onReceive(player, msg)
	if not ensureMarketAccess(player) then
		return
	end
	if msg:len() - msg:tell() < 10 then
		return
	end
	if isOnCooldown(player) then
		sendMarketMessage(player, "Please wait before creating another offer.")
		return
	end

	-- Lock player to prevent simultaneous create + create/cancel/accept
	local playerKey = "player:" .. player:getGuid()
	if not acquireLock(playerKey) then
		sendMarketMessage(player, "Market action already in progress.")
		return
	end

	expireOffers()

	local actionType = msg:getByte()
	local itemId = msg:getU16()
	local amount = msg:getU16()
	local price = msg:getU32()
	local anonymous = msg:getByte() ~= 0 and 1 or 0
	local requestedTier = nil
	if msg:len() - msg:tell() >= 1 then
		requestedTier = math.max(0, math.min(10, tonumber(msg:getByte()) or 0))
	end

	local valid, errorMessage = validateOfferPayload(player, actionType, itemId, amount, price)
	if not valid then
		releaseLock(playerKey)
		sendMarketMessage(player, errorMessage)
		return
	end

	local totalPrice = price * amount
	local fee = calculateFee(price, amount)
	local payment = nil
	local depotMap = nil
	local offerAttributes = nil
	local offerTier = requestedTier or 0

	if actionType == MARKET_ACTION_BUY then
		if getPlayerTotalMoney(player) < totalPrice + fee then
			releaseLock(playerKey)
			sendMarketMessage(player, "You do not have enough money for this buy offer.")
			return
		end
		payment = removePlayerMarketMoney(player, totalPrice + fee)
		if not payment then
			releaseLock(playerKey)
			sendMarketMessage(player, "You do not have enough money for this buy offer.")
			return
		end
	else
		depotMap = buildDepotItemMap(player)
		local availableAmount = depotMap[itemId] or 0
		if requestedTier ~= nil then
			availableAmount = depotMap[getDepotItemKey(itemId, requestedTier)] or 0
		end
		if availableAmount < amount then
			releaseLock(playerKey)
			sendMarketMessage(player, "You do not have enough items for this sell offer.")
			return
		end
		if getPlayerTotalMoney(player) < fee then
			releaseLock(playerKey)
			sendMarketMessage(player, "You do not have enough money to pay the market fee.")
			return
		end
		local reserved, attributes, reserveError = removeDepotItemsWithAttributes(player, itemId, amount, requestedTier)
		if not reserved then
			releaseLock(playerKey)
			sendMarketMessage(player, reserveError or "Could not reserve the items for this sell offer.")
			return
		end
		offerAttributes = attributes
		local attributesTier = getAttributesTier(itemId, offerAttributes)
		if attributesTier > 0 then
			offerTier = attributesTier
		end
		depotMap[itemId] = math.max(0, (depotMap[itemId] or 0) - amount)
		if requestedTier ~= nil then
			local tierKey = getDepotItemKey(itemId, requestedTier)
			depotMap[tierKey] = math.max(0, (depotMap[tierKey] or 0) - amount)
		end
		payment = removePlayerMarketMoney(player, fee)
		if not payment then
			addDepotItems(player, itemId, amount, offerAttributes)
			releaseLock(playerKey)
			sendMarketMessage(player, "Could not pay the market fee.")
			return
		end
	end

	local now = os.time()
	local ok = db.query("INSERT INTO `market_offers` (`player_id`, `sale`, `itemtype`, `amount`, `created`, `anonymous`, `price`, `tier`, `attributes`) VALUES (" ..
		player:getGuid() .. ", " .. actionType .. ", " .. itemId .. ", " .. amount .. ", " .. now .. ", " .. anonymous .. ", " .. price .. ", " ..
		math.max(0, math.min(10, tonumber(offerTier) or 0)) .. ", " ..
		escapeAttributes(offerAttributes) .. ")")
	if not ok then
		if actionType == MARKET_ACTION_BUY then
			refundPlayerMarketMoney(player, payment)
		else
			addDepotItems(player, itemId, amount, offerAttributes)
			refundPlayerMarketMoney(player, payment)
		end
		releaseLock(playerKey)
		sendMarketMessage(player, "Could not create the market offer.")
		return
	end

	offerCountCache[player:getGuid()] = nil
	releaseLock(playerKey)
	sendMarketMessage(player, "Market offer created.")
	refreshMarket(player, itemId, depotMap)
end
createHandler:register()

local cancelHandler = PacketHandler(OPCODE_MARKET_CANCEL)
function cancelHandler.onReceive(player, msg)
	if not ensureMarketAccess(player) then
		return
	end
	if msg:len() - msg:tell() < 4 then
		return
	end
	if isOnCooldown(player) then
		sendMarketMessage(player, "Please wait before cancelling another offer.")
		return
	end

	expireOffers()

	local offerId = msg:getU32()
	local offer = fetchOfferById(offerId)
	if not offer or offer.playerId ~= player:getGuid() then
		sendMarketMessage(player, "Market offer not found.")
		return
	end

	-- Lock both offer and player to prevent cancel + accept race
	local offerKey = "offer:" .. offerId
	local playerKey = "player:" .. player:getGuid()
	if not acquireMultipleLocks({ offerKey, playerKey }) then
		sendMarketMessage(player, "Market action already in progress.")
		return
	end

	-- Revalidate after lock: confirm offer still exists and belongs to player
	offer = fetchOfferById(offerId)
	if not offer or offer.playerId ~= player:getGuid() then
		releaseMultipleLocks({ offerKey, playerKey })
		sendMarketMessage(player, "Market offer not found.")
		return
	end

	-- Atomically claim (delete) the offer BEFORE returning goods
	if not db.query("DELETE FROM `market_offers` WHERE `id` = " .. offer.id .. " AND `player_id` = " .. player:getGuid()) then
		releaseMultipleLocks({ offerKey, playerKey })
		sendMarketMessage(player, "Could not cancel the market offer.")
		return
	end
	offerCountCache[player:getGuid()] = nil

	-- Return goods after safe DB claim
	if offer.sale == MARKET_ACTION_BUY then
		player:setBankBalance(player:getBankBalance() + offer.price * offer.amount)
	else
		-- Use deliverItemToPlayer (inbox/depot) instead of raw addItem
		if not deliverItemToPlayer(player:getGuid(), player:getName(), offer.itemId, offer.amount, offer.attributes) then
			logError("[CustomMarket] Failed to return cancelled sell offer " .. offer.id .. " items to player " .. player:getName())
		end
	end

	addHistory(player:getGuid(), offer.sale, offer.itemId, offer.amount, offer.price, MARKET_STATE_CANCELLED, offer.created, offer.tier)
	releaseMultipleLocks({ offerKey, playerKey })
	sendMarketMessage(player, "Market offer cancelled.")
	refreshMarket(player, MARKET_REQUEST_MY_OFFERS)
end
cancelHandler:register()

local acceptHandler = PacketHandler(OPCODE_MARKET_ACCEPT)
function acceptHandler.onReceive(player, msg)
	if not ensureMarketAccess(player) then
		return
	end
	if msg:len() - msg:tell() < 6 then
		return
	end
	if isOnCooldown(player) then
		sendMarketMessage(player, "Please wait before accepting another offer.")
		return
	end

	expireOffers()

	local offerId = msg:getU32()
	local amount = msg:getU16()

	-- Pre-check before acquiring locks (avoids locking on obvious invalid state)
	local offer = fetchOfferById(offerId)
	if not offer then
		sendMarketMessage(player, "Market offer not found.")
		return
	end
	if offer.playerId == player:getGuid() then
		sendMarketMessage(player, "You cannot accept your own market offer.")
		return
	end

	-- Acquire locks: offer + acceptor player + offer owner
	local offerKey  = "offer:"  .. offerId
	local buyerKey  = "player:" .. player:getGuid()
	local ownerKey  = "owner:"  .. offer.playerId
	if not acquireMultipleLocks({ offerKey, buyerKey, ownerKey }) then
		sendMarketMessage(player, "Market action already in progress.")
		return
	end

	-- Revalidate offer AFTER acquiring locks
	offer = fetchOfferById(offerId)
	if not offer then
		releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
		sendMarketMessage(player, "Market offer not found.")
		return
	end
	if offer.playerId == player:getGuid() then
		releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
		sendMarketMessage(player, "You cannot accept your own market offer.")
		return
	end

	amount = clamp(amount, 1, offer.amount)
	if hasSerializedAttributes(offer.attributes) and (amount ~= 1 or amount ~= offer.amount) then
		releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
		sendMarketMessage(player, "Tiered/custom market offers must be accepted in full.")
		return
	end

	local totalPrice = amount * offer.price
	local depotMap = nil
	local acceptedTier = tonumber(offer.tier) or 0

	-- ================================================================
	-- STEP 1: Atomically claim the offer in DB BEFORE any item/money
	-- transfer. This is the core anti-dupe guarantee.
	-- ================================================================
	local dbClaimed
	if amount == offer.amount then
		-- Full accept: DELETE with amount guard so a concurrent op can't sneak in
		dbClaimed = db.query(
			"DELETE FROM `market_offers` WHERE `id` = " .. offer.id ..
			" AND `amount` = " .. offer.amount
		)
	else
		-- Partial accept: UPDATE with guard that amount is still sufficient
		dbClaimed = db.query(
			"UPDATE `market_offers` SET `amount` = `amount` - " .. amount ..
			" WHERE `id` = " .. offer.id .. " AND `amount` >= " .. amount
		)
	end

	if not dbClaimed then
		releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
		sendMarketMessage(player, "Market offer is no longer available.")
		return
	end

	-- ================================================================
	-- STEP 2: Transfer money / items. On any failure, rollback the DB
	-- claim so the offer is restored and the player is not cheated.
	-- ================================================================
	if offer.sale == MARKET_ACTION_SELL then
		-- Acceptor (buyer) pays money, receives item
		if getPlayerTotalMoney(player) < totalPrice then
			rollbackOfferClaim(offer, amount)
			releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
			sendMarketMessage(player, "You do not have enough money.")
			return
		end

		local payment = removePlayerMarketMoney(player, totalPrice)
		if not payment then
			rollbackOfferClaim(offer, amount)
			releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
			sendMarketMessage(player, "You do not have enough money.")
			return
		end

		if not deliverItemToPlayer(player:getGuid(), player:getName(), offer.itemId, amount, offer.attributes) then
			refundPlayerMarketMoney(player, payment)
			rollbackOfferClaim(offer, amount)
			releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
			sendMarketMessage(player, "Could not deliver the item.")
			return
		end

		if not creditPlayerBank(offer.playerId, offer.playerName, totalPrice) then
			-- Critical: item already delivered to buyer — attempt to take it back
			if not removeInboxItems(player, offer.itemId, amount, offer.attributes) then
				logError("[CustomMarket] CRITICAL: Could not rollback inbox delivery for offer " ..
					offer.id .. " player " .. player:getName())
			end
			refundPlayerMarketMoney(player, payment)
			rollbackOfferClaim(offer, amount)
			releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
			sendMarketMessage(player, "Could not credit the seller.")
			return
		end

	else
		-- Acceptor (seller) provides item, receives money; buyer gets item
		local deliveryAttributes = nil
		depotMap = buildDepotItemMap(player)
		local requestedOfferTier = math.max(0, math.min(10, tonumber(offer.tier) or 0))
		if (depotMap[getDepotItemKey(offer.itemId, requestedOfferTier)] or 0) < amount then
			rollbackOfferClaim(offer, amount)
			releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
			sendMarketMessage(player, "You do not have enough items.")
			return
		end

		local removed, attributes, removeError = removeDepotItemsWithAttributes(player, offer.itemId, amount, requestedOfferTier)
		if not removed then
			rollbackOfferClaim(offer, amount)
			releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
			sendMarketMessage(player, removeError or "Could not remove the items.")
			return
		end
		deliveryAttributes = attributes
		acceptedTier = getAttributesTier(offer.itemId, deliveryAttributes)
		if acceptedTier == 0 then
			acceptedTier = requestedOfferTier
		end

		depotMap[offer.itemId] = math.max(0, (depotMap[offer.itemId] or 0) - amount)
		local acceptedTierKey = getDepotItemKey(offer.itemId, acceptedTier)
		depotMap[acceptedTierKey] = math.max(0, (depotMap[acceptedTierKey] or 0) - amount)

		if not deliverItemToPlayer(offer.playerId, offer.playerName, offer.itemId, amount, deliveryAttributes) then
			addDepotItems(player, offer.itemId, amount, deliveryAttributes)
			rollbackOfferClaim(offer, amount)
			releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
			sendMarketMessage(player, "Could not deliver the item to the buyer.")
			return
		end

		player:setBankBalance(player:getBankBalance() + totalPrice)
	end

	-- ================================================================
	-- STEP 3: All transfers succeeded — record history and notify.
	-- ================================================================
	offerCountCache[offer.playerId] = nil
	addHistory(offer.playerId, offer.sale, offer.itemId, amount, offer.price, MARKET_STATE_ACCEPTED, offer.created, acceptedTier)

	releaseMultipleLocks({ offerKey, buyerKey, ownerKey })

	sendMarketMessage(player, "Market offer accepted.")
	refreshMarket(player, offer.itemId, depotMap)

	local owner = Player(offer.playerName)
	if owner then
		sendMarketMessage(owner, "One of your market offers was accepted.")
		refreshMarket(owner, MARKET_REQUEST_MY_OFFERS)
	end
end
acceptHandler:register()

local marketSessionCleanup = CreatureEvent("CustomMarketSessionCleanup")
function marketSessionCleanup.onLogout(player)
	lastAction[player:getId()] = nil
	marketDepotSessions[player:getId()] = nil
	marketOpenSessions[player:getId()] = nil
	return true
end
marketSessionCleanup:register()

local marketSessionInit = CreatureEvent("CustomMarketSessionInit")
function marketSessionInit.onLogin(player)
	player:registerEvent("CustomMarketSessionCleanup")
	return true
end
marketSessionInit:register()

ensureTables()
loadMarketCatalog()
refreshMarketStatistics()

CustomMarket = {
	open = function(player, depotId)
		if not supportsCustomNetwork(player) then
			return false
		end

		if not hasCurrentMarketAccess(player) then
			sendMarketMessage(player, "You need to be near a depot or market.")
			sendMarketLeave(player)
			return false
		end

		setMarketDepotId(player, depotId or getPlayerLastDepotId(player))
		setMarketOpen(player)
		expireOffers()
		sendMarketEnter(player)
		return true
	end,
	close = closeMarket,
	checkAccess = function(player)
		if marketOpenSessions[player:getId()] and not hasCurrentMarketAccess(player) then
			return closeMarket(player, "Market closed.")
		end
		return false
	end,
	isOpen = function(player)
		return marketOpenSessions[player:getId()] == true
	end,
	isMarketCatalogItem = function(itemId)
		itemId = tonumber(itemId) or 0
		return marketItemsById[itemId] ~= nil
	end,
	browse = sendMarketBrowse,
	updateStatistics = refreshMarketStatistics
}
