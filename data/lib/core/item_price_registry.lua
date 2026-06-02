ItemPriceRegistry = ItemPriceRegistry or {}
ItemPriceRegistry.items = ItemPriceRegistry.items or {}

local function normalizePrice(value)
	value = tonumber(value) or 0
	if value < 0 then
		return 0
	end
	return value
end

local function getItemEntry(itemId)
	itemId = tonumber(itemId) or 0
	if itemId <= 0 then
		return nil
	end

	local entry = ItemPriceRegistry.items[itemId]
	if not entry then
		entry = {
			buyPrice = 0,
			sellPrice = 0,
			npcSaleData = {},
			seen = {}
		}
		ItemPriceRegistry.items[itemId] = entry
	end
	return entry
end

function ItemPriceRegistry.register(itemId, buy, sell, npcName, location, currencyQuestFlagDisplayName)
	local entry = getItemEntry(itemId)
	if not entry then
		return
	end

	buy = normalizePrice(buy)
	sell = normalizePrice(sell)
	if buy == 0 and sell == 0 then
		return
	end

	itemId = tonumber(itemId) or 0
	local itemType = ItemType(itemId)
	if itemType and itemType:getId() ~= 0 then
		if buy > 0 and itemType.setBuyPrice then
			itemType:setBuyPrice(buy)
		end
		if sell > 0 and itemType.setSellPrice then
			itemType:setSellPrice(sell)
		end
	end

	entry.buyPrice = math.max(entry.buyPrice or 0, buy)
	entry.sellPrice = math.max(entry.sellPrice or 0, sell)

	npcName = tostring(npcName or "NPC")
	location = tostring(location or "Unknown Location")
	currencyQuestFlagDisplayName = tostring(currencyQuestFlagDisplayName or "")

	local key = table.concat({ npcName, location, sell, buy, currencyQuestFlagDisplayName }, "|")
	if entry.seen[key] then
		return
	end

	entry.seen[key] = true
	entry.npcSaleData[#entry.npcSaleData + 1] = {
		name = npcName,
		location = location,
		buyPrice = sell,
		salePrice = buy,
		currencyQuestFlagDisplayName = currencyQuestFlagDisplayName
	}
end

function ItemPriceRegistry.get(itemId)
	return ItemPriceRegistry.items[tonumber(itemId) or 0]
end

function ItemPriceRegistry.getNpcSaleData(itemId)
	local entry = ItemPriceRegistry.get(itemId)
	return entry and entry.npcSaleData or {}
end

function ItemPriceRegistry.getDefaultBuyPrice(itemId)
	local entry = ItemPriceRegistry.get(itemId)
	return entry and entry.buyPrice or 0
end

function ItemPriceRegistry.getDefaultSellPrice(itemId)
	local entry = ItemPriceRegistry.get(itemId)
	return entry and entry.sellPrice or 0
end

function ItemPriceRegistry.getDefaultValue(itemId)
	local entry = ItemPriceRegistry.get(itemId)
	if not entry then
		return 0
	end
	return (entry.sellPrice and entry.sellPrice > 0) and entry.sellPrice or (entry.buyPrice or 0)
end
