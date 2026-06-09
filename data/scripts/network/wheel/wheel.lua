local wheelSystemConfigKey = configKeys and configKeys.WHEEL_SYSTEM_ENABLED or WHEEL_SYSTEM_ENABLED
if wheelSystemConfigKey and not configManager.getBoolean(wheelSystemConfigKey) then
	return
end

local OPCODE_WHEEL_OPEN = 0x61
local OPCODE_WHEEL_SAVE = 0x62
local OPCODE_WHEEL_GEM_ACTION = 0xE7
local OPCODE_WHEEL_WINDOW = 0x5F
local OPCODE_RESOURCE_BALANCE = 0xEE
local OPCODE_WHEEL_SKILLS = 0x91

local WHEEL_MIN_LEVEL = 51
local WHEEL_POINTS_PER_LEVEL = 1
local WHEEL_SLOT_COUNT = 36
local WHEEL_NO_GEM = 0
local WHEEL_REQUIRE_PROMOTION = true
local WHEEL_CONDITION_SUBID = 86061

local RESOURCE_BANK = 0
local RESOURCE_INVENTORY = 1
local RESOURCE_LESSER_GEMS = 81
local RESOURCE_REGULAR_GEMS = 82
local RESOURCE_GREATER_GEMS = 83
local RESOURCE_LESSER_FRAGMENTS = 84
local RESOURCE_GREATER_FRAGMENTS = 85

local ITEM_LESSER_FRAGMENT = 46625
local ITEM_GREATER_FRAGMENT = 46626

local GEM_ITEMS = {
	[1] = { 44602, 44603, 44604 }, -- Knight
	[2] = { 44605, 44606, 44607 }, -- Paladin
	[3] = { 44608, 44609, 44610 }, -- Sorcerer
	[4] = { 44611, 44612, 44613 }, -- Druid
	[5] = { 49371, 49372, 49373 }, -- Monk
}

local PROMOTION_SCROLLS = {
	[43946] = { name = "abridged", points = 3, itemName = "abridged promotion scroll" },
	[43947] = { name = "basic", points = 5, itemName = "basic promotion scroll" },
	[43948] = { name = "revised", points = 9, itemName = "revised promotion scroll" },
	[43949] = { name = "extended", points = 13, itemName = "extended promotion scroll" },
	[43950] = { name = "advanced", points = 20, itemName = "advanced promotion scroll" },
}

local PROMOTION_SCROLLS_BY_NAME = {}
for itemId, scroll in pairs(PROMOTION_SCROLLS) do
	scroll.itemId = itemId
	PROMOTION_SCROLLS_BY_NAME[scroll.name] = scroll
end

local WHEEL_SLOT_MAX_POINTS = {
	200, 150, 100, 100, 150, 200, 150, 100, 75,
	75, 100, 150, 100, 75, 50, 50, 75, 100,
	100, 75, 50, 50, 75, 100, 150, 100, 75,
	75, 100, 150, 200, 150, 100, 100, 150, 200
}

local WHEEL_MAX_ALLOCATABLE_POINTS = 4000

local WHEEL_SLOT_DOMAINS = {
	1, 1, 1, 2, 2, 2, 1, 1, 1,
	2, 2, 2, 1, 1, 1, 2, 2, 2,
	3, 3, 4, 4, 4, 4, 3, 3, 3,
	4, 4, 4, 3, 3, 3, 4, 4, 4
}

local WHEEL_SLOT_BONUSES = {
	[1] = { dedication = "lifemana", conviction = "special_1" },
	[2] = { dedication = "mitigation", conviction = "manaleech" },
	[3] = { dedication = "health", conviction = "vessel" },
	[4] = { dedication = "mana", conviction = "skill" },
	[5] = { dedication = "health", conviction = "vessel" },
	[6] = { dedication = "lifemana", conviction = "spell_1" },
	[7] = { dedication = "mitigation", conviction = "vessel" },
	[8] = { dedication = "health", conviction = "spell_2" },
	[9] = { dedication = "mana", conviction = "lifeleech" },
	[10] = { dedication = "capacity", conviction = "vessel" },
	[11] = { dedication = "mana", conviction = "spell_3" },
	[12] = { dedication = "health", conviction = "manaleech" },
	[13] = { dedication = "health", conviction = "spell_4" },
	[14] = { dedication = "mana", conviction = "skill" },
	[15] = { dedication = "capacity", conviction = "vessel" },
	[16] = { dedication = "mitigation", conviction = "spell_5" },
	[17] = { dedication = "capacity", conviction = "lifeleech" },
	[18] = { dedication = "mana", conviction = "vessel" },
	[19] = { dedication = "mitigation", conviction = "vessel" },
	[20] = { dedication = "health", conviction = "manaleech" },
	[21] = { dedication = "mana", conviction = "spell_1" },
	[22] = { dedication = "health", conviction = "vessel" },
	[23] = { dedication = "mitigation", conviction = "skill" },
	[24] = { dedication = "capacity", conviction = "spell_2" },
	[25] = { dedication = "capacity", conviction = "lifeleech" },
	[26] = { dedication = "mitigation", conviction = "spell_3" },
	[27] = { dedication = "health", conviction = "vessel" },
	[28] = { dedication = "mitigation", conviction = "manaleech" },
	[29] = { dedication = "capacity", conviction = "spell_4" },
	[30] = { dedication = "mana", conviction = "vessel" },
	[31] = { dedication = "lifemana", conviction = "spell_5" },
	[32] = { dedication = "capacity", conviction = "vessel" },
	[33] = { dedication = "mitigation", conviction = "skill" },
	[34] = { dedication = "capacity", conviction = "vessel" },
	[35] = { dedication = "mana", conviction = "lifeleech" },
	[36] = { dedication = "lifemana", conviction = "special_2" },
}

