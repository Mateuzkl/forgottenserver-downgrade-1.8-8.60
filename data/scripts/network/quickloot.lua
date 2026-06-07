if not configManager.getBoolean(configKeys.QUICK_LOOT_ENABLED) then
	return
end

local OPCODE_QUICK_LOOT = 0x8F
local OPCODE_LOOT_CONTAINER = 0x90
local OPCODE_BLACK_WHITELIST = 0x91
local OPCODE_SEND_LOOT_CONTAINERS = 0xC0
local OPCODE_SEND_LOOT_STATS = 0xCF

local ObjectCategory = {
	OBJECTCATEGORY_NONE = 0,
	OBJECTCATEGORY_ARMORS = 1,
	OBJECTCATEGORY_NECKLACES = 2,
	OBJECTCATEGORY_BOOTS = 3,
	OBJECTCATEGORY_CONTAINERS = 4,
	OBJECTCATEGORY_DECORATION = 5,
	OBJECTCATEGORY_FOOD = 6,
	OBJECTCATEGORY_HELMETS = 7,
	OBJECTCATEGORY_LEGS = 8,
	OBJECTCATEGORY_OTHERS = 9,
	OBJECTCATEGORY_POTIONS = 10,
	OBJECTCATEGORY_RINGS = 11,
	OBJECTCATEGORY_RUNES = 12,
	OBJECTCATEGORY_SHIELDS = 13,
	OBJECTCATEGORY_TOOLS = 14,
	OBJECTCATEGORY_VALUABLES = 15,
	OBJECTCATEGORY_AMMO = 16,
	OBJECTCATEGORY_AXES = 17,
	OBJECTCATEGORY_CLUBS = 18,
	OBJECTCATEGORY_DISTANCEWEAPONS = 19,
	OBJECTCATEGORY_SWORDS = 20,
	OBJECTCATEGORY_WANDS = 21,
	OBJECTCATEGORY_PREMIUMSCROLLS = 22,
	OBJECTCATEGORY_TIBIACOINS = 23,
	OBJECTCATEGORY_CREATUREPRODUCTS = 24,
	OBJECTCATEGORY_QUIVERS = 25,
	OBJECTCATEGORY_FISTWEAPONS = 27,
	OBJECTCATEGORY_GOLD = 30,
	OBJECTCATEGORY_DEFAULT = 31,
	OBJECTCATEGORY_FIRST = 1,
	OBJECTCATEGORY_LAST = 31,
}

local QUICKLOOTFILTER_SKIPPEDLOOT = 0
local QUICKLOOTFILTER_ACCEPTEDLOOT = 1

