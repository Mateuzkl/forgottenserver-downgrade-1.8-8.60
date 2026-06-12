-- GameStore PacketHandler
-- OTC sends 0xFB to open, 0xFC to buy, 0xFA for history and 0xF8 to transfer coins.
-- Server responds with 0xFD.

local OPCODE_STORE_TRANSFER = 0xF8
local OPCODE_STORE_HISTORY = 0xFA
local OPCODE_STORE_OPEN = 0xFB
local OPCODE_STORE_BUY = 0xFC
local OPCODE_STORE_SEND = 0xFD

-- Response sub-types (0xFD)
local RESP_ERROR = 0x00
local RESP_CATALOG = 0x01
local RESP_SUCCESS = 0x02
local RESP_HISTORY = 0x03

local STORE_ACTION_DELAY = 2
local MAX_TARGET_NAME_LENGTH = 50
local MAX_CHARACTER_NAME_LENGTH = 20
local MAX_HIRELING_NAME_LENGTH = 20
local MAX_CHARACTER_NAME_WORDS = 5
local CHANGE_NAME_KICK_DELAY = 3000
local CHANGE_NAME_SUCCESS_MESSAGE = "Your character name has been changed. You will be disconnected in 3 seconds. Please log in again to use your new name."
local STORE_HOME_BANNER_DELAY = 10
local STORE_HOME_BANNERS = {
	{image = "/images/store/home/banner_exercisedummies", action = 0, target = 0}
}

local storeCategories = {}
local storeItemsById = {}

local lastBuy = {}
local lastTransfer = {}

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function isHirelingOfferType(oftype)
	oftype = tostring(oftype or ""):lower()
	return oftype == "hireling" or oftype == "hireling_skill" or oftype == "hireling_outfit"
end

local function isHirelingCategory(category)
	local name = tostring(category and category.name or ""):lower()
	return name == "hirelings" or name == "hireling dresses"
end

local function supportsHirelingStore(player)
	return configManager.getBoolean(configKeys.HIRELING_SYSTEM_ENABLED) and
		configManager.getBoolean(configKeys.ASTRA_HIRELING_PROTOCOL_ENABLED) and
		player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function playerOwnsMount(player, mountId)
	if player.ownsMount then
		local ok, owned = pcall(function()
			return player:ownsMount(mountId)
		end)
		if ok then
			return owned == true
		end
	end

	if player.hasMount then
		local ok, owned = pcall(function()
			return player:hasMount(mountId)
		end)
		if ok then
			return owned == true
		end
	end

	return false
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

local deathSearchIndexes = {
	{tableName = "player_deaths", indexName = "idx_pd_killed_by", columnName = "killed_by"},
	{tableName = "player_deaths", indexName = "idx_pd_mostdamage_by", columnName = "mostdamage_by"},
	{tableName = "player_deaths_backup", indexName = "idx_pdb_killed_by", columnName = "killed_by"},
	{tableName = "player_deaths_backup", indexName = "idx_pdb_mostdamage_by", columnName = "mostdamage_by"}
}

local function deathColumnHasIndex(tableName, columnName)
	local resultId = db.storeQuery(
		"SELECT `INDEX_NAME` FROM `information_schema`.`STATISTICS` WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = " ..
			db.escapeString(tableName) ..
			" AND `COLUMN_NAME` = " ..
			db.escapeString(columnName) ..
			" AND `SEQ_IN_INDEX` = 1 LIMIT 1"
	)
	if resultId ~= false then
		result.free(resultId)
		return true
	end

	return false
end

local function ensureDeathSearchIndexes()
	for _, index in ipairs(deathSearchIndexes) do
		if (not db.tableExists or db.tableExists(index.tableName)) and not deathColumnHasIndex(index.tableName, index.columnName) then
			db.query(string.format("ALTER TABLE `%s` ADD INDEX `%s` (`%s`(64))", index.tableName, index.indexName, index.columnName))
		end
	end
end

local function scheduleChangeNameKick(player)
	local playerId = player:getId()
	player:sendTextMessage(MESSAGE_INFO_DESCR, CHANGE_NAME_SUCCESS_MESSAGE)

	addEvent(function(id)
		local onlinePlayer = Player(id)
		if onlinePlayer then
			onlinePlayer:remove()
		end
	end, CHANGE_NAME_KICK_DELAY, playerId)
