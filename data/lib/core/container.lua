function Container:isContainer() return true end

function Container:createLootItem(lootItem)
	if self:getEmptySlots() == 0 then return true end

	local itemCount = 0
	local randvalue = getLootRandom()
	local itemType = ItemType(lootItem.itemId)
	local corpseInstanceId = self:getInstanceId()

	if randvalue < lootItem.chance then
		if itemType:isStackable() then
			local max = math.floor(randvalue % lootItem.maxCount) + 1
			local min = lootItem.minCount ~= 0 and math.floor(randvalue % lootItem.minCount) + 1 or max
			if min > max then min, max = max, min end
			itemCount = math.random(min, max)
		else
			itemCount = 1
		end
	end

	while itemCount > 0 do
		local count = math.min(itemType:getStackSize(), itemCount)

		local subType = count
		if itemType:isFluidContainer() then subType = math.max(0, lootItem.subType) end

		local tmpItem = Game.createItem(lootItem.itemId, subType)
		if not tmpItem then return false end
		
		if corpseInstanceId and corpseInstanceId ~= 0 then
			tmpItem:setInstanceId(corpseInstanceId)
		end

		local tmpContainer = tmpItem:getContainer()
		if tmpContainer then
			for i = 1, #lootItem.childLoot do
				if not tmpContainer:createLootItem(lootItem.childLoot[i]) then
					tmpContainer:remove()
					return false
				end
			end

			if #lootItem.childLoot > 0 and tmpContainer:getSize() == 0 then
				tmpItem:remove()
				return true
			end
		end

		if lootItem.subType ~= -1 then tmpItem:setAttribute(ITEM_ATTRIBUTE_CHARGES, lootItem.subType) end

		if lootItem.actionId ~= -1 then tmpItem:setActionId(lootItem.actionId) end

		if lootItem.text and lootItem.text ~= "" then tmpItem:setText(lootItem.text) end

		local ret = self:addItemEx(tmpItem)
		if ret ~= RETURNVALUE_NOERROR then tmpItem:remove() end

		itemCount = itemCount - count
	end
	return true
end

local function getLootItemValue(item)
	local itemType = item and ItemType(item:getId())
	if not itemType or itemType:getId() == 0 then
		return 0
	end

	local value = itemType.getDefaultPrice and itemType:getDefaultPrice() or itemType:getWorth()
	local count = itemType:isStackable() and math.max(1, item:getCount()) or 1
	return (tonumber(value) or 0) * count
end

function Container:getContentDescription(colorizedLootValue)
	local items = self:getItems()
	if items and #items > 0 then
		local loot = {}
		for _, lootItem in ipairs(items) do
			local description = lootItem:getNameDescription(lootItem:getSubType(), true)
			if colorizedLootValue then description = ("{%d:%d|%s}"):format(lootItem:getId(), getLootItemValue(lootItem), description) end
			loot[#loot + 1] = description
		end

		return table.concat(loot, ", ")
	end

	return "nothing"
end
