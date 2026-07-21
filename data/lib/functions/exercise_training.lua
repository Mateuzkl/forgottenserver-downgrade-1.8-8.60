if staminaEvents == nil then
    staminaEvents = {}
end

ExerciseWeaponsTable = {
	[28540] = { skill = SKILL_SWORD },
	[28552] = { skill = SKILL_SWORD },
	[35279] = { skill = SKILL_SWORD },
	[35285] = { skill = SKILL_SWORD },
	[28553] = { skill = SKILL_AXE },
	[28541] = { skill = SKILL_AXE },
	[35280] = { skill = SKILL_AXE },
	[35286] = { skill = SKILL_AXE },
	[28554] = { skill = SKILL_CLUB },
	[28542] = { skill = SKILL_CLUB },
	[35281] = { skill = SKILL_CLUB },
	[35287] = { skill = SKILL_CLUB },
	[44064] = { skill = SKILL_SHIELD },
	[44065] = { skill = SKILL_SHIELD },
	[44066] = { skill = SKILL_SHIELD },
	[44067] = { skill = SKILL_SHIELD },
	-- ROD
	[28544] = { skill = SKILL_MAGLEVEL, effect = CONST_ANI_SMALLICE, allowFarUse = true },
	[28556] = { skill = SKILL_MAGLEVEL, effect = CONST_ANI_SMALLICE, allowFarUse = true },
	[35283] = { skill = SKILL_MAGLEVEL, effect = CONST_ANI_SMALLICE, allowFarUse = true },
	[35289] = { skill = SKILL_MAGLEVEL, effect = CONST_ANI_SMALLICE, allowFarUse = true },
	-- RANGE
	[28543] = { skill = SKILL_DISTANCE, effect = CONST_ANI_SIMPLEARROW, allowFarUse = true },
	[28555] = { skill = SKILL_DISTANCE, effect = CONST_ANI_SIMPLEARROW, allowFarUse = true },
	[35282] = { skill = SKILL_DISTANCE, effect = CONST_ANI_SIMPLEARROW, allowFarUse = true },
	[35288] = { skill = SKILL_DISTANCE, effect = CONST_ANI_SIMPLEARROW, allowFarUse = true },
	-- WAND
	[28545] = { skill = SKILL_MAGLEVEL, effect = CONST_ANI_FIRE, allowFarUse = true },
	[28557] = { skill = SKILL_MAGLEVEL, effect = CONST_ANI_FIRE, allowFarUse = true },
	[35284] = { skill = SKILL_MAGLEVEL, effect = CONST_ANI_FIRE, allowFarUse = true },
	[35290] = { skill = SKILL_MAGLEVEL, effect = CONST_ANI_FIRE, allowFarUse = true },
	-- FIST
	[50292] = { skill = SKILL_FIST, effect = CONST_ANI_WHIRLWINDAXE },
	[50293] = { skill = SKILL_FIST, effect = CONST_ANI_WHIRLWINDAXE },
	[50294] = { skill = SKILL_FIST, effect = CONST_ANI_WHIRLWINDAXE },
	[50295] = { skill = SKILL_FIST, effect = CONST_ANI_WHIRLWINDAXE },
}

FreeDummies = {5787, 5788, 28558, 28559, 28560, 28561, 28562, 28563, 28564, 28565}
HouseDummies = {5787, 5788, 28558, 28559, 28560, 28561, 28562, 28563, 28564, 28565}
MaxAllowedOnADummy = configManager.getNumber(configKeys.MAX_ALLOWED_ON_A_DUMMY)

local magicLevelRate = configManager.getNumber(configKeys.RATE_MAGIC)
local skillLevelRate = configManager.getNumber(configKeys.RATE_SKILL)

local function isItemOwnedByPlayer(item, player)
	local parent = item:getParent()
	while parent do
		if parent.isPlayer and parent:isPlayer() then
			return parent:getId() == player:getId()
		end
		if not parent.getParent then
			return false
		end
		parent = parent:getParent()
	end
	return false
end

function LeaveTraining(playerId)
	if onExerciseTraining[playerId] then
		stopEvent(onExerciseTraining[playerId].event)
		onExerciseTraining[playerId] = nil
	end
end