end

local forbiddenNameWords = {
	gm = true,
	adm = true,
	tutor = true,
	god = true,
	cm = true,
	admin = true,
	owner = true,
	administrator = true,
	senior = true,
	xangel = true,
	["x-angel"] = true
}

local function trim(value)
	return ((value or ""):gsub("^%s*(.-)%s*$", "%1"))
end

local function formatCharacterName(name)
	name = trim(name:lower())
	return (name:gsub("(%a)([%w']*)", function(first, rest)
		return first:upper() .. rest:lower()
	end))
end

local function wordCount(value)
	local count = 0
	for _ in value:gmatch("%a+") do
		count = count + 1
	end
	return count
end

local function isValidCharacterName(name)
	if name == "" or #name < 2 or #name > MAX_CHARACTER_NAME_LENGTH then
		return false
	end

	if name:find("  ", 1, true) or wordCount(name) > MAX_CHARACTER_NAME_WORDS then
		return false
	end

	local lowered = name:lower()
	if lowered == "g m" or lowered == "g o d" or lowered == "a d m" or lowered == "c m" then
		return false
	end

	for i = 1, #name do
		local char = name:sub(i, i)
		if not char:match("[A-Za-z ]") then
			return false
		end
	end

	for word in name:gmatch("%a+") do
		if forbiddenNameWords[word:lower()] then
			return false
		end
	end

	return true
end

local function characterNameExists(name)
	local resultId = db.storeQuery("SELECT `id` FROM `players` WHERE LOWER(`name`) = LOWER(" .. db.escapeString(name) .. ") LIMIT 1")
	if resultId ~= false then
		result.free(resultId)
		return true
	end

	return false
end

local function isOnCooldown(cache, playerId)
	local now = os.time()
	if cache[playerId] and now - cache[playerId] < STORE_ACTION_DELAY then
		return true
	end

	cache[playerId] = now
	return false
end

local function sendStoreError(player, message)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_STORE_SEND)
	out:addByte(RESP_ERROR)
	out:addString(message)
	return out:sendToPlayer(player)
end

local function sendStoreSuccess(player, offerId, message)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_STORE_SEND)
	out:addByte(RESP_SUCCESS)
	out:addU32(offerId)
	out:addString(message)
	out:addU32(player:getTibiaCoins())
	return out:sendToPlayer(player)
end

local _shopHistoryCache = nil
local function shopHistoryExists()
	if _shopHistoryCache ~= nil then
		return _shopHistoryCache
	end

	if db.tableExists then
		_shopHistoryCache = db.tableExists("shop_history")
	else
		_shopHistoryCache = true
	end

	return _shopHistoryCache
end

local function addStoreHistory(accountId, playerGuid, title, price, count, target)
	if not shopHistoryExists() then
		return
	end

	title = tostring(title or "Store Purchase"):sub(1, 100)
	price = tonumber(price) or 0
	count = tonumber(count) or 0

	local targetSql = "NULL"
	if target and target ~= "" then
		targetSql = db.escapeString(target)
	end

	db.query("INSERT INTO `shop_history` (`account`, `player`, `date`, `title`, `price`, `costSecond`, `count`, `target`) VALUES (" ..
		accountId .. ", " ..
		playerGuid .. ", NOW(), " ..
		db.escapeString(title) .. ", " ..
		price .. ", 0, " ..
		count .. ", " ..
		targetSql .. ")")
end

