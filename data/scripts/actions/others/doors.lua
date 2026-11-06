-- Door action script (Crystal Server style)
-- Uses structured tables from data/lib/core/doors.lua

local positionOffsets = {
	Position(1, 0, 0), -- east
	Position(0, 1, 0), -- south
	Position(-1, 0, 0), -- west
	Position(0, -1, 0) -- north
}

--[[
When closing a door with a creature in it findPushPosition will find the most appropriate
adjacent position following a prioritization order.
The function returns the position of the first tile that fulfills all the checks in a round.
The function loops trough east -> south -> west -> north on each following line in that order.
In round 1 it checks if there's an unhindered walkable tile without any creature.
In round 2 it checks if there's a tile with a creature.
In round 3 it checks if there's a tile blocked by a movable tile-blocking item.
In round 4 it checks if there's a tile blocked by a magic wall or wild growth.
]]
local function findPushPosition(creature, round)
	local pos = creature:getPosition()
	for _, offset in ipairs(positionOffsets) do
		local offsetPosition = pos + offset
		local tile = Tile(offsetPosition)
		if tile then
			local creatureCount = tile:getCreatureCount()
			if round == 1 then
				if tile:queryAdd(creature) == RETURNVALUE_NOERROR and creatureCount == 0 then
					if not tile:hasFlag(TILESTATE_PROTECTIONZONE) or
						(tile:hasFlag(TILESTATE_PROTECTIONZONE) and creature:canAccessPz()) then
						return offsetPosition
					end
				end
			elseif round == 2 then
				if creatureCount > 0 then
					if not tile:hasFlag(TILESTATE_PROTECTIONZONE) or
						(tile:hasFlag(TILESTATE_PROTECTIONZONE) and creature:canAccessPz()) then
						return offsetPosition
					end
				end
			elseif round == 3 then
				local topItem = tile:getTopDownItem()
				if topItem then
					if topItem:getType():isMovable() then return offsetPosition end
				end
			else
				if tile:getItemById(ITEM_MAGICWALL) or tile:getItemById(ITEM_WILDGROWTH) then
					return offsetPosition
				end
			end
		end
	end
	if round < 4 then return findPushPosition(creature, round + 1) end
end

-- Helper: push creatures out of a door position when closing it
local function pushCreaturesFromDoor(player, toPosition)
	local creaturePositionTable = {}
	local doorCreatures = Tile(toPosition):getCreatures()
	if doorCreatures and #doorCreatures > 0 then
		for _, doorCreature in pairs(doorCreatures) do
			local pushPosition = findPushPosition(doorCreature, 1)
			if not pushPosition then
				player:sendCancelMessage(RETURNVALUE_NOTENOUGHROOM)
				return false
			end
			table.insert(creaturePositionTable,
			             {creature = doorCreature, position = pushPosition})
		end
		for _, tableCreature in ipairs(creaturePositionTable) do
			tableCreature.creature:teleportTo(tableCreature.position, true, CONST_ME_NONE)
		end
	end
	return true
end

local function isHouseDoor(item)
	local doorId = item:getAttribute(ITEM_ATTRIBUTE_DOORID)
	if doorId and doorId > 0 then
		return true
	end

	local itemPosition = item:getPosition()
	local tile = itemPosition and Tile(itemPosition) or nil
	return tile and tile:getHouse() and item.actionid <= 0
end

-- ============================
-- Key Door Action (locked/unlocked/open)
-- ============================
local keyDoorIds = {}
local keyLockedDoorIds = {}

for _, value in ipairs(KeyDoorTable) do
	if not table.contains(keyDoorIds, value.closedDoor) then
		table.insert(keyDoorIds, value.closedDoor)
	end
	if not table.contains(keyDoorIds, value.lockedDoor) then
		table.insert(keyDoorIds, value.lockedDoor)
	end
	if not table.contains(keyDoorIds, value.openDoor) then
		table.insert(keyDoorIds, value.openDoor)
	end
	if not table.contains(keyLockedDoorIds, value.lockedDoor) then
		table.insert(keyLockedDoorIds, value.lockedDoor)
	end
end

for _, value in ipairs(keysID) do
	if not table.contains(keyDoorIds, value) then
		table.insert(keyDoorIds, value)
	end
end

local keyDoor = Action()
function keyDoor.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	-- Locked door message
	if table.contains(keyLockedDoorIds, item.itemid) then
		if isHouseDoor(item) then
			for _, value in ipairs(KeyDoorTable) do
				if value.lockedDoor == item.itemid then
					item:transform(value.openDoor)
					item:getPosition():sendSingleSoundEffect(SOUND_EFFECT_TYPE_ACTION_OPEN_DOOR, player)
					return true
				end
			end
		end

		player:sendTextMessage(MESSAGE_INFO_DESCR, "It is locked.")
		return true
	end

	-- Use unlocked key door (open it)
	for _, value in ipairs(KeyDoorTable) do
		if value.closedDoor == item.itemid then
			item:transform(value.openDoor)
			item:getPosition():sendSingleSoundEffect(SOUND_EFFECT_TYPE_ACTION_OPEN_DOOR, player)
			return true
		end
	end

	-- Close an open key door
	for _, value in ipairs(KeyDoorTable) do
		if value.openDoor == item.itemid then
			if not pushCreaturesFromDoor(player, toPosition) then
				return true
			end
			item:transform(value.closedDoor)
			item:getPosition():sendSingleSoundEffect(SOUND_EFFECT_TYPE_ACTION_CLOSE_DOOR, player)
			return true
		end
	end

	-- Key use on locked door
	if target and target.actionid and target.actionid > 0 then
		for _, value in ipairs(KeyDoorTable) do
			if item.actionid ~= target.actionid and value.lockedDoor == target.itemid then
				player:sendTextMessage(MESSAGE_STATUS_SMALL, "The key does not match.")
				return true
			end
			if item.actionid == target.actionid then
				if value.lockedDoor == target.itemid then
					target:transform(value.openDoor)
					target:getPosition():sendSingleSoundEffect(SOUND_EFFECT_TYPE_ACTION_OPEN_DOOR, player)
					return true
				elseif value.openDoor == target.itemid or value.closedDoor == target.itemid then
					target:transform(value.lockedDoor)
					target:getPosition():sendSingleSoundEffect(SOUND_EFFECT_TYPE_ACTION_CLOSE_DOOR, player)
					return true
				end
			end
		end
	end
	return false
