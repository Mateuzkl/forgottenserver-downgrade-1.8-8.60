ImbuingWindow = ImbuingWindow or {}

local WINDOW_OPCODE = 0xEB
local CLOSE_OPCODE = 0xEC
local RESOURCE_BALANCE_OPCODE = 0xEE
local USE_RANGE = 1

local SUCCESS_RATES = {
	[1] = 90,
	[2] = 70,
	[3] = 50,
}

local PROTECTION_COSTS = {
	[1] = 10000,
	[2] = 30000,
	[3] = 50000,
}

local sessions = {}
local cachedDefinitions = nil
local definitionsById = {}
local definitionsByTypeBase = {}

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function copyPosition(position)
	if not position then
		return nil
	end
	return Position(position.x, position.y, position.z)
end

local function getThingPosition(thing)
	if not thing or not thing.getPosition then
		return nil
	end

	local ok, position = pcall(function()
		return thing:getPosition()
	end)
	if ok then
		return copyPosition(position)
	end
	return nil
end

local function getThingInstanceId(thing)
	if not thing or not thing.getInstanceId then
		return 0
	end

	local ok, instanceId = pcall(function()
		return thing:getInstanceId()
	end)
	if ok then
		return tonumber(instanceId) or 0
	end
	return 0
end

local function getPlayerPosition(player)
	return player and copyPosition(player:getPosition()) or nil
end

local function setAccessContext(session, player, sourceThing, sourcePosition)
	session.sourcePosition = copyPosition(sourcePosition) or getThingPosition(sourceThing) or getPlayerPosition(player)
	session.sourceInstanceId = sourceThing and getThingInstanceId(sourceThing) or (player and player:getInstanceId() or 0)
end

local function isSessionInRange(player, session)
	if not player or not session or not session.sourcePosition then
		return true
	end

	if player:getInstanceId() ~= (session.sourceInstanceId or 0) then
		return false
	end
	return player:getPosition():getDistance(session.sourcePosition) <= USE_RANGE
end

local function ensureSessionInRange(player)
	local session = player and sessions[player:getId()] or nil
	if not session or isSessionInRange(player, session) then
		return true
	end

	ImbuingWindow.sendClose(player)
	return false
end

local IMBUEMENT_BASE_NAMES = {
	[1] = "Basic",
	[2] = "Intricate",
	[3] = "Powerful",
}

local IMBUEMENT_TYPE_NAMES = {
	[IMBUEMENT_TYPE_SWORD_SKILL] = "Slash",
	[IMBUEMENT_TYPE_AXE_SKILL] = "Chop",
	[IMBUEMENT_TYPE_CLUB_SKILL] = "Bash",
	[IMBUEMENT_TYPE_DISTANCE_SKILL] = "Precision",
	[IMBUEMENT_TYPE_SHIELD_SKILL] = "Blockade",
	[IMBUEMENT_TYPE_FIST_SKILL] = "Punch",
	[IMBUEMENT_TYPE_FISHING_SKILL] = "Fish",
	[IMBUEMENT_TYPE_MAGIC_LEVEL] = "Epiphany",
	[IMBUEMENT_TYPE_LIFE_LEECH] = "Vampirism",
	[IMBUEMENT_TYPE_MANA_LEECH] = "Void",
	[IMBUEMENT_TYPE_CRITICAL_CHANCE] = "Strike",
	[IMBUEMENT_TYPE_CRITICAL_AMOUNT] = "Strike",
	[IMBUEMENT_TYPE_FIRE_DAMAGE] = "Scorch",
	[IMBUEMENT_TYPE_EARTH_DAMAGE] = "Venom",
	[IMBUEMENT_TYPE_ICE_DAMAGE] = "Frost",
	[IMBUEMENT_TYPE_ENERGY_DAMAGE] = "Electrify",
	[IMBUEMENT_TYPE_DEATH_DAMAGE] = "Reap",
	[IMBUEMENT_TYPE_HOLY_DAMAGE] = "Divine",
	[IMBUEMENT_TYPE_FIRE_RESIST] = "Dragon Hide",
	[IMBUEMENT_TYPE_EARTH_RESIST] = "Snake Skin",
	[IMBUEMENT_TYPE_ICE_RESIST] = "Quara Scale",
	[IMBUEMENT_TYPE_ENERGY_RESIST] = "Cloud Fabric",
	[IMBUEMENT_TYPE_DEATH_RESIST] = "Lich Shroud",
	[IMBUEMENT_TYPE_HOLY_RESIST] = "Demon Presence",
	[IMBUEMENT_TYPE_PARALYSIS_DEFLECTION] = "Vibrancy",
	[IMBUEMENT_TYPE_SPEED_BOOST] = "Swiftness",
	[IMBUEMENT_TYPE_CAPACITY_BOOST] = "Featherweight",
}