local function supportsCustomNetwork(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function getObjectCategoryName(category)
	local names = {
		[ObjectCategory.OBJECTCATEGORY_ARMORS] = "Armors",
		[ObjectCategory.OBJECTCATEGORY_NECKLACES] = "Amulets",
		[ObjectCategory.OBJECTCATEGORY_BOOTS] = "Boots",
		[ObjectCategory.OBJECTCATEGORY_CONTAINERS] = "Containers",
		[ObjectCategory.OBJECTCATEGORY_DECORATION] = "Decoration",
		[ObjectCategory.OBJECTCATEGORY_FOOD] = "Food",
		[ObjectCategory.OBJECTCATEGORY_HELMETS] = "Helmets",
		[ObjectCategory.OBJECTCATEGORY_LEGS] = "Legs",
		[ObjectCategory.OBJECTCATEGORY_OTHERS] = "Others",
		[ObjectCategory.OBJECTCATEGORY_POTIONS] = "Potions",
		[ObjectCategory.OBJECTCATEGORY_RINGS] = "Rings",
		[ObjectCategory.OBJECTCATEGORY_RUNES] = "Runes",
		[ObjectCategory.OBJECTCATEGORY_SHIELDS] = "Shields",
		[ObjectCategory.OBJECTCATEGORY_TOOLS] = "Tools",
		[ObjectCategory.OBJECTCATEGORY_VALUABLES] = "Valuables",
		[ObjectCategory.OBJECTCATEGORY_AMMO] = "Weapons: Ammunition",
		[ObjectCategory.OBJECTCATEGORY_AXES] = "Weapons: Axes",
		[ObjectCategory.OBJECTCATEGORY_CLUBS] = "Weapons: Clubs",
		[ObjectCategory.OBJECTCATEGORY_DISTANCEWEAPONS] = "Weapons: Distance",
		[ObjectCategory.OBJECTCATEGORY_SWORDS] = "Weapons: Swords",
		[ObjectCategory.OBJECTCATEGORY_WANDS] = "Weapons: Wands",
		[ObjectCategory.OBJECTCATEGORY_PREMIUMSCROLLS] = "Premium Scrolls",
		[ObjectCategory.OBJECTCATEGORY_TIBIACOINS] = "Tibia Coins",
		[ObjectCategory.OBJECTCATEGORY_CREATUREPRODUCTS] = "Creature Products",
		[ObjectCategory.OBJECTCATEGORY_QUIVERS] = "Quiver",
		[ObjectCategory.OBJECTCATEGORY_FISTWEAPONS] = "Fist Weapons",
		[ObjectCategory.OBJECTCATEGORY_GOLD] = "Gold",
		[ObjectCategory.OBJECTCATEGORY_DEFAULT] = "Unassigned Loot",
	}
	return names[category] or "Unknown"
end

local managedContainers = {}
local quickLootState = {}
local lastNotification = {}

local function getPlayerState(player)
	local pid = player:getId()
	if not quickLootState[pid] then
		quickLootState[pid] = {
			filter = QUICKLOOTFILTER_SKIPPEDLOOT,
			itemIds = {},
			fallback = true,
		}
	end
	return quickLootState[pid]
end

local function getManagedContainer(player, category, isLootContainer)
	local pid = player:getId()
	if not player:isPremium() and category ~= ObjectCategory.OBJECTCATEGORY_DEFAULT then
		category = ObjectCategory.OBJECTCATEGORY_DEFAULT
	end

	local containers = managedContainers[pid] and managedContainers[pid][category]
	if not containers then
		if category ~= ObjectCategory.OBJECTCATEGORY_DEFAULT then
			return getManagedContainer(player, ObjectCategory.OBJECTCATEGORY_DEFAULT, isLootContainer)
		end
		return nil
	end

	if isLootContainer then
		return containers.loot
	else
		return containers.obtain
	end
end

local function setManagedContainer(player, category, containerId, isLootContainer)
	local pid = player:getId()
	if not managedContainers[pid] then
		managedContainers[pid] = {}
	end
	if not managedContainers[pid][category] then
		managedContainers[pid][category] = { loot = 0, obtain = 0 }
	end
	if isLootContainer then
		managedContainers[pid][category].loot = containerId
	else
		managedContainers[pid][category].obtain = containerId
	end
end

local function clearManagedContainer(player, category, isLootContainer)
	local pid = player:getId()
	if not managedContainers[pid] or not managedContainers[pid][category] then
		return
	end
	if isLootContainer then
		managedContainers[pid][category].loot = 0
	else
		managedContainers[pid][category].obtain = 0
	end
	if managedContainers[pid][category].loot == 0 and managedContainers[pid][category].obtain == 0 then
		managedContainers[pid][category] = nil
	end
end

local function findContainerInInventoryRecursive(item, containerId)
	if not item then
		return nil
	end

	if item:getId() == containerId then
		local container = item:getContainer()
		if container then
			return container
		end
	end

	local parentContainer = item:getContainer()
	if parentContainer then
		local items = parentContainer:getItems(true)
		for _, subItem in ipairs(items) do
			if subItem:getId() == containerId then
				local subContainer = subItem:getContainer()
				if subContainer then
					return subContainer
				end
			end
		end
	end

	return nil
end

local function findContainerInInventory(player, containerId)
	if containerId == 0 then
		return nil
	end

	for slot = CONST_SLOT_HEAD, CONST_SLOT_AMMO do
		local item = player:getSlotItem(slot)
		if item then
			local found = findContainerInInventoryRecursive(item, containerId)
			if found then
				return found
			end
		end
	end
	return nil
end

local function findFallbackContainer(player)
	local backpackItem = player:getSlotItem(CONST_SLOT_BACKPACK)
	if backpackItem then
		return backpackItem:getContainer()
	end
	return nil
end

local function getItemCategory(item)
	if not item then
		return ObjectCategory.OBJECTCATEGORY_NONE
	end

	local itemId = item:getId()
	if itemId == ITEM_GOLD_COIN or itemId == ITEM_PLATINUM_COIN or itemId == ITEM_CRYSTAL_COIN then
		return ObjectCategory.OBJECTCATEGORY_GOLD
	end

	local itemType = ItemType(itemId)
	if not itemType then
		return ObjectCategory.OBJECTCATEGORY_DEFAULT
	end

	local weaponType = itemType:getWeaponType()
	if weaponType ~= nil and weaponType ~= WEAPON_NONE then
		local weaponCategoryMap = {
			[WEAPON_FIST] = ObjectCategory.OBJECTCATEGORY_FISTWEAPONS,
			[WEAPON_SWORD] = ObjectCategory.OBJECTCATEGORY_SWORDS,
			[WEAPON_CLUB] = ObjectCategory.OBJECTCATEGORY_CLUBS,
			[WEAPON_AXE] = ObjectCategory.OBJECTCATEGORY_AXES,
			[WEAPON_SHIELD] = ObjectCategory.OBJECTCATEGORY_SHIELDS,
			[WEAPON_DISTANCE] = ObjectCategory.OBJECTCATEGORY_DISTANCEWEAPONS,
			[WEAPON_WAND] = ObjectCategory.OBJECTCATEGORY_WANDS,
			[WEAPON_AMMO] = ObjectCategory.OBJECTCATEGORY_AMMO,
		}
		if weaponCategoryMap[weaponType] then
			return weaponCategoryMap[weaponType]
		end
		return ObjectCategory.OBJECTCATEGORY_DEFAULT
	end

	local slotPosition = itemType:getSlotPosition()
	local slotMap = {
		[SLOTP_HEAD] = ObjectCategory.OBJECTCATEGORY_HELMETS,
		[SLOTP_NECKLACE] = ObjectCategory.OBJECTCATEGORY_NECKLACES,
		[SLOTP_BACKPACK] = ObjectCategory.OBJECTCATEGORY_CONTAINERS,
		[SLOTP_ARMOR] = ObjectCategory.OBJECTCATEGORY_ARMORS,
		[SLOTP_LEGS] = ObjectCategory.OBJECTCATEGORY_LEGS,
		[SLOTP_FEET] = ObjectCategory.OBJECTCATEGORY_BOOTS,
		[SLOTP_RING] = ObjectCategory.OBJECTCATEGORY_RINGS,
	}
	if slotPosition ~= 0 then
		for slot, category in pairs(slotMap) do
			if slotPosition & slot ~= 0 then
				return category
			end
		end
	end

	local itemTypeName = itemType.getType and itemType:getType()
	if itemTypeName == ITEM_TYPE_RUNE then
		return ObjectCategory.OBJECTCATEGORY_RUNES
	elseif itemTypeName == ITEM_TYPE_CREATUREPRODUCT then
		return ObjectCategory.OBJECTCATEGORY_CREATUREPRODUCTS
	elseif itemTypeName == ITEM_TYPE_FOOD then
		return ObjectCategory.OBJECTCATEGORY_FOOD
	elseif itemTypeName == ITEM_TYPE_VALUABLE then
		return ObjectCategory.OBJECTCATEGORY_VALUABLES
	elseif itemTypeName == ITEM_TYPE_POTION then
		return ObjectCategory.OBJECTCATEGORY_POTIONS
	elseif itemTypeName == ITEM_TYPE_CONTAINER then
		return ObjectCategory.OBJECTCATEGORY_CONTAINERS
	end

	return ObjectCategory.OBJECTCATEGORY_DEFAULT
end

local function isQuickLootListedItem(player, itemId)
	local state = getPlayerState(player)
	for _, id in ipairs(state.itemIds) do
		if id == itemId then
			return true
		end
	end
	return false
end

local function shouldLootItem(player, item)
	if not item then
		return false
	end

	local itemId = item:getId()
	local itemType = ItemType(itemId)
	if not itemType or not itemType:isPickupable() then
		return false
	end

	local state = getPlayerState(player)
	local listed = isQuickLootListedItem(player, itemId)

	if state.filter == QUICKLOOTFILTER_ACCEPTEDLOOT then
		return listed
	else
		return not listed
	end
end

local function findDestinationContainer(player, category)
	local pid = player:getId()
	local containerId = getManagedContainer(player, category, true)
	local lootContainer = nil

	if containerId and containerId ~= 0 then
		lootContainer = findContainerInInventory(player, containerId)
	end

	local state = getPlayerState(player)
	if not lootContainer and state.fallback then
		lootContainer = findFallbackContainer(player)
	end

	return lootContainer
end

local function sendLootContainers(player)
	local pid = player:getId()

	local msg = NetworkMessage(player)
	msg:addByte(OPCODE_SEND_LOOT_CONTAINERS)
	local state = getPlayerState(player)
	msg:addByte(state.fallback and 1 or 0)

	if not managedContainers[pid] then
		msg:addByte(0)
		msg:sendToPlayer(player)
		return
	end

	-- First section: loot containers (category + lootId, 3 bytes each)
	local lootCount = 0
	for category, containers in pairs(managedContainers[pid]) do
		if containers.loot and containers.loot ~= 0 then
			lootCount = lootCount + 1
		end
	end

	msg:addByte(lootCount)
	for category, containers in pairs(managedContainers[pid]) do
		if containers.loot and containers.loot ~= 0 then
			msg:addByte(category)
			msg:addU16(containers.loot)
		end
	end

	-- Second section: obtain containers (separate count + entries)
	local obtainCount = 0
	for category, containers in pairs(managedContainers[pid]) do
		if containers.obtain and containers.obtain ~= 0 then
			obtainCount = obtainCount + 1
		end
	end

	msg:addByte(obtainCount)
	for category, containers in pairs(managedContainers[pid]) do
		if containers.obtain and containers.obtain ~= 0 then
			msg:addByte(category)
			msg:addU16(containers.obtain)
		end
	end

	msg:sendToPlayer(player)
end

local function sendLootStats(player, itemId, count)
	local msg = NetworkMessage(player)
	msg:addByte(OPCODE_SEND_LOOT_STATS)
	msg:addItemId(itemId)
	msg:addByte(count)
	local itemType = ItemType(itemId)
	msg:addString(itemType and itemType:getName() or "")
	msg:sendToPlayer(player)
end

local function findDestinationContainerCached(player, category, cache)
	local containerId = getManagedContainer(player, category, true)

	if containerId and containerId ~= 0 then
		if cache[containerId] == nil then
			cache[containerId] = findContainerInInventory(player, containerId)
		end
		if cache[containerId] then
			return cache[containerId]
		end
	end

	local state = getPlayerState(player)
	if state.fallback then
		if cache._fallback == nil then
			cache._fallback = findFallbackContainer(player)
		end
		return cache._fallback
	end

	return nil
end

local function lootCorpse(player, corpse)
	if not corpse then
		return
	end

	local state = getPlayerState(player)
	local ignoreListItems = state.filter == QUICKLOOTFILTER_SKIPPEDLOOT

	local items = corpse:getItems(true)
	if not items or #items == 0 then
		return
	end

	local containerCache = {}
	local totalLooted = 0
	local capacityIssue = false
	local containerFullCategory = nil

	for _, item in ipairs(items) do
		local itemId = item:getId()
		local listed = isQuickLootListedItem(player, itemId)

		if (listed and ignoreListItems) or (not listed and not ignoreListItems) then
			goto continue
		end

		local category = getItemCategory(item)
		local dest = findDestinationContainerCached(player, category, containerCache)

		if not dest then
			if not containerFullCategory then
				containerFullCategory = category
			end
			goto continue
		end

		if player:getFreeCapacity() < item:getWeight() then
			capacityIssue = true
			goto continue
		end

		local success = item:moveTo(dest)
		if success then
			totalLooted = totalLooted + 1
		end
		::continue::
	end

	if totalLooted > 0 then
		player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "You looted " .. totalLooted .. " items.")
	end

	if capacityIssue then
		local pid = player:getId()
		local now = os.mtime()
		if not lastNotification[pid] or lastNotification[pid] + 15000 < now then
			player:sendTextMessage(MESSAGE_GAME_HIGHLIGHT, "Attention! The loot you are trying to pick up is too heavy for you to carry.")
			lastNotification[pid] = now
		else
			player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Attention! The loot you are trying to pick up is too heavy for you to carry.")
		end
	elseif containerFullCategory then
		local pid = player:getId()
		local now = os.mtime()
		local catName = getObjectCategoryName(containerFullCategory)
		if not lastNotification[pid] or lastNotification[pid] + 15000 < now then
			player:sendTextMessage(MESSAGE_GAME_HIGHLIGHT, "Attention! The container assigned to category " .. catName .. " is full.")
			lastNotification[pid] = now
		else
			player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Attention! The container assigned to category " .. catName .. " is full.")
		end
	end

	sendLootContainers(player)