end

for _, value in ipairs(keyDoorIds) do
	keyDoor:id(value)
end
keyDoor:register()

-- ============================
-- Custom Door Action (simple open/close, including house doors)
-- ============================
local customDoorIds = {}

for _, value in ipairs(CustomDoorTable) do
	if not table.contains(customDoorIds, value.openDoor) then
		table.insert(customDoorIds, value.openDoor)
	end
	if not table.contains(customDoorIds, value.closedDoor) then
		table.insert(customDoorIds, value.closedDoor)
	end
end

local customDoor = Action()
function customDoor.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	-- Open a closed custom door
	for _, value in ipairs(CustomDoorTable) do
		if value.closedDoor == item.itemid then
			item:transform(value.openDoor)
			item:getPosition():sendSingleSoundEffect(SOUND_EFFECT_TYPE_ACTION_OPEN_DOOR, player)
			return true
		end
	end

	-- Close an open custom door
	for _, value in ipairs(CustomDoorTable) do
		if value.openDoor == item.itemid then
			if not pushCreaturesFromDoor(player, toPosition) then
				return true
			end
			item:transform(value.closedDoor)
			item:getPosition():sendSingleSoundEffect(SOUND_EFFECT_TYPE_ACTION_CLOSE_DOOR, player)
			return true
		end
	end
	return true
end

for _, value in ipairs(customDoorIds) do
	customDoor:id(value)
end
customDoor:register()

-- ============================
-- Quest Door Action
-- ============================
local questDoorIds = {}

for _, value in ipairs(QuestDoorTable) do
	if not table.contains(questDoorIds, value.openDoor) then
		table.insert(questDoorIds, value.openDoor)
	end
	if not table.contains(questDoorIds, value.closedDoor) then
		table.insert(questDoorIds, value.closedDoor)
	end
end

local questDoor = Action()
function questDoor.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	for _, value in ipairs(QuestDoorTable) do
		if value.closedDoor == item.itemid then
			if item.actionid > 0 and (player:getStorageValue(item.actionid) ~= -1 or
				player:getGroup():getAccess()) then
				item:transform(value.openDoor)
				item:getPosition():sendSingleSoundEffect(SOUND_EFFECT_TYPE_ACTION_OPEN_DOOR, player)
				player:teleportTo(toPosition, true, CONST_ME_NONE)
				return true
			else
				player:sendTextMessage(MESSAGE_EVENT_ADVANCE,
				                       "The door seems to be sealed against unwanted intruders.")
				return true
			end
		end
	end

	-- Close an open quest door (push creatures out)
	for _, value in ipairs(QuestDoorTable) do
		if value.openDoor == item.itemid then
			if not pushCreaturesFromDoor(player, toPosition) then
				return true
			end
			item:transform(value.closedDoor)
			item:getPosition():sendSingleSoundEffect(SOUND_EFFECT_TYPE_ACTION_CLOSE_DOOR, player)
			return true
		end
	end
	return true
end

for _, value in ipairs(questDoorIds) do
	questDoor:id(value)
end
questDoor:register()

-- ============================
-- Level Door Action
-- ============================
local levelDoorIds = {}

for _, value in ipairs(LevelDoorTable) do
	if not table.contains(levelDoorIds, value.openDoor) then
		table.insert(levelDoorIds, value.openDoor)
	end
	if not table.contains(levelDoorIds, value.closedDoor) then
		table.insert(levelDoorIds, value.closedDoor)
	end
end

local levelDoor = Action()
function levelDoor.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	for _, value in ipairs(LevelDoorTable) do
		if value.closedDoor == item.itemid then
			if item.actionid > 0 and (player:getLevel() >= item.actionid -
				actionIds.levelDoor or player:getGroup():getAccess()) then
				item:transform(value.openDoor)
				item:getPosition():sendSingleSoundEffect(SOUND_EFFECT_TYPE_ACTION_OPEN_DOOR, player)
				player:teleportTo(toPosition, true, CONST_ME_NONE)
				return true
			else
				player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Only the worthy may pass.")
				return true
			end
		end
	end

	-- Close an open level door (push creatures out)
	for _, value in ipairs(LevelDoorTable) do
		if value.openDoor == item.itemid then
			if not pushCreaturesFromDoor(player, toPosition) then
				return true
			end
			item:transform(value.closedDoor)
			item:getPosition():sendSingleSoundEffect(SOUND_EFFECT_TYPE_ACTION_CLOSE_DOOR, player)
			return true
		end
	end
	return true
end

for _, value in ipairs(levelDoorIds) do
	levelDoor:id(value)
end
levelDoor:register()
