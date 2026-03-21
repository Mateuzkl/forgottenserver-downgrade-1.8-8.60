local WORKBENCH_ID = 25334
local WORKBENCH_POSITIONS = {
	Position(1123, 1208, 7),
	-- Position(0, 0, 0),
	-- Position(0, 0, 0),
	-- Position(0, 0, 0),
}
local ABANDON_TIME = 600000

local function findWorkbench()
	for _, pos in ipairs(WORKBENCH_POSITIONS) do
		local tile = Tile(pos)
		if tile then
			local wb = tile:getItemById(WORKBENCH_ID)
			if wb then return Container(wb.uid) end
		end
	end
	return nil
end

local exerciseItems = {
	[31208] = true, [31209] = true, [31210] = true,
	[37941] = true, [37942] = true, [37943] = true,
	[31211] = true, [37944] = true,
	[31212] = true, [37945] = true,
	[31213] = true, [37946] = true,
	[31196] = true, [31197] = true, [31198] = true,
	[31199] = true,
	[31200] = true,
	[31201] = true
}

local function containsExerciseItem(container)
	local items = container:getItems()
	for _, item in ipairs(items) do
		if exerciseItems[item:getId()] then
			return true
		end
		local sub = item:getContainer()
		if sub and containsExerciseItem(sub) then
			return true
		end
	end
	return false
end

local event = Event()
event.onMoveItem = function(self, item, count, fromPosition, toPosition,
                            fromCylinder, toCylinder)
	local isMovingTo   = toCylinder   and toCylinder:isItem()   and toCylinder:getId() == WORKBENCH_ID
	local isMovingFrom = fromCylinder and fromCylinder:isItem() and fromCylinder:getId() == WORKBENCH_ID

	if isMovingTo then
		item:setAttribute(ITEM_ATTRIBUTE_OWNER, self:getId())
		Game.setStorageValue(GlobalStorageKeys.workbenchOwner, self:getId())
		local storedEvent = Game.getStorageValue(GlobalStorageKeys.workbench)
		if storedEvent and storedEvent > 0 then
			stopEvent(storedEvent)
		end
		local eventId = addEvent(function(playerId)
			local workbench = findWorkbench()
			if not workbench then return end
			local size = workbench:getSize()
			for i = size - 1, 0, -1 do
				local subItem = workbench:getItem(i)
				if subItem and subItem:hasAttribute(ITEM_ATTRIBUTE_OWNER)
					and subItem:getAttribute(ITEM_ATTRIBUTE_OWNER) == playerId then
					local player = Player(playerId)
					if player then
						local depot = player:getDepotChest(0, true)
						subItem:moveTo(depot)
						player:sendTextMessage(MESSAGE_INFO_DESCR,
							"You left an item unattended on the imbuement workbench. It has been sent to your depot.")
						player:save()
					else
						subItem:removeAttribute(ITEM_ATTRIBUTE_OWNER)
					end
				end
			end
			Game.setStorageValue(GlobalStorageKeys.workbench, -1)
			Game.setStorageValue(GlobalStorageKeys.workbenchOwner, -1)
		end, ABANDON_TIME, self:getId())
		Game.setStorageValue(GlobalStorageKeys.workbench, eventId)
		return RETURNVALUE_NOERROR
	end

	if isMovingFrom then
		local ownerId = item:getAttribute(ITEM_ATTRIBUTE_OWNER)
		if ownerId and ownerId ~= 0 and ownerId ~= "" then
			if self:getId() ~= ownerId then
				self:sendCancelMessage("This item does not belong to you.")
				return RETURNVALUE_NOTPOSSIBLE
			end
			item:removeAttribute(ITEM_ATTRIBUTE_OWNER)
			local storedEvent = Game.getStorageValue(GlobalStorageKeys.workbench)
			if storedEvent and storedEvent > 0 then
				stopEvent(storedEvent)
				Game.setStorageValue(GlobalStorageKeys.workbench, -1)
			end
			local workbench = findWorkbench()
			if workbench then
				local hasOwned = false
				for i = 0, workbench:getSize() - 1 do
					local sub = workbench:getItem(i)
					if sub and sub:hasAttribute(ITEM_ATTRIBUTE_OWNER) then
						hasOwned = true
						break
					end
				end
				if not hasOwned then
					Game.setStorageValue(GlobalStorageKeys.workbenchOwner, -1)
				end
			end
		end
		return RETURNVALUE_NOERROR
	end

	if isRestricted then
		if not toCylinder or (toCylinder ~= self and (not toCylinder:isItem() or toCylinder:getTopParent() ~= self)) then
			return RETURNVALUE_CANNOTMOVEEXERCISEWEAPON
		end
	end

	if toPosition.x ~= CONTAINER_POSITION then return true end

	if item:getTopParent() == self and (toPosition.y & 0x40) == 0 then
		local itemType, moveItem = ItemType(item:getId())
		if (itemType:getSlotPosition() & SLOTP_TWO_HAND) ~= 0 and toPosition.y ==
			CONST_SLOT_LEFT then
			moveItem = self:getSlotItem(CONST_SLOT_RIGHT)
			if moveItem and ItemType(moveItem:getId()):getWeaponType() == WEAPON_QUIVER
				and itemType:getWeaponType() == WEAPON_DISTANCE then
				moveItem = nil
			end
		elseif itemType:getWeaponType() == WEAPON_SHIELD and toPosition.y ==
			CONST_SLOT_RIGHT then
			moveItem = self:getSlotItem(CONST_SLOT_LEFT)
			if moveItem and
				(ItemType(moveItem:getId()):getSlotPosition() & SLOTP_TWO_HAND) == 0 then
				return true
			end
		elseif itemType:getWeaponType() == WEAPON_QUIVER and toPosition.y ==
			CONST_SLOT_RIGHT then
			moveItem = self:getSlotItem(CONST_SLOT_LEFT)
			if moveItem and ItemType(moveItem:getId()):getWeaponType() == WEAPON_DISTANCE then
				moveItem = nil
			end
		end

		if moveItem then
			local parent = item:getParent()
			if parent:isContainer() and parent:getSize() == parent:getCapacity() then
				self:sendTextMessage(MESSAGE_STATUS_SMALL, Game.getReturnMessage(
					                     RETURNVALUE_CONTAINERNOTENOUGHROOM))
				return false
			else
				return moveItem:moveTo(parent)
			end
		end
	end

	return true
end
event:register()