end

local function findCorpsesOnTile(tile)
	if not tile then
		return {}
	end

	local corpses = {}
	local items = tile:getItems()
	if not items then
		return corpses
	end

	for _, item in ipairs(items) do
		if item:isContainer() then
			local container = item:getContainer()
			if container then
				local owner = container:getCorpseOwner()
				if owner ~= 0 or item:getId() == ITEM_REWARD_CONTAINER then
					table.insert(corpses, container)
				end
			end
		end
	end

	return corpses
end

local function canPlayerLootCorpse(player, corpse)
	if not corpse then
		return false
	end

	local owner = corpse:getCorpseOwner()
	if owner == 0 then
		return true
	end

	return owner == player:getId() or player:getAccountType() >= ACCOUNT_TYPE_GAMEMASTER
end

local function lootCorpsesOnTile(player, tile, maxCorpses)
	if not tile then
		return 0
	end

	local corpses = findCorpsesOnTile(tile)
	local lootedCount = 0

	for _, corpse in ipairs(corpses) do
		if lootedCount >= maxCorpses then
			break
		end
		if canPlayerLootCorpse(player, corpse) then
			lootCorpse(player, corpse)
			lootedCount = lootedCount + 1
		end
	end

	return lootedCount
end

-- Auto-loot variant: uses filterPlayer's blacklist/whitelist but delivers items to destPlayer's containers
local function lootCorpseAuto(filterPlayer, destPlayer, corpse)
	if not corpse then
		return 0
	end

	local filterState = getPlayerState(filterPlayer)
	local ignoreListItems = filterState.filter == QUICKLOOTFILTER_SKIPPEDLOOT

	local items = corpse:getItems(true)
	if not items or #items == 0 then
		return 0
	end


	local containerCache = {}
	local totalLooted = 0

	for _, item in ipairs(items) do
		local itemId = item:getId()
		local listed = isQuickLootListedItem(filterPlayer, itemId)

		if (listed and ignoreListItems) or (not listed and not ignoreListItems) then
			goto continue
		end

		local category = getItemCategory(item)
		local dest = findDestinationContainerCached(destPlayer, category, containerCache)

		if not dest then
			goto continue
		end

		if destPlayer:getFreeCapacity() < item:getWeight() then
			goto continue
		end

		if item:moveTo(dest) then
			totalLooted = totalLooted + 1
		end
		::continue::
	end

	if totalLooted > 0 then
		sendLootContainers(destPlayer)
	end

	return totalLooted
