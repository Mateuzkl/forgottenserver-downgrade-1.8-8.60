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

-- Releases each lock identified in `keys`.
-- @param keys Array of lock key strings to release.
local function releaseMultipleLocks(keys)
	for _, key in ipairs(keys) do
		releaseLock(key)
	end
end

-- Determines whether the provided attributes value represents serialized attributes.
-- @param attributes The attributes value to inspect (expected to be a string blob when serialized).
-- @return `true` if `attributes` is a non-empty string, `false` otherwise.
local function hasSerializedAttributes(attributes)
	return type(attributes) == "string" and #attributes > 0
end

-- Provide a SQL literal representing an empty serialized attributes value.
-- @return SQL expression for an empty attributes value; uses `db.escapeBlob("", 0)` when available, otherwise the empty-string literal `''`.
local function emptyAttributesSql()
	return db.escapeBlob and db.escapeBlob("", 0) or "''"
end

-- Escapes serialized item attributes for safe SQL insertion, or returns the SQL used to represent empty attributes.
-- @param attributes Serialized attributes string, or nil/empty when there are no serialized attributes.
-- @return SQL expression suitable for inserting the attributes into a query (`db.escapeBlob(attributes, #attributes)` is used when available, otherwise `db.escapeString(attributes)`); returns the database-specific empty-attributes SQL when attributes are not serialized.
local function escapeAttributes(attributes)
	if hasSerializedAttributes(attributes) then
		return db.escapeBlob and db.escapeBlob(attributes, #attributes) or db.escapeString(attributes)
	end
	return emptyAttributesSql()
end

-- Rollback a DB-claimed offer (used when delivery fails after DB claim).
-- For full-amount offers (deleted): tries to re-INSERT with original data.
-- Restores a market offer in the database after a previously claimed (deleted or reduced) offer.
-- If `acceptedAmount` is greater than or equal to `offer.amount`, the full offer row is reinserted;
-- otherwise the stored offer's `amount` is increased by `acceptedAmount`.
-- @param offer Table representing the offer row (must include `id`, `playerId`, `sale`, `itemId`, `amount`, `created`, `anonymous`, `price`, `tier`, `attributes`).
-- @param acceptedAmount Number of units to restore to the offer.
-- @return The result of the database query (truthy on success, `false` on failure).
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

-- Determine whether a database table with the given name exists.
-- @param name The name of the database table to check.
-- @return `true` if the table exists, `false` otherwise.
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

-- Checks whether a given column exists in the specified database table.
-- @return `true` if the column exists in the table, `false` otherwise.
local function columnExists(tableName, columnName)
	local resultId = db.storeQuery("SHOW COLUMNS FROM `" .. tableName .. "` LIKE " .. db.escapeString(columnName))
	if resultId ~= false then
		result.free(resultId)
		return true
	end
	return false
end

-- Ensures a column exists on a database table by adding it when missing.
-- Does nothing if the table does not exist or the column is already present.
-- @param tableName The name of the database table.
-- @param columnName The name of the column to ensure exists.
-- @param definition The SQL column definition (e.g. "INT NOT NULL DEFAULT 0").
local function ensureColumn(tableName, columnName, definition)
	if not tableExists(tableName) or columnExists(tableName, columnName) then
		return
	end
	db.query("ALTER TABLE `" .. tableName .. "` ADD COLUMN `" .. columnName .. "` " .. definition)
end

-- Retrieve the serialized attributes value for a given result row and column, if present.
-- Prefers a stream-backed column when available; otherwise reads the column as a string.
-- @param resultId The result row identifier returned by a query.
-- @param columnName Optional column name to read (default: "attributes").
-- @return The serialized attributes string when the column contains non-empty serialized data, `nil` otherwise.
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

-- Ensures the market-related database schema exists and adds required columns.
-- Creates `market_offers`, `market_history`, and `market_statistics` tables when absent,
-- and guarantees the presence of `attributes` and `tier` columns on `market_offers`
-- and the `tier` column on `market_history`.
-- This function has no return value and performs schema mutations as side effects.
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

-- Compute how many units of an item should be considered for trading based on stackability.
-- @param item The item instance whose count may be used.
-- @param itemType The item's type object; used to determine if the item is stackable.
-- @return The tradeable quantity: the item's count (clamped to at least 0) when stackable, otherwise 1.
local function getItemTradeCount(item, itemType)
	if itemType:isStackable() then
		return math.max(0, item:getCount() or 0)
	end
	return 1
end

-- Get the tier level of an item clamped between 0 and 10.
-- If `item` is nil or does not expose `getTier`, returns 0.
-- @param item The item instance to query (may be nil or lack `getTier`).
-- @return The tier as an integer between 0 and 10 inclusive.
local function getItemTier(item)
	if item and item.getTier then
		return math.max(0, math.min(10, tonumber(item:getTier()) or 0))
	end
	return 0
end

-- Builds a stable map key for a depot item by combining its item ID and tier.
-- @param itemId The numeric item type identifier.
-- @param tier The item tier or value convertible to number; values are clamped to the range 0..10.
-- @return A string in the form "itemId:tier" where `tier` is an integer between 0 and 10.
local function getDepotItemKey(itemId, tier)
	return tostring(itemId) .. ":" .. tostring(math.max(0, math.min(10, tonumber(tier) or 0)))
end

-- Get serialized attributes for an item when applicable.
-- Returns the serialized attributes string only for non-stackable items that provide a non-empty serialization.
-- @param item The item instance (must support `serializeAttributes`).
-- @param itemType Optional itemType used to determine stackability; if provided and stackable, attributes are ignored.
-- @return The serialized attributes `string` when present for a non-stackable item, `nil` otherwise.
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

-- Creates a game item instance and applies serialized attributes when provided.
-- @param itemId number The item type id to create.
-- @param count number The amount/count to create for stackable items.
-- @param attributes string|nil Serialized attributes to apply to the created item, if any.
-- @return Item|nil The created Item object, or `nil` if creation failed or provided serialized attributes could not be applied.
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

-- Determine the market item's tier from serialized attributes, clamped to 0..10.
-- @param itemId The numeric item type id to evaluate.
-- @param attributes Serialized item attributes string; when not serialized, tier is treated as 0.
-- @return The item's tier as an integer between 0 and 10; returns 0 if attributes are not serialized or the item cannot be instantiated.
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

local function getOfferTier(itemId, tier, attributes)
	tier = math.max(0, math.min(10, tonumber(tier) or 0))
	if tier == 0 and hasSerializedAttributes(attributes) then
		local attributesTier = getAttributesTier(itemId, attributes)
		if attributesTier > 0 then
			return attributesTier
		end
	end
	return tier
end

-- Collects the player's depot box containers for the current market depot.
-- @param player The player whose market depot is queried.
-- @return An array of depot box container objects present for that depot, ordered by ascending box index.
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

-- Builds a map of the player's depot item quantities, keyed by itemId and by itemId:tier.
-- Aggregates counts across all depot boxes; non-stackable items count as 1 and stackable items use their stack count.
-- Quantities are clamped to 65535.
-- @param player The player whose depot inventory will be scanned.
-- @return A table where keys are numeric item IDs and "itemId:tier" strings, and values are the corresponding aggregated amounts (0..65535).
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

-- Builds the list of catalog entries to send to a player when entering the market.
-- Each entry represents a catalog item and the available amount the player has for a specific tier.
-- @param depotMap Table mapping keys of the form "itemId:tier" to the available amount for that item/tier. If nil, treated as empty.
-- @return Array of entry tables. Each entry has fields:
--   - id (number): item type id
--   - category (string): catalog category
--   - name (string): display name
--   - amount (number): available quantity for the given tier (0 if none)
--   - tier (number): tier level (0..10)
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

-- Adds the specified item(s) into the player's depot boxes if space and requirements allow.
-- Attempts to place items across the player's depot boxes and respects stackability and serialized attributes rules.
-- @param player The player whose depot will receive the items.
-- @param itemId The item type id to add.
-- @param amount The number of items to add; for items with serialized attributes this must be 1.
-- @param attributes Optional serialized attributes string for the item instance; when present the function will add a single serialized item using no-limit placement.
-- @return `true` if the full requested amount was added to the depot, `false` otherwise.
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

-- Collects up to `amount` removable items matching `itemId` (and optional `tier`) from the player's depot and returns the removal plan.
-- The returned plan is an array of tables `{ item = <Item>, count = <number> }` describing which depot item instances to remove and how many units from each.
-- @param player The player whose depot will be scanned.
-- @param itemId The item type id to match.
-- @param amount The total quantity to collect.
-- @param tier Optional tier filter (0–10); when provided only items with this tier are considered.
-- @return A removal plan array when at least `amount` units are found, or `nil` if the requested quantity is not available.
local function collectDepotRemovals(player, itemId, amount, tier)
	local itemType = ItemType(itemId)
	if not itemType or itemType:getId() == 0 or amount <= 0 then
		return nil
	end

	local explicitTier = tier ~= nil
	local tierFilter = explicitTier and math.max(0, math.min(10, tonumber(tier) or 0)) or 0

	local removals = {}
	local found = 0
	for _, box in ipairs(getDepotBoxes(player)) do
		for _, item in ipairs(box:getItems(true)) do
			local itemTier = getItemTier(item)
			if item:getId() == itemId and itemTier == tierFilter then
				local attributes = nil
				if not explicitTier then
					attributes = getMarketItemAttributes(item, itemType)
				end
				if explicitTier or not hasSerializedAttributes(attributes) then
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
	end
	return nil
end

-- Remove up to `amount` items of `itemId` from the player's depot, optionally restricting to a specific `tier`.
-- If any removed item contains serialized attributes, enforces one-at-a-time trading and returns those attributes.
-- @param player Player object whose depot will be modified.
-- @param itemId Numeric item type identifier to remove.
-- @param amount Number of items to remove.
-- @param tier Optional numeric tier to restrict removals to (0..10); pass nil to ignore tier.
-- @return `true` and the stored serialized attributes (string) if removal succeeded.
-- @return `false`, `nil`, and an error message string if removal failed or trading rules were violated.
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

-- Attempts to remove the specified amount of an item from the player's depot for a market operation.
-- @param player The player object whose depot will be scanned.
-- @param itemId The item type id to remove.
-- @param amount The quantity to remove.
-- @return `true` if the requested amount was successfully removed/reserved, `false` otherwise.
local function removeDepotItems(player, itemId, amount)
	local ok = removeDepotItemsWithAttributes(player, itemId, amount)
	return ok
end

-- Get the number of active market offers for a player.
-- @param playerId The player's database id.
-- @return The count of offers for the player, or `0` if the `market_offers` table is missing or the query fails.
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

-- Compute the next sequential inbox `sid` for the given player.
-- Queries `player_inboxitems` for the current maximum `sid` (defaulting to 100) and returns that value plus one.
-- @param playerId The numeric player identifier.
-- @return The next available inbox `sid` (integer).
local function getNextInboxSid(playerId)
	local sid = 100
	local resultId = db.storeQuery("SELECT COALESCE(MAX(`sid`), 100) AS `sid` FROM `player_inboxitems` WHERE `player_id` = " .. playerId)
	if resultId ~= false then
		sid = result.getDataInt(resultId, "sid")
		result.free(resultId)
	end
	return sid + 1
end

-- Insert item(s) into the player's inbox DB table, splitting into stack-sized rows and storing serialized attributes when present.
-- Validates the item type and that the `player_inboxitems` table exists. If `attributes` are serialized, `amount` must be 1 and the attributes are stored in the row; otherwise the function splits `amount` into one or more rows using the item's stack size.
-- @param playerId The numeric database id of the player receiving the item(s).
-- @param itemId The item type id to insert.
-- @param amount The total quantity to insert.
-- @param attributes Serialized attributes string for the item instance, or nil/empty for no attributes.
-- @return `true` if all necessary rows were successfully inserted, `false` on validation failure or any DB error.
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

-- Adds the specified item(s) into the provided inbox container, honoring stack sizes and serialized attributes.
-- When `attributes` are serialized, a single unique item is created and inserted (requires `amount == 1`).
-- @param inbox The inbox container object (must support `addItem`/`addItemEx` operations).
-- @param itemId The item type id to add.
-- @param amount The quantity to add (for serialized attributes this must be 1).
-- @param attributes Serialized item attributes or `nil`. When present, attributes are applied to a single created item.
-- @return `true` on success, `false` otherwise.
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

-- Checks whether an item’s serialized attributes match the given serialized attributes.
-- @param item Item object to compare; may be nil.
-- @param attributes Serialized attribute string (or empty/non-serialized).
-- @return `true` if `attributes` is not serialized or if `item` exists, supports `serializeAttributes`, and its serialized attributes equal `attributes`, `false` otherwise.
local function itemMatchesAttributes(item, attributes)
	if not hasSerializedAttributes(attributes) then
		return true
	end
	return item and item.serializeAttributes and item:serializeAttributes() == attributes
end

-- Remove up to `amount` of the specified item from the player's inbox, matching both item ID and optional serialized `attributes`.
-- Matches stackable counts and non-stackable items as needed; removal fails if the inbox does not contain the requested total.
-- @param player The Player whose inbox will be scanned and modified.
-- @param itemId The numeric item type ID to remove.
-- @param amount The total quantity to remove (must be > 0).
-- @param attributes Optional serialized attributes string to match specific item instances; pass `nil` to ignore attributes.
-- @return `true` if the requested amount was successfully removed, `false` otherwise.
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

-- Delivers items to a player by adding them to their in-memory inbox when online or inserting them into the persistent DB inbox when offline.
-- @param playerId The target player's numeric ID (used for DB insertion when the player is offline).
-- @param playerName The target player's current name (used to resolve an online Player object); may be nil.
-- @param itemId The item type ID to deliver.
-- @param amount The quantity to deliver.
-- @param attributes Serialized attributes string for the item when applicable, or nil.
-- @return `true` if the items were successfully delivered (either added to the player's inbox or inserted into the DB), `false` otherwise.
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

-- Credits `amount` to a player's bank balance, preferring an online player object when available.
-- @param playerId Numeric player id used for the database update when the player is offline.
-- @param playerName Optional player name; when provided and the player is online, updates their in-memory bank balance.
-- @param amount The amount to credit; values less than or equal to 0 are treated as no-op and succeed.
-- @return `true` on success; otherwise returns the database query result (truthy on success, falsy on failure).
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

-- Records a market offer event in the `market_history` table.
-- Inserts a history row if the `market_history` table exists; computes `expires_at` as `(created or now) + offerDuration` and clamps `tier` into the range 0..10.
-- @param playerId Numeric id of the player who created or was affected by the offer.
-- @param sale Numeric action code indicating buy or sell (e.g., `MARKET_ACTION_BUY` / `MARKET_ACTION_SELL`).
-- @param itemId Numeric item type id involved in the offer.
-- @param amount Number of items in the recorded event.
-- @param price Price per item (used to record the total value in history rows).
-- @param state Numeric history state code (e.g., `MARKET_STATE_ACCEPTED`, `MARKET_STATE_EXPIRED`, `MARKET_STATE_CANCELLED`).
-- @param created Optional timestamp to use as the offer creation time; when omitted, the current time is used.
-- @param tier Optional numeric tier for the item; will be coerced to an integer and clamped between 0 and 10.
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

-- Retrieves a market offer by its database ID, resolving stored attributes and deriving the effective tier from those attributes when present.
-- @param offerId The offer database ID.
-- @return A table with fields: `id`, `playerId`, `sale`, `itemId`, `amount`, `created`, `anonymous`, `price`, `tier`, `attributes`, `playerName`; or `nil` if the offer does not exist.
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
	offer.tier = getOfferTier(offer.itemId, offer.tier, offer.attributes)
	result.free(resultId)
	return offer
end

-- Parses the result of an SQL query and returns a list of market offers built from its rows.
-- @param query The SQL query string expected to select offer rows (columns: id, player_id, sale, itemtype, amount, created, anonymous, price, tier, player_name and any attributes blob).
-- @return A numeric-indexed table of offer tables. Each offer contains:
--   - id: offer id.
--   - playerId: owner player id.
--   - sale: action type (buy/sell code).
--   - itemId: item type id.
--   - amount: quantity.
--   - created: creation timestamp.
--   - anonymous: `true` if the offer is anonymous, `false` otherwise.
--   - price: unit price.
--   - tier: resolved tier (database `tier` field, overridden by derived tier from `attributes` when present).
--   - attributes: serialized attributes blob (or `nil`).
--   - playerName: owner name string.
--   - state: offer state (set to `MARKET_STATE_ACTIVE` for returned rows).
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
		offers[#offers].tier = getOfferTier(offers[#offers].itemId, offers[#offers].tier, offers[#offers].attributes)
	until not result.next(resultId)

	result.free(resultId)
	return offers
end

-- Retrieve recent market history rows for the given player.
-- @param playerId number The player's numeric id.
-- @return table A list (array) of history entry tables ordered by most recent `inserted`. Each entry contains:
--   - id: history row id.
--   - playerId: owner player's id.
--   - sale: action type (buy/sell code).
--   - itemId: itemtype id.
--   - amount: quantity involved.
--   - created: insertion timestamp (`inserted`).
--   - anonymous: `false` (placeholder; anonymity handled elsewhere).
--   - price: unit price.
--   - tier: item tier stored with the history row.
--   - playerName: player name string (empty here).
--   - state: history state code.
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

-- Writes a market offer's serialized fields into the output packet.
-- @param out Packet builder/writer object with methods like `addU32`, `addU16`, `addByte`, and `addString`.
-- @param offer Table representing an offer. Expected keys:
--   - id: offer identifier.
--   - created: unix timestamp when the offer was created.
--   - itemId: item type id.
--   - tier: numeric tier (clamped to 0..10).
--   - amount: quantity (clamped to 0..65535).
--   - price: unit price (clamped to MARKET_MAX_PRICE).
--   - anonymous: truthy to send `"Anonymous"` as the seller name.
--   - playerName: seller name used when `anonymous` is falsy.
--   - state: offer state code (defaults to MARKET_STATE_ACTIVE when absent).
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

-- Sends the market browse response for the given browseId to the player, including separate buy and sell offer lists and item detail when applicable.
-- @param player The player who will receive the browse response.
-- @param browseId The requested browse identifier: a catalog item id, MARKET_REQUEST_MY_OFFERS, or MARKET_REQUEST_MY_HISTORY.
-- @return `true` if the response packet was sent to the player, `false` if the player's client does not support the custom market network, or `nil` if the requested browseId is not tradable (no packet sent).
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

-- Sends the market "enter" catalog to the player's client, including current balance, active offer count, and available depot entries.
-- @param player The player receiving the catalog.
-- @param depotMap Optional map of available depot items; keys are either `"itemId"` or `"itemId:tier"` and values are available counts. If omitted, the player's depot is scanned to build this map.
-- @return `true` if the catalog packet(s) were sent to the player, `false` if the player's client does not support the custom network protocol.
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

-- Expires market offers whose lifetime has elapsed, atomically claims them from the database, returns funds or items to the offer owners, and records the expiration in market history.
-- Skips execution if the configured expire-check interval has not passed or if the `market_offers` table is absent. For each claimed offer this function attempts to credit the seller/buyer (money or item delivery), clears the per-player offer count cache on success, and on delivery failure restores the offer so it can be retried.
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
-- Handle a client's request to create a market offer.
-- Validates access, payload and cooldown; acquires a per-player lock; processes buy or sell flows
-- (reserving money and/or removing items from the player's depot, including tiered/serialized attributes);
-- inserts the offer into the database, refunds or restores resources on failure, notifies the player,
-- and refreshes the market view.
-- @param player The Player who sent the create-offer request.
-- @param msg The incoming message/packet containing the offer payload.
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
		offerTier = getOfferTier(itemId, offerTier, offerAttributes)
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
-- Cancels a market offer owned by the calling player, returns the reserved goods or funds, records the cancellation in market history, and refreshes the player's offers view.
-- The handler ensures the player still has market access, enforces action cooldowns, acquires locks to prevent concurrent races, atomically claims the offer from persistent storage, and attempts to deliver refunded items or restore bank balance. On failure to return items it logs an error; on success it notifies the player and updates the market listing.
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
-- Handle an incoming "accept offer" request from a client: validate access and cooldown, claim the requested market offer atomically, perform the required item and/or money transfers with rollback on failure, record the accepted offer in history, clear related caches, and notify both parties.
-- Performs pre-checks (offer existence and ownership), acquires locks (offer, acceptor, and owner) to avoid races, and enforces rules for tiered/custom-attribute offers.
-- @param player The Player object that sent the accept request (the acceptor).
-- @param msg The incoming network message; expected payload: offerId (u32) followed by amount (u16).
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
		acceptedTier = getOfferTier(offer.itemId, acceptedTier, deliveryAttributes)

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
	getDescriptions = function(itemId)
		return buildMarketDescriptions(tonumber(itemId) or 0)
	end,
	getAveragePrice = function(itemId)
		itemId = tonumber(itemId) or 0
		local stats = fetchMarketStatistics(itemId, MARKET_ACTION_SELL)
		if #stats == 0 then
			stats = fetchMarketStatistics(itemId, MARKET_ACTION_BUY)
		end

		local transactions = 0
		local totalPrice = 0
		for _, stat in ipairs(stats) do
			transactions = transactions + (tonumber(stat.transactions) or 0)
			totalPrice = totalPrice + (tonumber(stat.totalPrice) or 0)
		end
		if transactions <= 0 then
			return 0
		end
		return math.floor(totalPrice / transactions)
	end,
	browse = sendMarketBrowse,
	updateStatistics = refreshMarketStatistics
}