local WHEEL_DEDICATION_VALUES = {
	health = { 3, 2, 1, 1, 2 },
	mana = { 1, 3, 6, 6, 2 },
	capacity = { 5, 4, 2, 2, 5 },
	lifemana = {
		health = { 3, 2, 1, 1, 2 },
		mana = { 1, 3, 6, 6, 2 },
	},
}

local WHEEL_CONVICTION_VALUES = {
	lifeleech = 75,
	manaleech = 25,
	skill = 1,
}

local AUGMENT_TYPE = {
	MANA_COST = 1,
	BASE_DAMAGE = 2,
	BASE_HEALING = 3,
	DURATION_INCREASED = 4,
	ADDITIONAL_TARGETS = 5,
	COOLDOWN = 6,
	SECONDARY_GROUP_COOLDOWN = 7,
	AFFECTED_AREA_ENLARGED = 8,
	INCREASED_DAMAGE_REDUCTION = 9,
	LIFE_LEECH = 14,
	MANA_LEECH = 15,
	CRITICAL_EXTRA_DAMAGE = 16,
	CRITICAL_HIT_CHANCE = 17,
}

local FOCUS_MAGE_SPELLS = { "Eternal Winter", "Hell's Core", "Rage of the Skies", "Wrath of Nature" }

-- Kept in the same order as Canary's wheel spell table. Each spell_N node exists
-- twice on the wheel: completing one unlocks grade I and completing both unlocks grade II.
local WHEEL_SPELL_BONUSES = {
	[1] = {
		spell_1 = { names = { "Front Sweep" }, grades = {
			{ { AUGMENT_TYPE.LIFE_LEECH, 0.05 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.14 } },
		} },
		spell_2 = { names = { "Groundshaker" }, grades = {
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.125 } },
			{ { AUGMENT_TYPE.COOLDOWN, -2 } },
		} },
		spell_3 = { names = { "Chivalrous Challenge" }, grades = {
			{ { AUGMENT_TYPE.MANA_COST, -20 } },
			{ { AUGMENT_TYPE.ADDITIONAL_TARGETS, 1 } },
		} },
		spell_4 = { names = { "Intense Wound Cleansing" }, grades = {
			{ { AUGMENT_TYPE.BASE_HEALING, 1.25 } },
			{ { AUGMENT_TYPE.COOLDOWN, -300 } },
		} },
		spell_5 = { names = { "Fierce Berserk" }, grades = {
			{ { AUGMENT_TYPE.MANA_COST, -30 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.10 } },
		} },
	},
	[2] = {
		spell_1 = { names = { "Sharpshooter" }, grades = {
			{ { AUGMENT_TYPE.SECONDARY_GROUP_COOLDOWN, -8 } },
			{ { AUGMENT_TYPE.COOLDOWN, -6 } },
		} },
		spell_2 = { names = { "Strong Ethereal Spear" }, grades = {
			{ { AUGMENT_TYPE.COOLDOWN, -2 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 3.80 } },
		} },
		spell_3 = { names = { "Divine Dazzle" }, grades = {
			{ { AUGMENT_TYPE.ADDITIONAL_TARGETS, 1 } },
			{ { AUGMENT_TYPE.DURATION_INCREASED, 4 }, { AUGMENT_TYPE.COOLDOWN, -4 } },
		} },
		spell_4 = { names = { "Swift Foot" }, grades = {
			{ { AUGMENT_TYPE.SECONDARY_GROUP_COOLDOWN, -8 } },
			{ { AUGMENT_TYPE.COOLDOWN, -6 } },
		} },
		spell_5 = { names = { "Divine Caldera" }, grades = {
			{ { AUGMENT_TYPE.MANA_COST, -20 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.085 } },
		} },
	},
	[3] = {
		spell_1 = { names = FOCUS_MAGE_SPELLS, grades = {
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.05 } },
			{ { AUGMENT_TYPE.COOLDOWN, -4 }, { AUGMENT_TYPE.SECONDARY_GROUP_COOLDOWN, -4 } },
		} },
		spell_2 = { names = { "Magic Shield" }, grades = {
			{},
			{ { AUGMENT_TYPE.COOLDOWN, -6 } },
		} },
		spell_3 = { names = { "Sap Strength" }, grades = {
			{ { AUGMENT_TYPE.AFFECTED_AREA_ENLARGED, 1 } },
			{ { AUGMENT_TYPE.INCREASED_DAMAGE_REDUCTION, 0.01 } },
		} },
		spell_4 = { names = { "Energy Wave" }, grades = {
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.05 } },
			{ { AUGMENT_TYPE.AFFECTED_AREA_ENLARGED, 1 } },
		} },
		spell_5 = { names = { "Great Fire Wave" }, grades = {
			{ { AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, 0.15 }, { AUGMENT_TYPE.CRITICAL_HIT_CHANCE, 0.10 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.05 } },
		} },
	},
	[4] = {
		spell_1 = { names = { "Strong Ice Wave" }, grades = {
			{ { AUGMENT_TYPE.MANA_LEECH, 0.03 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.10 } },
		} },
		spell_2 = { names = { "Mass Healing" }, grades = {
			{ { AUGMENT_TYPE.BASE_HEALING, 0.04 } },
			{ { AUGMENT_TYPE.AFFECTED_AREA_ENLARGED, 1 } },
		} },
		spell_3 = { names = { "Nature's Embrace" }, grades = {
			{ { AUGMENT_TYPE.BASE_HEALING, 0.11 } },
			{ { AUGMENT_TYPE.COOLDOWN, -10 } },
		} },
		spell_4 = { names = { "Terra Wave" }, grades = {
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.065 } },
			{ { AUGMENT_TYPE.LIFE_LEECH, 0.05 } },
		} },
		spell_5 = { names = { "Heal Friend" }, grades = {
			{ { AUGMENT_TYPE.MANA_COST, -10 } },
			{ { AUGMENT_TYPE.BASE_HEALING, 0.055 } },
		} },
	},
	[5] = {
		spell_1 = { names = { "Sweeping Takedown" }, grades = {
			{ { AUGMENT_TYPE.MANA_LEECH, 0.03 } },
			{ { AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, 0.25 }, { AUGMENT_TYPE.CRITICAL_HIT_CHANCE, 0.10 } },
		} },
		spell_2 = { names = { "Mass Spirit Mend" }, grades = {
			{ { AUGMENT_TYPE.BASE_HEALING, 0.08 } },
			{ { AUGMENT_TYPE.AFFECTED_AREA_ENLARGED, 1 } },
		} },
		spell_3 = { names = { "Mystic Repulse" }, grades = {
			{ { AUGMENT_TYPE.COOLDOWN, -4 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.40 } },
		} },
		spell_4 = { names = { "Chained Penance" }, grades = {
			{ { AUGMENT_TYPE.ADDITIONAL_TARGETS, 1 } },
			{ { AUGMENT_TYPE.ADDITIONAL_TARGETS, 2 } },
		} },
		spell_5 = { names = { "Flurry of Blows" }, grades = {
			{ { AUGMENT_TYPE.LIFE_LEECH, 0.05 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.12 } },
		} },
	},
}

