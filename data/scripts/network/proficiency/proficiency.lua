-- Equipment Proficiency
--
-- Any equipped item can carry proficiency. It accumulates experience while the
-- player fights, and it teaches spells to specific vocations:
--
--   borrowed  item worn, proficiency below the spell's mastery level
--             -> castable only while the item stays on
--   learned   proficiency reached the mastery level
--             -> player:learnSpell writes it to player_spells, castable forever
--
-- The engine enforces both states in Spell::playerSpellCheck and
-- InstantSpell::canCast; this script only decides what goes into each bucket.
--
-- Experience lives in player_weapon_proficiency (kept under its old name to
-- avoid a migration). The `perks` column of that table is a leftover from the
-- perk-tree design this replaced and is no longer written.

local proficiencySystemConfigKey = configKeys and configKeys.WEAPON_PROFICIENCY_SYSTEM_ENABLED or
	WEAPON_PROFICIENCY_SYSTEM_ENABLED
if configManager and proficiencySystemConfigKey and not configManager.getBoolean(proficiencySystemConfigKey) then
	EquipmentProficiencySystem = nil
	return
end

EquipmentProficiencySystem = EquipmentProficiencySystem or {}

local System = EquipmentProficiencySystem

local OPCODE_REQUEST = 0xB3
local OPCODE_CATALOG = 0x5A
local OPCODE_EXPERIENCE = 0x5C
local OPCODE_INFO = 0xC4
local OPCODE_INFO_BATCH = 0x5B

local ACTION_ITEM_INFO = 0
local ACTION_LIST_INFO = 1

local SPELL_STATE_BORROWED = 0
local SPELL_STATE_LEARNED = 1

local MAX_PROFICIENCY_LEVEL = 7
local EXPERIENCE_GAIN_MULTIPLIER = 0.01
local SAVE_DELAY_MS = 5000
local LIST_INFO_COOLDOWN_MS = 1000

-- Slots that can carry proficiency. Mirrors Player::getEquippedItems() minus the
-- backpack (a container, not gear) and the ammo slot.
local PROFICIENCY_SLOTS = {
	CONST_SLOT_HEAD,
	CONST_SLOT_NECKLACE,
	CONST_SLOT_ARMOR,
	CONST_SLOT_RIGHT,
	CONST_SLOT_LEFT,
	CONST_SLOT_LEGS,
	CONST_SLOT_FEET,
	CONST_SLOT_RING,
}

-- The first MAX_PROFICIENCY_LEVEL thresholds unlock levels. The remaining ones
-- keep progression running up to the final experience cap.
--
-- NOTE: these curves were calibrated when only the weapon could gain experience.
-- Every equipped piece now gains in parallel, so a fully-equipped character
-- progresses far faster than these numbers assume. They need rebalancing before
-- any serious tuning pass.
local EXPERIENCE_TABLES = {
	regular = { 1750, 25000, 100000, 400000, 2000000, 8000000, 30000000, 60000000, 90000000 },
	knight = { 1250, 20000, 80000, 300000, 1500000, 6000000, 20000000, 40000000, 60000000 },
	crossbow = { 600, 8000, 30000, 150000, 650000, 2500000, 10000000, 20000000, 30000000 },
}

local WEAPON_CATALOG = dofile(DATA_DIRECTORY .. "/scripts/network/proficiency/weapon_catalog.lua")
local EQUIPMENT_SPELLS = dofile(DATA_DIRECTORY .. "/scripts/network/proficiency/equipment_spells.lua")

local playerCache = {}
local catalogEntries
local catalogByServerId = {}
local serverIdByClientId = {}
local proficiencyTableReady = false

local function logError(message)
	if logger and logger.error then
		logger.error(message)
	else
		print(message)
	end
end

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
		logError("[EquipmentProficiency] Failed to create player_weapon_proficiency table.")
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

-- An item takes part in the system when it declares proficiency spells, whatever
-- slot it belongs to. This replaces the old "is it a weapon?" test.
local function hasProficiencyDefinition(itemId)
	itemId = tonumber(itemId)
	if not itemId or itemId <= 0 or itemId > 0xFFFF or itemId % 1 ~= 0 then
		return false
	end
	return EQUIPMENT_SPELLS[itemId] ~= nil and getItemType(itemId) ~= nil
