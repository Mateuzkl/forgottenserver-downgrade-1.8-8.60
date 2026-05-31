local proficiencySystemConfigKey = configKeys and configKeys.WEAPON_PROFICIENCY_SYSTEM_ENABLED or WEAPON_PROFICIENCY_SYSTEM_ENABLED
if configManager and proficiencySystemConfigKey and not configManager.getBoolean(proficiencySystemConfigKey) then
	WeaponProficiencySystem = nil
	return
end

WeaponProficiencySystem = WeaponProficiencySystem or {}

local System = WeaponProficiencySystem
local augmentSystemConfigKey = configKeys and configKeys.AUGMENT_SYSTEM_ENABLED or AUGMENT_SYSTEM_ENABLED

local function isAugmentSystemEnabled()
	return configManager and augmentSystemConfigKey and configManager.getBoolean(augmentSystemConfigKey) or false
end

local OPCODE_REQUEST = 0xB3
local OPCODE_CATALOG = 0x5A
local OPCODE_EXPERIENCE = 0x5C
local OPCODE_INFO = 0xC4
local OPCODE_INFO_BATCH = 0x5B

local ACTION_ITEM_INFO = 0
local ACTION_LIST_INFO = 1
local ACTION_RESET_PERKS = 2
local ACTION_APPLY_PERKS = 3

local MAX_PERK_LEVEL = 7
local MAX_PERK_POSITION = 2
local EXPERIENCE_GAIN_MULTIPLIER = 0.01
local SAVE_DELAY_MS = 5000
local LIST_INFO_COOLDOWN_MS = 1000

-- The first MAX_PERK_LEVEL thresholds unlock perk slots. The remaining
-- thresholds keep mastery progression active until the final experience cap.
local EXPERIENCE_TABLES = {
	regular = { 1750, 25000, 100000, 400000, 2000000, 8000000, 30000000, 60000000, 90000000 },
	knight = { 1250, 20000, 80000, 300000, 1500000, 6000000, 20000000, 40000000, 60000000 },
	crossbow = { 600, 8000, 30000, 150000, 650000, 2500000, 10000000, 20000000, 30000000 },
}

local WEAPON_CATALOG = dofile(DATA_DIRECTORY .. "/scripts/network/proficiency/weapon_catalog.lua")
local playerCache = {}
local catalogEntries
local catalogByServerId = {}
local serverIdByClientId = {}
local proficiencyTableReady = false
local proficiencyDefinitionsById = {}
local refreshProfileSpellAugments

local function logError(message)
	if logger and logger.error then
		logger.error(message)
	else
		print(message)
	end
end

local function loadProficiencyDefinitions()
	if not isAugmentSystemEnabled() then
		return
	end

	local file = io.open(DATA_DIRECTORY .. "/items/proficiencies.json", "r")
	if not file then
		logError("[WeaponProficiency] Failed to open data/items/proficiencies.json.")
		return
	end

	local content = file:read("*a")
	file:close()

	local ok, definitions = pcall(json.decode, content)
	if not ok or type(definitions) ~= "table" then
		logError("[WeaponProficiency] Failed to decode data/items/proficiencies.json.")
		return
	end

	for _, definition in ipairs(definitions) do
		local proficiencyId = tonumber(definition.ProficiencyId)
		if proficiencyId then
			proficiencyDefinitionsById[proficiencyId] = definition
		end
	end
end

loadProficiencyDefinitions()