local WHEEL_APPLIED_SPECIAL_MAGIC = {}
local WHEEL_APPLIED_MITIGATION = {}

local WHEEL_SLOT_PREREQUISITES = {
	[1] = { 2, 7 },
	[2] = { 3, 8, 7, 1 },
	[3] = { 8, 9, 4, 2 },
	[4] = { 3, 10, 11, 5 },
	[5] = { 4, 11, 12, 6 },
	[6] = { 12, 5 },
	[7] = { 8, 13, 2, 1 },
	[8] = { 14, 9, 13, 3, 7, 2 },
	[9] = { 14, 15, 10, 3, 8 },
	[10] = { 9, 16, 17, 4, 11 },
	[11] = { 10, 17, 4, 18, 5, 12 },
	[12] = { 11, 18, 5, 6 },
	[13] = { 8, 14, 19, 7 },
	[14] = { 9, 15, 20, 13, 8 },
	[17] = { 10, 16, 23, 18, 11 },
	[18] = { 17, 11, 24, 12 },
	[19] = { 13, 20, 26, 25 },
	[20] = { 21, 14, 27, 19, 26 },
	[23] = { 22, 28, 17, 24, 29 },
	[24] = { 23, 18, 29, 30 },
	[25] = { 19, 26, 32, 31 },
	[26] = { 27, 20, 19, 25, 32, 33 },
	[27] = { 21, 28, 20, 26, 33 },
	[28] = { 22, 23, 27, 29, 34 },
	[29] = { 23, 28, 24, 34, 30, 35 },
	[30] = { 24, 29, 35, 36 },
	[31] = { 25, 32 },
	[32] = { 26, 25, 33, 31 },
	[33] = { 27, 34, 26, 32 },
	[34] = { 28, 33, 29, 35 },
	[35] = { 29, 34, 30, 36 },
	[36] = { 30, 35 },
}

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function wheelKV(player)
	return player:kv():scoped("wheel")
end

local function wheelAppliedKV(player)
	return wheelKV(player):scoped("applied")
end

local function getWheelPlayerKey(player)
	if player.getGuid then
		return player:getGuid()
	end
	return player:getId()
end

local function scrollKV(player)
	return wheelKV(player):scoped("scrolls")
end

local function clampU16(value)
	value = math.floor(tonumber(value) or 0)
	if value < 0 then
		return 0
	end
	if value > 0xFFFF then
		return 0xFFFF
	end
	return value
end

local function getUnlockedScrolls(player)
	local store = scrollKV(player)
	local unlocked = {}
	for itemId, scroll in pairs(PROMOTION_SCROLLS) do
		if store:get(scroll.name) == true then
			unlocked[#unlocked + 1] = {
				itemId = itemId,
				name = scroll.name,
				points = scroll.points,
			}
		end
	end

	table.sort(unlocked, function(a, b)
		return a.itemId < b.itemId
	end)
	return unlocked
end