local function sendStoreHistory(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_STORE_SEND)
	out:addByte(RESP_HISTORY)

	local history = {}
	if shopHistoryExists() then
		local resultId = db.storeQuery("SELECT `date`, `price`, `costSecond`, `title`, `count` FROM `shop_history` WHERE `account` = " .. player:getAccountId() .. " ORDER BY `id` DESC LIMIT 100")
		if resultId ~= false then
			repeat
				table.insert(history, {
					date = result.getDataString(resultId, "date"),
					price = result.getDataInt(resultId, "price"),
					costSecond = result.getDataInt(resultId, "costSecond"),
					title = result.getDataString(resultId, "title"),
					count = result.getDataInt(resultId, "count"),
				})
			until not result.next(resultId)
			result.free(resultId)
		end
	end

	out:addU16(#history)
	for _, entry in ipairs(history) do
		local price = entry.price or 0
		out:addString(entry.date or "")
		out:addU32(math.abs(price))
		out:addByte(price >= 0 and 1 or 0)
		out:addByte(entry.costSecond == 1 and 1 or 0)
		out:addString(entry.title or "")
		out:addU16(math.max(entry.count or 0, 0))
	end

	return out:sendToPlayer(player)
end

local function loadStoreXML()
	storeCategories = {}
	storeItemsById = {}

	local xmlDoc = XMLDocument("data/store/gamestore.xml")
	if not xmlDoc then
		logError("[GameStore] Error: could not find data/store/gamestore.xml")
		return
	end

	local root = xmlDoc:child("store")
	if not root then
		return
	end

	for category in root:children() do
		if category:name() == "category" then
			local catName = category:attribute("name") or "?"
			local catIcon = category:attribute("icon") or ""
			local catParent = category:attribute("parent") or ""
			local catDescription = category:attribute("description") or ""
			local offers = {}

			for offer in category:children() do
				if offer:name() == "offer" then
					local item = {
						id = tonumber(offer:attribute("id")) or 0,
						name = offer:attribute("name") or "Unknown",
						icon = offer:attribute("icon") or "",
						price = tonumber(offer:attribute("price")) or 0,
						eid = tonumber(offer:attribute("eid")) or 0,
						itemid = tonumber(offer:attribute("itemid")) or 0,
						count = tonumber(offer:attribute("count")) or 1,
						description = offer:attribute("description") or "",
						oftype = offer:attribute("type") or "item",
						value = tonumber(offer:attribute("value")) or 0,
						femalevalue = tonumber(offer:attribute("femalevalue")) or 0,
						addon = tonumber(offer:attribute("addon")) or 0,
					}
					table.insert(offers, item)
					storeItemsById[item.id] = item
				end
			end

			table.insert(storeCategories, {
				name = catName,
				icon = catIcon,
				parent = catParent,
				description = catDescription,
				offers = offers
			})
		end
	end

	logInfo(">> Loading Game Store System")
end

loadStoreXML()

ensureDeathSearchIndexes()

local function sendStoreCatalog(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local visibleCategories = {}
	for _, cat in ipairs(storeCategories) do
		local visibleOffers = {}
		for _, offer in ipairs(cat.offers) do
			if not isHirelingOfferType(offer.oftype) or supportsHirelingStore(player) then
				visibleOffers[#visibleOffers + 1] = offer
			end
		end

		if #visibleOffers > 0 or not isHirelingCategory(cat) then
			visibleCategories[#visibleCategories + 1] = {
				name = cat.name,
				icon = cat.icon,
				parent = cat.parent,
				description = cat.description,
				offers = visibleOffers
			}
		end
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_STORE_SEND)
	out:addByte(RESP_CATALOG)

	out:addU32(player:getTibiaCoins())
	out:addU16(#visibleCategories)

	for _, cat in ipairs(visibleCategories) do
		out:addString(cat.name)
		out:addString(cat.icon)
		out:addString(cat.parent)
		out:addString(cat.description)
		out:addU16(#cat.offers)

		for _, offer in ipairs(cat.offers) do
			out:addU32(offer.id)
			out:addString(offer.name)
			out:addString(offer.icon)
			out:addU32(offer.price)
			out:addU16(offer.eid)
			out:addU16(offer.count)
			out:addString(offer.description)
			out:addString(offer.oftype)
		end
	end

	out:addByte(#STORE_HOME_BANNERS)
	for _, banner in ipairs(STORE_HOME_BANNERS) do
		out:addString(banner.image)
		out:addByte(banner.action)
		out:addU32(banner.target)
	end
	out:addByte(STORE_HOME_BANNER_DELAY)

	return out:sendToPlayer(player)
end

local function deliverOffer(player, offer, extra)
	if offer.oftype == "premium" then
		if offer.value <= 0 then
			return "Invalid premium amount."
		end
		player:addPremiumDays(offer.value)
		return nil
	end

	if offer.oftype == "battlepass" then
		if not BattlePassSystem or not BattlePassSystem.purchasePremium then
			return "Battle Pass system is not available."
		end

		return BattlePassSystem.purchasePremium(player, true)
	end

	if offer.oftype == "blessing" or offer.oftype == "bless" then
		if offer.value == -1 then
			local added = false
			for blessing = 1, 5 do
				if not player:hasBlessing(blessing) then
					player:addBlessing(blessing)
					added = true
				end
			end

			if not added then
				return "You already have all regular blessings."
			end
			return nil
		end

		if offer.value >= 1 and offer.value <= 5 then
			if player:hasBlessing(offer.value) then
				return "You already have this blessing."
			end

			player:addBlessing(offer.value)
			return nil
		end

		return "Invalid blessing."
	end

	if offer.oftype == "outfit" then
		local outfitValues = {offer.value}
		if offer.femalevalue > 0 and offer.femalevalue ~= offer.value then
			table.insert(outfitValues, offer.femalevalue)
		end

		local added = false
		for _, lookType in ipairs(outfitValues) do
			if lookType > 0 and not player:hasOutfit(lookType, offer.addon) then
				player:addOutfit(lookType)
				if offer.addon > 0 then
					player:addOutfitAddon(lookType, offer.addon)
				end
				added = true
			end
		end

		if not added then
			return "You already have this outfit."
		end
		return nil
	end

	if offer.oftype == "mount" then
		if playerOwnsMount(player, offer.value) then
			return "You already have this mount."
		end

		if not player:addMount(offer.value) then
			return "Failed to deliver mount."
		end
		return nil
	end

	if offer.oftype == "prey_wildcard" then
		if not PreySystem or not PreySystem.addWildcards then
			return "Prey System is not available."
		end

		if offer.value <= 0 then
			return "Invalid Prey Wildcard amount."
		end

		PreySystem.addWildcards(player, offer.value)
		return nil
	end

	if offer.oftype == "item" and offer.itemid > 0 then
		local inbox = player:getStoreInbox()
		if not inbox then
			return "Your store inbox is not available."
		end

		local item = Game.createItem(offer.itemid, offer.count)
		if not item then
			return "Failed to create item."
		end

		if inbox:addItemEx(item) ~= RETURNVALUE_NOERROR then
			item:remove()
			return "Your store inbox is full."
		end
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "Your item was sent to your store inbox.")
		return nil
	end

	if offer.oftype == "changename" then
		local newName = formatCharacterName(extra and extra.name or "")
		if not isValidCharacterName(newName) then
			return "You cannot use this character name."
		end

		if characterNameExists(newName) then
			return "Character name already taken."
		end

		if player:getCondition(CONDITION_INFIGHT, CONDITIONID_DEFAULT) or player:getCondition(CONDITION_INFIGHT, CONDITIONID_COMBAT) then
			return "You cannot do this during a fight."
		end

		local tile = player:getTile()
		if not tile or not tile:hasFlag(TILESTATE_PROTECTIONZONE) then
			return "You need to be in a protection zone."
		end

		local oldName = player:getName()
		db.query("UPDATE `players` SET `name` = " .. db.escapeString(newName) .. " WHERE `id` = " .. player:getGuid())
		db.query("UPDATE `player_deaths` SET `killed_by` = " .. db.escapeString(newName) .. ", `mostdamage_by` = " .. db.escapeString(newName) .. " WHERE `killed_by` = " .. db.escapeString(oldName) .. " OR `mostdamage_by` = " .. db.escapeString(oldName))
		db.query("UPDATE `player_deaths_backup` SET `killed_by` = " .. db.escapeString(newName) .. ", `mostdamage_by` = " .. db.escapeString(newName) .. " WHERE `killed_by` = " .. db.escapeString(oldName) .. " OR `mostdamage_by` = " .. db.escapeString(oldName))

		if db.tableExists and db.tableExists("change_name_history") then
			db.query("INSERT INTO `change_name_history` (`player_id`, `last_name`, `current_name`, `changed_name_in`) VALUES (" .. player:getGuid() .. ", " .. db.escapeString(oldName) .. ", " .. db.escapeString(newName) .. ", " .. os.time() .. ")")
		end

		return nil
	end

	if offer.oftype == "sexchange" then
		if player:getCondition(CONDITION_INFIGHT, CONDITIONID_DEFAULT) or player:getCondition(CONDITION_INFIGHT, CONDITIONID_COMBAT) then
			return "You cannot do this during a fight."
		end

		local tile = player:getTile()
		if not tile or not tile:hasFlag(TILESTATE_PROTECTIONZONE) then
			return "You need to be in a protection zone."
		end

		player:setSex(player:getSex() == PLAYERSEX_FEMALE and PLAYERSEX_MALE or PLAYERSEX_FEMALE)

		local outfit = player:getOutfit()
		if player:getSex() == PLAYERSEX_MALE then
			outfit.lookType = 128
		else
			outfit.lookType = 136
		end
		player:setOutfit(outfit)
		return nil
	end

	if offer.oftype == "hireling" then
		if not supportsHirelingStore(player) then
			return "Hireling purchases are only available on AstraClient."
		end

		if not player.addNewHireling then
			return "Hireling system is not available."
		end

		local hireling, err = player:addNewHireling(extra and extra.name or "", extra and extra.sex or HIRELING_SEX.MALE)
		if not hireling then
			return err or "Failed to create hireling."
		end

		player:sendTextMessage(MESSAGE_STATUS_SMALL, "Your hireling lamp was sent to your Store Inbox.")
		return nil
	end

	if offer.oftype == "hireling_skill" then
		if not supportsHirelingStore(player) then
			return "Hireling purchases are only available on AstraClient."
		end

		local skillName = GetHirelingSkillNameById(offer.value > 0 and offer.value or offer.eid)
		if not skillName then
			return "Invalid hireling skill."
		end

		if player:hasHirelingSkill(skillName) then
			return "You already have this hireling skill."
		end

		player:enableHirelingSkill(skillName)
		return nil
	end

	if offer.oftype == "hireling_outfit" then
		if not supportsHirelingStore(player) then
			return "Hireling purchases are only available on AstraClient."
		end

		local outfitName = GetHirelingOutfitNameById(offer.value > 0 and offer.value or offer.eid)
		if not outfitName then
			return "Invalid hireling dress."
		end

		if player:hasHirelingOutfit(outfitName) then
			return "You already have this hireling dress."
		end

		player:enableHirelingOutfit(outfitName)
		return nil
	end

	return "Invalid offer type."
end

-- ============================================================
-- Handler: OTC asks to open the store (opcode 0xFB)
-- ============================================================
local openHandler = PacketHandler(OPCODE_STORE_OPEN)

function openHandler.onReceive(player, msg)
	sendStoreCatalog(player)
end

openHandler:register()

-- ============================================================
-- Handler: OTC asks for purchase history (opcode 0xFA)
-- ============================================================
local historyHandler = PacketHandler(OPCODE_STORE_HISTORY)

function historyHandler.onReceive(player, msg)
	sendStoreHistory(player)
end

historyHandler:register()

-- ============================================================
-- Handler: OTC buys an offer (opcode 0xFC)
-- ============================================================
local buyHandler = PacketHandler(OPCODE_STORE_BUY)

function buyHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 4 then
		return
	end

	local pid = player:getId()
	if isOnCooldown(lastBuy, pid) then
		sendStoreError(player, "Please wait before buying again.")
		return
	end

	local offerId = NetworkGuard.readU32(msg)
	if not offerId then
		return
	end

	local offer = storeItemsById[offerId]
	if not offer then
		sendStoreError(player, "Offer not found.")
		return
	end

	local extra = {}
	if offer.oftype == "changename" then
		extra.name = NetworkGuard.readString(msg, MAX_CHARACTER_NAME_LENGTH)
		if not extra.name then
			sendStoreError(player, "You need to choose a new character name.")
			return
		end
	elseif offer.oftype == "hireling" then
		extra.name = NetworkGuard.readString(msg, MAX_HIRELING_NAME_LENGTH)
		if not extra.name then
			sendStoreError(player, "You need to choose a hireling name.")
			return
		end

		extra.sex = NetworkGuard.readByte(msg)
		if extra.sex == nil then
			sendStoreError(player, "You need to choose a hireling sex.")
			return
		end
	end

	local coins = player:getTibiaCoins()
	if coins < offer.price then
		sendStoreError(player, "Not enough Tibia Coins.")
		return
	end

	local deliveryError = deliverOffer(player, offer, extra)
	if deliveryError then
		sendStoreError(player, deliveryError)
		return
	end

	player:setTibiaCoins(coins - offer.price)

	local historyCount = offer.oftype == "item" and offer.count or (offer.oftype == "prey_wildcard" and offer.value or 1)
	addStoreHistory(player:getAccountId(), player:getGuid(), offer.name, -offer.price, historyCount, nil)

	if offer.oftype == "changename" then
		sendStoreSuccess(player, offerId, CHANGE_NAME_SUCCESS_MESSAGE)
		scheduleChangeNameKick(player)
	else
		sendStoreSuccess(player, offerId, "Purchase complete: " .. offer.name)
	end
end

buyHandler:register()

-- ============================================================
-- Handler: OTC transfers Tibia Coins to another player (opcode 0xF8)
-- Payload: String(targetName) + U32(amount)
-- ============================================================
local transferHandler = PacketHandler(OPCODE_STORE_TRANSFER)

function transferHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 6 then
		return
	end

	local pid = player:getId()
	if isOnCooldown(lastTransfer, pid) then
		sendStoreError(player, "Please wait before transferring again.")
		return
	end

	local targetName = NetworkGuard.readString(msg, MAX_TARGET_NAME_LENGTH)
	if not targetName or not NetworkGuard.canRead(msg, 4) then
		sendStoreError(player, "Target player not found.")
		return
	end

	targetName = trim(targetName)
	local amount = NetworkGuard.readU32(msg)
	if not amount then
		sendStoreError(player, "Invalid amount.")
		return
	end

	if targetName == "" or #targetName > MAX_TARGET_NAME_LENGTH then
		sendStoreError(player, "Target player not found.")
		return
	end

	if amount <= 0 then
		sendStoreError(player, "Invalid amount.")
		return
	end

	if targetName:lower() == player:getName():lower() then
		sendStoreError(player, "You cannot transfer coins to yourself.")
		return
	end

	local targetGuid = 0
	local targetAccountId = 0
	local storedTargetName = ""
	local targetCoins = 0
	local resultId = db.storeQuery("SELECT p.`id`, p.`account_id`, p.`name`, a.`tibia_coins` FROM `players` p JOIN `accounts` a ON a.`id` = p.`account_id` WHERE p.`name` = " .. db.escapeString(targetName) .. " LIMIT 1")
	if resultId ~= false then
		targetGuid = result.getDataInt(resultId, "id")
		targetAccountId = result.getDataInt(resultId, "account_id")
		storedTargetName = result.getDataString(resultId, "name")
		targetCoins = result.getDataInt(resultId, "tibia_coins")
		result.free(resultId)
	end

	if targetAccountId == 0 then
		sendStoreError(player, "Target player not found.")
		return
	end

	if targetAccountId == player:getAccountId() then
		sendStoreError(player, "You cannot transfer coins to your own account.")
		return
	end

	local accountId = player:getAccountId()
	local coins = player:getTibiaCoins()
	if coins < amount then
		sendStoreError(player, "Not enough Tibia Coins.")
		return
	end

	local targetPlayer = Player(storedTargetName)
	player:setTibiaCoins(coins - amount)
	if targetPlayer then
		targetPlayer:setTibiaCoins(targetCoins + amount)
	else
		local creditOk = db.query("UPDATE `accounts` SET `tibia_coins` = " .. (targetCoins + amount) .. " WHERE `id` = " .. targetAccountId)
		if not creditOk then
			player:setTibiaCoins(coins)
			sendStoreError(player, "Transfer failed, please try again.")
			return
		end
	end

	addStoreHistory(accountId, player:getGuid(), "Coin Transfer to " .. storedTargetName, -amount, 1, storedTargetName)
	addStoreHistory(targetAccountId, targetGuid, "Coin Transfer from " .. player:getName(), amount, 1, player:getName())

	sendStoreSuccess(player, 0, "You sent " .. amount .. " Tibia Coins to " .. storedTargetName .. ".")
	sendStoreHistory(player)

	if targetPlayer and targetPlayer:isUsingOtcV8() then
		sendStoreCatalog(targetPlayer)
		sendStoreHistory(targetPlayer)
	end
end

transferHandler:register()

local storeSessionCleanup = CreatureEvent("StoreSessionCleanup")
function storeSessionCleanup.onLogout(player)
	local pid = player:getId()
	lastBuy[pid] = nil
	lastTransfer[pid] = nil
	return true
end

storeSessionCleanup:register()

local storeSessionInit = CreatureEvent("StoreSessionInit")
function storeSessionInit.onLogin(player)
	player:registerEvent("StoreSessionCleanup")
	return true
end

storeSessionInit:register()