local function ensureTables()
	if proficiencyTableReady then
		return true
	end

	local ok, success = pcall(db.query, [[
		CREATE TABLE IF NOT EXISTS `player_weapon_proficiency` (
			`player_id` int NOT NULL,
			`item_id` smallint unsigned NOT NULL,
			`experience` int unsigned NOT NULL DEFAULT '0',
			`perks` varchar(64) NOT NULL DEFAULT '',
			PRIMARY KEY (`player_id`, `item_id`),
			FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;
	]])
	if not ok or not success then
		logError("[WeaponProficiency] Failed to create player_weapon_proficiency table.")
		return false
	end

	proficiencyTableReady = true
	return true
end

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function getItemType(itemId)
	local itemType = ItemType(itemId)
	if not itemType or itemType:getId() == 0 then
		return nil
	end
	return itemType
end

local function isValidWeaponId(itemId)
	itemId = tonumber(itemId)
	if not itemId or itemId <= 0 or itemId > 0xFFFF or itemId % 1 ~= 0 then
		return false
	end

	local itemType = getItemType(itemId)
	return itemType and itemType:getWeaponType() ~= WEAPON_NONE or false
end

local function ensureCatalog()
	if catalogEntries then
		return
	end

	catalogEntries = {}
	local serverIds = {}
	for serverId in pairs(WEAPON_CATALOG) do
		serverIds[#serverIds + 1] = serverId
	end
	table.sort(serverIds)

	for _, serverId in ipairs(serverIds) do
		if isValidWeaponId(serverId) then
			local itemType = getItemType(serverId)
			local clientId = itemType:getClientId()
			if clientId and clientId > 0 and clientId <= 0xFFFF then
				local entry = catalogByServerId[serverIdByClientId[clientId]]
				if not entry then
					entry = {
						serverId = serverId,
						clientId = clientId,
						category = WEAPON_CATALOG[serverId],
						name = itemType:getName(),
					}
					catalogEntries[#catalogEntries + 1] = entry
					serverIdByClientId[clientId] = serverId
				end
				catalogByServerId[serverId] = entry
			end
		end
	end

	table.sort(catalogEntries, function(left, right)
		return left.clientId < right.clientId
	end)
end

local function resolveServerId(clientId)
	ensureCatalog()
	return serverIdByClientId[tonumber(clientId) or 0]
end

local function canonicalizeServerId(serverId)
	ensureCatalog()
	local entry = catalogByServerId[tonumber(serverId) or 0]
	return entry and entry.serverId or nil
end

local function getCatalogEntry(serverId)
	ensureCatalog()
	return catalogByServerId[serverId]
end

local function getExperienceTable(itemId)
	local itemType = getItemType(itemId)
	if not itemType then
		return EXPERIENCE_TABLES.regular
	end

	local name = itemType:getName():lower()
	if name:find("crossbow", 1, true) then
		return EXPERIENCE_TABLES.crossbow
	end

	local weaponType = itemType:getWeaponType()
	if weaponType == WEAPON_SWORD or weaponType == WEAPON_AXE or weaponType == WEAPON_CLUB then
		return EXPERIENCE_TABLES.knight
	end

	return EXPERIENCE_TABLES.regular
end

local function getUnlockedLevelCount(itemId, experience)
	-- Stored and network perk levels are zero-based, while this count
	-- represents how many perk slots are currently available.
	local count = 0
	local experienceTable = getExperienceTable(itemId)
	for level = 1, MAX_PERK_LEVEL do
		if experience >= experienceTable[level] then
			count = level
		end
	end
	return count
end

local function hasUnusedPerk(itemId, state)
	local unlocked = getUnlockedLevelCount(itemId, state.experience)
	local selected = 0
	for level in pairs(state.perks) do
		if level < unlocked then
			selected = selected + 1
		end
	end
	return selected < unlocked
end

local function encodePerks(perks)
	local levels = {}
	for level in pairs(perks) do
		levels[#levels + 1] = level
	end
	table.sort(levels)

	local encoded = {}
	for _, level in ipairs(levels) do
		encoded[#encoded + 1] = level .. ":" .. perks[level]
	end
	return table.concat(encoded, ",")
end

local function decodePerks(encoded)
	local perks = {}
	for entry in tostring(encoded or ""):gmatch("[^,]+") do
		local level, position = entry:match("^(%d+):(%d+)$")
		level = tonumber(level)
		position = tonumber(position)
		if level and position and level >= 0 and level < MAX_PERK_LEVEL and position >= 0 and position <= MAX_PERK_POSITION then
			perks[level] = position
		end
	end
	return perks
end

local supportsAliasedUpsert

local function canUseAliasedUpsert()
	if supportsAliasedUpsert ~= nil then
		return supportsAliasedUpsert
	end

	supportsAliasedUpsert = false
	local resultId = db.storeQuery("SELECT VERSION() AS `version`")
	if not resultId then
		return false
	end

	local version = result.getString(resultId, "version")
	result.free(resultId)
	if version:lower():find("mariadb", 1, true) then
		return false
	end

	local major, minor, patch = version:match("^(%d+)%.(%d+)%.(%d+)")
	major, minor, patch = tonumber(major), tonumber(minor), tonumber(patch)
	supportsAliasedUpsert = major and minor and patch and
		(major > 8 or (major == 8 and (minor > 0 or patch >= 20))) or false
	return supportsAliasedUpsert
end

local function saveState(guid, itemId, state)
	if not ensureTables() then
		return
	end

	local upsertClause = "ON DUPLICATE KEY UPDATE `experience` = VALUES(`experience`), `perks` = VALUES(`perks`)"
	if canUseAliasedUpsert() then
		upsertClause = "AS new ON DUPLICATE KEY UPDATE `experience` = new.`experience`, `perks` = new.`perks`"
	end

	db.asyncQuery(string.format(
		"INSERT INTO `player_weapon_proficiency` (`player_id`, `item_id`, `experience`, `perks`) VALUES (%d, %d, %d, %s) " ..
		upsertClause,
		guid, itemId, state.experience, db.escapeString(encodePerks(state.perks))
	))
end

local function loadProfile(player)
	local guid = player:getGuid()
	local cached = playerCache[guid]
	if cached then
		return cached
	end

	local profile = { weapons = {}, dirty = {}, catalogSent = false }
	if ensureTables() then
		local resultId = db.storeQuery(
			"SELECT `item_id`, `experience`, `perks` FROM `player_weapon_proficiency` WHERE `player_id` = " .. guid
		)
		if resultId then
			repeat
				local itemId = result.getDataInt(resultId, "item_id")
				local canonicalId = canonicalizeServerId(itemId)
				if canonicalId then
					profile.weapons[canonicalId] = {
						experience = math.max(0, result.getDataInt(resultId, "experience")),
						perks = decodePerks(result.getDataString(resultId, "perks")),
					}
				end
			until not result.next(resultId)
			result.free(resultId)
		end
	end

	playerCache[guid] = profile
	player:registerEvent("WeaponProficiencyLogout")
	if refreshProfileSpellAugments then
		refreshProfileSpellAugments(player, profile)
	end
	return profile
end

local function flushProfile(guid)
	local profile = playerCache[guid]
	if not profile then
		return
	end

	profile.saveEvent = nil
	for itemId in pairs(profile.dirty) do
		local state = profile.weapons[itemId]
		if state then
			saveState(guid, itemId, state)
		end
	end
	profile.dirty = {}
end

local function queueSave(player, itemId)
	local profile = loadProfile(player)
	profile.dirty[itemId] = true
	if not profile.saveEvent then
		profile.saveEvent = addEvent(flushProfile, SAVE_DELAY_MS, player:getGuid())
	end
end

local function getState(player, itemId)
	local profile = loadProfile(player)
	if not profile.weapons[itemId] then
		profile.weapons[itemId] = { experience = 0, perks = {} }
	end
	return profile.weapons[itemId]
end

refreshProfileSpellAugments = function(player, profile)
	if not player.clearProficiencySpellAugments or not player.addProficiencySpellAugment then
		return
	end

	player:clearProficiencySpellAugments()
	if not isAugmentSystemEnabled() then
		return
	end

	profile = profile or playerCache[player:getGuid()]
	if not profile then
		return
	end

	for itemId, state in pairs(profile.weapons) do
		local entry = getCatalogEntry(itemId)
		local definition = entry and proficiencyDefinitionsById[entry.category]
		if definition and type(definition.Levels) == "table" then
			for level, position in pairs(state.perks) do
				local levelData = definition.Levels[level + 1]
				local perk = levelData and levelData.Perks and levelData.Perks[position + 1]
				if perk and tonumber(perk.Type) == 5 then
					local spellId = tonumber(perk.SpellId)
					local augmentType = tonumber(perk.AugmentType)
					local value = tonumber(perk.Value)
					if spellId and augmentType and value then
						player:addProficiencySpellAugment(itemId, spellId, augmentType, value)
					end
				end
			end
		end
	end
end

local function getEquippedWeaponId(player)
	if player.getWeaponProficiencyId then
		local itemId = canonicalizeServerId(player:getWeaponProficiencyId())
		if itemId then
			return itemId
		end
	end

	for _, slot in ipairs({ CONST_SLOT_LEFT, CONST_SLOT_RIGHT }) do
		local item = player:getSlotItem(slot)
		local itemId = item and canonicalizeServerId(item:getId())
		if itemId then
			return itemId
		end
	end
	return 0
end

local function writeInfoPayload(out, entry, state)
	local levels = {}
	for level in pairs(state.perks) do
		levels[#levels + 1] = level
	end
	table.sort(levels)

	out:addU16(entry.clientId)
	out:addU32(state.experience)
	out:addByte(math.min(#levels, 0xFF))
	for index = 1, math.min(#levels, 0xFF) do
		local level = levels[index]
		out:addByte(level)
		out:addByte(state.perks[level])
	end
	out:addU16(entry.category)
end

local function sendInfo(player, itemId)
	local entry = getCatalogEntry(itemId)
	if not supportsCustomNetwork(player) or not entry then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_INFO)
	writeInfoPayload(out, entry, getState(player, itemId))
	return out:sendToPlayer(player)
end

local function sendExperience(player, itemId)
	local entry = getCatalogEntry(itemId)
	if not supportsCustomNetwork(player) or not entry then
		return false
	end

	local state = getState(player, itemId)
	local out = NetworkMessage(player)
	out:addByte(OPCODE_EXPERIENCE)
	out:addU16(entry.clientId)
	out:addU32(state.experience)
	out:addByte(hasUnusedPerk(itemId, state) and 1 or 0)
	return out:sendToPlayer(player)
end

local function sendCatalog(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	ensureCatalog()
	local count = math.min(#catalogEntries, 0xFFFF)
	local out = NetworkMessage(player)
	out:addByte(OPCODE_CATALOG)
	out:addU16(count)
	for index = 1, count do
		local entry = catalogEntries[index]
		out:addU16(entry.clientId)
		out:addU16(entry.category)
		out:addString(entry.name)
	end
	return out:sendToPlayer(player)
end

local function sendAllInfo(player, itemIds)
	if not supportsCustomNetwork(player) then
		return false
	end

	local entries = {}
	for index = 1, math.min(#itemIds, 0xFFFF) do
		local itemId = itemIds[index]
		local entry = getCatalogEntry(itemId)
		if entry then
			entries[#entries + 1] = { itemId = itemId, entry = entry }
		end
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_INFO_BATCH)
	out:addU16(#entries)
	for _, info in ipairs(entries) do
		writeInfoPayload(out, info.entry, getState(player, info.itemId))
	end
	return out:sendToPlayer(player)
end

local function sendAll(player)
	local profile = loadProfile(player)
	if profile.catalogSent ~= true and sendCatalog(player) then
		profile.catalogSent = true
	end

	local itemIds = {}
	for itemId in pairs(profile.weapons) do
		itemIds[#itemIds + 1] = itemId
	end
	table.sort(itemIds)

	sendAllInfo(player, itemIds)
end

local function clearPerks(player, itemId)
	if not isValidWeaponId(itemId) then
		return
	end

	local state = getState(player, itemId)
	state.perks = {}
	refreshProfileSpellAugments(player)
	queueSave(player, itemId)
	sendInfo(player, itemId)
end

local function applyPerks(player, msg, itemId)
	if not isValidWeaponId(itemId) or msg:len() - msg:tell() < 1 then
		return
	end

	local state = getState(player, itemId)
	local unlocked = getUnlockedLevelCount(itemId, state.experience)
	local perks = {}
	local count = msg:getByte()
	if count > MAX_PERK_LEVEL then
		return
	end
	for _ = 1, count do
		if msg:len() - msg:tell() < 2 then
			return
		end
		local level = msg:getByte()
		local position = msg:getByte()
		if level < unlocked and level < MAX_PERK_LEVEL and position <= MAX_PERK_POSITION then
			perks[level] = position
		end
	end

	state.perks = perks
	refreshProfileSpellAugments(player)
	queueSave(player, itemId)
	sendInfo(player, itemId)
end

function System.addExperience(player, source, experience, itemId, applyMultiplier)
	if not player or (source and source.isPlayer and source:isPlayer()) then
		return false
	end

	if itemId then
		itemId = canonicalizeServerId(itemId) or resolveServerId(itemId)
	else
		itemId = getEquippedWeaponId(player)
	end
	if not isValidWeaponId(itemId) then
		return false
	end

	experience = math.max(0, tonumber(experience) or 0)
	if experience <= 0 then
		return false
	end
	if applyMultiplier ~= false then
		experience = math.floor(experience * EXPERIENCE_GAIN_MULTIPLIER)
	else
		experience = math.floor(experience)
	end
	if experience <= 0 then
		return false
	end

	local state = getState(player, itemId)
	local previousUnlocked = getUnlockedLevelCount(itemId, state.experience)
	local experienceTable = getExperienceTable(itemId)
	state.experience = math.min(experienceTable[#experienceTable], state.experience + experience)
	queueSave(player, itemId)
	sendExperience(player, itemId)

	if getUnlockedLevelCount(itemId, state.experience) > previousUnlocked then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "Your weapon proficiency has unlocked a new perk.")
		sendInfo(player, itemId)
	end
	return true
end

function System.clearPlayerCache(player)
	if player then
		local guid = player:getGuid()
		local profile = playerCache[guid]
		if profile then
			if profile.saveEvent then
				stopEvent(profile.saveEvent)
			end
			profile.catalogSent = false
			flushProfile(guid)
			playerCache[guid] = nil
		end
		if player.clearProficiencySpellAugments then
			player:clearProficiencySpellAugments()
		end
	end
end

local requestHandler = PacketHandler(OPCODE_REQUEST)

function requestHandler.onReceive(player, msg)
	if not supportsCustomNetwork(player) or msg:len() - msg:tell() < 1 then
		return
	end

	local action = msg:getByte()
	if action == ACTION_LIST_INFO then
		local profile = loadProfile(player)
		local now = os.mtime()
		if profile.lastListInfoAt and now - profile.lastListInfoAt < LIST_INFO_COOLDOWN_MS then
			return
		end
		profile.lastListInfoAt = now
		sendAll(player)
		return
	end

	if msg:len() - msg:tell() < 2 then
		return
	end

	local itemId = resolveServerId(msg:getU16())
	if action == ACTION_ITEM_INFO then
		sendInfo(player, itemId)
	elseif action == ACTION_RESET_PERKS then
		clearPerks(player, itemId)
	elseif action == ACTION_APPLY_PERKS then
		applyPerks(player, msg, itemId)
	end
end

requestHandler:register()

local loginEvent = CreatureEvent("WeaponProficiencyLogin")

function loginEvent.onLogin(player)
	loadProfile(player)
	return true
end

loginEvent:register()

local logoutEvent = CreatureEvent("WeaponProficiencyLogout")

function logoutEvent.onLogout(player)
	System.clearPlayerCache(player)
	return true
end

logoutEvent:register()
