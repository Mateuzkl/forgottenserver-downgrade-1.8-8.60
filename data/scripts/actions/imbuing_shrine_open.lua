local function hasImbuementSlots(item)
	if not item then
		return false
	end

	local ok, slots = pcall(function()
		return item:getImbuementSlots()
	end)
	return ok and slots and slots > 0
end

local function isItem(thing)
	if not thing then
		return false
	end

	local ok, isItem = pcall(function()
		return thing:isItem()
	end)
	return ok and isItem
end

local function containerHasItem(container, target)
	for _, item in ipairs(container:getItems()) do
		if item == target then
			return true
		end

		local childContainer = item:getContainer()
		if childContainer and containerHasItem(childContainer, target) then
			return true
		end
	end
	return false
end

local function isPlayerBackpackItem(player, thing)
	if not isItem(thing) or thing:getTopParent() ~= player then
		return false
	end

	local backpack = player:getSlotItem(CONST_SLOT_BACKPACK)
	local container = backpack and backpack:getContainer()
	return container and containerHasItem(container, thing)
end

local function findImbuableItem(container)
	for _, item in ipairs(container:getItems()) do
		if hasImbuementSlots(item) then
			return item
		end

		local childContainer = item:getContainer()
		if childContainer then
			local found = findImbuableItem(childContainer)
			if found then
				return found
			end
		end
	end
	return nil
end

local function findPlayerBackpackItem(player)
	local backpack = player:getSlotItem(CONST_SLOT_BACKPACK)
	local container = backpack and backpack:getContainer()
	if container then
		return findImbuableItem(container)
	end
	return nil
end

local action = Action()

function action.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local selectedItem = nil

	if isItem(target) then
		if not isPlayerBackpackItem(player, target) then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "Use an item from your backpack on the imbuing shrine.")
			return true
		end

		if not hasImbuementSlots(target) then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "Use an item with imbuement slots from your backpack on the imbuing shrine.")
			return true
		end
		selectedItem = target
	end

	if not selectedItem then
		selectedItem = findPlayerBackpackItem(player)
	end

	if not selectedItem then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "Use an item with imbuement slots from your backpack on the imbuing shrine.")
		return true
	end

	ImbuingWindow.openItem(player, selectedItem, nil, item:getPosition(), item)
	return true
end

action:id(25060, 25061)
action:register()
