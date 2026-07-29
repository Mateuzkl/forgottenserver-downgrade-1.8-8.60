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
local WHEEL_NO_GEM = -1
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

local GEM_ACTION = {
	DESTROY = 0,
	REVEAL = 1,
	SWITCH_DOMAIN = 2,
	TOGGLE_LOCK = 3,
	IMPROVE_GRADE = 4,
}

local GEM_QUALITY = {
	LESSER = 0,
	REGULAR = 1,
	GREATER = 2,
}

local FRAGMENT_TYPE = {
	GREATER = 0,
	LESSER = 1,
}

local GEM_REVEAL_COST = {
	[GEM_QUALITY.LESSER] = 125000,
	[GEM_QUALITY.REGULAR] = 1000000,
	[GEM_QUALITY.GREATER] = 6000000,
}

local GEM_ROTATE_COST = {
	[GEM_QUALITY.LESSER] = 125000,
	[GEM_QUALITY.REGULAR] = 250000,
	[GEM_QUALITY.GREATER] = 500000,
}

local BASIC_SLOT_1_MODIFIERS = {
	3, 5, 6, 4, 30, 31, 37, 48, 38, 41,
	39, 40, 33, 34, 35, 36, 44, 45, 46, 47,
}

local BASIC_SLOT_2_MODIFIERS = {
	3, 5, 6, 4, 0, 1, 7, 8, 9, 10,
	11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
}

local BASIC_MODIFIER_POSITIONS = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
	30, 31, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 44, 45, 46, 47, 48,
}

local SUPREME_MODIFIER_POSITIONS = {
	[1] = { 0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 14, 16, 17, 19, 18, 20, 21, 22, 23 },
	[2] = { 0, 1, 2, 3, 5, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41 },
	[3] = { 0, 1, 2, 3, 4, 5, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58 },
	[4] = { 0, 1, 2, 3, 4, 5, 59, 60, 61, 62, 63, 64, 66, 65, 67, 68, 69, 70, 71, 72, 73, 74, 75 },
	[5] = { 0, 1, 2, 3, 5, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93 },
}

local BASIC_GRADE_COST = {
	[1] = { money = 2000000, fragments = 5 },
	[2] = { money = 5000000, fragments = 15 },
	[3] = { money = 30000000, fragments = 30 },
}

local SUPREME_GRADE_COST = {
	[1] = { money = 5000000, fragments = 5 },
	[2] = { money = 12000000, fragments = 15 },
	[3] = { money = 75000000, fragments = 30 },
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
local SPECIAL_MAGE_SPELLS = {
	"Strong Energy Strike", "Strong Flame Strike", "Strong Ice Strike", "Strong Terra Strike",
	"Ultimate Energy Strike", "Ultimate Flame Strike", "Ultimate Ice Strike", "Ultimate Terra Strike",
}
local FORKED_DRUID_SPELLS = { "Forked Glacier", "Forked Thorns" }

-- Kept in the same order as Canary's wheel spell table. Each spell_N node exists
-- twice on the wheel: completing one unlocks grade I and completing both unlocks grade II.
local WHEEL_SPELL_BONUSES = {
	[1] = {
		spell_1 = { names = { "Front Sweep" }, grades = {
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.40 } },
			{ { AUGMENT_TYPE.AFFECTED_AREA_ENLARGED, 1 } },
		} },
		spell_2 = { names = { "Groundshaker" }, grades = {
			{ { AUGMENT_TYPE.COOLDOWN, -2 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.125 } },
		} },
		spell_3 = { names = { "Shield Slam" }, grades = {
			{ { AUGMENT_TYPE.LIFE_LEECH, 0.15 } },
			{ { AUGMENT_TYPE.INCREASED_DAMAGE_REDUCTION, 0.25 } },
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
		spell_1 = { names = { "Ethereal Barrage" }, grades = {
			{ { AUGMENT_TYPE.LIFE_LEECH, 0.10 } },
			{ { AUGMENT_TYPE.CRITICAL_HIT_CHANCE, 0.10 } },
		} },
		spell_2 = { names = { "Strong Ethereal Spear" }, grades = {
			{ { AUGMENT_TYPE.COOLDOWN, -2 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 3.80 } },
		} },
		spell_3 = { names = { "Divine Dazzle" }, grades = {
			{ { AUGMENT_TYPE.ADDITIONAL_TARGETS, 2 } },
			{ { AUGMENT_TYPE.DURATION_INCREASED, 4 }, { AUGMENT_TYPE.COOLDOWN, -8 } },
		} },
		spell_4 = { names = { "Divine Barrage" }, grades = {
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.10 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.15 } },
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
		spell_2 = { names = SPECIAL_MAGE_SPELLS, grades = {
			{ { AUGMENT_TYPE.COOLDOWN, -4 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.50 } },
		} },
		spell_3 = { names = { "Death Echo" }, grades = {
			{ { AUGMENT_TYPE.COOLDOWN, -2 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.08 } },
		} },
		spell_4 = { names = { "Energy Wave" }, grades = {
			{ { AUGMENT_TYPE.AFFECTED_AREA_ENLARGED, 1 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.10 } },
		} },
		spell_5 = { names = { "Great Fire Wave" }, grades = {
			{ { AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, 0.15 }, { AUGMENT_TYPE.CRITICAL_HIT_CHANCE, 0.10 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.05 } },
		} },
	},
	[4] = {
		spell_1 = { names = { "Strong Ice Wave" }, grades = {
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.06 } },
			{ { AUGMENT_TYPE.AFFECTED_AREA_ENLARGED, 1 } },
		} },
		spell_2 = { names = { "Mass Healing" }, grades = {
			{ { AUGMENT_TYPE.BASE_HEALING, 0.04 } },
			{ { AUGMENT_TYPE.AFFECTED_AREA_ENLARGED, 1 } },
		} },
		spell_3 = { names = FORKED_DRUID_SPELLS, grades = {
			{ { AUGMENT_TYPE.COOLDOWN, -2 } },
			{ { AUGMENT_TYPE.ADDITIONAL_TARGETS, 1 } },
		} },
		spell_4 = { names = { "Terra Wave" }, grades = {
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.065 } },
			{ { AUGMENT_TYPE.LIFE_LEECH, 0.10 } },
		} },
		spell_5 = { names = { "Heal Friend" }, grades = {
			{ { AUGMENT_TYPE.BASE_HEALING, 0.04 } },
			{ { AUGMENT_TYPE.BASE_HEALING, 0.06 } },
		} },
	},
	[5] = {
		spell_1 = { names = { "Thousand Fist Blows" }, grades = {
			{ { AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, 0.40 } },
			{ { AUGMENT_TYPE.COOLDOWN, -6 } },
		} },
		spell_2 = { names = { "Mass Spirit Mend" }, grades = {
			{ { AUGMENT_TYPE.BASE_HEALING, 0.08 } },
			{ { AUGMENT_TYPE.COOLDOWN, -4 } },
		} },
		spell_3 = { names = { "Mystic Repulse" }, grades = {
			{ { AUGMENT_TYPE.COOLDOWN, -6 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.40 } },
		} },
		spell_4 = { names = { "Chained Penance" }, grades = {
			{ { AUGMENT_TYPE.ADDITIONAL_TARGETS, 1 } },
			{ { AUGMENT_TYPE.ADDITIONAL_TARGETS, 1 } },
		} },
		spell_5 = { names = { "Flurry of Blows" }, grades = {
			{ { AUGMENT_TYPE.AFFECTED_AREA_ENLARGED, 1 } },
			{ { AUGMENT_TYPE.BASE_DAMAGE, 0.15 } },
		} },
	},
}

