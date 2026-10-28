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
local MARKET_DESC_SKILL_BOOST = 19
local MARKET_DESC_PROTECTION = 20
local MARKET_DESC_AUGMENTS = 21

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
local CATEGORY_DECORATION = 5
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
local CATEGORY_PREMIUM_SCROLLS = 22
local CATEGORY_CREATURE_PRODUCTS = 24
local CATEGORY_QUIVERS = 25
local CATEGORY_SOUL_CORES = 26
local CATEGORY_GOLD = 30

local METADATA_CATEGORY_BY_TOKEN = {
	["food"] = CATEGORY_FOOD,
	["potion"] = CATEGORY_POTIONS,
	["potions"] = CATEGORY_POTIONS,
	["valuable"] = CATEGORY_VALUABLES,
	["valuables"] = CATEGORY_VALUABLES,
	["creature product"] = CATEGORY_CREATURE_PRODUCTS,
	["creature products"] = CATEGORY_CREATURE_PRODUCTS,
	["creatureproduct"] = CATEGORY_CREATURE_PRODUCTS,
	["tool"] = CATEGORY_TOOLS,
	["tools"] = CATEGORY_TOOLS,
	["decoration"] = CATEGORY_DECORATION,
}

-- ItemTypes_t loot categories (src/items.h, after ITEM_TYPE_CARPET).
local ITEM_TYPE_CATEGORY = {
	[14] = CATEGORY_FOOD,
	[15] = CATEGORY_POTIONS,
	[16] = CATEGORY_VALUABLES,
	[17] = CATEGORY_CREATURE_PRODUCTS,
	[18] = CATEGORY_TOOLS,
	[19] = CATEGORY_DECORATION,
}

local shopCategoryLookups = {
	[CATEGORY_CREATURE_PRODUCTS] = {},
	[CATEGORY_DECORATION] = {},
}

local SHOP_CATEGORY_NAMES = {
	[CATEGORY_CREATURE_PRODUCTS] = "creature products",
	[CATEGORY_DECORATION] = "decoration",
}

local function buildShopCategoryLookups()
	for categoryId in pairs(shopCategoryLookups) do
		shopCategoryLookups[categoryId] = {}
	end

	if type(LootShopConfigTable) ~= "table" then
		return
	end

	for categoryId, shopName in pairs(SHOP_CATEGORY_NAMES) do
		local items = LootShopConfigTable[shopName]
		if type(items) == "table" then
			for _, entry in ipairs(items) do
				local itemId = tonumber(entry.clientId)
				if itemId and itemId > 0 then
					shopCategoryLookups[categoryId][itemId] = true
				end
			end
		end
	end
end

local function isShopCategoryItem(categoryId, itemId)
	local lookup = shopCategoryLookups[categoryId]
	return lookup ~= nil and lookup[itemId] == true
end

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

-- Shown in cyclopedia/market catalog, but players cannot create buy/sell offers.
local marketTradeBlockedItems = {}
for _, itemId in ipairs({
	_G.ITEM_GOLD_COIN,
	_G.ITEM_PLATINUM_COIN,
	_G.ITEM_CRYSTAL_COIN,
	_G.ITEM_GOLD_NUGGET,
}) do
	if itemId then
		marketTradeBlockedItems[itemId] = true
	end
end

-- Never listed in the custom market catalog (in-world market object).
local marketCatalogBlockedItems = {
	[MARKET_ITEM_ID] = true,
}

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

-- Describe the committed receipt, not a guessed home town or a successful enqueue.
local function describeMarketDelivery(player, offerId, prefix, verb)
	local status = Game.getMarketDeliveryStatus(player:getGuid(), offerId)
	if not status then
		return prefix .. " Delivery status is unavailable. Please check again shortly."
	end
	if status.itemId == 0 then
		return prefix .. " " .. status.bankCredit .. " gold " ..
			(status.applied and "credited to your bank account." or "reserved; awaiting payment to your bank account.")
	end
	local town = Town(status.depotId)
	local destination = town and ("your inbox in " .. town:getName()) or "your legacy Market inbox"
	local itemType = ItemType(status.itemId)
	local itemName = itemType and itemType:getName() or ("item " .. status.itemId)
	local item = status.amount .. "x " .. itemName
	if status.applied then
		return prefix .. " " .. item .. ": " .. (verb or "delivered") .. " to " .. destination .. "."
	end
	return prefix .. " " .. item .. " is awaiting delivery to " .. destination ..
		". You will be notified once delivery is complete."
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