local function unlockWheelScroll(player, scrollName)
	local scroll = PROMOTION_SCROLLS_BY_NAME[scrollName]
	if not scroll then
		return false
	end

	local store = scrollKV(player)
	if store:get(scroll.name) == true then
		return false
	end

	store:set(scroll.name, true)
	return true
end

function Player.wheelUnlockScroll(self, scrollName)
	return unlockWheelScroll(self, scrollName)
end

local function getWheelVocation(player)
	local vocation = player:getVocation()
	local clientId = vocation and vocation:getClientId() or 0
	if clientId == 1 or clientId == 11 then
		return 1
	elseif clientId == 2 or clientId == 12 then
		return 2
	elseif clientId == 3 or clientId == 13 then
		return 3
	elseif clientId == 4 or clientId == 14 then
		return 4
	elseif clientId == 5 or clientId == 15 then
		return 5
	end
	return 0
end

local function getWheelPoints(player)
	local levelPoints = math.max(0, (player:getLevel() - (WHEEL_MIN_LEVEL - 1)) * WHEEL_POINTS_PER_LEVEL)
	return clampU16(math.min(WHEEL_MAX_ALLOCATABLE_POINTS, levelPoints))
end

local function getWheelExtraPoints(player)
	local total = 0
	for _, scroll in ipairs(getUnlockedScrolls(player)) do
		total = total + scroll.points
	end
	return clampU16(math.min(total, math.max(0, WHEEL_MAX_ALLOCATABLE_POINTS - getWheelPoints(player))))
end

local function getWheelTotalPoints(player)
	return clampU16(math.min(WHEEL_MAX_ALLOCATABLE_POINTS, getWheelPoints(player) + getWheelExtraPoints(player)))
end

local function hasWheelPremium(player)
	return not player.isPremium or player:isPremium()
end

local function isWheelPromoted(player)
	if not WHEEL_REQUIRE_PROMOTION then
		return true
	end

	if not player.isPromoted then
		return false
	end

	local ok, promoted = pcall(function()
		return player:isPromoted()
	end)
	return ok and promoted == true
end

local function canOpenWheel(player)
	return getWheelVocation(player) > 0 and player:getLevel() >= WHEEL_MIN_LEVEL and hasWheelPremium(player) and
	       isWheelPromoted(player)
end

local function emptyPoints()
	local points = {}
	for slot = 1, WHEEL_SLOT_COUNT do
		points[slot] = 0
	end
	return points
end

local function emptyGems()
	return { WHEEL_NO_GEM, WHEEL_NO_GEM, WHEEL_NO_GEM, WHEEL_NO_GEM }
end

local function normalizePointTable(points)
	local normalized = emptyPoints()
	if type(points) ~= "table" then
		return normalized
	end

	for slot = 1, WHEEL_SLOT_COUNT do
		normalized[slot] = clampU16(points[slot])
	end
	return normalized
end

local function normalizeGemTable(gems)
	local normalized = emptyGems()
	if type(gems) ~= "table" then
		return normalized
	end

	for index = 1, 4 do
		normalized[index] = clampU16(gems[index])
	end
	return normalized
end

local function calculateDomainPoints(points)
	local domains = { 0, 0, 0, 0 }
	for slot = 1, WHEEL_SLOT_COUNT do
		local domain = WHEEL_SLOT_DOMAINS[slot]
		domains[domain] = domains[domain] + (points[slot] or 0)
	end
	return domains
end

local function getStage(points)
	if points >= 1000 then
		return 3
	elseif points >= 500 then
		return 2
	elseif points >= 250 then
		return 1
	end
	return 0
end

local function buildRevelationStages(domainPoints)
	local domain1 = getStage(domainPoints[1] or 0)
	local domain2 = getStage(domainPoints[2] or 0)
	local domain3 = getStage(domainPoints[3] or 0)
	local domain4 = getStage(domainPoints[4] or 0)

	return {
		["Gift of Life"] = domain1,
		["Executioner's Throw"] = domain2,
		["Divine Grenade"] = domain2,
		["Beam Mastery"] = domain2,
		["Blessing of the Grove"] = domain2,
		["Spiritual Outburst"] = domain2,
		["Combat Mastery"] = domain3,
		["Divine Empowerment"] = domain3,
		["Drain Body"] = domain3,
		["Twin Bursts"] = domain3,
		["Ascetic"] = domain3,
		["Avatar of Steel"] = domain4,
		["Avatar of Light"] = domain4,
		["Avatar of Storm"] = domain4,
		["Avatar of Nature"] = domain4,
		["Avatar of Balance"] = domain4,
	}
end

local function loadProfile(player)
	local store = wheelKV(player)
	return {
		points = normalizePointTable(store:get("points")),
		gems = normalizeGemTable(store:get("gems")),
	}
end

local function saveProfile(player, points, gems)
	local domainPoints = calculateDomainPoints(points)
	local stages = buildRevelationStages(domainPoints)
	local usedPoints = 0
	for slot = 1, WHEEL_SLOT_COUNT do
		usedPoints = usedPoints + (points[slot] or 0)
	end

	local store = wheelKV(player)
	store:set("version", 1)
	store:set("points", points)
	store:set("gems", gems)
	store:set("domainPoints", domainPoints)
	store:set("revelationStages", stages)
	store:set("usedPoints", usedPoints)
	store:set("vocation", getWheelVocation(player))
	store:set("conditionSubId", WHEEL_CONDITION_SUBID)
	store:set("savedAt", os.time())
