local SUPPLY_STASH_ITEM_ID = ITEM_SUPPLY_STASH or 28750

local OPCODE_SUPPLY_STASH_REQUEST = 0x28
local OPCODE_SUPPLY_STASH_SEND = 0x29

local ACTION_OPEN = 1
local ACTION_STOW_ALL = 2
local ACTION_WITHDRAW = 3

local SUPPLY_STASH_MAX_UNIQUE_ITEMS = 1000
local SUPPLY_STASH_MAX_WITHDRAW = 100000
local SUPPLY_STASH_MAX_WITHDRAW_NON_STACKABLE = 100
local SUPPLY_STACK_SIZE = 100
local SUPPLY_STASH_DEPOT_BOX_FIRST = 1
local SUPPLY_STASH_DEPOT_BOX_LAST = 15
local SUPPLY_STASH_DETAILS_MARKER = 0x5354

local CATEGORY_ARMORS = 1
local CATEGORY_AMULETS = 2
local CATEGORY_BOOTS = 3
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
local CATEGORY_CREATURE_PRODUCTS = 24
local CATEGORY_FISTS = 25

local supplyStashDepotSessions = {}

local blockedItems = {}
for _, itemId in ipairs({
	_G.ITEM_GOLD_COIN,
	_G.ITEM_PLATINUM_COIN,
	_G.ITEM_CRYSTAL_COIN,
	_G.ITEM_GOLD_NUGGET,
	ITEM_MARKET,
	SUPPLY_STASH_ITEM_ID,
	ITEM_INBOX,
	ITEM_STORE_INBOX,
	ITEM_DEPOT
}) do
	if itemId then
		blockedItems[itemId] = true
	end
end

-- Checks whether the given player is using OTClient network features.
-- @param player The player object (may be nil).
-- @return `true` if `player` exists and `player:isUsingOtClient()` is truthy, `false` otherwise.
local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function logError(message)
	if logger and logger.error then
		logger.error(message)
	else
		print(message)
	end
end

local function runSchemaQuery(query, errorMessage)
	local ok, result = pcall(db.query, query)
	if not ok or not result then
		logError("[SupplyStash] " .. errorMessage)
		return false
	end
	return true
end

-- Checks whether a given column name exists in the specified database table.
-- @param tableName The name of the database table to inspect.
-- @param columnName The column name to look for (pattern is matched with SQL LIKE).
-- @return `true` if the column exists in the table, `false` otherwise.
local function tableColumnExists(tableName, columnName)
	local resultId = db.storeQuery("SHOW COLUMNS FROM `" .. tableName .. "` LIKE " .. db.escapeString(columnName))
	if resultId then
		result.free(resultId)
		return true
	end
	return false
end

local function tableForeignKeyExists(tableName, constraintName)
	local resultId = db.storeQuery(
		"SELECT `CONSTRAINT_NAME` FROM `information_schema`.`TABLE_CONSTRAINTS` WHERE `CONSTRAINT_SCHEMA` = DATABASE() " ..
		"AND `TABLE_NAME` = " .. db.escapeString(tableName) ..
		" AND `CONSTRAINT_NAME` = " .. db.escapeString(constraintName) ..
		" AND `CONSTRAINT_TYPE` = 'FOREIGN KEY' LIMIT 1"
	)
	if resultId then
		result.free(resultId)
		return true
	end
	return false
end

local function addSupplyStashForeignKey(tableName, errorMessage)
	return runSchemaQuery(
		"ALTER TABLE `" .. tableName .. "` ADD CONSTRAINT `player_supplystash_player_fk` " ..
		"FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE",
		errorMessage
	)
end