end

-- CreatureEvent: auto-loot on kill
local killEvent = CreatureEvent("QuickLootKill")
function killEvent.onKill(player, target)

	local killerId = player:getId()
	local targetPos = target:getPosition()
	local targetName = target:getName()

	addEvent(function()
		local killer = Player(killerId)
		if not killer then
			return
		end

		local settings = killer:kv():scoped("settings")
		if not settings:get("quickLoot") then
			return
		end

		local tile = Tile(targetPos)
		if not tile then
			return
		end

		local recipient = killer
		local party = killer:getParty()
		if party then
			local leader = party:getLeader()
			if leader and leader ~= killer then
				local leaderPos = leader:getPosition()
				if leaderPos:getDistance(targetPos) <= 10 then
					recipient = leader
				end
			end
		end

		local corpses = findCorpsesOnTile(tile)
		local totalLooted = 0

		for _, corpse in ipairs(corpses) do
			if canPlayerLootCorpse(killer, corpse) then
				local looted = lootCorpseAuto(killer, recipient, corpse)
				totalLooted = totalLooted + looted
			end
		end

		if totalLooted > 0 then
			recipient:sendTextMessage(MESSAGE_STATUS_DEFAULT, "You looted " .. totalLooted .. " items via QuickLoot.")
		end
	end, 500)

	return true