local function tileHasMarketAccess(tile, allowStandaloneMarket)
	if not tile then
		return false
	end

	if tile:getItemByType(ITEM_TYPE_DEPOT) then
		return true
	end
	if allowStandaloneMarket and tile.getItemById and tile:getItemById(MARKET_ITEM_ID) then
		return true
	end
	return false
end

local function hasMarketAccessAtPosition(player, position)
	if not player or not position then
		return false
	end

	local playerTile = Tile(position)
	if not playerTile then
		return false
	end

	-- A physical depot is usable even if a city's tiles are not marked PZ.
	-- Keep the PZ requirement for standalone market objects and keep all access
	-- limited to the current floor and adjacent tiles, including after movement.
	local allowStandaloneMarket = playerTile:hasFlag(TILESTATE_PROTECTIONZONE)
	if tileHasMarketAccess(playerTile, allowStandaloneMarket) then
		return true
	end

	for x = -1, 1 do
		for y = -1, 1 do
			if x ~= 0 or y ~= 0 then
				local tile = Tile(Position(position.x + x, position.y + y, position.z))
				if tileHasMarketAccess(tile, allowStandaloneMarket) then
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

local LEGACY_QUIVER_ITEM_IDS = {
	-- Genuine quivers should use WEAPON_QUIVER; keep verified legacy IDs here only.
}

local function isQuiverItem(itemType)
	if itemType:getWeaponType() == WEAPON_QUIVER then
		return true
	end

	return LEGACY_QUIVER_ITEM_IDS[itemType:getId()] == true
end

local function isPremiumScrollItem(itemType)
	if not itemType or itemType:getId() == 0 then
		return false
	end

	return itemType:getName():lower():find("premium scroll", 1, true) ~= nil
end

local function normalizeMetadataToken(value)
	if type(value) ~= "string" or value == "" then
		return nil
	end
	return value:lower():gsub("[%s_%-]+", " "):gsub("^%s+", ""):gsub("%s+$", "")
end

local function getCategoryFromMetadata(attributes)
	if not attributes then
		return nil
	end

	for _, key in ipairs({ "loottype", "primarytype" }) do
		local token = normalizeMetadataToken(attributes[key])
		if token and METADATA_CATEGORY_BY_TOKEN[token] then
			return METADATA_CATEGORY_BY_TOKEN[token]
		end
	end

	return nil
end

local function getCategoryFromItemTypeEnum(itemType)
	if not itemType or not itemType.getType then
		return nil
	end

	return ITEM_TYPE_CATEGORY[itemType:getType()]
end

local function getItemCategory(itemType, xmlAttributes)
	-- Currency must stay in cyclopedia Gold (30); Crystal XML tags coins as valuables.
	if itemType:getWorth() > 0 then
		return CATEGORY_GOLD
	end

	local metadataCategory = getCategoryFromMetadata(xmlAttributes)
	if metadataCategory then
		return metadataCategory
	end

	local enumCategory = getCategoryFromItemTypeEnum(itemType)
	if enumCategory then
		return enumCategory
	end

	if itemType:isRune() then
		return CATEGORY_RUNES
	end

	if isQuiverItem(itemType) then
		return CATEGORY_QUIVERS
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

	if isShopCategoryItem(CATEGORY_DECORATION, itemType:getId()) then
		return CATEGORY_DECORATION
	end

	if isShopCategoryItem(CATEGORY_CREATURE_PRODUCTS, itemType:getId()) then
		return CATEGORY_CREATURE_PRODUCTS
	end

	local name = itemType:getName():lower()

	if isPremiumScrollItem(itemType) then
		return CATEGORY_PREMIUM_SCROLLS
	end

	if name:find("soul core", 1, true) then
		return CATEGORY_SOUL_CORES
	end

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
	if itemId <= 0 or itemId > 0xFFFF or marketCatalogBlockedItems[itemId] then
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

	if isPremiumScrollItem(itemType) then
		return true
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

local function readMoveeventAttribute(itemNode, key)
	if not itemNode or not key then
		return nil
	end

	for attributeNode in itemNode:children() do
		if attributeNode:name() == "attribute" then
			local nodeKey = attributeNode:attribute("key")
			local nodeValue = attributeNode:attribute("value")
			if nodeKey == "script" and nodeValue == "moveevent" then
				for subNode in attributeNode:children() do
					if subNode:name() == "attribute" and subNode:attribute("key") == key then
						return subNode:attribute("value")
					end
				end
			end
		end
	end

	return nil