end

local function addBonus(bonuses, key, value)
	if value and value ~= 0 then
		bonuses[key] = (bonuses[key] or 0) + value
	end
end

local function addSpecialMagicBonus(bonuses, combatType, value)
	if not combatType or not value or value == 0 then
		return
	end

	bonuses.specialMagic[combatType] = (bonuses.specialMagic[combatType] or 0) + value
end

local function addWheelSpellGrade(bonuses, conviction)
	bonuses.spellGrades[conviction] = (bonuses.spellGrades[conviction] or 0) + 1
end

local function buildWheelSpellAugments(bonuses, vocationId)
	local vocationSpells = WHEEL_SPELL_BONUSES[vocationId] or {}
	for conviction, grade in pairs(bonuses.spellGrades) do
		local spell = vocationSpells[conviction]
		if spell then
			for _, spellName in ipairs(spell.names) do
				for index = 1, math.min(grade, #spell.grades) do
					for _, augment in ipairs(spell.grades[index]) do
						bonuses.spellAugments[#bonuses.spellAugments + 1] = {
							spellName = spellName,
							augmentType = augment[1],
							value = augment[2],
						}
					end
				end
			end
		end
	end
end

local function calculateWheelBonuses(player, points)
	local vocationId = getWheelVocation(player)
	local bonuses = {
		health = 0,
		mana = 0,
		capacity = 0,
		magic = 0,
		melee = 0,
		distance = 0,
		fist = 0,
		lifeLeech = 0,
		manaLeech = 0,
		mitigation = 0,
		specialMagic = {},
		spellGrades = {},
		spellAugments = {},
	}

	if vocationId == 0 then
		return bonuses
	end

	for slot = 1, WHEEL_SLOT_COUNT do
		local invested = points[slot] or 0
		local slotBonus = WHEEL_SLOT_BONUSES[slot]
		if invested > 0 and slotBonus then
			local dedication = slotBonus.dedication
			if dedication == "health" then
				addBonus(bonuses, "health", invested * (WHEEL_DEDICATION_VALUES.health[vocationId] or 0))
			elseif dedication == "mana" then
				addBonus(bonuses, "mana", invested * (WHEEL_DEDICATION_VALUES.mana[vocationId] or 0))
			elseif dedication == "capacity" then
				addBonus(bonuses, "capacity", invested * (WHEEL_DEDICATION_VALUES.capacity[vocationId] or 0))
			elseif dedication == "lifemana" then
				addBonus(bonuses, "health", invested * (WHEEL_DEDICATION_VALUES.lifemana.health[vocationId] or 0))
				addBonus(bonuses, "mana", invested * (WHEEL_DEDICATION_VALUES.lifemana.mana[vocationId] or 0))
			elseif dedication == "mitigation" then
				bonuses.mitigation = bonuses.mitigation + invested * 0.03
			end
		end

		if invested >= (WHEEL_SLOT_MAX_POINTS[slot] or 0) and slotBonus then
			local conviction = slotBonus.conviction
			if conviction == "lifeleech" then
				addBonus(bonuses, "lifeLeech", WHEEL_CONVICTION_VALUES.lifeleech)
			elseif conviction == "manaleech" then
				addBonus(bonuses, "manaLeech", WHEEL_CONVICTION_VALUES.manaleech)
			elseif conviction == "skill" then
				if vocationId == 1 then
					addBonus(bonuses, "melee", WHEEL_CONVICTION_VALUES.skill)
				elseif vocationId == 2 then
					addBonus(bonuses, "distance", WHEEL_CONVICTION_VALUES.skill)
				elseif vocationId == 3 or vocationId == 4 then
					addBonus(bonuses, "magic", WHEEL_CONVICTION_VALUES.skill)
				elseif vocationId == 5 then
					addBonus(bonuses, "fist", WHEEL_CONVICTION_VALUES.skill)
				end
			elseif conviction == "special_1" and vocationId == 2 then
				addSpecialMagicBonus(bonuses, COMBAT_HOLYDAMAGE, 3)
				addSpecialMagicBonus(bonuses, COMBAT_HEALING, 3)
			elseif WHEEL_SPELL_BONUSES[vocationId] and WHEEL_SPELL_BONUSES[vocationId][conviction] then
				addWheelSpellGrade(bonuses, conviction)
			end
		end
	end

	buildWheelSpellAugments(bonuses, vocationId)
	return bonuses
end

local function removeAppliedSpecialMagic(player)
	local key = getWheelPlayerKey(player)
	local applied = WHEEL_APPLIED_SPECIAL_MAGIC[key]
	if not applied or not player.addSpecialMagicLevel then
		WHEEL_APPLIED_SPECIAL_MAGIC[key] = nil
		return
	end

	for combatType, value in pairs(applied) do
		if value ~= 0 then
			player:addSpecialMagicLevel(combatType, -value)
		end
	end
	WHEEL_APPLIED_SPECIAL_MAGIC[key] = nil
end

local function removeAppliedMitigation(player)
	local key = getWheelPlayerKey(player)
	local applied = WHEEL_APPLIED_MITIGATION[key]
	if applied and applied ~= 0 and player.addMitigation then
		player:addMitigation(-applied)
	end
	WHEEL_APPLIED_MITIGATION[key] = nil
end

local function removeWheelBonuses(player)
	player:removeCondition(CONDITION_ATTRIBUTES, CONDITIONID_DEFAULT, WHEEL_CONDITION_SUBID, true)
	if player.clearWheelSpellAugments then
		player:clearWheelSpellAugments()
	end
	removeAppliedSpecialMagic(player)
	removeAppliedMitigation(player)

	local appliedStore = wheelAppliedKV(player)
	appliedStore:set("conditionSubId", WHEEL_CONDITION_SUBID)
	appliedStore:set("conditionApplied", false)
	appliedStore:set("specialMagic", {})
	appliedStore:set("mitigation", 0)
	appliedStore:set("updatedAt", os.time())
end

local function setConditionBonus(condition, parameter, value)
	if value and value ~= 0 then
		condition:setParameter(parameter, value)
		return true
	end
	return false
end

local WHEEL_SKILL_ABSORBS = {
	physical = COMBAT_PHYSICALDAMAGE,
	fire = COMBAT_FIREDAMAGE,
	earth = COMBAT_EARTHDAMAGE,
	energy = COMBAT_ENERGYDAMAGE,
	ice = COMBAT_ICEDAMAGE,
	holy = COMBAT_HOLYDAMAGE,
	death = COMBAT_DEATHDAMAGE,
	healing = COMBAT_HEALING,
	drown = COMBAT_DROWNDAMAGE,
	lifedrain = COMBAT_LIFEDRAIN,
	manadrain = COMBAT_MANADRAIN,
}

local COMBAT_TO_CIPBIA_ELEMENT = {
	[COMBAT_PHYSICALDAMAGE] = 0,
	[COMBAT_FIREDAMAGE] = 1,
	[COMBAT_EARTHDAMAGE] = 2,
	[COMBAT_ENERGYDAMAGE] = 3,
	[COMBAT_ICEDAMAGE] = 4,
	[COMBAT_HOLYDAMAGE] = 5,
	[COMBAT_DEATHDAMAGE] = 6,
	[COMBAT_HEALING] = 7,
	[COMBAT_DROWNDAMAGE] = 8,
	[COMBAT_LIFEDRAIN] = 9,
	[COMBAT_MANADRAIN] = 10,
	[COMBAT_AGONYDAMAGE] = 11,
}

local SHOOT_TO_CIPBIA_ELEMENT = {
	[CONST_ANI_FIRE] = 1,
	[CONST_ANI_ENERGY] = 3,       [CONST_ANI_ENERGYBALL] = 3,
	[CONST_ANI_SMALLICE] = 4,     [CONST_ANI_ICE] = 4,
	[CONST_ANI_SMALLEARTH] = 2,   [CONST_ANI_EARTH] = 2, [CONST_ANI_EARTHARROW] = 2,
	[CONST_ANI_DEATH] = 6,        [CONST_ANI_SUDDENDEATH] = 6,
	[CONST_ANI_SMALLHOLY] = 5,    [CONST_ANI_HOLY] = 5,
}

local function sendWheelSkillStats(player)
	if not supportsCustomNetwork(player) or not player.sendExtendedOpcode then
		return false
	end

	local lifeLeech = player:getSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT) / 10000
	local manaLeech = player:getSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT) / 10000
	local criticalChance = player:getSpecialSkill(SPECIALSKILL_CRITICALHITCHANCE) / 10000
	local criticalDamage = player:getSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT) / 10000

	local absorbs = {}
	if player.getCombatAbsorbPercent then
		for name, combatType in pairs(WHEEL_SKILL_ABSORBS) do
			absorbs[name] = player:getCombatAbsorbPercent(combatType) / 100
		end
	end

	local defense = player.getDefense and player:getDefense() or 0
	local armor = player.getArmor and player:getArmor() or 0

	local damageAndHealing = 0
	local attackValue = 0
	local attackElement = 0
	local convertedValue = 0
	local convertedElement = 0

	local weapon = player:getSlotItem(CONST_SLOT_LEFT)
	if not weapon or weapon:getId() == 0 then
		weapon = player:getSlotItem(CONST_SLOT_RIGHT)
	end

	if weapon and weapon:getId() ~= 0 then
		local it = ItemType(weapon:getId())
		attackValue = player:getWeaponAttackValue() or 0

		local elemCombatType = it:getElementType()
		local elemDamage = it:getElementDamage() or 0
		local shootType = it:getShootType()

		if elemCombatType and elemCombatType ~= COMBAT_NONE then
			attackElement = COMBAT_TO_CIPBIA_ELEMENT[elemCombatType] or 0
			local baseAtk = attackValue
			local totalAtk = baseAtk + elemDamage
			if totalAtk > 0 and elemDamage > 0 then
				convertedValue = elemDamage / totalAtk
				convertedElement = attackElement
			end
		elseif shootType and shootType ~= CONST_ANI_NONE then
			attackElement = SHOOT_TO_CIPBIA_ELEMENT[shootType] or 0
		else
			attackElement = 0
		end
	else
		attackValue = 7
		attackElement = 0
	end

	damageAndHealing = attackValue

	return player:sendExtendedOpcode(OPCODE_WHEEL_SKILLS, json.encode({
		lifeLeech = lifeLeech,
		manaLeech = manaLeech,
		criticalChance = criticalChance,
		criticalDamage = criticalDamage,
		defense = defense,
		armor = armor,
		mitigation = player:getMitigation() / 100,
		absorbs = absorbs,
		damageAndHealing = damageAndHealing,
		attackValue = attackValue,
		attackElement = attackElement,
		convertedValue = convertedValue,
		convertedElement = convertedElement,
	}))