end
killEvent:register()

-- PacketHandler: 0x8F - QuickLoot action
local quickLootHandler = PacketHandler(OPCODE_QUICK_LOOT)
function quickLootHandler.onReceive(player, msg)
	if not supportsCustomNetwork(player) then
		return
	end
	local variant = msg:getByte()
	local pos = msg:getPosition()

	local maxCorpses = configManager.getNumber(configKeys.QUICK_LOOT_MAX_CORPSES)
	if not maxCorpses or maxCorpses <= 0 then
		maxCorpses = 30
	end

	if variant == 2 then
		-- Loot nearby (3x3 area)
		local playerPos = player:getPosition()
		local totalLooted = 0
		local done = false

		for dx = -1, 1 do
			if done then break end
			for dy = -1, 1 do
				if totalLooted >= maxCorpses then
					done = true
					break
				end
				local tile = Tile(Position(playerPos.x + dx, playerPos.y + dy, playerPos.z))
				totalLooted = totalLooted + lootCorpsesOnTile(player, tile, maxCorpses - totalLooted)
			end
		end

		if totalLooted == 0 then
			player:sendCancelMessage("No lootable corpses nearby.")
		elseif totalLooted > 1 then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You looted " .. totalLooted .. " corpses.")
		end
		sendLootContainers(player)
		return
	end

	if variant == 1 then
		-- Loot all corpses on tile
		local msgLen = msg:len()
		local msgTell = msg:tell()
		if msgLen - msgTell >= 3 then
			msg:getU16()
			msg:getByte()
		end

		local tile = Tile(pos)
		if not tile then
			player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
			return
		end

		local looted = lootCorpsesOnTile(player, tile, maxCorpses)
		if looted == 0 then
			player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		elseif looted > 1 then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You looted " .. looted .. " corpses.")
		end
		sendLootContainers(player)
		return
	end

	-- variant 0: single item from a specific position
	local msgLen = msg:len()
	local msgTell = msg:tell()
	if msgLen - msgTell < 3 then
		return
	end

	local itemId = msg:getU16()
	local stackPos = msg:getByte()

	if pos.x == 0xFFFF then
		local container = player:getContainerById(pos.y)
		if container then
			local item = container:getItem(stackPos - 1)
			if item and shouldLootItem(player, item) then
				local category = getItemCategory(item)
				local dest = findDestinationContainer(player, category)
				if dest then
					item:moveTo(dest)
					sendLootContainers(player)
				end
			end
		end
		return
	end

	local tile = Tile(pos)
	if not tile then
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		return
	end

	local tileItems = tile:getItems()
	if not tileItems or #tileItems == 0 then
		return
	end

	local thing = nil
	for i, tileItem in ipairs(tileItems) do
		if i - 1 == stackPos and tileItem:getId() == itemId then
			thing = tileItem
			break
		end
	end

	if not thing then
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		return
	end

	if thing:isContainer() then
		local corpse = thing:getContainer()
		if corpse and canPlayerLootCorpse(player, corpse) then
			lootCorpse(player, corpse)
			sendLootContainers(player)
		end
	elseif shouldLootItem(player, thing) then
		local category = getItemCategory(thing)
		local dest = findDestinationContainer(player, category)
		if dest then
			thing:moveTo(dest)
			sendLootContainers(player)
		end
	end