local function isEnabled()
	return configManager.getBoolean(configKeys.IMBUEMENT_SYSTEM_ENABLED)
end

local function protocolId(def)
	return (def.imbuementType * 100) + def.baseId
end

local function definitionKey(imbuementType, baseId)
	return string.format("%d:%d", tonumber(imbuementType) or 0, tonumber(baseId) or 0)
end

local function ensureDefinitions()
	if cachedDefinitions then
		return
	end

	cachedDefinitions = Game.getImbuementDefinitions() or {}
	table.sort(cachedDefinitions, function(a, b)
		if a.name ~= b.name then
			return a.name < b.name
		end
		return a.baseId < b.baseId
	end)

	for _, def in ipairs(cachedDefinitions) do
		local id = protocolId(def)
		definitionsById[id] = def
		definitionsByTypeBase[definitionKey(def.imbuementType, def.baseId)] = def
	end
end

local function getDefinitionByImbuement(imbuement)
	if not imbuement then
		return nil
	end

	ensureDefinitions()
	return definitionsByTypeBase[definitionKey(imbuement:getType(), imbuement:getBaseId())]
end

local function displayName(def)
	if def.baseName and def.baseName ~= "" then
		return def.baseName .. " " .. def.name
	end
	return def.name
end

local function getSuccessRate(defOrImbuement)
	local baseId = defOrImbuement and tonumber(defOrImbuement.baseId)
	if not baseId and defOrImbuement and defOrImbuement.getBaseId then
		baseId = tonumber(defOrImbuement:getBaseId())
	end
	return SUCCESS_RATES[baseId] or 90
end

local function getProtectionCost(defOrImbuement)
	local baseId = defOrImbuement and tonumber(defOrImbuement.baseId)
	if not baseId and defOrImbuement and defOrImbuement.getBaseId then
		baseId = tonumber(defOrImbuement:getBaseId())
	end
	return PROTECTION_COSTS[baseId] or PROTECTION_COSTS[1]
end

local function fallbackImbuementName(imbuement)
	local imbuementType = imbuement and tonumber(imbuement:getType()) or nil
	local baseId = imbuement and tonumber(imbuement:getBaseId()) or nil
	local name = IMBUEMENT_TYPE_NAMES[imbuementType] or "Unknown Imbuement"
	local baseName = IMBUEMENT_BASE_NAMES[baseId] or ""

	if baseName ~= "" and name ~= "Unknown Imbuement" then
		return baseName .. " " .. name, name
	end
	return name, name
end

local itemNameCache = {}

local function getItemName(itemId)
	local cachedName = itemNameCache[itemId]
	if cachedName then
		return cachedName
	end

	local ok, itemType = pcall(ItemType, itemId)
	local name = "item"
	if ok and itemType then
		name = itemType:getName()
		if not name or name == "" then
			name = "item"
		end
	end

	itemNameCache[itemId] = name
	return name
end