end

function Player.wheelSendSkillStats(self)
	return sendWheelSkillStats(self)
end

local function applyWheelBonuses(player)
	removeWheelBonuses(player)

	local profile = loadProfile(player)
	local bonuses = calculateWheelBonuses(player, profile.points)
	local spellGrades = bonuses.spellGrades
	local spellAugments = bonuses.spellAugments
	bonuses.spellGrades = nil
	bonuses.spellAugments = nil
	wheelKV(player):set("bonusStats", bonuses)
	bonuses.spellGrades = spellGrades
	bonuses.spellAugments = spellAugments

	local condition = Condition(CONDITION_ATTRIBUTES, CONDITIONID_DEFAULT)
	condition:setParameter(CONDITION_PARAM_SUBID, WHEEL_CONDITION_SUBID)
	condition:setParameter(CONDITION_PARAM_TICKS, -1)

	local hasConditionBonus = false
	hasConditionBonus = setConditionBonus(condition, CONDITION_PARAM_STAT_MAXHITPOINTS, bonuses.health) or hasConditionBonus
	hasConditionBonus = setConditionBonus(condition, CONDITION_PARAM_STAT_MAXMANAPOINTS, bonuses.mana) or hasConditionBonus
	hasConditionBonus = setConditionBonus(condition, CONDITION_PARAM_STAT_CAPACITY, bonuses.capacity) or hasConditionBonus
	hasConditionBonus = setConditionBonus(condition, CONDITION_PARAM_STAT_MAGICPOINTS, bonuses.magic) or hasConditionBonus
	hasConditionBonus = setConditionBonus(condition, CONDITION_PARAM_SKILL_MELEE, bonuses.melee) or hasConditionBonus
	hasConditionBonus = setConditionBonus(condition, CONDITION_PARAM_SKILL_DISTANCE, bonuses.distance) or hasConditionBonus
	hasConditionBonus = setConditionBonus(condition, CONDITION_PARAM_SKILL_FIST, bonuses.fist) or hasConditionBonus
	hasConditionBonus = setConditionBonus(condition, CONDITION_PARAM_SPECIALSKILL_LIFELEECHAMOUNT, bonuses.lifeLeech) or hasConditionBonus
	hasConditionBonus = setConditionBonus(condition, CONDITION_PARAM_SPECIALSKILL_MANALEECHAMOUNT, bonuses.manaLeech) or hasConditionBonus

	if hasConditionBonus then
		player:addCondition(condition)
	end

	if player.addWheelSpellAugment then
		for _, augment in ipairs(bonuses.spellAugments) do
			player:addWheelSpellAugment(augment.spellName, augment.augmentType, augment.value)
		end
	end

	local key = getWheelPlayerKey(player)
	local appliedSpecialMagic = {}
	if player.addSpecialMagicLevel then
		for combatType, value in pairs(bonuses.specialMagic) do
			if value ~= 0 then
				player:addSpecialMagicLevel(combatType, value)
				appliedSpecialMagic[combatType] = value
			end
		end
	end

	if next(appliedSpecialMagic) then
		WHEEL_APPLIED_SPECIAL_MAGIC[key] = appliedSpecialMagic
	else
		WHEEL_APPLIED_SPECIAL_MAGIC[key] = nil
	end

	if bonuses.mitigation ~= 0 and player.addMitigation then
		WHEEL_APPLIED_MITIGATION[key] = bonuses.mitigation
		player:addMitigation(bonuses.mitigation)
	else
		WHEEL_APPLIED_MITIGATION[key] = nil
	end

	local appliedStore = wheelAppliedKV(player)
	appliedStore:set("conditionSubId", WHEEL_CONDITION_SUBID)
	appliedStore:set("conditionApplied", hasConditionBonus)
	appliedStore:set("specialMagic", appliedSpecialMagic)
	appliedStore:set("mitigation", bonuses.mitigation or 0)
	appliedStore:set("updatedAt", os.time())

	player:reloadData()
	sendWheelSkillStats(player)
	return bonuses