end
quickLootHandler:register()

-- PacketHandler: 0x90 - Loot Container management
local lootContainerHandler = PacketHandler(OPCODE_LOOT_CONTAINER)
function lootContainerHandler.onReceive(player, msg)
	if not supportsCustomNetwork(player) then
		return
	end
	local action = msg:getByte()

	if action == 0 or action == 4 then
		local msgLen = msg:len()
		local msgTell = msg:tell()
		if msgLen - msgTell < 9 then
			sendLootContainers(player)
			return
		end
		local category = msg:getByte()
		local containerPos = msg:getPosition()
		local itemId = msg:getU16()
		local stackPos = msg:getByte()

		local isLootContainer = (action == 0)
		setManagedContainer(player, category, itemId, isLootContainer)
		sendLootContainers(player)

	elseif action == 1 or action == 2 or action == 5 or action == 6 then
		local msgLen = msg:len()
		local msgTell = msg:tell()
		if msgLen - msgTell < 1 then
			sendLootContainers(player)
			return
		end
		local category = msg:getByte()
		local isLootContainer = (action == 1 or action == 2)
		clearManagedContainer(player, category, isLootContainer)
		sendLootContainers(player)

	elseif action == 3 then
		local msgLen = msg:len()
		local msgTell = msg:tell()
		if msgLen - msgTell < 1 then
			sendLootContainers(player)
			return
		end
		local fallback = msg:getByte() == 1
		local state = getPlayerState(player)
		state.fallback = fallback
		sendLootContainers(player)
	end
