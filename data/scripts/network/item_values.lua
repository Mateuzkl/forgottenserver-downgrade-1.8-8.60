local OPCODE_ITEM_VALUES = 0xC6
local OPCODE_ITEM_DETAILS = 0xC7

local MARKET_DETAIL_NAMES = {
	[1] = "Armor",
	[2] = "Attack",
	[3] = "Container",
	[4] = "Defense",
	[5] = "Description",
	[6] = "Decay Time",
	[7] = "Element",
	[8] = "Required Level",
	[9] = "Required Magic Level",
	[10] = "Vocation",
	[11] = "Rune Spell",
	[12] = "Ability",
	[13] = "Charges",
	[14] = "Weapon Type",
	[15] = "Weight",
	[16] = "Imbuement Slots",
	[17] = "Classification",
	[18] = "Tier"
}

local function supportsCustomNetwork(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function colorizedLootEnabled()
	return configManager.getBoolean(configKeys.COLORIZED_LOOT_VALUE)
end

local function safeCall(object, method)
	if not object or type(object[method]) ~= "function" then
		return nil
	end

	local ok, value = pcall(object[method], object)
	if ok then
		return value
	end
	return nil
end

local function safeCall1(object, method, arg)
	if not object or type(object[method]) ~= "function" then
		return nil
	end

	local ok, value = pcall(object[method], object, arg)
	if ok then
		return value
	end
	return nil
end

local function addDetail(details, detail, description)
	if description == nil then
		return
	end

	detail = tostring(detail)
	description = tostring(description)
	if description == "" then
		return
	end

	details.seen = details.seen or {}
	local key = detail .. "\0" .. description
	if details.seen[key] then
		return
	end
	details.seen[key] = true

	details[#details + 1] = {
		detail = detail,
		description = description
	}
end

local function formatWeight(weight)
	weight = tonumber(weight) or 0
	if weight <= 0 then
		return nil
	end
	return string.format("%.2f oz", weight / 100)
end

local function getSlotDescription(slotPosition)
	slotPosition = tonumber(slotPosition) or 0
	if slotPosition == 0 then
		return nil
	end

	local names = {}
	local function addSlot(constName, name)
		local bit = _G[constName]
		if bit and (slotPosition & bit) ~= 0 then
			names[#names + 1] = name
		end
	end

	addSlot("SLOTP_HEAD", "Head")
	addSlot("SLOTP_NECKLACE", "Necklace")
	addSlot("SLOTP_BACKPACK", "Backpack")
	addSlot("SLOTP_ARMOR", "Armor")
	addSlot("SLOTP_RIGHT", "Right Hand")
	addSlot("SLOTP_LEFT", "Left Hand")
	addSlot("SLOTP_LEGS", "Legs")
	addSlot("SLOTP_FEET", "Feet")
	addSlot("SLOTP_RING", "Ring")
	addSlot("SLOTP_AMMO", "Ammo")

	if _G.SLOTP_TWO_HAND and (slotPosition & SLOTP_TWO_HAND) ~= 0 then
		names[#names + 1] = "Two-Handed"
	end

	if #names == 0 then
		return nil
	end
	return table.concat(names, ", ")
end

local function addMarketDescriptions(details, itemId)
	if not CustomMarket or not CustomMarket.getDescriptions then
		return
	end

	local marketDescriptions = CustomMarket.getDescriptions(itemId) or {}
	for _, description in ipairs(marketDescriptions) do
		addDetail(details, MARKET_DETAIL_NAMES[description.type] or "Detail", description.text)
	end
end

local function buildItemDescriptions(itemId)
	local itemType = ItemType(itemId)
	local details = {}
	if not itemType or itemType:getId() == 0 then
		return details
	end

	local description = safeCall(itemType, "getDescription") or ""
	addDetail(details, "Description", description ~= "" and description or safeCall(itemType, "getName"))

	addMarketDescriptions(details, itemId)

	local armor = tonumber(safeCall(itemType, "getArmor")) or 0
	if armor > 0 then
		addDetail(details, "Armor", armor)
	end

	local attack = tonumber(safeCall(itemType, "getAttack")) or 0
	if attack > 0 then
		addDetail(details, "Attack", attack)
	end

	local defense = tonumber(safeCall(itemType, "getDefense")) or 0
	local extraDefense = tonumber(safeCall(itemType, "getExtraDefense")) or 0
	if defense > 0 then
		addDetail(details, "Defense", extraDefense ~= 0 and string.format("%d %+d", defense, extraDefense) or defense)
	end

	local imbuementSlots = tonumber(safeCall(itemType, "getImbuementSlot")) or 0
	if imbuementSlots > 0 then
		addDetail(details, "Imbuement Slots", string.format("%d slot%s", imbuementSlots, imbuementSlots == 1 and "" or "s"))
	end

	local weight = tonumber(safeCall1(itemType, "getWeight", 1)) or tonumber(safeCall(itemType, "getWeight")) or 0
	addDetail(details, "Weight", formatWeight(weight))

	local slotDescription = getSlotDescription(safeCall(itemType, "getSlotPosition"))
	addDetail(details, "Body Position", slotDescription)

	local classification = tonumber(safeCall(itemType, "getClassification")) or 0
	if classification > 0 then
		addDetail(details, "Classification", classification)
	end

	local tier = tonumber(safeCall(itemType, "getTier")) or 0
	if tier > 0 then
		addDetail(details, "Tier", tier)
	end

	if CustomMarket and CustomMarket.isMarketCatalogItem then
		addDetail(details, "Tradeable in Market", CustomMarket.isMarketCatalogItem(itemId) and "yes" or "no")
	end

	local defaultValue = ItemPriceRegistry and ItemPriceRegistry.getDefaultValue and ItemPriceRegistry.getDefaultValue(itemId) or 0
	if defaultValue <= 0 and itemType.getDefaultValue then
		defaultValue = tonumber(itemType:getDefaultValue()) or 0
	end
	if defaultValue > 0 then
		addDetail(details, "Default Value", string.format("%d gp", defaultValue))
	end

	return details
end

local function getAverageMarketPrice(itemId)
	if CustomMarket and CustomMarket.getAveragePrice then
		return tonumber(CustomMarket.getAveragePrice(itemId)) or 0
	end
	return 0
end

local function getDefaultItemValue(itemId)
	local itemType = ItemType(itemId)
	local registryValue = ItemPriceRegistry and ItemPriceRegistry.getDefaultValue and ItemPriceRegistry.getDefaultValue(itemId) or 0
	if registryValue > 0 then
		return registryValue
	end
	if itemType and itemType:getId() ~= 0 and itemType.getDefaultValue then
		return tonumber(itemType:getDefaultValue()) or 0
	end
	return 0
end

local function getDefaultBuyPrice(itemId)
	local itemType = ItemType(itemId)
	local registryValue = ItemPriceRegistry and ItemPriceRegistry.getDefaultBuyPrice and ItemPriceRegistry.getDefaultBuyPrice(itemId) or 0
	if registryValue > 0 then
		return registryValue
	end
	if itemType and itemType:getId() ~= 0 and itemType.getBuyPrice then
		return tonumber(itemType:getBuyPrice()) or 0
	end
	return 0
end

local function sendItemValues(playerId, attempt)
	attempt = tonumber(attempt) or 1
	local player = Player(playerId)
	if not player or not Game.getItemPrices then
		return
	end
	if not supportsCustomNetwork(player) then
		if attempt < 6 then
			addEvent(sendItemValues, 1000, playerId, attempt + 1)
		end
		return
	end
	if not colorizedLootEnabled() then
		return
	end

	local entries = {}
	for itemId, value in pairs(Game.getItemPrices()) do
		value = tonumber(value) or 0
		if value > 0 then
			entries[#entries + 1] = { itemId = tonumber(itemId) or 0, value = value }
		end
	end

	if #entries == 0 then
		return
	end

	table.sort(entries, function(a, b) return a.itemId < b.itemId end)

	local msg = NetworkMessage(player)
	msg:addByte(OPCODE_ITEM_VALUES)
	msg:addU16(math.min(#entries, 0xFFFF))
	for i = 1, math.min(#entries, 0xFFFF) do
		msg:addU16(entries[i].itemId)
		msg:addU32(math.min(entries[i].value, 0xFFFFFFFF))
	end
	msg:sendToPlayer(player)
end

local function sendItemDetails(player, itemId)
	itemId = tonumber(itemId) or 0
	if itemId <= 0 or not supportsCustomNetwork(player) or not colorizedLootEnabled() then
		return false
	end

	local itemType = ItemType(itemId)
	if not itemType or itemType:getId() == 0 then
		return false
	end

	local details = buildItemDescriptions(itemId)
	local npcSaleData = ItemPriceRegistry and ItemPriceRegistry.getNpcSaleData and ItemPriceRegistry.getNpcSaleData(itemId) or {}
	local defaultValue = getDefaultItemValue(itemId)
	local defaultBuyPrice = getDefaultBuyPrice(itemId)
	local averageMarketPrice = getAverageMarketPrice(itemId)

	local msg = NetworkMessage(player)
	msg:addByte(OPCODE_ITEM_DETAILS)
	msg:addU16(itemId)
	msg:addU32(math.min(defaultValue, 0xFFFFFFFF))
	msg:addU32(math.min(defaultBuyPrice, 0xFFFFFFFF))
	msg:addU32(math.min(averageMarketPrice, 0xFFFFFFFF))
	msg:addByte(math.min(#details, 0xFF))
	for i = 1, math.min(#details, 0xFF) do
		msg:addString(details[i].detail)
		msg:addString(details[i].description)
	end
	msg:addU16(math.min(#npcSaleData, 0xFFFF))
	for i = 1, math.min(#npcSaleData, 0xFFFF) do
		local offer = npcSaleData[i]
		msg:addString(tostring(offer.name or "NPC"))
		msg:addString(tostring(offer.location or "Unknown Location"))
		msg:addU32(math.min(tonumber(offer.buyPrice) or 0, 0xFFFFFFFF))
		msg:addU32(math.min(tonumber(offer.salePrice) or 0, 0xFFFFFFFF))
		msg:addString(tostring(offer.currencyQuestFlagDisplayName or ""))
	end
	return msg:sendToPlayer(player)
end

local loginEvent = CreatureEvent("ItemValuesLogin")

function loginEvent.onLogin(player)
	addEvent(sendItemValues, 1000, player:getId(), 1)
	return true
end

loginEvent:register()

local reconnectEvent = CreatureEvent("ItemValuesReconnect")

function reconnectEvent.onReconnect(player)
	addEvent(sendItemValues, 250, player:getId(), 1)
	return true
end

reconnectEvent:register()

local itemDetailsHandler = PacketHandler(OPCODE_ITEM_DETAILS)

function itemDetailsHandler.onReceive(player, msg)
	if not NetworkGuard.cooldown(player, "item-details", 300) then
		return false
	end

	local itemId = NetworkGuard.readU16(msg)
	if not itemId then
		return false
	end

	return sendItemDetails(player, itemId)
end

itemDetailsHandler:register()