local function findContainerItemById(container, weaponId, weaponUid)
	if not container or not container:isContainer() then
		return nil
	end

	for i = 0, container:getSize() - 1 do
		local item = container:getItem(i)
		if item then
			if item:getId() == weaponId and item:getUniqueId() == weaponUid then
				return item
			end

			if item:isContainer() then
				local found = findContainerItemById(item, weaponId, weaponUid)
				if found then
					return found
				end
			end
		end
	end

	return nil
end

local function findPlayerWeaponById(player, weaponId, weaponUid)
	for slot = CONST_SLOT_HEAD, CONST_SLOT_AMMO do
		local item = player:getSlotItem(slot)
		if item and item:getId() == weaponId and item:getUniqueId() == weaponUid then
			return item
		end
	end

	local backpack = player:getSlotItem(CONST_SLOT_BACKPACK)
	return findContainerItemById(backpack, weaponId, weaponUid)
end

function ExerciseEvent(playerId, tilePosition, weaponId, dummyId)
	local player = Player(playerId)
	if not player then
		return LeaveTraining(playerId)
	end

	local training = onExerciseTraining[playerId]
	if not training then
		return false
	end

	if training.ownerGuid and training.ownerGuid ~= player:getGuid() then
		LeaveTraining(playerId)
		return false
	end

	if not Tile(tilePosition):getItemById(dummyId) then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Someone has moved the dummy, the training has stopped.")
		LeaveTraining(playerId)
		return false
	end

	local playerPosition = player:getPosition()
	if not player:getTile():hasFlag(TILESTATE_PROTECTIONZONE) and not staminaEvents[playerId] then
        player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You are no longer in a protection zone, the training has stopped.")
		LeaveTraining(playerId)
        return true
    end

	local weapon = findPlayerWeaponById(player, weaponId, training.weaponUid)
	if not weapon then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The training weapon is no longer available, the training has stopped.")
		LeaveTraining(playerId)
		return false
	end

	onExerciseTraining[playerId].weaponUid = weapon:getUniqueId()

	if not isItemOwnedByPlayer(weapon, player) then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The training weapon is no longer yours, the training has stopped.")
		LeaveTraining(playerId)
		return false
	end

	if not weapon:hasAttribute(ITEM_ATTRIBUTE_CHARGES) then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The selected item is not a training weapon, the training has stopped.")
		LeaveTraining(playerId)
		return false
	end

	local weaponCharges = weapon:getAttribute(ITEM_ATTRIBUTE_CHARGES)
	if not weaponCharges or weaponCharges <= 0 then
		weapon:remove(1)
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Your training weapon has disappeared.")
		LeaveTraining(playerId)
		return false
	end

	local isMagic = ExerciseWeaponsTable[weaponId].skill == SKILL_MAGLEVEL
	local bonusDummy = 1

	if table.contains(HouseDummies, dummyId) then
		bonusDummy = 1.5 -- 50%
	elseif table.contains(FreeDummies, dummyId) then
		bonusDummy = 1
	end

	if isMagic then
		player:addManaSpent(500 * bonusDummy)
	else
		player:addSkillTries(ExerciseWeaponsTable[weaponId].skill, 7 * bonusDummy)
	end

	weapon:setAttribute(ITEM_ATTRIBUTE_CHARGES, (weaponCharges - 1))
	tilePosition:sendMagicEffect(CONST_ME_HITAREA)

	if ExerciseWeaponsTable[weaponId].effect then
		playerPosition:sendDistanceEffect(tilePosition, ExerciseWeaponsTable[weaponId].effect)
	end

	if weapon:getAttribute(ITEM_ATTRIBUTE_CHARGES) <= 0 then
		weapon:remove(1)
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Your training weapon has disappeared.")
		LeaveTraining(playerId)
		return false
	end

	local vocation = player:getVocation()
	onExerciseTraining[playerId].event = addEvent(ExerciseEvent, vocation:getAttackSpeed() / configManager.getNumber(configKeys.RATE_EXERCISE_TRAINING_SPEED), playerId, tilePosition, weaponId, dummyId)
	onExerciseTraining[playerId].ownerGuid = player:getGuid()
	return true
end

if onExerciseTraining == nil then
	onExerciseTraining = {}
end