local WHEEL_APPLIED_SPECIAL_MAGIC = {}
local WHEEL_APPLIED_MITIGATION = {}
local WHEEL_APPLIED_MITIGATION_MULTIPLIER = {}
local WHEEL_APPLIED_RESISTANCES = {}
local WHEEL_APPLIED_DODGE = {}

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

local function emptyGems()
	return { WHEEL_NO_GEM, WHEEL_NO_GEM, WHEEL_NO_GEM, WHEEL_NO_GEM }
end

local function randomFrom(values)
	return values[math.random(1, #values)]
end

local function randomBasicModifier2(firstModifier)
	local modifier = randomFrom(BASIC_SLOT_2_MODIFIERS)
	while modifier == firstModifier do
		modifier = randomFrom(BASIC_SLOT_2_MODIFIERS)
	end
	return modifier
end

local function isPositionAllowed(positions, position)
	for _, allowed in ipairs(positions or {}) do
		if allowed == position then
			return true
		end
	end
	return false
end

local function createWheelGem(vocationId, affinity, quality)
	local gem = {
		locked = false,
		affinity = affinity,
		quality = quality,
		basic1 = randomFrom(BASIC_SLOT_1_MODIFIERS),
		basic2 = 0,
		supreme = 0,
	}
	if quality >= GEM_QUALITY.REGULAR then
		gem.basic2 = randomBasicModifier2(gem.basic1)
	end
	if quality >= GEM_QUALITY.GREATER then
		gem.supreme = randomFrom(SUPREME_MODIFIER_POSITIONS[vocationId] or { 0 })
	end
	return gem
end

local function normalizeRevealedGems(gems, vocationId)
	local normalized = {}
	if type(gems) ~= "table" then
		return normalized
	end

	for _, gem in ipairs(gems) do
		if type(gem) == "table" and #normalized < 0xFF then
			local affinity = math.floor(tonumber(gem.affinity) or -1)
			local quality = math.floor(tonumber(gem.quality) or -1)
			local basic1 = math.floor(tonumber(gem.basic1) or -1)
			local basic2 = math.floor(tonumber(gem.basic2) or 0)
			local supreme = math.floor(tonumber(gem.supreme) or 0)
			if affinity >= 0 and affinity <= 3 and quality >= GEM_QUALITY.LESSER and quality <= GEM_QUALITY.GREATER and
			   isPositionAllowed(BASIC_SLOT_1_MODIFIERS, basic1) and
			   (quality < GEM_QUALITY.REGULAR or isPositionAllowed(BASIC_SLOT_2_MODIFIERS, basic2)) and
			   (quality < GEM_QUALITY.GREATER or isPositionAllowed(SUPREME_MODIFIER_POSITIONS[vocationId], supreme)) then
				normalized[#normalized + 1] = {
					locked = gem.locked == true or gem.locked == 1,
					affinity = affinity,
					quality = quality,
					basic1 = basic1,
					basic2 = basic2,
					supreme = supreme,
				}
			end
		end
	end
	return normalized
end

local function normalizeGrades(grades, maxPosition)
	local normalized = {}
	for index = 1, maxPosition + 1 do
		local grade = type(grades) == "table" and math.floor(tonumber(grades[index]) or 0) or 0
		normalized[index] = math.max(0, math.min(3, grade))
	end
	return normalized
end

local function ensureInitialGems(player)
	local store = wheelKV(player)
	local vocationId = getWheelVocation(player)
	local revealed = normalizeRevealedGems(store:get("revealedGems"), vocationId)
	if store:get("initialGems") ~= true then
		for affinity = 0, 3 do
			revealed[#revealed + 1] = createWheelGem(vocationId, affinity, GEM_QUALITY.LESSER)
			revealed[#revealed + 1] = createWheelGem(vocationId, affinity, GEM_QUALITY.REGULAR)
		end
		store:set("revealedGems", revealed)
		store:set("initialGems", true)
	end
	return revealed
end

local function loadGemState(player)
	return {
		revealed = ensureInitialGems(player),
		basicGrades = normalizeGrades(wheelKV(player):get("basicGrades"), 48),
		supremeGrades = normalizeGrades(wheelKV(player):get("supremeGrades"), 93),
	}
end

local function saveGemState(player, state)
	local store = wheelKV(player)
	store:set("revealedGems", state.revealed)
	store:set("basicGrades", state.basicGrades)
	store:set("supremeGrades", state.supremeGrades)
end

local function getMaxGradeModifierPoints(player, state)
	state = state or loadGemState(player)
	local points = 0
	for _, position in ipairs(BASIC_MODIFIER_POSITIONS) do
		if state.basicGrades[position + 1] == 3 then
			points = points + 1
		end
	end
	for _, position in ipairs(SUPREME_MODIFIER_POSITIONS[getWheelVocation(player)] or {}) do
		if state.supremeGrades[position + 1] == 3 then
			points = points + 1
		end
	end
	return points
end

local function validateActiveGems(gems, revealed)
	local normalized = emptyGems()
	for affinityIndex = 1, 4 do
		local gemIndex = gems[affinityIndex]
		local gem = gemIndex and gemIndex >= 0 and revealed[gemIndex + 1] or nil
		if gem and gem.affinity == affinityIndex - 1 then
			normalized[affinityIndex] = gemIndex
		end
	end
	return normalized
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
	return clampU16(math.min(WHEEL_MAX_ALLOCATABLE_POINTS,
	                        getWheelPoints(player) + getWheelExtraPoints(player) + getMaxGradeModifierPoints(player)))
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
		local gemIndex = math.floor(tonumber(gems[index]) or WHEEL_NO_GEM)
		if gemIndex >= 0 and gemIndex <= 0xFFFF then
			normalized[index] = gemIndex
		end
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

local REVELATION_SPELLS = {
	[1] = { red = "Executioner's Throw", blue = "Combat Mastery", purple = "Avatar of Steel" },
	[2] = { red = "Divine Grenade", blue = "Divine Empowerment", purple = "Avatar of Light" },
	[3] = { red = "Beam Mastery", blue = "Drain Body", purple = "Avatar of Storm" },
	[4] = { red = "Blessing of the Grove", blue = "Twin Bursts", purple = "Avatar of Nature" },
	[5] = { red = "Spiritual Outburst", blue = "Ascetic", purple = "Avatar of Balance" },
}

local function buildRevelationStages(domainPoints, vocationId, revelationBonus, masteryPoints)
	revelationBonus = revelationBonus or { 0, 0, 0, 0 }
	masteryPoints = masteryPoints or 0
	local stages = {
		["Gift of Life"] = getStage((domainPoints[1] or 0) + (revelationBonus[1] or 0) + masteryPoints),
	}
	local vocationSpells = REVELATION_SPELLS[vocationId]
	if vocationSpells then
		stages[vocationSpells.red] = getStage((domainPoints[2] or 0) + (revelationBonus[2] or 0) + masteryPoints)
		stages[vocationSpells.blue] = getStage((domainPoints[3] or 0) + (revelationBonus[3] or 0) + masteryPoints)
		stages[vocationSpells.purple] = getStage((domainPoints[4] or 0) + (revelationBonus[4] or 0) + masteryPoints)
	end
	return stages
end

local function loadProfile(player)
	local store = wheelKV(player)
	local gems = normalizeGemTable(store:get("gems"))
	if (tonumber(store:get("version")) or 0) < 2 then
		for affinityIndex = 1, 4 do
			if gems[affinityIndex] == 0 then
				gems[affinityIndex] = WHEEL_NO_GEM
			end
		end
	end
	return {
		points = normalizePointTable(store:get("points")),
		gems = gems,
	}
end

local function saveProfile(player, points, gems)
	local domainPoints = calculateDomainPoints(points)
	local stages = buildRevelationStages(domainPoints, getWheelVocation(player), nil, getMaxGradeModifierPoints(player))
	local usedPoints = 0
	for slot = 1, WHEEL_SLOT_COUNT do
		usedPoints = usedPoints + (points[slot] or 0)
	end

	local store = wheelKV(player)
	store:set("version", 2)
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

local BASIC_RESISTANCE_EFFECTS = {
	[0] = { { COMBAT_PHYSICALDAMAGE, 1 } },
	[1] = { { COMBAT_HOLYDAMAGE, 1 } },
	[2] = { { COMBAT_DEATHDAMAGE, 1 } },
	[3] = { { COMBAT_FIREDAMAGE, 2 } },
	[4] = { { COMBAT_EARTHDAMAGE, 2 } },
	[5] = { { COMBAT_ICEDAMAGE, 2 } },
	[6] = { { COMBAT_ENERGYDAMAGE, 2 } },
	[7] = { { COMBAT_HOLYDAMAGE, 1.5 }, { COMBAT_DEATHDAMAGE, -1, true } },
	[8] = { { COMBAT_DEATHDAMAGE, 1.5 }, { COMBAT_HOLYDAMAGE, -1, true } },
	[9] = { { COMBAT_FIREDAMAGE, 1 }, { COMBAT_EARTHDAMAGE, 1 } },
	[10] = { { COMBAT_FIREDAMAGE, 1 }, { COMBAT_ICEDAMAGE, 1 } },
	[11] = { { COMBAT_FIREDAMAGE, 1 }, { COMBAT_ENERGYDAMAGE, 1 } },
	[12] = { { COMBAT_EARTHDAMAGE, 1 }, { COMBAT_ICEDAMAGE, 1 } },
	[13] = { { COMBAT_EARTHDAMAGE, 1 }, { COMBAT_ENERGYDAMAGE, 1 } },
	[14] = { { COMBAT_ICEDAMAGE, 1 }, { COMBAT_ENERGYDAMAGE, 1 } },
	[15] = { { COMBAT_FIREDAMAGE, 3 }, { COMBAT_EARTHDAMAGE, -2, true } },
	[16] = { { COMBAT_FIREDAMAGE, 3 }, { COMBAT_ICEDAMAGE, -2, true } },
	[17] = { { COMBAT_FIREDAMAGE, 3 }, { COMBAT_ENERGYDAMAGE, -2, true } },
	[18] = { { COMBAT_EARTHDAMAGE, 3 }, { COMBAT_FIREDAMAGE, -2, true } },
	[19] = { { COMBAT_EARTHDAMAGE, 3 }, { COMBAT_ICEDAMAGE, -2, true } },
	[20] = { { COMBAT_EARTHDAMAGE, 3 }, { COMBAT_ENERGYDAMAGE, -2, true } },
	[21] = { { COMBAT_ICEDAMAGE, 3 }, { COMBAT_EARTHDAMAGE, -2, true } },
	[22] = { { COMBAT_ICEDAMAGE, 3 }, { COMBAT_FIREDAMAGE, -2, true } },
	[23] = { { COMBAT_ICEDAMAGE, 3 }, { COMBAT_ENERGYDAMAGE, -2, true } },
	[24] = { { COMBAT_ENERGYDAMAGE, 3 }, { COMBAT_EARTHDAMAGE, -2, true } },
	[25] = { { COMBAT_ENERGYDAMAGE, 3 }, { COMBAT_ICEDAMAGE, -2, true } },
	[26] = { { COMBAT_ENERGYDAMAGE, 3 }, { COMBAT_FIREDAMAGE, -2, true } },
	[27] = { { COMBAT_MANADRAIN, 3 } },
	[28] = { { COMBAT_LIFEDRAIN, 3 } },
	[29] = { { COMBAT_MANADRAIN, 1.5 }, { COMBAT_LIFEDRAIN, 1.5 } },
}

local GEM_HEALTH_FULL = { 300, 200, 100, 100, 200 }
local GEM_HEALTH_PARTIAL = { 150, 100, 50, 50, 100 }
local GEM_MANA_FULL = { 100, 300, 600, 600, 200 }
local GEM_MANA_PARTIAL = { 50, 150, 300, 300, 0 }
local GEM_CAPACITY_FULL = { 500, 400, 200, 200, 500 }
local GEM_CAPACITY_PARTIAL = { 250, 200, 100, 100, 250 }

local BASIC_STAT_EFFECTS = {
	[31] = { stat = "health", values = GEM_HEALTH_FULL },
	[33] = { stat = "mana", values = GEM_MANA_PARTIAL, combatType = COMBAT_FIREDAMAGE },
	[34] = { stat = "mana", values = GEM_MANA_PARTIAL, combatType = COMBAT_ENERGYDAMAGE },
	[35] = { stat = "mana", values = GEM_MANA_PARTIAL, combatType = COMBAT_EARTHDAMAGE },
	[36] = { stat = "mana", values = GEM_MANA_PARTIAL, combatType = COMBAT_ICEDAMAGE },
	[37] = { stat = "mana", values = GEM_MANA_FULL },
	[38] = { stat = "health", values = GEM_HEALTH_PARTIAL, combatType = COMBAT_FIREDAMAGE },
	[39] = { stat = "health", values = GEM_HEALTH_PARTIAL, combatType = COMBAT_ENERGYDAMAGE },
	[40] = { stat = "health", values = GEM_HEALTH_PARTIAL, combatType = COMBAT_EARTHDAMAGE },
	[41] = { stat = "health", values = GEM_HEALTH_PARTIAL, combatType = COMBAT_ICEDAMAGE },
	[44] = { stat = "capacity", values = GEM_CAPACITY_PARTIAL, combatType = COMBAT_FIREDAMAGE },
	[45] = { stat = "capacity", values = GEM_CAPACITY_PARTIAL, combatType = COMBAT_ENERGYDAMAGE },
	[46] = { stat = "capacity", values = GEM_CAPACITY_PARTIAL, combatType = COMBAT_EARTHDAMAGE },
	[47] = { stat = "capacity", values = GEM_CAPACITY_PARTIAL, combatType = COMBAT_ICEDAMAGE },
	[48] = { stat = "capacity", values = GEM_CAPACITY_FULL },
}

local SUPREME_EFFECTS = {
	[0] = { stat = "dodge", value = 0.28 },
	[1] = { stat = "criticalDamage", value = 200 },
	[2] = { stat = "lifeLeech", value = 200 },
	[3] = { stat = "manaLeech", value = 80 },
	[4] = { spell = "Ultimate Healing", augment = AUGMENT_TYPE.BASE_HEALING, value = 0.05 },
	[5] = { revelation = 1, value = 150 },

	[6] = { spell = "Avatar of Steel", augment = AUGMENT_TYPE.COOLDOWN, value = -900 },
	[7] = { spell = "Executioner's Throw", augment = AUGMENT_TYPE.COOLDOWN, value = -2 },
	[8] = { spell = "Executioner's Throw", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.06 },
	[9] = { spell = "Executioner's Throw", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[10] = { spell = "Fierce Berserk", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.05 },
	[11] = { spell = "Fierce Berserk", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.08 },
	[12] = { spell = "Berserk", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.05 },
	[13] = { spell = "Berserk", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[14] = { spell = "Front Sweep", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.08 },
	[15] = { spell = "Front Sweep", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[16] = { spell = "Groundshaker", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.065 },
	[17] = { spell = "Groundshaker", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[18] = { spell = "Annihilation", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.12 },
	[19] = { spell = "Annihilation", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.15 },
	[20] = { spell = "Fair Wound Cleansing", augment = AUGMENT_TYPE.BASE_HEALING, value = 0.10 },
	[21] = { revelation = 4, value = 150 },
	[22] = { revelation = 2, value = 150 },
	[23] = { revelation = 3, value = 150 },

	[24] = { spell = "Avatar of Light", augment = AUGMENT_TYPE.COOLDOWN, value = -900 },
	[25] = { spell = "Divine Dazzle", augment = AUGMENT_TYPE.COOLDOWN, value = -4 },
	[26] = { spell = "Divine Grenade", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.06 },
	[27] = { spell = "Divine Grenade", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[28] = { spell = "Divine Caldera", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.05 },
	[29] = { spell = "Divine Caldera", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[30] = { spell = "Divine Missile", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.08 },
	[31] = { spell = "Divine Missile", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[32] = { spell = "Ethereal Spear", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.10 },
	[33] = { spell = "Ethereal Spear", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.15 },
	[34] = { spell = "Strong Ethereal Spear", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.08 },
	[35] = { spell = "Strong Ethereal Spear", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[36] = { spell = "Divine Empowerment", augment = AUGMENT_TYPE.COOLDOWN, value = -6 },
	[37] = { spell = "Divine Grenade", augment = AUGMENT_TYPE.COOLDOWN, value = -2 },
	[38] = { spell = "Salvation", augment = AUGMENT_TYPE.BASE_HEALING, value = 0.06 },
	[39] = { revelation = 4, value = 150 },
	[40] = { revelation = 2, value = 150 },
	[41] = { revelation = 3, value = 150 },

	[42] = { spell = "Avatar of Storm", augment = AUGMENT_TYPE.COOLDOWN, value = -900 },
	[43] = { spell = "Energy Wave", augment = AUGMENT_TYPE.COOLDOWN, value = -1 },
	[44] = { spell = "Great Death Beam", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.10 },
	[45] = { spell = "Great Death Beam", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.15 },
	[46] = { spell = "Hell's Core", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.08 },
	[47] = { spell = "Hell's Core", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[48] = { spell = "Energy Wave", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.05 },
	[49] = { spell = "Energy Wave", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[50] = { spell = "Great Fire Wave", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.05 },
	[51] = { spell = "Great Fire Wave", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.08 },
	[52] = { spell = "Rage of the Skies", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.08 },
	[53] = { spell = "Rage of the Skies", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[54] = { spell = "Great Energy Beam", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.10 },
	[55] = { spell = "Great Energy Beam", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.15 },
	[56] = { revelation = 4, value = 150 },
	[57] = { revelation = 2, value = 150 },
	[58] = { revelation = 3, value = 150 },

	[59] = { spell = "Avatar of Nature", augment = AUGMENT_TYPE.COOLDOWN, value = -900 },
	[60] = { spell = "Nature's Embrace", augment = AUGMENT_TYPE.COOLDOWN, value = -5 },
	[61] = { spell = "Terra Burst", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.07 },
	[62] = { spell = "Terra Burst", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[63] = { spell = "Ice Burst", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.07 },
	[64] = { spell = "Ice Burst", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[65] = { spell = "Eternal Winter", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[66] = { spell = "Eternal Winter", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.08 },
	[67] = { spell = "Terra Wave", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.05 },
	[68] = { spell = "Terra Wave", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.12 },
	[69] = { spell = "Strong Ice Wave", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.08 },
	[70] = { spell = "Strong Ice Wave", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.15 },
	[71] = { spell = "Heal Friend", augment = AUGMENT_TYPE.BASE_HEALING, value = 0.05 },
	[72] = { spell = "Mass Healing", augment = AUGMENT_TYPE.BASE_HEALING, value = 0.05 },
	[73] = { revelation = 4, value = 150 },
	[74] = { revelation = 2, value = 150 },
	[75] = { revelation = 3, value = 150 },

	[76] = { spell = "Avatar of Balance", augment = AUGMENT_TYPE.COOLDOWN, value = -900 },
	[77] = { spell = "Spirit Mend", augment = AUGMENT_TYPE.BASE_HEALING, value = 0.06 },
	[78] = { spell = "Spiritual Outburst", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.05 },
	[79] = { spell = "Spiritual Outburst", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.08 },
	[80] = { spell = "Forceful Uppercut", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.10 },
	[81] = { spell = "Forceful Uppercut", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.08 },
	[82] = { spell = "Flurry of Blows", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.065 },
	[83] = { spell = "Flurry of Blows", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.08 },
	[84] = { spell = "Greater Flurry of Blows", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.05 },
	[85] = { spell = "Greater Flurry of Blows", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.08 },
	[86] = { spell = "Sweeping Takedown", augment = AUGMENT_TYPE.BASE_DAMAGE, value = 0.08 },
	[87] = { spell = "Sweeping Takedown", augment = AUGMENT_TYPE.CRITICAL_EXTRA_DAMAGE, value = 0.08 },
	[88] = { spell = "Focus Serenity", augment = AUGMENT_TYPE.COOLDOWN, value = -150 },
	[89] = { spell = "Focus Harmony", augment = AUGMENT_TYPE.COOLDOWN, value = -30 },
	[90] = { spell = "Mass Spirit Mend", augment = AUGMENT_TYPE.BASE_HEALING, value = 0.05 },
	[91] = { revelation = 4, value = 150 },
	[92] = { revelation = 2, value = 150 },
	[93] = { revelation = 3, value = 150 },
}

local function getGradeMultiplier(grade)
	if grade == 1 then
		return 1.1
	elseif grade == 2 then
		return 1.2
	elseif grade == 3 then
		return 1.5
	end
	return 1
end

local function addResistanceBonus(bonuses, combatType, value)
	bonuses.resistances[combatType] = (bonuses.resistances[combatType] or 0) + value
end

local function applyBasicGemModifier(bonuses, vocationId, modifier, grade)
	local multiplier = getGradeMultiplier(grade)
	local resistanceEffects = BASIC_RESISTANCE_EFFECTS[modifier]
	if resistanceEffects then
		for _, effect in ipairs(resistanceEffects) do
			addResistanceBonus(bonuses, effect[1], effect[3] and effect[2] or effect[2] * multiplier)
		end
		return
	end

	if modifier == 30 then
		addBonus(bonuses, "mitigationMultiplier", 20 * multiplier)
		return
	end

	local statEffect = BASIC_STAT_EFFECTS[modifier]
	if statEffect then
		addBonus(bonuses, statEffect.stat, (statEffect.values[vocationId] or 0) * multiplier)
		if statEffect.combatType then
			addResistanceBonus(bonuses, statEffect.combatType, 1 * multiplier)
		end
	end
end

local function applySupremeGemModifier(bonuses, modifier, grade)
	local effect = SUPREME_EFFECTS[modifier]
	if not effect then
		return
	end

	local multiplier = getGradeMultiplier(grade)
	if effect.stat then
		addBonus(bonuses, effect.stat, effect.value * multiplier)
	elseif effect.revelation then
		bonuses.revelation[effect.revelation] = (bonuses.revelation[effect.revelation] or 0) + effect.value * multiplier
	elseif effect.spell then
		local value = effect.augment == AUGMENT_TYPE.COOLDOWN and effect.value or effect.value * multiplier
		bonuses.spellAugments[#bonuses.spellAugments + 1] = {
			spellName = effect.spell,
			augmentType = effect.augment,
			value = value,
		}
	end
end

local function applyActiveGemBonuses(player, bonuses, points, activeGems)
	local state = loadGemState(player)
	activeGems = validateActiveGems(activeGems, state.revealed)
	local vesselResonance = { 0, 0, 0, 0 }
	for slot = 1, WHEEL_SLOT_COUNT do
		local slotBonus = WHEEL_SLOT_BONUSES[slot]
		if slotBonus and slotBonus.conviction == "vessel" and
		   (points[slot] or 0) >= (WHEEL_SLOT_MAX_POINTS[slot] or 0) then
			local domain = WHEEL_SLOT_DOMAINS[slot]
			vesselResonance[domain] = vesselResonance[domain] + 1
		end
	end

	for affinityIndex = 1, 4 do
		local gemIndex = activeGems[affinityIndex]
		local gem = gemIndex >= 0 and state.revealed[gemIndex + 1] or nil
		local resonance = vesselResonance[affinityIndex] or 0
		if gem and resonance >= 1 then
			applyBasicGemModifier(bonuses, getWheelVocation(player), gem.basic1,
			                     state.basicGrades[gem.basic1 + 1] or 0)
			if resonance >= 2 and gem.quality >= GEM_QUALITY.REGULAR then
				applyBasicGemModifier(bonuses, getWheelVocation(player), gem.basic2,
				                     state.basicGrades[gem.basic2 + 1] or 0)
			end
			if resonance >= 3 and gem.quality >= GEM_QUALITY.GREATER then
				applySupremeGemModifier(bonuses, gem.supreme, state.supremeGrades[gem.supreme + 1] or 0)
			end
		end
	end
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

local function calculateWheelBonuses(player, points, activeGems)
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
		criticalDamage = 0,
		dodge = 0,
		mitigation = 0,
		mitigationMultiplier = 0,
		resistances = {},
		revelation = { 0, 0, 0, 0 },
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
				bonuses.mitigation = bonuses.mitigation + invested * 0.075
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

	applyActiveGemBonuses(player, bonuses, points, activeGems or emptyGems())
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

local function removeAppliedMitigationMultiplier(player)
	local key = getWheelPlayerKey(player)
	local applied = WHEEL_APPLIED_MITIGATION_MULTIPLIER[key]
	if applied and applied ~= 0 and player.addWheelMitigationMultiplier then
		player:addWheelMitigationMultiplier(-applied)
	end
	WHEEL_APPLIED_MITIGATION_MULTIPLIER[key] = nil
end

local function removeAppliedResistances(player)
	local key = getWheelPlayerKey(player)
	local applied = WHEEL_APPLIED_RESISTANCES[key]
	if applied and player.addCombatAbsorbPercent then
		for combatType, value in pairs(applied) do
			if value ~= 0 then
				player:addCombatAbsorbPercent(combatType, -value)
			end
		end
	end
	WHEEL_APPLIED_RESISTANCES[key] = nil
end

local function removeAppliedDodge(player)
	local key = getWheelPlayerKey(player)
	local applied = WHEEL_APPLIED_DODGE[key]
	if applied and applied ~= 0 and player.addWheelDodgeChance then
		player:addWheelDodgeChance(-applied)
	end
	WHEEL_APPLIED_DODGE[key] = nil
end

local function removeWheelBonuses(player)
	player:removeCondition(CONDITION_ATTRIBUTES, CONDITIONID_DEFAULT, WHEEL_CONDITION_SUBID, true)
	if player.clearWheelSpellAugments then
		player:clearWheelSpellAugments()
	end
	removeAppliedSpecialMagic(player)
	removeAppliedMitigation(player)
	removeAppliedMitigationMultiplier(player)
	removeAppliedResistances(player)
	removeAppliedDodge(player)

	local appliedStore = wheelAppliedKV(player)
	appliedStore:set("conditionSubId", WHEEL_CONDITION_SUBID)
	appliedStore:set("conditionApplied", false)
	appliedStore:set("specialMagic", {})
	appliedStore:set("mitigation", 0)
	appliedStore:set("mitigationMultiplier", 0)
	appliedStore:set("resistances", {})
	appliedStore:set("dodge", 0)
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
	local bonuses = calculateWheelBonuses(player, profile.points, profile.gems)
	wheelKV(player):set("revelationStages",
	                   buildRevelationStages(calculateDomainPoints(profile.points), getWheelVocation(player),
	                                         bonuses.revelation, getMaxGradeModifierPoints(player)))
	local spellGrades = bonuses.spellGrades
	local spellAugments = bonuses.spellAugments
	local spellGradesByName = {}
	for conviction, grade in pairs(spellGrades) do
		local spell = WHEEL_SPELL_BONUSES[getWheelVocation(player)] and
		              WHEEL_SPELL_BONUSES[getWheelVocation(player)][conviction]
		if spell then
			for _, spellName in ipairs(spell.names) do
				spellGradesByName[spellName] = grade
			end
		end
	end
	wheelKV(player):set("spellGrades", spellGradesByName)
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
	hasConditionBonus = setConditionBonus(condition, CONDITION_PARAM_SPECIALSKILL_CRITICALHITAMOUNT, bonuses.criticalDamage) or hasConditionBonus

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

	if bonuses.mitigationMultiplier ~= 0 and player.addWheelMitigationMultiplier then
		player:addWheelMitigationMultiplier(bonuses.mitigationMultiplier)
		WHEEL_APPLIED_MITIGATION_MULTIPLIER[key] = bonuses.mitigationMultiplier
	else
		WHEEL_APPLIED_MITIGATION_MULTIPLIER[key] = nil
	end

	local appliedResistances = {}
	if player.addCombatAbsorbPercent then
		for combatType, value in pairs(bonuses.resistances) do
			if value ~= 0 then
				player:addCombatAbsorbPercent(combatType, value)
				appliedResistances[combatType] = value
			end
		end
	end
	WHEEL_APPLIED_RESISTANCES[key] = next(appliedResistances) and appliedResistances or nil

	if bonuses.dodge ~= 0 and player.addWheelDodgeChance then
		player:addWheelDodgeChance(bonuses.dodge)
		WHEEL_APPLIED_DODGE[key] = bonuses.dodge
	else
		WHEEL_APPLIED_DODGE[key] = nil
	end

	local appliedStore = wheelAppliedKV(player)
	appliedStore:set("conditionSubId", WHEEL_CONDITION_SUBID)
	appliedStore:set("conditionApplied", hasConditionBonus)
	appliedStore:set("specialMagic", appliedSpecialMagic)
	appliedStore:set("mitigation", bonuses.mitigation or 0)
	appliedStore:set("mitigationMultiplier", bonuses.mitigationMultiplier or 0)
	appliedStore:set("resistances", appliedResistances)
	appliedStore:set("dodge", bonuses.dodge or 0)
	appliedStore:set("updatedAt", os.time())

	player:reloadData()
	sendWheelSkillStats(player)
	return bonuses
end

function Player.wheelApplyBonuses(self)
	return applyWheelBonuses(self)
end

function Player.upgradeSpellsWOD(self, spellName)
	local grades = wheelKV(self):get("spellGrades")
	if type(grades) ~= "table" then
		return 0
	end
	return math.max(0, math.min(2, math.floor(tonumber(grades[spellName]) or 0)))
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

local function addWheelGems(out, profile, state)
	local activeGems = validateActiveGems(profile.gems, state.revealed)
	local activeCount = 0
	for affinityIndex = 1, 4 do
		if activeGems[affinityIndex] >= 0 then
			activeCount = activeCount + 1
		end
	end
	out:addByte(activeCount)
	for affinityIndex = 1, 4 do
		if activeGems[affinityIndex] >= 0 then
			out:addU16(activeGems[affinityIndex])
		end
	end

	out:addU16(#state.revealed)
	for index, gem in ipairs(state.revealed) do
		out:addU16(index - 1)
		out:addByte(gem.locked and 1 or 0)
		out:addByte(gem.affinity)
		out:addByte(gem.quality)
		out:addByte(gem.basic1)
		if gem.quality >= GEM_QUALITY.REGULAR then
			out:addByte(gem.basic2)
		end
		if gem.quality >= GEM_QUALITY.GREATER then
			out:addByte(gem.supreme)
		end
	end
end

local function addWheelGrades(out, vocationId, state)
	out:addByte(#BASIC_MODIFIER_POSITIONS)
	for _, position in ipairs(BASIC_MODIFIER_POSITIONS) do
		out:addByte(position)
		out:addByte(state.basicGrades[position + 1] or 0)
	end

	local supremePositions = SUPREME_MODIFIER_POSITIONS[vocationId] or {}
	out:addByte(#supremePositions)
	for _, position in ipairs(supremePositions) do
		out:addByte(position)
		out:addByte(state.supremeGrades[position + 1] or 0)
	end
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
	local gemState = loadGemState(player)
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
	addWheelGems(out, profile, gemState)
	addWheelGrades(out, vocationId, gemState)

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

local function consumeGemActionCost(player, itemId, itemCount, money)
	if itemCount > 0 and player:getItemCount(itemId) < itemCount then
		return false, "You do not have enough fragments or gems."
	end
	if player:getMoney() + player:getBankBalance() < money then
		return false, "You do not have enough gold."
	end
	if itemCount > 0 and not player:removeItem(itemId, itemCount) then
		return false, "Could not remove the required item."
	end
	if money > 0 and not player:removeMoneyBank(money) then
		if itemCount > 0 then
			player:addItem(itemId, itemCount)
		end
		return false, "Could not remove the required gold."
	end
	return true
end

local function revealWheelGem(player, state, quality)
	local vocationId = getWheelVocation(player)
	local gemItemId = GEM_ITEMS[vocationId] and GEM_ITEMS[vocationId][quality + 1]
	local cost = GEM_REVEAL_COST[quality]
	if not gemItemId or not cost or #state.revealed >= 0xFF then
		return false, "Invalid gem quality."
	end

	local paid, reason = consumeGemActionCost(player, gemItemId, 1, cost)
	if not paid then
		return false, reason
	end
	state.revealed[#state.revealed + 1] = createWheelGem(vocationId, math.random(0, 3), quality)
	return true
end

local function destroyWheelGem(player, profile, state, gemIndex)
	local gem = state.revealed[gemIndex + 1]
	if not gem then
		return false, "Invalid gem."
	end
	if gem.locked then
		return false, "Unlock this gem before destroying it."
	end

	local fragmentId
	local fragmentCount
	if gem.quality == GEM_QUALITY.LESSER then
		fragmentId, fragmentCount = ITEM_LESSER_FRAGMENT, math.random(1, 5)
	elseif gem.quality == GEM_QUALITY.REGULAR then
		fragmentId, fragmentCount = ITEM_LESSER_FRAGMENT, math.random(2, 10)
	else
		fragmentId, fragmentCount = ITEM_GREATER_FRAGMENT, math.random(1, 5)
	end
	if not player:addItem(fragmentId, fragmentCount) then
		return false, "There is no room for the gem fragments."
	end

	table.remove(state.revealed, gemIndex + 1)
	for affinityIndex = 1, 4 do
		if profile.gems[affinityIndex] == gemIndex then
			profile.gems[affinityIndex] = WHEEL_NO_GEM
		elseif profile.gems[affinityIndex] > gemIndex then
			profile.gems[affinityIndex] = profile.gems[affinityIndex] - 1
		end
	end
	return true
end

local NEXT_GEM_AFFINITY = {
	[0] = 1,
	[1] = 3,
	[3] = 2,
	[2] = 0,
}

local function switchWheelGemDomain(player, profile, state, gemIndex)
	local gem = state.revealed[gemIndex + 1]
	if not gem then
		return false, "Invalid gem."
	end
	if gem.locked then
		return false, "Unlock this gem before changing its domain."
	end

	local paid, reason = consumeGemActionCost(player, 0, 0, GEM_ROTATE_COST[gem.quality] or 0)
	if not paid then
		return false, reason
	end
	gem.affinity = NEXT_GEM_AFFINITY[gem.affinity]
	for affinityIndex = 1, 4 do
		if profile.gems[affinityIndex] == gemIndex then
			profile.gems[affinityIndex] = WHEEL_NO_GEM
		end
	end
	return true
end

local function toggleWheelGemLock(state, gemIndex)
	local gem = state.revealed[gemIndex + 1]
	if not gem then
		return false, "Invalid gem."
	end
	gem.locked = not gem.locked
	return true
end

local function improveWheelGemGrade(player, state, fragmentType, position)
	local grades
	local positions
	local costs
	local fragmentId
	if fragmentType == FRAGMENT_TYPE.LESSER then
		grades, positions, costs, fragmentId = state.basicGrades, BASIC_MODIFIER_POSITIONS, BASIC_GRADE_COST,
			ITEM_LESSER_FRAGMENT
	elseif fragmentType == FRAGMENT_TYPE.GREATER then
		grades, positions, costs, fragmentId = state.supremeGrades,
			SUPREME_MODIFIER_POSITIONS[getWheelVocation(player)], SUPREME_GRADE_COST, ITEM_GREATER_FRAGMENT
	else
		return false, "Invalid fragment type."
	end
	if not isPositionAllowed(positions, position) then
		return false, "Invalid gem modifier."
	end

	local nextGrade = (grades[position + 1] or 0) + 1
	local cost = costs[nextGrade]
	if not cost then
		return false, "This gem modifier is already at maximum grade."
	end
	local paid, reason = consumeGemActionCost(player, fragmentId, cost.fragments, cost.money)
	if not paid then
		return false, reason
	end
	grades[position + 1] = nextGrade
	return true
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
	local gemState = loadGemState(player)
	local validatedGems = validateActiveGems(gems, gemState.revealed)
	for affinityIndex = 1, 4 do
		if gems[affinityIndex] ~= validatedGems[affinityIndex] then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "Invalid wheel gem selection.")
			sendWheelWindow(player, player:getId())
			return
		end
	end

	local valid, reason = validatePoints(player, points)
	if not valid then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, reason)
		sendWheelWindow(player, player:getId())
		return
	end

	saveProfile(player, points, validatedGems)
	applyWheelBonuses(player)
	sendWheelWindow(player, player:getId())
end

saveHandler:register()

local gemActionHandler = PacketHandler(OPCODE_WHEEL_GEM_ACTION)

function gemActionHandler.onReceive(player, msg)
	if msg:len() - msg:tell() < 2 then
		return
	end

	if not canOpenWheel(player) then
		sendWheelWindow(player, player:getId())
		return
	end

	local action = msg:getByte()
	local parameter = msg:getByte()
	local position
	if action == GEM_ACTION.IMPROVE_GRADE then
		if msg:len() - msg:tell() < 1 then
			return
		end
		position = msg:getByte()
	end

	local profile = loadProfile(player)
	local state = loadGemState(player)
	local success, reason
	if action == GEM_ACTION.DESTROY then
		success, reason = destroyWheelGem(player, profile, state, parameter)
	elseif action == GEM_ACTION.REVEAL then
		success, reason = revealWheelGem(player, state, parameter)
	elseif action == GEM_ACTION.SWITCH_DOMAIN then
		success, reason = switchWheelGemDomain(player, profile, state, parameter)
	elseif action == GEM_ACTION.TOGGLE_LOCK then
		success, reason = toggleWheelGemLock(state, parameter)
	elseif action == GEM_ACTION.IMPROVE_GRADE then
		success, reason = improveWheelGemGrade(player, state, parameter, position)
	else
		success, reason = false, "Invalid wheel gem action."
	end

	if success then
		saveGemState(player, state)
		saveProfile(player, profile.points, validateActiveGems(profile.gems, state.revealed))
		applyWheelBonuses(player)
	elseif reason then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, reason)
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
	WHEEL_APPLIED_MITIGATION_MULTIPLIER[key] = nil
	WHEEL_APPLIED_RESISTANCES[key] = nil
	WHEEL_APPLIED_DODGE[key] = nil
	return true
end

wheelLogoutEvent:register()