end

function Player.wheelApplyBonuses(self)
	return applyWheelBonuses(self)
end

local function validatePoints(player, points)
	local total = 0
	for slot = 1, WHEEL_SLOT_COUNT do
		local value = points[slot] or 0
		if value > WHEEL_SLOT_MAX_POINTS[slot] then
			return false, "Invalid wheel slot points."
		end
		total = total + value
	end

	if total > getWheelTotalPoints(player) then
		return false, "Not enough promotion points."
	end

	for slot = 1, WHEEL_SLOT_COUNT do
		local value = points[slot] or 0
		if value > 0 and WHEEL_SLOT_MAX_POINTS[slot] ~= 50 then
			local prerequisites = WHEEL_SLOT_PREREQUISITES[slot]
			if prerequisites and #prerequisites > 0 then
				local unlocked = false
				for _, prerequisite in ipairs(prerequisites) do
					if (points[prerequisite] or 0) >= WHEEL_SLOT_MAX_POINTS[prerequisite] then
						unlocked = true
						break
					end
				end
				if not unlocked then
					return false, "Wheel path is not connected."
				end
			end
		end
	end

	return true
end

local function sendResourceBalance(player, resourceType, value)
	if not supportsCustomNetwork(player) then
		return false
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_RESOURCE_BALANCE)
	out:addByte(resourceType)
	out:addU64(math.max(0, tonumber(value) or 0))
	return out:sendToPlayer(player)