end

local function ensureCatalog()
	if catalogEntries then
		return
	end

	catalogEntries = {}
	local serverIds = {}
	for serverId in pairs(EQUIPMENT_SPELLS) do
		serverIds[#serverIds + 1] = serverId
	end
	table.sort(serverIds)

	for _, serverId in ipairs(serverIds) do
		if hasProficiencyDefinition(serverId) then
			local itemType = getItemType(serverId)
			local clientId = itemType:getClientId()
			if not clientId or clientId == 0 or clientId > 0xFFFF then
				clientId = serverId
			end
			local entry = catalogByServerId[serverIdByClientId[clientId]]
			if not entry then
				entry = {
					serverId = serverId,
					clientId = clientId,
					category = WEAPON_CATALOG[serverId] or 0,
					name = itemType:getName(),
				}
				catalogEntries[#catalogEntries + 1] = entry
				serverIdByClientId[clientId] = serverId
			end
			catalogByServerId[serverId] = entry
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

local function getProficiencyLevel(itemId, experience)
	local level = 0
	local experienceTable = getExperienceTable(itemId)
	for index = 1, MAX_PROFICIENCY_LEVEL do
		if experience >= experienceTable[index] then
			level = index
		end
	end
	return level
end

-- Promoted vocations resolve down to their base, so equipment_spells.lua only
-- ever lists base ids. Base vocations point at themselves in vocations.xml, so
-- one hop is always enough.
local function getBaseVocationId(player)
	local vocation = player:getVocation()
	if not vocation then
		return 0
	end
	local demotion = vocation:getDemotion()
	return demotion and demotion:getId() or vocation:getId()
end

local function getSpellDefinitions(itemId, vocationId)
	local byVocation = EQUIPMENT_SPELLS[itemId]
	if not byVocation then
		return nil
	end
	return byVocation[vocationId]
end

---------------------------------------------------------------------------
-- Persistence
---------------------------------------------------------------------------

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

	local upsertClause = "ON DUPLICATE KEY UPDATE `experience` = VALUES(`experience`)"
	if canUseAliasedUpsert() then
		upsertClause = "AS new ON DUPLICATE KEY UPDATE `experience` = new.`experience`"
	end

	db.asyncQuery(string.format(
		"INSERT INTO `player_weapon_proficiency` (`player_id`, `item_id`, `experience`) VALUES (%d, %d, %d) " ..
		upsertClause,
		guid, itemId, state.experience
	))
end

local refreshEquipmentSpells

local function loadProfile(player)
	local guid = player:getGuid()
	local cached = playerCache[guid]
	if cached then
		return cached
	end

	local profile = { items = {}, dirty = {}, catalogSent = false }
	if ensureTables() then
		local resultId = db.storeQuery(
			"SELECT `item_id`, `experience` FROM `player_weapon_proficiency` WHERE `player_id` = " .. guid
		)
		if resultId then
			repeat
				local itemId = result.getDataInt(resultId, "item_id")
				local canonicalId = canonicalizeServerId(itemId)
				if canonicalId then
					profile.items[canonicalId] = {
						experience = math.max(0, result.getDataInt(resultId, "experience")),
					}
				end
			until not result.next(resultId)
			result.free(resultId)
		end
	end

	playerCache[guid] = profile
	player:registerEvent("EquipmentProficiencyLogout")
	return profile
end

local function flushProfile(guid)
	local profile = playerCache[guid]
	if not profile then
		return
	end

	profile.saveEvent = nil
	for itemId in pairs(profile.dirty) do
		local state = profile.items[itemId]
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
	if not profile.items[itemId] then
		profile.items[itemId] = { experience = 0 }
	end
	return profile.items[itemId]
end

---------------------------------------------------------------------------
-- Equipment scanning
---------------------------------------------------------------------------

-- Every equipped item that takes part in the system, deduplicated: wearing two
-- copies of the same ring must not award experience twice.
local function getProficiencyItemIds(player)
	local seen = {}
	local itemIds = {}
	for _, slot in ipairs(PROFICIENCY_SLOTS) do
		local item = player:getSlotItem(slot)
		local itemId = item and canonicalizeServerId(item:getId())
		if itemId and not seen[itemId] then
			seen[itemId] = true
			itemIds[#itemIds + 1] = itemId
		end
	end
	return itemIds
end

-- Modifiers scale with the item's proficiency level, so the curve lives in data
-- and rebalancing never needs a recompile. `perLevel` is multiplied by the
-- level; `value` is flat.
local function applySpellModifiers(player, definition, level)
	if not definition.modifiers or not player.addProficiencySpellAugment then
		return
	end

	for _, modifier in ipairs(definition.modifiers) do
		local amount = (modifier.value or 0) + (modifier.perLevel or 0) * level
		if modifier.type and amount ~= 0 then
			player:addProficiencySpellAugment(definition.spell, modifier.type, amount)
		end
	end
end

-- Rebuilds the borrowed-spell set and the modifier map from scratch, and
-- promotes anything that has reached its mastery level. Called on login, on any
-- inventory change, and whenever an item gains a proficiency level.
refreshEquipmentSpells = function(player)
	if not player or not player.clearEquipmentGrantedSpells then
		return
	end

	player:clearEquipmentGrantedSpells()
	if player.clearProficiencySpellAugments then
		player:clearProficiencySpellAugments()
	end

	local vocationId = getBaseVocationId(player)
	local mastered = {}

	for _, itemId in ipairs(getProficiencyItemIds(player)) do
		local definitions = getSpellDefinitions(itemId, vocationId)
		if definitions then
			local level = getProficiencyLevel(itemId, getState(player, itemId).experience)
			for _, definition in ipairs(definitions) do
				if level >= (definition.masterLevel or MAX_PROFICIENCY_LEVEL) then
					if not player:hasLearnedSpell(definition.spell) then
						player:learnSpell(definition.spell)
						mastered[#mastered + 1] = definition.spell
					end
				else
					player:addEquipmentGrantedSpell(definition.spell)
				end

				-- Only the equipped item empowers its spell: a mastered spell can
				-- still be cast bare, just without the item's bonus.
				applySpellModifiers(player, definition, level)
			end
		end
	end

	for _, spellName in ipairs(mastered) do
		player:sendTextMessage(MESSAGE_STATUS_SMALL,
			string.format("You mastered %s. You can now cast it without the equipment.", spellName))
	end

	-- Resends packet 0x9F so the client's spell list matches what the server will
	-- actually allow.
	if player.reloadData then
		player:reloadData()
	end
end

---------------------------------------------------------------------------
-- Protocol
---------------------------------------------------------------------------

local function writeInfoPayload(out, player, entry, state)
	local level = getProficiencyLevel(entry.serverId, state.experience)
	local definitions = getSpellDefinitions(entry.serverId, getBaseVocationId(player)) or {}

	out:addU16(entry.clientId)
	out:addU32(state.experience)
	out:addByte(level)
	out:addU16(entry.category)
	out:addByte(math.min(#definitions, 0xFF))
	for index = 1, math.min(#definitions, 0xFF) do
		local definition = definitions[index]
		local masterLevel = definition.masterLevel or MAX_PROFICIENCY_LEVEL
		out:addString(definition.spell)
		out:addByte(masterLevel)
		out:addByte(level >= masterLevel and SPELL_STATE_LEARNED or SPELL_STATE_BORROWED)
	end
end

local function sendInfo(player, itemId)
	local entry = getCatalogEntry(itemId)
	if not supportsCustomNetwork(player) or not entry then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_INFO)
	writeInfoPayload(out, player, entry, getState(player, itemId))
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
	out:addByte(getProficiencyLevel(itemId, state.experience))
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
		writeInfoPayload(out, player, info.entry, getState(player, info.itemId))
	end
	return out:sendToPlayer(player)
end

local function sendAll(player, forceCatalog)
	local profile = loadProfile(player)
	if (forceCatalog or profile.catalogSent ~= true) and sendCatalog(player) then
		profile.catalogSent = true
	end

	local itemIds = {}
	for itemId in pairs(profile.items) do
		itemIds[#itemIds + 1] = itemId
	end
	table.sort(itemIds)

	sendAllInfo(player, itemIds)
end

---------------------------------------------------------------------------
-- Public API
---------------------------------------------------------------------------

-- Awards experience to every equipped item that takes part in the system. Each
-- item receives the full share, so gear progresses together rather than
-- competing for one pool.
function System.addExperience(player, source, experience, itemId, applyMultiplier)
	if not player or (source and source.isPlayer and source:isPlayer()) then
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

	local itemIds
	if itemId then
		local resolved = canonicalizeServerId(itemId) or resolveServerId(itemId)
		if not resolved then
			return false
		end
		itemIds = { resolved }
	else
		itemIds = getProficiencyItemIds(player)
	end

	local awarded = false
	local levelledUp = false

	for _, targetId in ipairs(itemIds) do
		local state = getState(player, targetId)
		local experienceTable = getExperienceTable(targetId)
		local previousLevel = getProficiencyLevel(targetId, state.experience)

		state.experience = math.min(experienceTable[#experienceTable], state.experience + experience)
		queueSave(player, targetId)
		sendExperience(player, targetId)
		awarded = true

		if getProficiencyLevel(targetId, state.experience) > previousLevel then
			levelledUp = true
			sendInfo(player, targetId)
		end
	end

	-- One refresh for the whole batch: it rescans every slot anyway, and it is
	-- what promotes a borrowed spell once its mastery level is crossed.
	if levelledUp then
		refreshEquipmentSpells(player)
	end

	return awarded
end

function System.refreshEquippedSpells(player)
	refreshEquipmentSpells(player)
end

function System.sendEquippedInfo(player)
	for _, itemId in ipairs(getProficiencyItemIds(player)) do
		sendExperience(player, itemId)
		sendInfo(player, itemId)
	end
end

function System.getProficiencyLevel(player, itemId)
	local resolved = canonicalizeServerId(itemId)
	if not resolved then
		return 0
	end
	return getProficiencyLevel(resolved, getState(player, resolved).experience)
end

function System.getEquippedItemIds(player)
	return getProficiencyItemIds(player)
end

function System.getExperience(player, itemId)
	local resolved = canonicalizeServerId(itemId)
	if not resolved then
		return 0
	end
	return getState(player, resolved).experience
end

-- What this item teaches the player's vocation, and whether each spell is still
-- only borrowed. Used by the GM talkaction and available to any UI script.
function System.getSpellStates(player, itemId)
	local resolved = canonicalizeServerId(itemId)
	if not resolved then
		return {}
	end

	local definitions = getSpellDefinitions(resolved, getBaseVocationId(player)) or {}
	local level = getProficiencyLevel(resolved, getState(player, resolved).experience)

	local states = {}
	for _, definition in ipairs(definitions) do
		local masterLevel = definition.masterLevel or MAX_PROFICIENCY_LEVEL
		states[#states + 1] = {
			spell = definition.spell,
			masterLevel = masterLevel,
			learned = level >= masterLevel,
		}
	end
	return states
end

function System.clearPlayerCache(player)
	if not player then
		return
	end

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
	if player.clearEquipmentGrantedSpells then
		player:clearEquipmentGrantedSpells()
	end
end

---------------------------------------------------------------------------
-- Events
---------------------------------------------------------------------------

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
		sendAll(player, true)
		return
	end

	if msg:len() - msg:tell() < 2 then
		return
	end

	local itemId = resolveServerId(msg:getU16())
	if action == ACTION_ITEM_INFO and itemId then
		sendInfo(player, itemId)
	end
end

requestHandler:register()

local loginEvent = CreatureEvent("EquipmentProficiencyLogin")

function loginEvent.onLogin(player)
	loadProfile(player)
	refreshEquipmentSpells(player)
	System.sendEquippedInfo(player)
	return true
end

loginEvent:register()

local logoutEvent = CreatureEvent("EquipmentProficiencyLogout")

function logoutEvent.onLogout(player)
	System.clearPlayerCache(player)
	return true
end

logoutEvent:register()