-- Returns the primary key column signature for the `player_supplystash` table as a comma-separated string.
-- @return A string containing primary key column names in index order (e.g., "player_id,itemtype,tier"). Returns an empty string if the table has no PRIMARY index or cannot be queried.
local function getSupplyStashPrimaryKeySignature()
	local indexes = {}
	local resultId = db.storeQuery("SHOW INDEX FROM `player_supplystash` WHERE `Key_name` = 'PRIMARY'")
	if resultId then
		repeat
			indexes[#indexes + 1] = {
				seq = result.getDataInt(resultId, "Seq_in_index"),
				column = result.getDataString(resultId, "Column_name")
			}
		until not result.next(resultId)
		result.free(resultId)
	end

	table.sort(indexes, function(a, b)
		return a.seq < b.seq
	end)

	local columns = {}
	for _, index in ipairs(indexes) do
		columns[#columns + 1] = index.column
	end
	return table.concat(columns, ",")
end

-- Rebuilds the `player_supplystash` table to ensure a composite primary key that includes `tier` and migrates existing rows, aggregating amounts by `(player_id, itemtype, tier)`.
-- @return `true` if the table was successfully rebuilt and migrated, `false` otherwise.
local function rebuildSupplyStashTable()
	db.query("DROP TABLE IF EXISTS `player_supplystash_tier_migration`")
	local droppedOriginalForeignKey = false
	if tableForeignKeyExists("player_supplystash", "player_supplystash_player_fk") then
		if not runSchemaQuery("ALTER TABLE `player_supplystash` DROP FOREIGN KEY `player_supplystash_player_fk`", "Could not drop old supply stash foreign key for migration.") then
			return false
		end
		droppedOriginalForeignKey = true
	end

	local function restoreOriginalForeignKey()
		if droppedOriginalForeignKey and not tableForeignKeyExists("player_supplystash", "player_supplystash_player_fk") then
			addSupplyStashForeignKey("player_supplystash", "Could not restore old supply stash foreign key after failed migration.")
		end
	end

	if not runSchemaQuery([[
		CREATE TABLE `player_supplystash_tier_migration` (
			`player_id` INT NOT NULL,
			`itemtype` SMALLINT UNSIGNED NOT NULL,
			`tier` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`amount` INT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `itemtype`, `tier`),
			CONSTRAINT `player_supplystash_player_fk`
				FOREIGN KEY (`player_id`) REFERENCES `players` (`id`)
				ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;
	]], "Could not create tier migration table.") then
		restoreOriginalForeignKey()
		db.query("DROP TABLE IF EXISTS `player_supplystash_tier_migration`")
		return false
	end

	if not runSchemaQuery([[
		INSERT INTO `player_supplystash_tier_migration` (`player_id`, `itemtype`, `tier`, `amount`)
		SELECT ps.`player_id`, ps.`itemtype`, COALESCE(ps.`tier`, 0), SUM(ps.`amount`)
		FROM `player_supplystash` ps
		INNER JOIN `players` p ON p.`id` = ps.`player_id`
		WHERE ps.`amount` > 0
		GROUP BY ps.`player_id`, ps.`itemtype`, COALESCE(ps.`tier`, 0)
	]], "Could not copy supply stash rows into tier migration table.") then
		restoreOriginalForeignKey()
		db.query("DROP TABLE IF EXISTS `player_supplystash_tier_migration`")
		return false
	end

	if not runSchemaQuery("DROP TABLE `player_supplystash`", "Could not drop old supply stash table.") then
		restoreOriginalForeignKey()
		db.query("DROP TABLE IF EXISTS `player_supplystash_tier_migration`")
		return false
	end
	return runSchemaQuery("RENAME TABLE `player_supplystash_tier_migration` TO `player_supplystash`", "Could not rename tier migration table.")
end

-- Ensures the `player_supplystash` table has a composite primary key on `player_id,itemtype,tier`, altering or rebuilding the table if necessary.
-- This may modify the database schema to enforce the correct primary key.
-- @return `true` if the primary key is configured as `player_id,itemtype,tier`, `false` otherwise.
local function ensureSupplyStashPrimaryKey()
	if getSupplyStashPrimaryKeySignature() == "player_id,itemtype,tier" then
		return true
	end

	if runSchemaQuery("ALTER TABLE `player_supplystash` DROP PRIMARY KEY, ADD PRIMARY KEY (`player_id`, `itemtype`, `tier`)", "Could not alter supply stash primary key; trying table rebuild.") and
			getSupplyStashPrimaryKeySignature() == "player_id,itemtype,tier" then
		return true
	end

	return rebuildSupplyStashTable()
end

-- Ensures the database schema for the supply stash exists and is up-to-date.
-- Creates the `player_supplystash` table if missing (columns: `player_id`, `itemtype`, `tier`, `amount`),
-- guarantees the `tier` column exists, and enforces the composite primary key (`player_id`, `itemtype`, `tier`)
-- along with the foreign key constraint to `players(id)`.
local function ensureTables()
	if not runSchemaQuery([[
		CREATE TABLE IF NOT EXISTS `player_supplystash` (
			`player_id` INT NOT NULL,
			`itemtype` SMALLINT UNSIGNED NOT NULL,
			`tier` TINYINT UNSIGNED NOT NULL DEFAULT 0,
			`amount` INT UNSIGNED NOT NULL DEFAULT 0,
			PRIMARY KEY (`player_id`, `itemtype`, `tier`),
			CONSTRAINT `player_supplystash_player_fk`
				FOREIGN KEY (`player_id`) REFERENCES `players` (`id`)
				ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;
	]], "Could not create supply stash table.") then
		return false
	end

	if not tableColumnExists("player_supplystash", "tier") then
		if not runSchemaQuery("ALTER TABLE `player_supplystash` ADD COLUMN `tier` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `itemtype`", "Could not add tier column to supply stash table.") then
			return false
		end
	end
	if not ensureSupplyStashPrimaryKey() then
		logError("[SupplyStash] Could not ensure supply stash primary key.")
		return false
	end
	return true
end

-- Get the ItemType corresponding to the provided item id or nil when the id is invalid or the item type does not exist.
-- @param itemId Number or string convertible to a number representing the item id.
-- @return The `ItemType` instance for the id, or `nil` if `itemId` is not a positive number or no item type exists for that id.
local function getItemType(itemId)
	itemId = tonumber(itemId) or 0
	if itemId <= 0 then
		return nil
	end

	local itemType = ItemType(itemId)
	if not itemType or itemType:getId() == 0 then
		return nil
	end
	return itemType
end

local function getCategoryFromName(name)
	name = (name or ""):lower()
	if name == "" then
		return CATEGORY_OTHERS
	end

	if name:find("sword", 1, true) or name:find("blade", 1, true) or name:find("sabre", 1, true) or name:find("katana", 1, true) then
		return CATEGORY_SWORDS
	elseif name:find("fist", 1, true) or name:find("claw", 1, true) or name:find("knuckle", 1, true) then
		return CATEGORY_FISTS
	elseif name:find("axe", 1, true) or name:find("hatchet", 1, true) then
		return CATEGORY_AXES
	elseif name:find("club", 1, true) or name:find("mace", 1, true) or name:find("hammer", 1, true) then
		return CATEGORY_CLUBS
	elseif name:find("bow", 1, true) or name:find("crossbow", 1, true) or name:find("spear", 1, true) then
		return CATEGORY_DISTANCE
	elseif name:find("wand", 1, true) or name:find("rod", 1, true) then
		return CATEGORY_WANDS
	elseif name:find("arrow", 1, true) or name:find("bolt", 1, true) then
		return CATEGORY_AMMUNITION
	elseif name:find("helmet", 1, true) or name:find("hat", 1, true) then
		return CATEGORY_HELMETS
	elseif name:find("armor", 1, true) or name:find("mail", 1, true) or name:find("plate", 1, true) then
		return CATEGORY_ARMORS
	elseif name:find("legs", 1, true) then
		return CATEGORY_LEGS
	elseif name:find("boots", 1, true) then
		return CATEGORY_BOOTS
	elseif name:find("shield", 1, true) then
		return CATEGORY_SHIELDS
	elseif name:find("amulet", 1, true) or name:find("necklace", 1, true) then
		return CATEGORY_AMULETS
	elseif name:find("potion", 1, true) or name:find("fluid", 1, true) then
		return CATEGORY_POTIONS
	elseif name:find("rune", 1, true) then
		return CATEGORY_RUNES
	elseif name:find("ring", 1, true) then
		return CATEGORY_RINGS
	elseif name:find("food", 1, true) or name:find("ham", 1, true) or name:find("meat", 1, true) or name:find("fish", 1, true) or name:find("bread", 1, true) then
		return CATEGORY_FOOD
	elseif name:find("rope", 1, true) or name:find("shovel", 1, true) or name:find("pick", 1, true) or name:find("machete", 1, true) then
		return CATEGORY_TOOLS
	elseif name:find("gem", 1, true) or name:find("crystal", 1, true) or name:find("pearl", 1, true) then
		return CATEGORY_VALUABLES
	end
	return CATEGORY_OTHERS
end

local function getSupplyItemCategory(itemType)
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
	elseif weaponType == WEAPON_AMMO then
		return CATEGORY_AMMUNITION
	elseif weaponType == WEAPON_FIST then
		return CATEGORY_FISTS
	elseif weaponType == WEAPON_SHIELD then
		return CATEGORY_SHIELDS
	end

	if itemType:isRune() then
		return CATEGORY_RUNES
	elseif itemType:isHelmet() then
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
	elseif (itemType.getDefaultPrice and itemType:getDefaultPrice() > 0) or itemType:getWorth() > 0 then
		return CATEGORY_VALUABLES
	end

	return getCategoryFromName(itemType:getName())
end

local function isSupplyItem(itemId)
	itemId = tonumber(itemId) or 0
	if itemId <= 0 or itemId > 0xFFFF or blockedItems[itemId] then
		return false
	end

	local itemType = getItemType(itemId)
	if not itemType then
		return false
	end

	local name = itemType:getName()
	if not name or name == "" then
		return false
	end

	if itemType:isCorpse() or itemType:isDoor() or itemType:isContainer() or itemType:isFluidContainer()
		or itemType:isMagicField() or itemType:isGroundTile() then
		return false
	end

	if CustomMarket and CustomMarket.isMarketCatalogItem then
		return CustomMarket.isMarketCatalogItem(itemId)
	end

	return itemType:isMovable() and itemType:isPickupable()
end

local function hasAnyAttribute(item, attributes)
	for _, attribute in ipairs(attributes) do
		if attribute and item:hasAttribute(attribute) then
			return true
		end
	end
	return false
end

local restrictedInstanceAttributes = {
	ITEM_ATTRIBUTE_ACTIONID,
	ITEM_ATTRIBUTE_UNIQUEID,
	ITEM_ATTRIBUTE_DESCRIPTION,
	ITEM_ATTRIBUTE_TEXT,
	ITEM_ATTRIBUTE_DATE,
	ITEM_ATTRIBUTE_WRITER,
	ITEM_ATTRIBUTE_NAME,
	ITEM_ATTRIBUTE_ARTICLE,
	ITEM_ATTRIBUTE_PLURALNAME,
	ITEM_ATTRIBUTE_WEIGHT,
	ITEM_ATTRIBUTE_ATTACK,
	ITEM_ATTRIBUTE_DEFENSE,
	ITEM_ATTRIBUTE_EXTRADEFENSE,
	ITEM_ATTRIBUTE_ARMOR,
	ITEM_ATTRIBUTE_HITCHANCE,
	ITEM_ATTRIBUTE_SHOOTRANGE,
	ITEM_ATTRIBUTE_OWNER,
	ITEM_ATTRIBUTE_CORPSEOWNER,
	ITEM_ATTRIBUTE_FLUIDTYPE,
	ITEM_ATTRIBUTE_DOORID,
	ITEM_ATTRIBUTE_WRAPID,
	ITEM_ATTRIBUTE_STOREITEM,
	ITEM_ATTRIBUTE_ATTACK_SPEED,
	ITEM_ATTRIBUTE_REWARDID
}

-- Returns the item's tier clamped to the range 0 through 10.
-- If `item` is nil or does not expose `getTier`, returns 0.
-- @param item The item instance (may be nil) which may implement `getTier`.
-- @return number The tier as an integer between 0 and 10 inclusive.
local function getItemTier(item)
	if item and item.getTier then
		return math.max(0, math.min(10, tonumber(item:getTier()) or 0))
	end
	return 0
end

-- Determines whether an item instance is eligible to be stored in the supply stash.
-- The check excludes containers, blocked or non-supply items, store/imbuement items, instances with restricted attributes,
-- items with decay/timestamp attributes, and items with duration or charges that are present but not at their item-type maximums.
-- @param item The item instance to evaluate.
-- @return `true` if the item meets all eligibility requirements for stowing in the supply stash, `false` otherwise.
local function isPristineSupplyItem(item)
	if not item or item:isContainer() then
		return false
	end

	local itemId = item:getId()
	if blockedItems[itemId] then
		return false
	end

	local itemType = getItemType(itemId)
	if not itemType or not isSupplyItem(itemId) then
		return false
	end

	if item.isStoreItem and item:isStoreItem() then
		return false
	end

	if item.hasImbuements and item:hasImbuements() then
		return false
	end

	if hasAnyAttribute(item, restrictedInstanceAttributes) then
		return false
	end

	if item:hasAttribute(ITEM_ATTRIBUTE_DURATION_TIMESTAMP) or item:hasAttribute(ITEM_ATTRIBUTE_DECAYSTATE) then
		return false
	end

	if item:hasAttribute(ITEM_ATTRIBUTE_DURATION) then
		local duration = tonumber(item:getAttribute(ITEM_ATTRIBUTE_DURATION)) or 0
		local maxDuration = math.max(tonumber(itemType:getDurationMin()) or 0, tonumber(itemType:getDurationMax()) or 0)
		if maxDuration <= 0 or duration < maxDuration then
			return false
		end
	end

	if item:hasAttribute(ITEM_ATTRIBUTE_CHARGES) then
		local charges = tonumber(item:getCharges()) or 0
		local maxCharges = tonumber(itemType:getCharges()) or 0
		if maxCharges <= 0 or charges < maxCharges then
			return false
		end
	end

	return true
end

local function getSupplyItemAmount(item)
	local itemType = getItemType(item:getId())
	if itemType and itemType:isStackable() then
		return math.max(1, tonumber(item:getCount()) or 0)
	end
	return 1
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

-- Obtains the player's last used depot ID and normalizes it to the range 0..0xFFFF.
-- @param player The player object to query.
-- @return The normalized depot ID (number between 0 and 65535). Returns 0 if the value is unavailable or an error occurs.
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

-- Set the active depot ID used for the player's supply stash session.
-- @param player The player whose supply stash session will be updated.
-- @param depotId The depot identifier to associate with the player; it will be normalized to the valid depot range.
local function setSupplyStashDepotId(player, depotId)
	supplyStashDepotSessions[player:getId()] = normalizeDepotId(depotId)
end

-- Get the active supply stash depot id for a player.
-- Returns the session depot id set for the player if present; otherwise returns the player's last depot id.
-- @param player The player object.
-- @return number The depot id (normalized to 0..65535).
local function getSupplyStashDepotId(player)
	local depotId = supplyStashDepotSessions[player:getId()]
	if depotId ~= nil then
		return depotId
	end
	return getPlayerLastDepotId(player)
end

-- Collects the player's depot box instances for the currently selected depot session.
-- @param player The player whose depot boxes will be retrieved.
-- @return An array of depot box objects for indices 1..15 that exist for the player's selected depot (may be empty).
local function getDepotBoxes(player)
	local boxes = {}
	local depotId = getSupplyStashDepotId(player)
	for boxIndex = SUPPLY_STASH_DEPOT_BOX_FIRST, SUPPLY_STASH_DEPOT_BOX_LAST do
		local box = player:getDepotBox(depotId, boxIndex)
		if box then
			boxes[#boxes + 1] = box
		end
	end
	return boxes
end

-- Retrieves stored supply stash rows for the given player, excluding entries with non-positive amounts and items that are not valid supply items.
-- @param player The player whose stash rows to fetch.
-- @return An array of tables { itemId = <number>, tier = <number>, amount = <number> } for each stored entry; `amount` is clamped to at most 0xFFFFFFFF.
local function getRows(player)
	local rows = {}
	local resultId = db.storeQuery(
		"SELECT `itemtype`, `tier`, `amount` FROM `player_supplystash` WHERE `player_id` = " ..
		player:getGuid() .. " AND `amount` > 0 ORDER BY `itemtype` ASC, `tier` ASC"
	)

	if resultId then
		repeat
			local itemId = result.getDataInt(resultId, "itemtype")
			local tier = result.getDataInt(resultId, "tier")
			local amount = result.getDataLong and result.getDataLong(resultId, "amount") or result.getDataInt(resultId, "amount")
			if isSupplyItem(itemId) and amount > 0 then
				rows[#rows + 1] = {itemId = itemId, tier = tier, amount = math.min(amount, 0xFFFFFFFF)}
			end
		until not result.next(resultId)
		result.free(resultId)
	end

	return rows
end

-- Serialize and send the player's supply stash (including per-item tier and metadata) to the client.
-- If the player does not support the custom network protocol, no message is sent.
-- @param player The player to receive the stash payload.
-- @return `true` if the stash message was sent to the player, `false` otherwise.
local function sendStash(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local rows = getRows(player)
	local freeSlots = math.max(0, SUPPLY_STASH_MAX_UNIQUE_ITEMS - #rows)

	local msg = NetworkMessage(player)
	msg:addByte(OPCODE_SUPPLY_STASH_SEND)
	msg:addU16(math.min(#rows, 0xFFFF))
	local rowCount = math.min(#rows, 0xFFFF)
	for i = 1, rowCount do
		msg:addU16(rows[i].itemId)
		msg:addU32(rows[i].amount)
		msg:addByte(rows[i].tier or 0)
	end
	msg:addU16(math.min(freeSlots, 0xFFFF))
	msg:addU16(SUPPLY_STASH_DETAILS_MARKER)
	msg:addU16(rowCount)
	for i = 1, rowCount do
		local itemType = getItemType(rows[i].itemId)
		msg:addU16(rows[i].itemId)
		msg:addString(itemType and itemType:getName() or "")
		msg:addU16(itemType and getSupplyItemCategory(itemType) or CATEGORY_OTHERS)
		msg:addByte(itemType and itemType:isStackable() and 1 or 0)
		msg:addU32(itemType and itemType.getDefaultPrice and itemType:getDefaultPrice() or 0)
	end
	return msg:sendToPlayer(player)
end

-- Retrieves the stored amount for a specific item and tier in the player's supply stash.
-- @param player The player whose stash is queried.
-- @param itemId The item type id to query.
-- @param tier Tier of the item; values are clamped to the range 0..10.
-- @return The stored amount for the (itemId, tier) entry, or 0 if none exists.
local function getStoredAmount(player, itemId, tier)
	local amount = 0
	tier = math.max(0, math.min(10, tonumber(tier) or 0))
	local resultId = db.storeQuery(
		"SELECT `amount` FROM `player_supplystash` WHERE `player_id` = " ..
		player:getGuid() .. " AND `itemtype` = " .. itemId .. " AND `tier` = " .. tier .. " LIMIT 1"
	)
	if resultId then
		amount = result.getDataLong and result.getDataLong(resultId, "amount") or result.getDataInt(resultId, "amount")
		result.free(resultId)
	end
	return amount
end

-- Adds `amount` to the stored quantity for the given player's item and tier, creating a row if none exists.
-- @param player The player whose stash will be modified.
-- @param itemId The item type id to increment.
-- @param amount The amount to add to the stored quantity.
-- @param tier The item tier; values are clamped to the range 0..10.
-- @return The database query result on success, `false` on failure.
local function addStoredAmount(player, itemId, amount, tier)
	tier = math.max(0, math.min(10, tonumber(tier) or 0))
	return db.query(string.format(
		"INSERT INTO `player_supplystash` (`player_id`, `itemtype`, `tier`, `amount`) VALUES (%d, %d, %d, %d) " ..
		"ON DUPLICATE KEY UPDATE `amount` = `amount` + VALUES(`amount`)",
		player:getGuid(), itemId, tier, amount
	))
end

-- Decrease the stored quantity for a specific item and tier in a player's supply stash.
-- @param player The player whose stash is modified.
-- @param itemId The item type id to decrement.
-- @param amount The quantity to subtract.
-- @param tier The item tier; coerced to an integer in the range 0..10.
-- @return The result of the database update query.
local function removeStoredAmount(player, itemId, amount, tier)
	tier = math.max(0, math.min(10, tonumber(tier) or 0))
	return db.query(string.format(
		"UPDATE `player_supplystash` SET `amount` = `amount` - %d WHERE `player_id` = %d AND `itemtype` = %d AND `tier` = %d AND `amount` >= %d",
		amount, player:getGuid(), itemId, tier, amount
	))
end

local function cleanupEmptyRows(player)
	db.query("DELETE FROM `player_supplystash` WHERE `player_id` = " .. player:getGuid() .. " AND `amount` = 0")
end

local function collectSupplyItems(container, list)
	for _, item in ipairs(container:getItems(false)) do
		if item:isContainer() then
			collectSupplyItems(item, list)
		elseif isPristineSupplyItem(item) then
			list[#list + 1] = item
		end
	end
end

local function collectPlayerInventory(player, list)
	for slot = CONST_SLOT_HEAD, CONST_SLOT_AMMO do
		local item = player:getSlotItem(slot)
		if item then
			if item:isContainer() then
				collectSupplyItems(item, list)
			elseif isPristineSupplyItem(item) then
				local itemType = getItemType(item:getId())
				if itemType and itemType:isStackable() then
					list[#list + 1] = item
				end
			end
		end
	end
end

local function collectDepotItems(player, list)
	for _, box in ipairs(getDepotBoxes(player)) do
		collectSupplyItems(box, list)
	end
end

-- Determines whether the stash can accommodate the given additional unique item types without exceeding the maximum unique-item limit.
-- @param amounts Table whose keys are strings in the form "itemId:tier" representing types to add; values are ignored.
-- @return `true` if adding the unique keys in `amounts` to the player's current stored types stays within SUPPLY_STASH_MAX_UNIQUE_ITEMS, `false` otherwise.
local function canAddUniqueTypes(player, amounts)
	local rows = getRows(player)
	local storedTypes = {}
	for _, row in ipairs(rows) do
		storedTypes[row.itemId .. ":" .. (row.tier or 0)] = true
	end

	local newTypes = 0
	for key in pairs(amounts) do
		if not storedTypes[key] then
			newTypes = newTypes + 1
		end
	end
	return #rows + newTypes <= SUPPLY_STASH_MAX_UNIQUE_ITEMS
end

-- Stores all eligible supply items found in the player's depot boxes (1–15) and worn/backpack inventory into the player's supply stash.
-- Aggregates amounts by item ID and tier, validates capacity, persists amounts, removes the source items, and rolls back on any failure.
-- @param player The player whose items will be collected and stowed.
-- @return `true` on completion (operation succeeds or is cancelled and the stash UI is refreshed).
local function stowAll(player)
	local items = {}
	collectDepotItems(player, items)
	collectPlayerInventory(player, items)

	if #items == 0 then
		player:sendCancelMessage("Put stashable items in your backpack or Depot Locker boxes 1 to 15.")
		sendStash(player)
		return true
	end

	local amounts = {}
	for _, item in ipairs(items) do
		local itemId = item:getId()
		local tier = getItemTier(item)
		local key = itemId .. ":" .. tier
		local entry = amounts[key]
		if not entry then
			entry = { itemId = itemId, tier = tier, amount = 0 }
			amounts[key] = entry
		end
		entry.amount = entry.amount + getSupplyItemAmount(item)
	end

	if not canAddUniqueTypes(player, amounts) then
		player:sendCancelMessage("Your supply stash does not have enough free slots.")
		sendStash(player)
		return true
	end

	local addedAmounts = {}
	for key, entry in pairs(amounts) do
		if not addStoredAmount(player, entry.itemId, entry.amount, entry.tier) then
			for _, addedEntry in pairs(addedAmounts) do
				removeStoredAmount(player, addedEntry.itemId, addedEntry.amount, addedEntry.tier)
			end
			cleanupEmptyRows(player)
			player:sendCancelMessage("Could not store these items in your supply stash.")
			sendStash(player)
			return true
		end
		addedAmounts[key] = entry
	end

	local remainingAmounts = {}
	for key, entry in pairs(amounts) do
		remainingAmounts[key] = { itemId = entry.itemId, tier = entry.tier, amount = entry.amount }
	end

	for _, item in ipairs(items) do
		local itemId = item:getId()
		local tier = getItemTier(item)
		local key = itemId .. ":" .. tier
		local amount = getSupplyItemAmount(item)
		if not item:remove() then
			for _, remainingEntry in pairs(remainingAmounts) do
				if remainingEntry.amount > 0 then
					removeStoredAmount(player, remainingEntry.itemId, remainingEntry.amount, remainingEntry.tier)
				end
			end
			cleanupEmptyRows(player)
			player:sendCancelMessage("Could not remove one of the items from its source.")
			sendStash(player)
			return true
		end
		if remainingAmounts[key] then
			remainingAmounts[key].amount = (remainingAmounts[key].amount or 0) - amount
		end
	end

	player:sendTextMessage(MESSAGE_STATUS_SMALL, "Supplies stowed.")
	sendStash(player)
	return true
end

-- Checks whether the player has enough free capacity to carry the specified amount of an item type.
-- @param player The player whose free capacity is checked.
-- @param itemType The ItemType of the item being carried.
-- @param amount The quantity of the item to carry.
-- @return `true` if the player's free capacity is greater than or equal to the weight of the specified amount, `false` otherwise.
local function canCarry(player, itemType, amount)
	local weight = itemType:getWeight(amount)
	return player:getFreeCapacity() >= weight
end

-- Attempts to create and add the requested amount of the specified item (with an optional tier) to the player's inventory.
-- @param player The player who will receive the items.
-- @param itemId The numeric item type identifier to deliver.
-- @param amount The total quantity to deliver; stackable items may be split into multiple stacks.
-- @param tier The tier to apply to created items; values are clamped to the range 0..10.
-- @return `true` on success, `false` and an error message string on failure.
local function deliverToPlayer(player, itemId, amount, tier)
	local itemType = getItemType(itemId)
	if not itemType then
		return false, "This item does not exist."
	end
	if not canCarry(player, itemType, amount) then
		return false, "You do not have enough capacity."
	end

	local createdItems = {}
	local remaining = amount
	tier = math.max(0, math.min(10, tonumber(tier) or 0))
	local stackSize = itemType:isStackable() and math.max(1, itemType:getStackSize()) or 1
	while remaining > 0 do
		local count = itemType:isStackable() and math.min(remaining, stackSize, SUPPLY_STACK_SIZE) or 1
		local item = Game.createItem(itemId, count)
		if not item then
			for _, created in ipairs(createdItems) do
				created:remove()
			end
			return false, "Could not create item."
		end

		if tier > 0 and item.setTier then
			item:setTier(tier)
		end

		createdItems[#createdItems + 1] = item
		local ret = player:addItemEx(item, false)
		if ret ~= RETURNVALUE_NOERROR then
			for _, created in ipairs(createdItems) do
				created:remove()
			end
			return false, "You do not have enough room."
		end
		remaining = remaining - count
	end

	return true
end

-- Withdraws a specified quantity of an item (optionally a tier) from the player's supply stash and delivers it to the player.
-- @param player The player performing the withdrawal.
-- @param itemId The item type id to withdraw.
-- @param amount The number of items to withdraw (must be > 0 and ≤ SUPPLY_STASH_MAX_WITHDRAW).
-- @param tier The item tier to withdraw (integer clamped to 0..10).
-- @return `true` if the request was handled (success or user-facing failure), otherwise `false`.
local function withdraw(player, itemId, amount, tier)
	itemId = tonumber(itemId) or 0
	amount = math.floor(tonumber(amount) or 0)
	tier = math.max(0, math.min(10, tonumber(tier) or 0))

	if amount <= 0 or amount > SUPPLY_STASH_MAX_WITHDRAW then
		player:sendCancelMessage("Invalid amount.")
		sendStash(player)
		return true
	end
	if not isSupplyItem(itemId) then
		player:sendCancelMessage("This item cannot be withdrawn from the supply stash.")
		sendStash(player)
		return true
	end

	local itemType = getItemType(itemId)
	if itemType and not itemType:isStackable() and amount > SUPPLY_STASH_MAX_WITHDRAW_NON_STACKABLE then
		player:sendCancelMessage("You can withdraw at most 100 of this item at once.")
		sendStash(player)
		return true
	end

	if getStoredAmount(player, itemId, tier) < amount then
		player:sendCancelMessage("You do not have enough items in your supply stash.")
		sendStash(player)
		return true
	end

	if not removeStoredAmount(player, itemId, amount, tier) then
		player:sendCancelMessage("Could not remove the items from your supply stash.")
		sendStash(player)
		return true
	end

	local delivered, reason = deliverToPlayer(player, itemId, amount, tier)
	if not delivered then
		addStoredAmount(player, itemId, amount, tier)
		player:sendCancelMessage(reason)
		sendStash(player)
		return true
	end

	db.query("DELETE FROM `player_supplystash` WHERE `player_id` = " .. player:getGuid() .. " AND `amount` = 0")
	sendStash(player)
	return true
end

local handler = PacketHandler(OPCODE_SUPPLY_STASH_REQUEST)

-- Handles incoming supply stash request packets and dispatches open, stow-all, and withdraw actions.
-- Ensures database tables exist, verifies the player uses the custom network client, and sends appropriate responses or cancel messages.
-- @param player The player who sent the packet.
-- @param msg The incoming network message containing the action and any parameters.
-- @return `true` to indicate the packet was handled.
function handler.onReceive(player, msg)
	if not ensureTables() then
		player:sendCancelMessage("Supply stash is temporarily unavailable.")
		return true
	end

	local action = msg:getByte()
	if not supportsCustomNetwork(player) then
		player:sendCancelMessage("The supply stash is only available on OTClient.")
		return true
	end

	if action == ACTION_OPEN then
		setSupplyStashDepotId(player, getPlayerLastDepotId(player))
		sendStash(player)
	elseif action == ACTION_STOW_ALL then
		stowAll(player)
	elseif action == ACTION_WITHDRAW then
		local itemId = msg:getU16()
		local amount = msg:getU32()
		local tier = 0
		if msg:len() - msg:tell() >= 1 then
			tier = msg:getByte()
		end
		withdraw(player, itemId, amount, tier)
	else
		player:sendCancelMessage("Invalid supply stash action.")
	end
	return true
end

handler:register()

CustomSupplyStash = {
	open = function(player, depotId)
		if not supportsCustomNetwork(player) then
			return false
		end

		if not ensureTables() then
			player:sendCancelMessage("Supply stash is temporarily unavailable.")
			return false
		end
		setSupplyStashDepotId(player, depotId or getPlayerLastDepotId(player))
		return sendStash(player)
	end,
	stowAll = stowAll,
	withdraw = withdraw
}

ensureTables()
