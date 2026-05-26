local WORKBENCH_ID = 27547
local SUPPLY_STASH_ITEM_ID = ITEM_SUPPLY_STASH or 28750
local ABANDON_TIME = 600000

local function findWorkbench(position, instanceId)
	local tile = Tile(position)
	if not tile then
		return nil
	end

	local items = tile:getItems()
	if not items then
		return nil
	end

	for _, item in ipairs(items) do
		if item:getId() == WORKBENCH_ID and item:getInstanceId() == instanceId then
			return Container(item.uid), item
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

local function isSupplyStashCylinder(cylinder)
	if not cylinder or not cylinder.isItem or not cylinder:isItem() then
		return false
	end

	if cylinder:getId() == SUPPLY_STASH_ITEM_ID then
		return true
	end

	if cylinder.getTopParent then
		local topParent = cylinder:getTopParent()
		return topParent and topParent.isItem and topParent:isItem() and topParent:getId() == SUPPLY_STASH_ITEM_ID
	end
	return false
end

local event = Event()
event.onMoveItem = function(self, item, count, fromPosition, toPosition,
                            fromCylinder, toCylinder)
	if isSupplyStashCylinder(toCylinder) then
		self:sendCancelMessage("Put items inside Depot Locker boxes 1 to 15, then use Stow All.")
		return RETURNVALUE_NOTPOSSIBLE
	end

	local isMovingTo   = toCylinder   and toCylinder:isItem()   and toCylinder:getId() == WORKBENCH_ID
	local isMovingFrom = fromCylinder and fromCylinder:isItem() and fromCylinder:getId() == WORKBENCH_ID

	if isMovingTo then
		if not WorkbenchSessions.isSameInstance(toCylinder, self) then
			self:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
			return RETURNVALUE_NOTPOSSIBLE
		end

		if WorkbenchSessions.isInUseByOther(toCylinder, self) then
			self:sendCancelMessage("This workbench is currently in use.")
			return RETURNVALUE_NOTPOSSIBLE
		end

		local workbenchContainer = Container(toCylinder.uid)
		local itemOwnerId = WorkbenchSessions.getOwnedItemOwner(workbenchContainer)
		if itemOwnerId and itemOwnerId ~= self:getId() then
			self:sendCancelMessage("This workbench is currently in use.")
			return RETURNVALUE_NOTPOSSIBLE
		end

		item:setAttribute(ITEM_ATTRIBUTE_OWNER, self:getId())
		local workbenchKey = WorkbenchSessions.reserve(toCylinder, self:getId())
		if not workbenchKey then
			item:removeAttribute(ITEM_ATTRIBUTE_OWNER)
			return RETURNVALUE_NOTPOSSIBLE
		end

		local workbenchPosition = toCylinder:getPosition()
		local workbenchInstanceId = toCylinder:getInstanceId()
		local eventId = addEvent(function(playerId, key, x, y, z, instanceId)
			local workbench = findWorkbench(Position(x, y, z), instanceId)
			if not workbench then
				WorkbenchSessions.clear(key, false)
				return
			end

			local size = workbench:getSize()
			for i = size - 1, 0, -1 do
				local subItem = workbench:getItem(i)
				if subItem and subItem:hasAttribute(ITEM_ATTRIBUTE_OWNER)
					and tonumber(subItem:getAttribute(ITEM_ATTRIBUTE_OWNER)) == playerId then
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
			WorkbenchSessions.clear(key, false)
		end, ABANDON_TIME, self:getId(), workbenchKey, workbenchPosition.x, workbenchPosition.y, workbenchPosition.z, workbenchInstanceId)
		WorkbenchSessions.setEvent(workbenchKey, eventId)
		return RETURNVALUE_NOERROR
	end

	if isMovingFrom then
		if not WorkbenchSessions.isSameInstance(fromCylinder, self) then
			self:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
			return RETURNVALUE_NOTPOSSIBLE
		end

		local ownerId = item:getAttribute(ITEM_ATTRIBUTE_OWNER)
		if ownerId and ownerId ~= 0 and ownerId ~= "" then
			if self:getId() ~= ownerId then
				self:sendCancelMessage("This item does not belong to you.")
				return RETURNVALUE_NOTPOSSIBLE
			end
			item:removeAttribute(ITEM_ATTRIBUTE_OWNER)
			local workbench = Container(fromCylinder.uid)
			if not WorkbenchSessions.hasOwnedItems(workbench) then
				WorkbenchSessions.clear(fromCylinder)
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
