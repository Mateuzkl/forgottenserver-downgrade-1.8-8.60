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
local OPCODE_RESHAPE = 0x3D
local OPCODE_RESOURCE_BALANCE = 0xEE

local ACTION_ITEM_INFO = 0
local ACTION_LIST_INFO = 1
local ACTION_RESET_PERKS = 2
local ACTION_APPLY_PERKS = 3
local ACTION_MODIFY_SLOT = 4
local ACTION_REFINE_SLOT = 5
local ACTION_MAXIMISE_SLOT = 6
local ACTION_RESHAPE_SLOT = 7
local ACTION_PICK_RESHAPE = 8
local ACTION_CLEAR_SLOT = 9

local MAX_PERK_LEVEL = 7
local MAX_PERK_POSITION = 2
local EXPERIENCE_GAIN_MULTIPLIER = 0.01
local SAVE_DELAY_MS = 5000
local LIST_INFO_COOLDOWN_MS = 1000
local MAX_MODIFIED_SLOTS = 2
local MAX_MODIFIER_RANK = 10
local MODIFY_DUST_COST = 250
local REFINE_DUST_COST = 200
local RESHAPE_DUST_COST = 250
local RESHAPE_OFFER_COUNT = 3
local RESHAPE_OFFER_TTL_MS = 30000
local RESOURCE_FORGE_DUST = 23

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

-- Element mapping: Cipbia unshifted index -> TFS CombatType_t (bitmask)
local CIPBIA_TO_COMBAT = {
	[0]  = COMBAT_PHYSICALDAMAGE,
	[1]  = COMBAT_FIREDAMAGE,
	[2]  = COMBAT_EARTHDAMAGE,
	[3]  = COMBAT_ENERGYDAMAGE,
	[4]  = COMBAT_ICEDAMAGE,
	[5]  = COMBAT_HOLYDAMAGE,
	[6]  = COMBAT_DEATHDAMAGE,
	[7]  = COMBAT_HEALING,
	[8]  = COMBAT_DROWNDAMAGE,
	[9]  = COMBAT_LIFEDRAIN,
	[10] = COMBAT_MANADRAIN,
	[11] = COMBAT_AGONYDAMAGE,
	[18] = COMBAT_HEALING,
}

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
			`modifiers` varchar(512) NOT NULL DEFAULT '',
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
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function sendProficiencyBanner(player, itemId, message)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(0x75)
	out:addByte(10) -- SCREENSHOT_AND_BANNER_TYPE_PROFICIENCY
	out:addU16(itemId)
	out:addString(message)
	return out:sendToPlayer(player)
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
			if not clientId or clientId == 0 or clientId > 0xFFFF then
				clientId = serverId
			end
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

local function modifierKey(level, position)
	return level .. ":" .. position
end

local function isValidModifierEnum(modifierEnum)
	if modifierEnum >= 1 and modifierEnum <= 250 then
		local offset = (modifierEnum - 1) % 50
		local group = math.floor(offset / 10)
		return group <= 4 and offset % 10 <= 5
	end
	return (modifierEnum >= 251 and modifierEnum <= 271)
		or (modifierEnum >= 281 and modifierEnum <= 288)
		or (modifierEnum >= 291 and modifierEnum <= 297)
		or (modifierEnum >= 301 and modifierEnum <= 307)
		or (modifierEnum >= 311 and modifierEnum <= 317 and modifierEnum ~= 313)
		or (modifierEnum >= 321 and modifierEnum <= 323)
end