end

local function sendWheelResources(player, vocationId)
	local gemItems = GEM_ITEMS[vocationId] or {}
	sendResourceBalance(player, RESOURCE_BANK, player:getBankBalance())
	sendResourceBalance(player, RESOURCE_INVENTORY, player:getMoney())
	sendResourceBalance(player, RESOURCE_LESSER_GEMS, gemItems[1] and player:getItemCount(gemItems[1]) or 0)
	sendResourceBalance(player, RESOURCE_REGULAR_GEMS, gemItems[2] and player:getItemCount(gemItems[2]) or 0)
	sendResourceBalance(player, RESOURCE_GREATER_GEMS, gemItems[3] and player:getItemCount(gemItems[3]) or 0)
	sendResourceBalance(player, RESOURCE_LESSER_FRAGMENTS, player:getItemCount(ITEM_LESSER_FRAGMENT))
	sendResourceBalance(player, RESOURCE_GREATER_FRAGMENTS, player:getItemCount(ITEM_GREATER_FRAGMENT))
end

local function sendWheelWindow(player, ownerId)
	if not supportsCustomNetwork(player) then
		return false
	end

	ownerId = tonumber(ownerId) or player:getId()
	local vocationId = getWheelVocation(player)
	local canView = canOpenWheel(player)
	sendWheelResources(player, vocationId)

	local out = NetworkMessage(player)
	out:addByte(OPCODE_WHEEL_WINDOW)
	out:addU32(ownerId)
	out:addByte(canView and 1 or 0)
	if not canView then
		return out:sendToPlayer(player)
	end

	local profile = loadProfile(player)
	local unlockedScrolls = getUnlockedScrolls(player)
	local canEdit = ownerId == player:getId()
	out:addByte(canEdit and 1 or 0)
	out:addByte(vocationId)
	out:addU16(getWheelPoints(player))
	out:addU16(getWheelExtraPoints(player))

	for slot = 1, WHEEL_SLOT_COUNT do
		out:addU16(profile.points[slot] or 0)
	end

	out:addU16(#unlockedScrolls)
	for _, scroll in ipairs(unlockedScrolls) do
		out:addU16(scroll.itemId)
	end
	out:addByte(0) -- active gem count
	out:addU16(0) -- revealed gem count
	out:addByte(0) -- basic upgrade count
	out:addByte(0) -- supreme upgrade count

	return out:sendToPlayer(player)
end

local function readSaveGems(msg)
	local gems = emptyGems()
	for index = 1, 4 do
		if msg:len() - msg:tell() < 1 then
			return gems
		end

		local hasGem = msg:getByte() ~= 0
		if hasGem then
			if msg:len() - msg:tell() < 2 then
				return nil
			end
			gems[index] = msg:getU16()
		end
	end
	return gems
end

local openHandler = PacketHandler(OPCODE_WHEEL_OPEN)

function openHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 4 then
		return
	end

	sendWheelWindow(player, msg:getU32())
end

openHandler:register()

local saveHandler = PacketHandler(OPCODE_WHEEL_SAVE)

function saveHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < WHEEL_SLOT_COUNT * 2 then
		return
	end

	if not canOpenWheel(player) then
		sendWheelWindow(player, player:getId())
		return
	end

	local points = {}
	for slot = 1, WHEEL_SLOT_COUNT do
		points[slot] = msg:getU16()
	end

	local gems = readSaveGems(msg)
	if not gems then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "Invalid wheel packet.")
		sendWheelWindow(player, player:getId())
		return
	end

	local valid, reason = validatePoints(player, points)
	if not valid then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, reason)
		sendWheelWindow(player, player:getId())
		return
	end

	saveProfile(player, points, gems)
	applyWheelBonuses(player)
	sendWheelWindow(player, player:getId())
end

saveHandler:register()

local gemActionHandler = PacketHandler(OPCODE_WHEEL_GEM_ACTION)

function gemActionHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 2 then
		return
	end

	msg:getByte() -- action type
	msg:getByte() -- parameter
	if msg:len() - msg:tell() >= 1 then
		msg:getByte() -- optional position for grade improvement
	end

	sendWheelWindow(player, player:getId())
end

gemActionHandler:register()

local wheelLoginEvent = CreatureEvent("WheelOfDestinyLogin")

function wheelLoginEvent.onLogin(player)
	player:registerEvent("WheelOfDestinyLogout")
	applyWheelBonuses(player)
	return true
end

wheelLoginEvent:register()

local wheelLogoutEvent = CreatureEvent("WheelOfDestinyLogout")

function wheelLogoutEvent.onLogout(player)
	local key = getWheelPlayerKey(player)
	WHEEL_APPLIED_SPECIAL_MAGIC[key] = nil
	WHEEL_APPLIED_MITIGATION[key] = nil
	return true
end

wheelLogoutEvent:register()