local function writeImbuementInfo(msg, def)
	msg:addU32(protocolId(def))
	msg:addString(displayName(def))
	msg:addString(def.description or "")
	msg:addString(def.name or "")
	msg:addU16(def.iconId or 0)
	msg:addU32(def.duration or 0)
	msg:addByte(def.premium and 1 or 0)

	local items = def.items or {}
	msg:addByte(#items)
	for _, req in ipairs(items) do
		msg:addU16(req.itemId)
		msg:addString(string.format("%dx %s", req.count, getItemName(req.itemId)))
		msg:addU16(req.count)
	end

	msg:addU32(def.price or 0)
	msg:addByte(getSuccessRate(def))
	msg:addU32(getProtectionCost(def))
end

local function writeFallbackImbuementInfo(msg, imbuement)
	msg:addU32(0)
	local name, group = fallbackImbuementName(imbuement)
	msg:addString(name)
	msg:addString("")
	msg:addString(group)
	msg:addU16(0)
	msg:addU32(imbuement and imbuement:getDuration() or 0)
	msg:addByte(0)
	msg:addByte(0)
	msg:addU32(0)
	msg:addByte(getSuccessRate(imbuement))
	msg:addU32(getProtectionCost(imbuement))
end

local function getActiveImbuements(item)
	local imbuements = item:getImbuements()
	if not imbuements then
		return {}
	end
	return imbuements
end

local function getApplicableDefinitions(item)
	ensureDefinitions()

	local activeTypes = {}
	for _, imbuement in ipairs(getActiveImbuements(item)) do
		activeTypes[imbuement:getType()] = true
	end

	local list = {}
	for _, def in ipairs(cachedDefinitions) do
		if not activeTypes[def.imbuementType] and item:canApplyImbuement(def.categoryId, def.baseId) then
			table.insert(list, def)
		end
	end
	return list
end

local function writeNeededItems(msg, player, definitions)
	local itemIds = {}
	local seen = {}

	for _, def in ipairs(definitions) do
		for _, req in ipairs(def.items or {}) do
			if not seen[req.itemId] then
				seen[req.itemId] = true
				table.insert(itemIds, req.itemId)
			end
		end
	end

	table.sort(itemIds)
	msg:addU32(#itemIds)
	for _, itemId in ipairs(itemIds) do
		msg:addU16(itemId)
		msg:addU16(math.min(player:getItemCount(itemId, -1, true), 0xFFFF))
	end
end

local function sendBalances(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local msg = NetworkMessage(player)
	msg:addByte(RESOURCE_BALANCE_OPCODE)
	msg:addByte(0)
	msg:addU64(player:getBankBalance())
	msg:addByte(RESOURCE_BALANCE_OPCODE)
	msg:addByte(1)
	msg:addU64(player:getMoney())
	return msg:sendToPlayer(player)
end

local function findEquipment(container)
	for i = 0, container:getSize() - 1 do
		local item = container:getItem(i)
		if item and item:getImbuementSlots() > 0 then
			return item
		end
	end
	return nil
end

local function clearSession(player)
	sessions[player:getId()] = nil
end

local function itemBelongsToPlayer(player, item)
	return item and item:getTopParent() == player
end

local function containerHasItem(container, target)
	if not container or not target then
		return false
	end

	for i = 0, container:getSize() - 1 do
		local item = container:getItem(i)
		if item == target then
			return true
		end
	end
	return false
end

local function itemIsInPlayerBackpack(player, item)
	if not itemBelongsToPlayer(player, item) then
		return false
	end

	local backpack = player:getSlotItem(CONST_SLOT_BACKPACK)
	local container = backpack and backpack:getContainer()
	return container and containerHasItem(container, item)
end

local function validateSessionItem(player, item)
	if not item or item:getImbuementSlots() <= 0 then
		clearSession(player)
		return false
	end

	local owner = item:getAttribute(ITEM_ATTRIBUTE_OWNER)
	if owner and owner ~= 0 and owner ~= player:getId() then
		clearSession(player)
		return false
	end

	return true
end

local function getSessionEquipment(player)
	local session = sessions[player:getId()]
	if not session then
		return nil
	end

	local item = session.item
	if not validateSessionItem(player, item) then
		return nil
	end

	if session.container then
		local container = session.container
		if not container then
			clearSession(player)
			return nil
		end

		local ok, size = pcall(function()
			return container:getSize()
		end)
		if not ok then
			clearSession(player)
			return nil
		end

		local found = false
		for i = 0, size - 1 do
			if container:getItem(i) == item then
				found = true
				break
			end
		end
		if not found then
			clearSession(player)
			return nil
		end
	elseif session.backpackOnly then
		if not itemIsInPlayerBackpack(player, item) then
			clearSession(player)
			return nil
		end
	elseif not itemBelongsToPlayer(player, item) then
		clearSession(player)
		return nil
	end

	return item
end

local function sendWindow(player, item)
	if not supportsCustomNetwork(player) then
		return false
	end

	local slots = item:getImbuementSlots()
	if slots <= 0 then
		return false
	end

	local definitions = getApplicableDefinitions(item)
	sendBalances(player)

	local msg = NetworkMessage(player)
	msg:addByte(WINDOW_OPCODE)
	msg:addU16(item:getId())
	msg:addByte(math.min(slots, 3))

	local activeImbuements = getActiveImbuements(item)
	for slot = 1, math.min(slots, 3) do
		local imbuement = activeImbuements[slot]
		if imbuement then
			local def = getDefinitionByImbuement(imbuement)
			msg:addByte(1)
			if def then
				writeImbuementInfo(msg, def)
				msg:addU32(imbuement:getDuration())
				msg:addU32(def.removeCost or 0)
			else
				writeFallbackImbuementInfo(msg, imbuement)
				msg:addU32(imbuement:getDuration())
				msg:addU32(0)
			end
		else
			msg:addByte(0)
		end
	end

	msg:addU16(#definitions)
	for _, def in ipairs(definitions) do
		writeImbuementInfo(msg, def)
	end

	writeNeededItems(msg, player, definitions)
	msg:sendToPlayer(player)
	return true
end

function ImbuingWindow.open(player, container, silent)
	if not supportsCustomNetwork(player) then
		if not silent then
			player:sendCancelMessage("The imbuing window is only available on OTClient.")
		end
		return false
	end

	if not isEnabled() then
		if not silent then
			player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		end
		return false
	end

	local item = findEquipment(container)
	if not item then
		if not silent then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "Place equipment with imbuement slots on the workbench.")
		end
		return false
	end

	local owner = item:getAttribute(ITEM_ATTRIBUTE_OWNER)
	if owner and owner ~= 0 and owner ~= player:getId() then
		player:sendCancelMessage("This equipment does not belong to you.")
		return true
	end

	local session = {container = container, item = item}
	setAccessContext(session, player, container)
	sessions[player:getId()] = session
	return sendWindow(player, item)
end

function ImbuingWindow.openItem(player, item, silent, sourcePosition, sourceThing)
	if not supportsCustomNetwork(player) then
		if not silent then
			player:sendCancelMessage("The imbuing window is only available on OTClient.")
		end
		return false
	end

	if not isEnabled() then
		if not silent then
			player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		end
		return false
	end

	if not item or item:getImbuementSlots() <= 0 then
		if not silent then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "Use an item with imbuement slots.")
		end
		return false
	end

	if not itemIsInPlayerBackpack(player, item) then
		if not silent then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "Use an item from your backpack.")
		end
		return false
	end

	local owner = item:getAttribute(ITEM_ATTRIBUTE_OWNER)
	if owner and owner ~= 0 and owner ~= player:getId() then
		player:sendCancelMessage("This equipment does not belong to you.")
		return true
	end

	local session = {item = item, backpackOnly = true}
	setAccessContext(session, player, sourceThing, sourcePosition)
	sessions[player:getId()] = session
	return sendWindow(player, item)
end

function ImbuingWindow.close(player)
	clearSession(player)
end

function ImbuingWindow.onStepTile(player)
	ensureSessionInRange(player)
end

function ImbuingWindow.sendClose(player)
	if not supportsCustomNetwork(player) then
		ImbuingWindow.close(player)
		return false
	end

	local msg = NetworkMessage(player)
	msg:addByte(CLOSE_OPCODE)
	local sent = msg:sendToPlayer(player)
	ImbuingWindow.close(player)
	return sent
end

function ImbuingWindow.apply(player, slot, imbuementId, protection)
	if not isEnabled() then
		return
	end

	if not ensureSessionInRange(player) then
		return
	end

	local item = getSessionEquipment(player)
	if not item then
		ImbuingWindow.sendClose(player)
		return
	end

	ensureDefinitions()
	local def = definitionsById[imbuementId]
	if not def then
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		sendWindow(player, item)
		return
	end

	local slots = item:getImbuementSlots()
	local activeImbuements = getActiveImbuements(item)
	local firstFreeSlot = #activeImbuements
	if slot < 0 or slot >= slots or slot ~= firstFreeSlot then
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		sendWindow(player, item)
		return
	end

	if item:hasImbuementType(def.imbuementType) or not item:canApplyImbuement(def.categoryId, def.baseId) then
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		sendWindow(player, item)
		return
	end

	for _, req in ipairs(def.items or {}) do
		if player:getItemCount(req.itemId, -1, true) < req.count then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have all required astral sources.")
			sendWindow(player, item)
			return
		end
	end

	local cost = def.price or 0
	if protection then
		cost = cost + getProtectionCost(def)
	end
	if cost > 0 and player:getMoney() + player:getBankBalance() < cost then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have enough gold.")
		sendWindow(player, item)
		return
	end

	local success = protection or math.random(100) <= getSuccessRate(def)
	if not success then
		for _, req in ipairs(def.items or {}) do
			player:removeItem(req.itemId, req.count, -1, true)
		end
		if cost > 0 then
			player:removeTotalMoney(cost)
		end

		player:sendTextMessage(MESSAGE_STATUS_SMALL, "Imbuement failed.")
		sendWindow(player, item)
		return
	end

	local imbuement = Imbuement(def.imbuementType, def.value, def.duration, def.decayType, def.baseId)
	if not item:addImbuement(imbuement) then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "Failed to apply imbuement. Unequip the item first.")
		sendWindow(player, item)
		return
	end

	for _, req in ipairs(def.items or {}) do
		player:removeItem(req.itemId, req.count, -1, true)
	end
	if cost > 0 then
		player:removeTotalMoney(cost)
	end

	player:sendTextMessage(MESSAGE_STATUS_SMALL, "Imbuement applied.")
	sendWindow(player, item)
end

function ImbuingWindow.clear(player, slot)
	if not isEnabled() then
		return
	end

	if not ensureSessionInRange(player) then
		return
	end

	local item = getSessionEquipment(player)
	if not item then
		ImbuingWindow.sendClose(player)
		return
	end

	local imbuement = getActiveImbuements(item)[slot + 1]
	if not imbuement then
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		sendWindow(player, item)
		return
	end

	local def = getDefinitionByImbuement(imbuement)
	local cost = def and def.removeCost or 0
	if cost > 0 and player:getMoney() + player:getBankBalance() < cost then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have enough gold.")
		sendWindow(player, item)
		return
	end

	if not item:removeImbuement(imbuement) then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "Failed to clear imbuement.")
		sendWindow(player, item)
		return
	end

	if cost > 0 then
		player:removeTotalMoney(cost)
	end

	player:sendTextMessage(MESSAGE_STATUS_SMALL, "Imbuement cleared.")
	sendWindow(player, item)
end