end

local function addMarketItem(itemId, xmlName, xmlAttributes, itemNode)
	itemId = tonumber(itemId)
	if not itemId or marketItemsById[itemId] or not isMarketableItem(itemId) then
		return
	end

	local itemType = ItemType(itemId)
	local name = xmlName or itemType:getName()
	if not name or name == "" then
		return
	end

	local moveeventVocation = readMoveeventAttribute(itemNode, "vocation")
	local moveeventLevel = tonumber(readMoveeventAttribute(itemNode, "level")) or 0
	local requiredLevel = math.max(tonumber(itemType:getMinReqLevel()) or 0, moveeventLevel)
	local restrictVocation = 0

	local entry = {
		id = itemId,
		name = name,
		category = getItemCategory(itemType, xmlAttributes),
		moveeventVocation = moveeventVocation,
		requiredLevel = requiredLevel,
		restrictVocation = restrictVocation,
	}

	marketItemsById[itemId] = entry
	marketItemXmlAttributes[itemId] = copyAttributes(xmlAttributes)
	marketItems[#marketItems + 1] = entry
end

local function loadMarketCatalog()
	buildShopCategoryLookups()
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
				addMarketItem(id, name, attributes, itemNode)
			elseif fromId and toId and toId >= fromId then
				for itemId = fromId, toId do
					addMarketItem(itemId, name, attributes, itemNode)
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
end

local function getPlayerTotalMoney(player)
	local inventoryMoney = math.max(0, tonumber(player:getMoney()) or 0)
	local bankBalance = math.max(0, tonumber(player:getBankBalance()) or 0)
	return inventoryMoney + bankBalance
end

local function removePlayerMarketMoney(player, amount)
	-- C++ retains the exact original coins/parents and bank balance for this handler.
	if Game.reserveMarketMoney(player:getGuid(), math.max(0, tonumber(amount) or 0)) then
		return true
	end
	return nil
end

local function finishPlayerMarketMoney(player, payment, restore)
	if payment then
		Game.finishMarketMoney(player:getGuid(), restore)
	end
end

local function refundPlayerMarketMoney(player, payment)
	finishPlayerMarketMoney(player, payment, true)
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

	-- Ignore only the ordinary stack count. Custom stack attributes must never
	-- silently disappear when an offer is converted to a quantity payload.
	if itemType and itemType:isStackable() then
		local normal = Game.createItem(item:getId(), item:getCount())
		if not normal then
			return attributes
		end
		local ordinary = normal:serializeAttributes() == attributes
		normal:remove()
		if ordinary then
			return nil
		end
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

local function getMarketItemClassification(itemId)
	itemId = tonumber(itemId) or 0
	if itemId <= 0 then
		return 0
	end

	local xmlAttributes = marketItemXmlAttributes[itemId] or {}
	local itemType = ItemType(itemId)
	return tonumber(xmlAttributes.classification) or tonumber(itemType:getClassification()) or 0
end

-- Client cyclopedia/market filters use OTC vocation ids (1=sorc, 2=druid, 3=pally, 4=knight, 9=monk).
local MARKET_VOCATION_NAME_TO_CLIENT_ID = {
	["sorcerer"] = 1,
	["master sorcerer"] = 1,
	["druid"] = 2,
	["elder druid"] = 2,
	["paladin"] = 3,
	["royal paladin"] = 3,
	["knight"] = 4,
	["elite knight"] = 4,
	["monk"] = 9,
	["exalted monk"] = 9,
}

local marketRestrictVocationCache = {}

local function normalizeMarketVocationName(name)
	name = name:gsub("^%s+", ""):gsub("%s+$", ""):lower()
	if name:sub(-1) == "s" then
		name = name:sub(1, -2)
	end
	return name
end

local function addVocationMaskFromName(mask, rawName)
	local name = normalizeMarketVocationName(rawName:match("^([^;]+)") or rawName)
	local clientVocId = MARKET_VOCATION_NAME_TO_CLIENT_ID[name]
	if clientVocId and clientVocId > 0 then
		return mask | (2 ^ (clientVocId - 1))
	end
	return mask
end

local function parseVocationMaskFromRawAttribute(vocationValue)
	if not vocationValue or vocationValue == "" then
		return 0
	end

	local mask = 0
	for part in vocationValue:gmatch("[^,]+") do
		mask = addVocationMaskFromName(mask, part)
	end
	return mask
end

local function parseVocationMaskFromDisplayString(vocationString)
	if not vocationString or vocationString == "" then
		return 0
	end

	local normalized = vocationString:lower():gsub("%s+and%s+", ",")
	local mask = 0
	for part in normalized:gmatch("[^,]+") do
		mask = addVocationMaskFromName(mask, part)
	end
	return mask
end

local function getMarketRestrictVocation(itemId)
	itemId = tonumber(itemId) or 0
	if itemId <= 0 then
		return 0
	end

	if marketRestrictVocationCache[itemId] ~= nil then
		return marketRestrictVocationCache[itemId]
	end

	local entry = marketItemsById[itemId]
	if entry and entry.restrictVocation and entry.restrictVocation > 0 then
		marketRestrictVocationCache[itemId] = entry.restrictVocation
		return entry.restrictVocation
	end

	local mask = 0
	if entry and entry.moveeventVocation then
		mask = parseVocationMaskFromRawAttribute(entry.moveeventVocation)
	end

	if mask == 0 then
		mask = parseVocationMaskFromDisplayString(ItemType(itemId):getVocationString())
	end

	if entry then
		entry.restrictVocation = mask
	end
	marketRestrictVocationCache[itemId] = mask
	return mask
end

local function getMarketRequiredLevel(itemId)
	itemId = tonumber(itemId) or 0
	if itemId <= 0 then
		return 0
	end

	local entry = marketItemsById[itemId]
	if entry and entry.requiredLevel and entry.requiredLevel > 0 then
		return entry.requiredLevel
	end

	return math.max(0, tonumber(ItemType(itemId):getMinReqLevel()) or 0)
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
	local tierAmountsByItem = {}

	for key, amount in pairs(depotMap) do
		if type(key) == "string" and amount > 0 then
			local itemId, tier = key:match("^(%d+):(%d+)$")
			itemId = tonumber(itemId)
			tier = tonumber(tier)
			if itemId and tier and tier > 0 and tier <= 10 then
				local tierAmounts = tierAmountsByItem[itemId]
				if not tierAmounts then
					tierAmounts = {}
					tierAmountsByItem[itemId] = tierAmounts
				end
				tierAmounts[tier] = amount
			end
		end
	end

	for _, entry in ipairs(marketItems) do
		local classification = getMarketItemClassification(entry.id)
		local requiredLevel = getMarketRequiredLevel(entry.id)
		local restrictVocation = getMarketRestrictVocation(entry.id)
		entries[#entries + 1] = {
			id = entry.id,
			category = entry.category,
			name = entry.name,
			amount = depotMap[getDepotItemKey(entry.id, 0)] or 0,
			tier = 0,
			classification = classification,
			requiredLevel = requiredLevel,
			restrictVocation = restrictVocation
		}

		local tierAmounts = tierAmountsByItem[entry.id]
		if tierAmounts then
			for tier = 1, 10 do
				local amount = tierAmounts[tier]
				if amount then
					entries[#entries + 1] = {
						id = entry.id,
						category = entry.category,
						name = entry.name,
						amount = amount,
						tier = tier,
						classification = classification,
						requiredLevel = requiredLevel,
						restrictVocation = restrictVocation
					}
				end
			end
		end
	end

	return entries
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
-- Keep exact rollback copies, including parent container, subtype and attributes.
-- All clones are made before removing anything. Non-empty containers cannot be
-- represented by the Market's single-item attributes payload.
local function finishReservation(reservation, restore)
	if not reservation then
		return
	end
	for _, entry in ipairs(reservation) do
		if restore and entry.removed then
			local ret = entry.parent:addItemEx(entry.backup, INDEX_WHEREEVER, FLAG_NOLIMIT)
			if ret ~= RETURNVALUE_NOERROR then
				error("Could not restore Market reservation to its original container")
			end
		else
			entry.backup:remove()
		end
	end
end

local function removeDepotItemsWithAttributes(player, itemId, amount, tier)
	local removals = collectDepotRemovals(player, itemId, amount, tier)
	if not removals then
		return false, nil, "You do not have enough items for this tier."
	end
	local itemType = ItemType(itemId)
	local storedAttributes = nil
	for _, entry in ipairs(removals) do
		if entry.item:isContainer() and entry.item:getSize() ~= 0 then
			return false, nil, "Empty containers before trading them on the Market."
		end
		local attributes = getMarketItemAttributes(entry.item, itemType)
		if hasSerializedAttributes(attributes) then
			if amount ~= 1 or entry.count ~= 1 or (itemType:isStackable() and entry.item:getCount() ~= 1) then
				return false, nil, "Tiered/custom items must be traded one at a time."
			end
			storedAttributes = attributes
		end
	end
	local reservation = {}
	for _, entry in ipairs(removals) do
		local backup = entry.item:clone()
		if not backup then
			finishReservation(reservation, false)
			return false, nil, "Could not reserve the items."
		end
		if itemType:isStackable() and entry.count ~= backup:getCount() then
			backup:transform(itemId, entry.count)
		end
		reservation[#reservation + 1] = { item = entry.item, count = entry.count,
			backup = backup, parent = entry.item:getParent() }
	end
	for _, entry in ipairs(reservation) do
		if not entry.item:remove(entry.count) then
			finishReservation(reservation, true)
			return false, nil, "Could not reserve the items."
		end
		entry.removed = true
	end
	return true, storedAttributes, nil, reservation
end

-- Calculates the one-percent market fee, clamped to the protocol's 20..1000 range.
local function calculateFee(price, amount)
	local fee = math.ceil((price * amount) / 100)
	if fee < 20 then
		return 20
	elseif fee > 1000 then
		return 1000
	end
	return fee
end

-- Retrieves a market offer by its database ID, resolving stored attributes and deriving the effective tier from those attributes when present.
-- @param offerId The offer database ID.
-- @return A table with fields: `id`, `playerId`, `sale`, `itemId`, `amount`, `created`, `anonymous`, `price`, `tier`, `attributes`, `playerName`; or `nil` if the offer does not exist.
local function fetchOfferById(offerId)
	local offer = Game.getMarketOffer(offerId)
	if not offer then
		return nil
	end
	offer.tier = getOfferTier(offer.itemId, offer.tier, offer.attributes)
	return offer
end

-- Resolves the effective tier for offers returned by the C++ persistence layer.
-- @param offers Market offer tables returned by Game market methods.
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
local function resolveOfferTiers(offers)
	for _, offer in ipairs(offers) do
		offer.tier = getOfferTier(offer.itemId, offer.tier, offer.attributes)
	end
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
	return Game.getMarketHistory(playerId, MARKET_MAX_PACKET_OFFERS)
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

local function buildSkillBoostDescription(itemType)
	local abilities = itemType:getAbilities()
	if not abilities then
		return nil
	end

	local parts = {}

	if abilities.stats then
		for statIndex, value in pairs(abilities.stats) do
			value = tonumber(value) or 0
			if value ~= 0 and getStatName then
				local statId = (tonumber(statIndex) or 1) - 1
				local statName = getStatName(statId)
				if statName and statName ~= "unknown" then
					parts[#parts + 1] = string.format("%s %+d", statName, value)
				end
			end
		end
	end

	if abilities.skills then
		for skillIndex, value in pairs(abilities.skills) do
			value = tonumber(value) or 0
			if value ~= 0 and getSkillName then
				local skillId = (tonumber(skillIndex) or 1) - 1
				local skillName = getSkillName(skillId)
				if skillName and skillName ~= "unknown" then
					parts[#parts + 1] = string.format("%s %+d", skillName, value)
				end
			end
		end
	end

	if abilities.specialMagicLevel then
		for element, value in pairs(abilities.specialMagicLevel) do
			value = tonumber(value) or 0
			if value ~= 0 and getCombatName then
				local elementName = getCombatName(2 ^ (element - 1))
				if elementName then
					parts[#parts + 1] = string.format("%s magic level %+d", elementName, value)
				end
			end
		end
	end

	if abilities.specialSkills then
		for skillIndex, value in pairs(abilities.specialSkills) do
			value = tonumber(value) or 0
			if value ~= 0 and getSpecialSkillName then
				local specialSkillId = (tonumber(skillIndex) or 1) - 1
				local skillName = getSpecialSkillName(specialSkillId)
				if skillName and skillName ~= "unknown" then
					local formattedValue
					if specialSkillId >= 6 then
						formattedValue = string.format("%g", value / 100)
					else
						formattedValue = string.format("%+g", value / 100)
					end
					parts[#parts + 1] = string.format("%s %s%%", skillName, formattedValue)
				end
			end
		end
	end

	if #parts == 0 then
		return nil
	end
	return table.concat(parts, ", ")
end

local function addSkillBoostDescriptions(descriptions, itemType)
	local text = buildSkillBoostDescription(itemType)
	if text then
		addDescription(descriptions, MARKET_DESC_SKILL_BOOST, text)
	end
end

local function formatAugmentDescription(itemType)
	local text = itemType:getAugmentDescription()
	if not text or text == "" then
		return nil
	end

	text = text:gsub("^%s+", "")
	text = text:gsub("^Augments:%s*", "")
	if text:sub(-1) == "." then
		text = text:sub(1, -2)
	end
	if text == "" then
		return nil
	end
	return text
end

local function addAugmentDescriptions(descriptions, itemType)
	local text = formatAugmentDescription(itemType)
	if text then
		addDescription(descriptions, MARKET_DESC_AUGMENTS, text)
	end
end

local function buildProtectionDescription(itemType)
	local abilities = itemType:getAbilities()
	if not abilities or not abilities.absorbPercent then
		return nil
	end

	local absorbPercent = abilities.absorbPercent
	local protectionPhysical = absorbPercent[1]
	for elem = 2, #absorbPercent do
		local val = absorbPercent[elem]
		if protectionPhysical ~= val then
			protectionPhysical = nil
			break
		end
	end

	if protectionPhysical and protectionPhysical ~= 0 then
		return string.format("all %+d%%", protectionPhysical)
	end

	local protections = {}
	for element, value in pairs(absorbPercent) do
		value = tonumber(value) or 0
		if value ~= 0 and getCombatName then
			local elementName = getCombatName(2 ^ (element - 1))
			if elementName then
				protections[#protections + 1] = string.format("%s %+d%%", elementName, value)
			end
		end
	end

	if #protections == 0 then
		return nil
	end
	return table.concat(protections, ", ")
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

	local protection = buildProtectionDescription(itemType)
	if protection then
		addDescription(descriptions, MARKET_DESC_PROTECTION, protection)
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
	addSkillBoostDescriptions(descriptions, itemType)
	addAugmentDescriptions(descriptions, itemType)

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
	local since = os.time() - ((MARKET_STATISTICS_DAYS - 1) * 24 * 60 * 60)
	local firstDay = math.floor(since / 86400) * 86400
	return Game.getMarketStatistics(itemId, actionType, firstDay, MARKET_STATISTICS_DAYS)
end

local function refreshMarketStatistics()
	local since = os.time() - ((MARKET_STATISTICS_DAYS - 1) * 24 * 60 * 60)
	local firstDay = math.floor(since / 86400) * 86400
	return Game.refreshMarketStatistics(firstDay, MARKET_STATE_ACCEPTED)
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
		for _, offer in ipairs(resolveOfferTiers(Game.getOwnMarketOffers(playerId, MARKET_MAX_PACKET_OFFERS))) do
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
		buyOffers = resolveOfferTiers(Game.getItemMarketOffers(browseId, MARKET_ACTION_BUY, MARKET_MAX_PACKET_OFFERS))
		sellOffers = resolveOfferTiers(Game.getItemMarketOffers(browseId, MARKET_ACTION_SELL, MARKET_MAX_PACKET_OFFERS))
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
	local sendItemMetadata = player.supportsCustomItemNetwork and player:supportsCustomItemNetwork()
	local offers = offerCountCache[playerGuid]
	if offers == nil then
		offers = clamp(Game.getMarketOfferCount(playerGuid), 0, MARKET_MAX_OFFERS)
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
			if sendItemMetadata then
				out:addByte(entry.classification or 0)
				out:addU16(entry.requiredLevel or 0)
				out:addU16(entry.restrictVocation or 0)
			end
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

-- Claim, history and the durable return are one SQL transaction. Offline recipients
-- keep their pending delivery until login; no SQL is written directly to their inbox.
local function expireOffers()
	local now = os.time()
	if now - lastExpireCheck < MARKET_EXPIRE_CHECK_INTERVAL then
		return
	end
	lastExpireCheck = now
	for _, offer in ipairs(Game.getExpiredMarketOffers(now - getOfferDuration(), 100)) do
		local offerKey = "offer:" .. offer.id
		if acquireLock(offerKey) then
			local settled = Game.settleMarketOffer(0, offer.id, offer.amount, offer.amount,
				MARKET_STATE_EXPIRED, getOfferDuration(), offer.tier)
			if settled then
				offerCountCache[offer.playerId] = nil
				Game.processMarketDeliveries(offer.playerId)
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

	if marketTradeBlockedItems[itemId] then
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

	if Game.getMarketOfferCount(player:getGuid()) >= MARKET_MAX_OFFERS then
		return false, "You have too many active market offers."
	end

	return true
end

local openHandler = PacketHandler(OPCODE_MARKET_OPEN)
function openHandler.onReceive(player, msg)
	expireOffers()

	-- Cyclopedia/OTC can request the catalog without opening a market session.
	if not hasCurrentMarketAccess(player) then
		sendMarketEnter(player, {})
		return
	end

	setMarketDepotId(player, getPlayerLastDepotId(player))
	setMarketOpen(player)
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
	local anonymous = msg:getByte() ~= 0
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

	if not Game.isMarketPlayerReady(player:getGuid()) then
		releaseLock(playerKey)
		sendMarketMessage(player, "Your items are being saved. Please try again shortly.")
		return
	end

	local totalPrice = price * amount
	local fee = calculateFee(price, amount)
	local payment = nil
	local depotMap = nil
	local offerAttributes = nil
	local reservation = nil
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
		local reserved, attributes, reserveError, reservedItems = removeDepotItemsWithAttributes(player, itemId, amount, requestedTier)
		if not reserved then
			releaseLock(playerKey)
			sendMarketMessage(player, reserveError or "Could not reserve the items for this sell offer.")
			return
		end
		reservation = reservedItems
		offerAttributes = attributes
		offerTier = getOfferTier(itemId, offerTier, offerAttributes)
		depotMap[itemId] = math.max(0, (depotMap[itemId] or 0) - amount)
		if requestedTier ~= nil then
			local tierKey = getDepotItemKey(itemId, requestedTier)
			depotMap[tierKey] = math.max(0, (depotMap[tierKey] or 0) - amount)
		end
		payment = removePlayerMarketMoney(player, fee)
		if not payment then
			finishReservation(reservation, true)
			releaseLock(playerKey)
			sendMarketMessage(player, "Could not pay the market fee.")
			return
		end
	end

	local now = os.time()
	local ok = Game.createMarketOffer(
		player:getGuid(), actionType, itemId, amount, now, anonymous, price,
		math.max(0, math.min(10, tonumber(offerTier) or 0)), offerAttributes
	)
	if ok == nil then
		finishPlayerMarketMoney(player, payment, false)
		finishReservation(reservation, false)
		-- COMMIT outcome unknown. C++ disconnected the player and blocked saves.
		releaseLock(playerKey)
		return
	end
	if not ok then
		if actionType == MARKET_ACTION_BUY then
			refundPlayerMarketMoney(player, payment)
		else
			finishReservation(reservation, true)
			refundPlayerMarketMoney(player, payment)
		end
		releaseLock(playerKey)
		sendMarketMessage(player, "Could not create the market offer.")
		return
	end

	finishPlayerMarketMoney(player, payment, false)
	finishReservation(reservation, false)
	offerCountCache[player:getGuid()] = nil
	releaseLock(playerKey)
	sendMarketMessage(player, "Market offer created.")
	refreshMarket(player, itemId, depotMap)
end
createHandler:register()

local cancelHandler = PacketHandler(OPCODE_MARKET_CANCEL)
-- Cancels a market offer owned by the calling player, returns the reserved goods or funds, records the cancellation in market history, and refreshes the player's offers view.
-- The offer, history and return entitlement commit together; materialization may be deferred while the player is saving.
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

	local settled = Game.settleMarketOffer(player:getGuid(), offer.id, offer.amount, offer.amount,
		MARKET_STATE_CANCELLED, getOfferDuration(), offer.tier)
	if not settled then
		releaseMultipleLocks({ offerKey, playerKey })
		sendMarketMessage(player, settled == nil and "Cancellation confirmation is pending." or "Could not cancel the market offer.")
		return
	end

	offerCountCache[player:getGuid()] = nil
	Game.processMarketDeliveries(player:getGuid())
	releaseMultipleLocks({ offerKey, playerKey })
	sendMarketMessage(player, describeMarketDelivery(player, offer.id, "Offer #" .. offer.id .. " cancelled.", "returned"))
	refreshMarket(player, MARKET_REQUEST_MY_OFFERS)
end
cancelHandler:register()

local acceptHandler = PacketHandler(OPCODE_MARKET_ACCEPT)
-- Reserve the acceptor resources, then commit their snapshot, offer claim, history and durable deliveries together.
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

	if not Game.isMarketPlayerReady(player:getGuid()) then
		releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
		sendMarketMessage(player, "Your items are being saved. Please try again shortly.")
		return
	end

	local totalPrice = amount * offer.price
	local depotMap = nil
	local acceptedTier = tonumber(offer.tier) or 0
	local payment = nil
	local deliveryAttributes = nil
	local reservation = nil
	local oldBalance = player:getBankBalance()

	-- Reserve only the acceptor's resources. C++ persists this snapshot together
	-- with the offer claim, recipient deliveries and history, or rolls it all back.
	if offer.sale == MARKET_ACTION_SELL then
		payment = removePlayerMarketMoney(player, totalPrice)
		if not payment then
			releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
			sendMarketMessage(player, "You do not have enough money.")
			return
		end
	else
		-- Lua bank balances use signed integers; reject before reserving items.
		if oldBalance < 0 or totalPrice > math.maxinteger - oldBalance then
			releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
			sendMarketMessage(player, "Your bank balance is too high to accept this offer.")
			return
		end
		local removed, attributes, removeError, reservedItems = removeDepotItemsWithAttributes(player, offer.itemId, amount, acceptedTier)
		if not removed then
			releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
			sendMarketMessage(player, removeError or "Could not remove the items.")
			return
		end
		reservation = reservedItems
		deliveryAttributes = attributes
		acceptedTier = getOfferTier(offer.itemId, acceptedTier, attributes)
		player:setBankBalance(oldBalance + totalPrice)
	end

	local settled = Game.settleMarketOffer(player:getGuid(), offer.id, offer.amount, amount,
		MARKET_STATE_ACCEPTED, getOfferDuration(), acceptedTier, deliveryAttributes)
	if settled == nil then
		finishPlayerMarketMoney(player, payment, false)
		finishReservation(reservation, false)
		-- Never compensate an uncertain commit; C++ has quarantined this player.
		releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
		return
	end
	if not settled then
		if offer.sale == MARKET_ACTION_SELL then
			refundPlayerMarketMoney(player, payment)
		else
			player:setBankBalance(oldBalance)
			finishReservation(reservation, true)
		end
		releaseMultipleLocks({ offerKey, buyerKey, ownerKey })
		sendMarketMessage(player, "Could not accept the market offer. Please try again shortly.")
		return
	end

	finishPlayerMarketMoney(player, payment, false)
	finishReservation(reservation, false)
	offerCountCache[offer.playerId] = nil
	Game.processMarketDeliveries(player:getGuid())
	Game.processMarketDeliveries(offer.playerId)
	releaseMultipleLocks({ offerKey, buyerKey, ownerKey })

	if offer.sale == MARKET_ACTION_SELL then
		sendMarketMessage(player, describeMarketDelivery(player, offer.id, "Offer #" .. offer.id .. " accepted."))
	else
		sendMarketMessage(player, "Offer #" .. offer.id .. " accepted. " .. totalPrice .. " gold credited to your bank account.")
	end
	refreshMarket(player, offer.itemId, depotMap)

	local owner = Player(offer.playerName)
	if owner then
		sendMarketMessage(owner, describeMarketDelivery(owner, offer.id, "Your offer #" .. offer.id .. " has been fulfilled."))
		if marketOpenSessions[owner:getId()] then
			sendMarketBrowse(owner, MARKET_REQUEST_MY_OFFERS)
		end
	end
end
acceptHandler:register()

marketSessionCleanup = CreatureEvent("CustomMarketSessionCleanup")
function marketSessionCleanup.onLogout(player)
	lastAction[player:getId()] = nil
	marketDepotSessions[player:getId()] = nil
	marketOpenSessions[player:getId()] = nil
	offerCountCache[player:getGuid()] = nil
	return true
end
marketSessionCleanup:register()

marketSessionInit = CreatureEvent("CustomMarketSessionInit")
function marketSessionInit.onLogin(player)
	player:registerEvent("CustomMarketSessionCleanup")
	return true
end
marketSessionInit:register()

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