local function encodeModifiers(modifiers)
	local entries = {}
	for _, modifier in pairs(modifiers) do
		entries[#entries + 1] = modifier
	end
	table.sort(entries, function(left, right)
		return left.level == right.level and left.position < right.position or left.level < right.level
	end)

	local encoded = {}
	for _, modifier in ipairs(entries) do
		encoded[#encoded + 1] = string.format("%d:%d:%d:%d", modifier.level, modifier.position,
			modifier.modifierEnum, modifier.refineLevel)
	end
	return table.concat(encoded, ",")
end

local function decodeModifiers(encoded)
	local modifiers = {}
	local count = 0
	for entry in tostring(encoded or ""):gmatch("[^,]+") do
		local level, position, modifierEnum, refineLevel = entry:match("^(%d+):(%d+):(%d+):(%d+)$")
		level, position = tonumber(level), tonumber(position)
		modifierEnum, refineLevel = tonumber(modifierEnum), tonumber(refineLevel)
		if count < MAX_MODIFIED_SLOTS and level and position and modifierEnum and refineLevel
			and level >= 0 and level < MAX_PERK_LEVEL and position >= 0 and position <= MAX_PERK_POSITION
			and isValidModifierEnum(modifierEnum) and refineLevel >= 1 and refineLevel <= MAX_MODIFIER_RANK then
			modifiers[modifierKey(level, position)] = {
				level = level,
				position = position,
				modifierEnum = modifierEnum,
				refineLevel = refineLevel,
			}
			count = count + 1
		end
	end
	return modifiers
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

	local upsertClause = "ON DUPLICATE KEY UPDATE `experience` = VALUES(`experience`), `perks` = VALUES(`perks`), `modifiers` = VALUES(`modifiers`)"
	if canUseAliasedUpsert() then
		upsertClause = "AS new ON DUPLICATE KEY UPDATE `experience` = new.`experience`, `perks` = new.`perks`, `modifiers` = new.`modifiers`"
	end

	db.asyncQuery(string.format(
		"INSERT INTO `player_weapon_proficiency` (`player_id`, `item_id`, `experience`, `perks`, `modifiers`) VALUES (%d, %d, %d, %s, %s) " ..
		upsertClause,
		guid, itemId, state.experience, db.escapeString(encodePerks(state.perks)),
		db.escapeString(encodeModifiers(state.modifiers))
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
			"SELECT `item_id`, `experience`, `perks`, `modifiers` FROM `player_weapon_proficiency` WHERE `player_id` = " .. guid
		)
		if resultId then
			repeat
				local itemId = result.getDataInt(resultId, "item_id")
				local canonicalId = canonicalizeServerId(itemId)
				if canonicalId then
					profile.weapons[canonicalId] = {
						experience = math.max(0, result.getDataInt(resultId, "experience")),
						perks = decodePerks(result.getDataString(resultId, "perks")),
						modifiers = decodeModifiers(result.getDataString(resultId, "modifiers")),
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
		profile.weapons[itemId] = { experience = 0, perks = {}, modifiers = {} }
	end
	profile.weapons[itemId].modifiers = profile.weapons[itemId].modifiers or {}
	return profile.weapons[itemId]
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

local MODIFIER_SPELLS = {
	[0] = { 80, 105, 106, 59, 316, 261 },
	[1] = { 124, 302, 303, 258, 57, 122 },
	[2] = { 13, 24, 240, 260, 310, 23 },
	[3] = { 43, 120, 262, 263, 317, 318 },
	[4] = { 289, 288, 294, 287, 301, 290 },
}

local MODIFIER_SPELL_OFFSETS = { 1, 2, 3, 4, 5, 11, 12, 13, 14, 15, 21, 22, 23, 24, 25 }
local MODIFIER_GENERAL_POOL = {}
for value = 251, 271 do MODIFIER_GENERAL_POOL[#MODIFIER_GENERAL_POOL + 1] = value end
for value = 281, 288 do MODIFIER_GENERAL_POOL[#MODIFIER_GENERAL_POOL + 1] = value end
for value = 291, 297 do MODIFIER_GENERAL_POOL[#MODIFIER_GENERAL_POOL + 1] = value end
for value = 301, 307 do MODIFIER_GENERAL_POOL[#MODIFIER_GENERAL_POOL + 1] = value end
for value = 311, 317 do
	if value ~= 313 then MODIFIER_GENERAL_POOL[#MODIFIER_GENERAL_POOL + 1] = value end
end
for value = 321, 323 do MODIFIER_GENERAL_POOL[#MODIFIER_GENERAL_POOL + 1] = value end

local MODIFIER_SKILLS = { 1, 6, 7, 8, 9, 10, 11 }

local function interpolateModifierValue(minimum, maximum, rank)
	rank = math.max(0, math.min(MAX_MODIFIER_RANK, tonumber(rank) or 0))
	return minimum + math.floor(((maximum - minimum) / MAX_MODIFIER_RANK) * rank)
end

local function modifierPercent(minimum, maximum, rank)
	return interpolateModifierValue(minimum, maximum, rank) / 10000
end

local function getModifierPerkData(modifierEnum, rank)
	if modifierEnum >= 1 and modifierEnum <= 250 then
		local region = math.floor((modifierEnum - 1) / 50)
		local offset = (modifierEnum - 1) % 50
		local spellIndex = offset % 10
		local group = math.floor(offset / 10)
		local spellId = MODIFIER_SPELLS[region] and MODIFIER_SPELLS[region][spellIndex + 1]
		if not spellId or group > 4 then
			return nil
		end
		local augmentTypes = { 17, 16, 2, 15, 14 }
		local ranges = {
			{ 100, 300 }, { 500, 2000 }, { 100, 300 }, { 100, 600 }, { 100, 1200 },
		}
		return {
			Type = 5,
			SpellId = spellId,
			AugmentType = augmentTypes[group + 1],
			Value = modifierPercent(ranges[group + 1][1], ranges[group + 1][2], rank),
		}
	end

	if modifierEnum >= 251 and modifierEnum <= 271 then
		return { Type = 6, BestiaryId = modifierEnum - 250, Value = modifierPercent(50, 250, rank) }
	end

	local direct = {
		[281] = { Type = 16, Value = modifierPercent(100, 800, rank) },
		[282] = { Type = 17, Value = modifierPercent(100, 1600, rank) },
		[283] = { Type = 18, Value = interpolateModifierValue(2, 12, rank) },
		[284] = { Type = 19, Value = interpolateModifierValue(5, 25, rank) },
		[285] = { Type = 20, Value = interpolateModifierValue(4, 24, rank) },
		[286] = { Type = 21, Value = interpolateModifierValue(10, 50, rank) },
		[287] = { Type = 28, Value = modifierPercent(200, 1000, rank) },
		[288] = { Type = 29, Value = modifierPercent(100, 400, rank) },
		[321] = { Type = 30, Value = modifierPercent(500, 1500, rank) },
		[322] = { Type = 31, Value = modifierPercent(500, 1500, rank), AllElements = true },
		[323] = { Type = 7, Value = modifierPercent(100, 500, rank) },
	}
	if direct[modifierEnum] then
		return direct[modifierEnum]
	end

	local rangeStart, perkType, minimum, maximum
	if modifierEnum >= 291 and modifierEnum <= 297 then
		rangeStart, perkType, minimum, maximum = 291, 25, 200, 1000
	elseif modifierEnum >= 301 and modifierEnum <= 307 then
		rangeStart, perkType, minimum, maximum = 301, 26, 100, 800
	elseif modifierEnum >= 311 and modifierEnum <= 317 and modifierEnum ~= 313 then
		rangeStart, perkType, minimum, maximum = 311, 27, 200, 1000
	end
	if rangeStart then
		return {
			Type = perkType,
			SkillId = MODIFIER_SKILLS[modifierEnum - rangeStart + 1],
			Value = modifierPercent(minimum, maximum, rank),
		}
	end

	return nil
end

local function getModifierRegion(player, itemId)
	local function getBaseVocationId()
		local vocation = player:getVocation()
		for _ = 1, 4 do
			local demotion = vocation and vocation:getDemotion() or nil
			if not demotion or demotion:getId() == vocation:getId() then
				break
			end
			vocation = demotion
		end
		return vocation and vocation:getId() or 4
	end

	local itemType = getItemType(itemId)
	local weaponType = itemType and itemType:getWeaponType() or WEAPON_NONE
	if weaponType == WEAPON_SWORD or weaponType == WEAPON_AXE or weaponType == WEAPON_CLUB then
		return 0
	elseif weaponType == WEAPON_DISTANCE or weaponType == WEAPON_AMMO then
		return 1
	elseif WEAPON_FIST and weaponType == WEAPON_FIST then
		return 4
	elseif weaponType == WEAPON_WAND then
		return getBaseVocationId() == 2 and 3 or 2
	end

	local vocationId = getBaseVocationId()
	return ({ [4] = 0, [3] = 1, [1] = 2, [2] = 3, [5] = 4 })[vocationId] or 0
end

local function rollModifier(player, itemId, excluded)
	local region = getModifierRegion(player, itemId)
	local poolSize = #MODIFIER_SPELL_OFFSETS + #MODIFIER_GENERAL_POOL
	for _ = 1, 100 do
		local index = math.random(poolSize)
		local modifierEnum
		if index <= #MODIFIER_SPELL_OFFSETS then
			modifierEnum = region * 50 + MODIFIER_SPELL_OFFSETS[index]
		else
			modifierEnum = MODIFIER_GENERAL_POOL[index - #MODIFIER_SPELL_OFFSETS]
		end
		if not excluded[modifierEnum] and getModifierPerkData(modifierEnum, 1) then
			return modifierEnum
		end
	end
	return nil
end

refreshProfileSpellAugments = function(player, profile)
	if not player.clearProficiencySpellAugments
	   or not player.addProficiencySpellAugment
	   or not player.resetWeaponProficiencyStats
	   or not player.applyWeaponProficiencyPerk then
		return
	end

	player:clearProficiencySpellAugments()
	player:resetWeaponProficiencyStats()

	if not isAugmentSystemEnabled() then
		return
	end

	profile = profile or playerCache[player:getGuid()]
	if not profile then
		return
	end

	-- Cipbia skill ID -> TFS skills_t (non-linear mapping from Canary's CipbiaSkills_t)
	local CIPBIA_SKILL_TO_TFS = {
		[1]  = SKILL_MAGLEVEL,
		[6]  = SKILL_SHIELD,
		[7]  = SKILL_DISTANCE,
		[8]  = SKILL_SWORD,
		[9]  = SKILL_CLUB,
		[10] = SKILL_AXE,
		[11] = SKILL_FIST,
		[13] = SKILL_FISHING,
	}

	-- Market category -> Proficiency ID (matches client getProficiencyIdFromCategory)
	local MARKET_CATEGORY_TO_PROFICIENCY = {
		[17] = 8,  -- Axes → Sanguine 1H Axe
		[18] = 9,  -- Clubs → Sanguine 1H Club
		[19] = 13, -- Distance → Sanguine 2H Bow
		[20] = 6,  -- Swords → Sanguine 1H Sword
		[21] = 15, -- Wands/Rods → Sanguine 1H Wand
		[27] = 14, -- Fist → Sanguine 2H Fist
	}
	local STELLAR_PROFICIENCY_BY_ITEM = {
		[53208] = 489, [53210] = 496,
		[53212] = 486, [53214] = 491,
		[53216] = 487, [53218] = 493,
		[53220] = 490, [53222] = 488,
		[53224] = 495, [53226] = 492,
		[53228] = 494,
	}

	local function getProficiencyId(itemId, category)
		return STELLAR_PROFICIENCY_BY_ITEM[itemId] or MARKET_CATEGORY_TO_PROFICIENCY[category] or category
	end

	local function cipbiaSkillToTfs(cipbiaSkill)
		if not cipbiaSkill then return SKILL_FIST end
		return CIPBIA_SKILL_TO_TFS[cipbiaSkill] or SKILL_FIST
	end

	local function getElementFromJson(perk)
		local shifted = tonumber(perk.ElementId) or tonumber(perk.DamageType)
		if not shifted or shifted == 0 then
			return COMBAT_NONE
		end
		-- undoShift: trailingZeros - 2
		local unshifted = 0
		local n = shifted
		while n > 0 and (n % 2) == 0 do
			unshifted = unshifted + 1
			n = n / 2
		end
		unshifted = unshifted - 2
		if unshifted < 0 then
			return COMBAT_NONE
		end
		return CIPBIA_TO_COMBAT[unshifted] or COMBAT_NONE
	end

	local equippedId = getEquippedWeaponId(player)
	local elementalTypes = {
		COMBAT_PHYSICALDAMAGE, COMBAT_FIREDAMAGE, COMBAT_EARTHDAMAGE,
		COMBAT_ENERGYDAMAGE, COMBAT_ICEDAMAGE, COMBAT_HOLYDAMAGE, COMBAT_DEATHDAMAGE,
	}

	for itemId, state in pairs(profile.weapons) do
		local entry = getCatalogEntry(itemId)
		local proficiencyId = getProficiencyId(itemId, entry and entry.category)
		local definition = proficiencyDefinitionsById[proficiencyId]
		if definition and type(definition.Levels) == "table" then
			local isEquipped = (itemId == equippedId)
			for level, position in pairs(state.perks) do
				local levelData = definition.Levels[level + 1]
				local perk = levelData and levelData.Perks and levelData.Perks[position + 1]
				local modifier = state.modifiers and state.modifiers[modifierKey(level, position)]
				if modifier then
					perk = getModifierPerkData(modifier.modifierEnum, modifier.refineLevel)
				end
				if perk then
					local perkType = tonumber(perk.Type)
					local value = tonumber(perk.Value) or 0
					local rawSkillId = tonumber(perk.SkillId)
					if perkType then
						if perkType == 5 then
							-- Type 5 (Spell Augment): always register for lookup
							local spellId = tonumber(perk.SpellId)
							local augmentType = tonumber(perk.AugmentType)
							if spellId and augmentType then
								player:addProficiencySpellAugment(itemId, spellId, augmentType, value)
							end
						elseif isEquipped then
							local spellId = tonumber(perk.SpellId) or 0
							local augmentType = tonumber(perk.AugmentType) or 0
							local skillId = cipbiaSkillToTfs(rawSkillId)
							local element = getElementFromJson(perk)
							local range = tonumber(perk.Range) or 0
							local bestiaryId = tonumber(perk.BestiaryId) or 0
							if perk.AllElements then
								for _, combatType in ipairs(elementalTypes) do
									player:applyWeaponProficiencyPerk(perkType, value, 0, 0, skillId, combatType)
								end
							else
								player:applyWeaponProficiencyPerk(perkType, value, spellId, augmentType, skillId, element,
									range, bestiaryId, tonumber(perk.MissileId) or 0,
									tonumber(perk.Multiplier) or 0, tonumber(perk.Probability) or 0)
							end
						end
					end
				end
			end
		end
	end

	if player.sendSkills then
		player:sendSkills()
	end
	if player.wheelSendSkillStats then
		player:wheelSendSkillStats()
	end
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

	local modifiers = {}
	for _, modifier in pairs(state.modifiers or {}) do
		modifiers[#modifiers + 1] = modifier
	end
	table.sort(modifiers, function(left, right)
		return left.level == right.level and left.position < right.position or left.level < right.level
	end)
	out:addByte(math.min(#modifiers, MAX_MODIFIED_SLOTS))
	for index = 1, math.min(#modifiers, MAX_MODIFIED_SLOTS) do
		local modifier = modifiers[index]
		out:addByte(modifier.level)
		out:addByte(modifier.position)
		out:addU16(modifier.modifierEnum)
		out:addByte(modifier.refineLevel)
	end
	out:addU16(entry.category)
end

local function getForgeDust(player)
	local value = player:getStorageValue(PlayerStorageKeys.forgeDust)
	return value and value > 0 and value or 0
end

local function sendForgeDustBalance(player)
	if not supportsCustomNetwork(player) then
		return false
	end
	local out = NetworkMessage(player)
	out:addByte(OPCODE_RESOURCE_BALANCE)
	out:addByte(RESOURCE_FORGE_DUST)
	out:addU64(getForgeDust(player))
	return out:sendToPlayer(player)
end

local function removeForgeDust(player, amount)
	local current = getForgeDust(player)
	if current < amount then
		return false
	end
	player:setStorageValue(PlayerStorageKeys.forgeDust, current - amount)
	sendForgeDustBalance(player)
	return true
end

local function sendInfo(player, itemId)
	local entry = getCatalogEntry(itemId)
	if not supportsCustomNetwork(player) or not entry then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_INFO)
	writeInfoPayload(out, entry, getState(player, itemId))
	local sent = out:sendToPlayer(player)
	sendForgeDustBalance(player)
	return sent
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
	local sent = out:sendToPlayer(player)
	sendForgeDustBalance(player)
	return sent
end

local function sendAll(player, forceCatalog)
	local profile = loadProfile(player)
	if (forceCatalog or profile.catalogSent ~= true) and sendCatalog(player) then
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

local function sendShapeFailure(player, message)
	player:sendTextMessage(MESSAGE_FAILURE, message)
	return false
end

local function validateShapeSlot(player, itemId, level, position, requireModifier)
	if not isValidWeaponId(itemId) or level < 0 or level >= MAX_PERK_LEVEL
		or position < 0 or position > MAX_PERK_POSITION then
		return nil, sendShapeFailure(player, "Invalid weapon proficiency slot.")
	end
	local tile = player:getTile()
	if not tile or not tile:hasFlag(TILESTATE_PROTECTIONZONE) then
		return nil, sendShapeFailure(player, "You can only shape weapon proficiency inside a protection zone.")
	end

	local state = getState(player, itemId)
	if state.perks[level] ~= position then
		return nil, sendShapeFailure(player, "Select and apply this perk before shaping it.")
	end
	local modifier = state.modifiers[modifierKey(level, position)]
	if requireModifier and not modifier then
		return nil, sendShapeFailure(player, "That perk has not been modified yet.")
	end
	return state, modifier
end

local function countModifiers(state)
	local count = 0
	for _ in pairs(state.modifiers) do count = count + 1 end
	return count
end

local function finishShapeChange(player, itemId)
	refreshProfileSpellAugments(player)
	queueSave(player, itemId)
	sendInfo(player, itemId)
end

local function modifySlot(player, itemId, level, position)
	local state, modifier = validateShapeSlot(player, itemId, level, position, false)
	if not state then return end
	if modifier then
		return sendShapeFailure(player, "That perk is already modified.")
	end
	if countModifiers(state) >= MAX_MODIFIED_SLOTS then
		return sendShapeFailure(player, "You can modify at most two perks on this weapon.")
	end
	if getForgeDust(player) < MODIFY_DUST_COST then
		return sendShapeFailure(player, string.format("You need %d dust to modify this perk.", MODIFY_DUST_COST))
	end

	local excluded = {}
	for _, current in pairs(state.modifiers) do excluded[current.modifierEnum] = true end
	local modifierEnum = rollModifier(player, itemId, excluded)
	if not modifierEnum then
		return sendShapeFailure(player, "No valid proficiency modifier is available.")
	end
	if not removeForgeDust(player, MODIFY_DUST_COST) then return end
	state.modifiers[modifierKey(level, position)] = {
		level = level,
		position = position,
		modifierEnum = modifierEnum,
		refineLevel = 1,
	}
	finishShapeChange(player, itemId)
end

local function refineSlot(player, itemId, level, position)
	local state, modifier = validateShapeSlot(player, itemId, level, position, true)
	if not state then return end
	if modifier.refineLevel >= MAX_MODIFIER_RANK then
		return sendShapeFailure(player, "This modifier is already at maximum rank.")
	end
	if not removeForgeDust(player, REFINE_DUST_COST) then
		return sendShapeFailure(player, string.format("You need %d dust to refine this perk.", REFINE_DUST_COST))
	end
	modifier.refineLevel = modifier.refineLevel + 1
	finishShapeChange(player, itemId)
end

local function maximiseSlot(player, itemId, level, position)
	local state, modifier = validateShapeSlot(player, itemId, level, position, true)
	if not state then return end
	if modifier.refineLevel >= MAX_MODIFIER_RANK then
		return sendShapeFailure(player, "This modifier is already at maximum rank.")
	end
	-- The official action consumes a Lunar Ascension Orb. That item is not part of this
	-- server's current OTB, so never grant a free maximum-rank upgrade.
	return sendShapeFailure(player, "Maximise requires a Lunar Ascension Orb, which is not available yet.")
end

local function clearSlot(player, itemId, level, position)
	local state = validateShapeSlot(player, itemId, level, position, true)
	if not state then return end
	state.modifiers[modifierKey(level, position)] = nil
	local profile = loadProfile(player)
	profile.pendingReshape = nil
	finishShapeChange(player, itemId)
end

local function sendReshapeOffers(player, itemId, level, position, offers, rank)
	local entry = getCatalogEntry(itemId)
	if not entry then return false end
	local out = NetworkMessage(player)
	out:addByte(OPCODE_RESHAPE)
	out:addU16(entry.clientId)
	out:addByte(level)
	out:addByte(position)
	out:addByte(#offers)
	for _, modifierEnum in ipairs(offers) do
		out:addU16(modifierEnum)
		out:addByte(rank)
	end
	return out:sendToPlayer(player)
end

local function reshapeSlot(player, itemId, level, position)
	local state, modifier = validateShapeSlot(player, itemId, level, position, true)
	if not state then return end
	if getForgeDust(player) < RESHAPE_DUST_COST then
		return sendShapeFailure(player, string.format("You need %d dust to reshape this perk.", RESHAPE_DUST_COST))
	end

	local excluded = { [modifier.modifierEnum] = true }
	local offers = {}
	for _ = 1, RESHAPE_OFFER_COUNT do
		local offer = rollModifier(player, itemId, excluded)
		if not offer then
			return sendShapeFailure(player, "Unable to create reshape offers.")
		end
		excluded[offer] = true
		offers[#offers + 1] = offer
	end
	if not removeForgeDust(player, RESHAPE_DUST_COST) then return end

	local profile = loadProfile(player)
	profile.pendingReshape = {
		itemId = itemId,
		level = level,
		position = position,
		offers = offers,
		expiresAt = os.mtime() + RESHAPE_OFFER_TTL_MS,
	}
	sendReshapeOffers(player, itemId, level, position, offers, modifier.refineLevel)
end

local function pickReshapeOffer(player, itemId, level, position, offerIndex)
	local profile = loadProfile(player)
	local pending = profile.pendingReshape
	profile.pendingReshape = nil
	if not pending or pending.expiresAt < os.mtime() or pending.itemId ~= itemId
		or pending.level ~= level or pending.position ~= position then
		return sendShapeFailure(player, "These reshape offers have expired.")
	end
	local chosen = pending.offers[offerIndex + 1]
	if not chosen then
		return sendShapeFailure(player, "Invalid reshape offer.")
	end
	local state, modifier = validateShapeSlot(player, itemId, level, position, true)
	if not state then return end
	modifier.modifierEnum = chosen
	finishShapeChange(player, itemId)
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
		local itemType = getItemType(itemId)
		sendProficiencyBanner(player, itemId, itemType and itemType:getName() or "your weapon")
		sendInfo(player, itemId)
	end
	return true
end

function System.sendEquippedExperience(player)
	local itemId = getEquippedWeaponId(player)
	if itemId and itemId ~= 0 then
		sendExperience(player, itemId)
		sendInfo(player, itemId)
	end
end

-- Validates the player, then synchronizes equipped spell augments and experience/perks
-- through refreshProfileSpellAugments and System.sendEquippedExperience.
function System.refreshEquippedPerks(player)
	if not player then
		return
	end

	refreshProfileSpellAugments(player)
	System.sendEquippedExperience(player)
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
		sendAll(player, true)
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
	elseif action >= ACTION_MODIFY_SLOT and action <= ACTION_CLEAR_SLOT then
		if not itemId or msg:len() - msg:tell() < 2 then
			return
		end
		local level = msg:getByte()
		local position = msg:getByte()
		if action == ACTION_MODIFY_SLOT then
			modifySlot(player, itemId, level, position)
		elseif action == ACTION_REFINE_SLOT then
			refineSlot(player, itemId, level, position)
		elseif action == ACTION_MAXIMISE_SLOT then
			maximiseSlot(player, itemId, level, position)
		elseif action == ACTION_RESHAPE_SLOT then
			reshapeSlot(player, itemId, level, position)
		elseif action == ACTION_PICK_RESHAPE then
			if msg:len() - msg:tell() < 1 then return end
			pickReshapeOffer(player, itemId, level, position, msg:getByte())
		elseif action == ACTION_CLEAR_SLOT then
			clearSlot(player, itemId, level, position)
		end
	end
end

requestHandler:register()

local loginEvent = CreatureEvent("WeaponProficiencyLogin")

function loginEvent.onLogin(player)
	loadProfile(player)
	local itemId = getEquippedWeaponId(player)
	if itemId and itemId ~= 0 then
		sendExperience(player, itemId)
		sendInfo(player, itemId)
	end
	return true
end

loginEvent:register()

local logoutEvent = CreatureEvent("WeaponProficiencyLogout")

function logoutEvent.onLogout(player)
	System.clearPlayerCache(player)
	return true
end

logoutEvent:register()