end
lootContainerHandler:register()

-- PacketHandler: 0x91 - QuickLoot Black/Whitelist
local blackWhitelistHandler = PacketHandler(OPCODE_BLACK_WHITELIST)
function blackWhitelistHandler.onReceive(player, msg)
	if not supportsCustomNetwork(player) then
		return
	end
	local filterByte = msg:getByte()

	if filterByte ~= QUICKLOOTFILTER_SKIPPEDLOOT and filterByte ~= QUICKLOOTFILTER_ACCEPTEDLOOT then
		return
	end

	local size = msg:getU16()
	if size > 4096 then
		return
	end

	local itemIds = {}
	for i = 1, size do
		local id = msg:getU16()
		if id ~= 0 then
			table.insert(itemIds, id)
		end
	end

	local state = getPlayerState(player)
	state.filter = filterByte
	state.itemIds = itemIds
	sendLootContainers(player)
end
blackWhitelistHandler:register()

-- Login handler: restore state from KV and send loot container state
local loginEvent = CreatureEvent("QuickLootLogin")
function loginEvent.onLogin(player)
	local pid = player:getId()
	managedContainers[pid] = managedContainers[pid] or {}
	quickLootState[pid] = quickLootState[pid] or {
		filter = QUICKLOOTFILTER_SKIPPEDLOOT,
		itemIds = {},
		fallback = true,
	}

	local store = player:kv():scoped("quickloot")
	local savedContainers = store:get("managedContainers")
	if savedContainers then
		local restored = {}
		for catStr, containers in pairs(savedContainers) do
			restored[tonumber(catStr)] = containers
		end
		managedContainers[pid] = restored
	end

	local state = quickLootState[pid]
	local savedFilter = store:get("filter")
	if savedFilter ~= nil then
		state.filter = savedFilter
	end

	local savedItemIds = store:get("itemIds")
	if savedItemIds then
		state.itemIds = savedItemIds
	end

	local savedFallback = store:get("fallback")
	if savedFallback ~= nil then
		state.fallback = savedFallback
	end

	player:registerEvent("QuickLootLogout")

	local settings = player:kv():scoped("settings")
	local autoLootEnabled = settings:get("quickLoot")
	if autoLootEnabled then
		player:registerEvent("QuickLootKill")
	else
	end

	local mcCount = 0
	for _ in pairs(managedContainers[pid] or {}) do mcCount = mcCount + 1 end
	sendLootContainers(player)
	return true
end
loginEvent:register()

-- Logout handler: persist state to KV and clean up
local logoutEvent = CreatureEvent("QuickLootLogout")
function logoutEvent.onLogout(player)
	local pid = player:getId()
	local state = quickLootState[pid]

	if state then
		local store = player:kv():scoped("quickloot")
		local serializedContainers = {}
		local mcCount = 0
		for category, containers in pairs(managedContainers[pid] or {}) do
			serializedContainers[tostring(category)] = containers
			mcCount = mcCount + 1
		end
		store:set("managedContainers", serializedContainers)
		store:set("filter", state.filter)
		store:set("itemIds", state.itemIds)
		store:set("fallback", state.fallback)
	end

	managedContainers[pid] = nil
	quickLootState[pid] = nil
	lastNotification[pid] = nil
	return true
end
logoutEvent:register()
