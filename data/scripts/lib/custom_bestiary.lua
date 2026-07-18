if not configManager.getBoolean(configKeys.BESTIARY_SYSTEM_ENABLED) then
	CustomBestiary = nil
	function Game.getMonstersByBestiaryStars(_stars)
		return {}
	end
	return
end

CustomBestiary = CustomBestiary or {}

CustomBestiary.monstersByRaceId = CustomBestiary.monstersByRaceId or {}
CustomBestiary.classOrder = CustomBestiary.classOrder or {}
CustomBestiary.classes = CustomBestiary.classes or {}
CustomBestiary.classRace = CustomBestiary.classRace or {}
CustomBestiary.corpseRaceById = CustomBestiary.corpseRaceById or {}
CustomBestiary.maxClientCreatureLookType = CustomBestiary.maxClientCreatureLookType or 1921
CustomBestiary.fallbackCreatureLookType = CustomBestiary.fallbackCreatureLookType or 128
local classesDirty = false

CustomBestiary.charmRunes = {
	{id = 0, category = "major", kind = "offensive", name = "Wound", description = "Your attacks have a %d%% chance to deal physical damage equal to 5%% of the target's initial hit points.", prices = {240, 360, 1200}, bonuses = {5, 10, 11}, percent = 5, price = 240},
	{id = 1, category = "major", kind = "offensive", name = "Enflame", description = "Your attacks have a %d%% chance to deal fire damage equal to 5%% of the target's initial hit points.", prices = {400, 600, 2000}, bonuses = {5, 10, 11}, percent = 5, price = 400},
	{id = 2, category = "major", kind = "offensive", name = "Poison", description = "Your attacks have a %d%% chance to deal earth damage equal to 5%% of the target's initial hit points.", prices = {240, 360, 1200}, bonuses = {5, 10, 11}, percent = 5, price = 240},
	{id = 3, category = "major", kind = "offensive", name = "Freeze", description = "Your attacks have a %d%% chance to deal ice damage equal to 5%% of the target's initial hit points.", prices = {320, 480, 1600}, bonuses = {5, 10, 11}, percent = 5, price = 320},
	{id = 4, category = "major", kind = "offensive", name = "Zap", description = "Your attacks have a %d%% chance to deal energy damage equal to 5%% of the target's initial hit points.", prices = {320, 480, 1600}, bonuses = {5, 10, 11}, percent = 5, price = 320},
	{id = 5, category = "major", kind = "offensive", name = "Curse", description = "Your attacks have a %d%% chance to deal death damage equal to 5%% of the target's initial hit points.", prices = {360, 540, 1800}, bonuses = {5, 10, 11}, percent = 5, price = 360},
	{id = 6, category = "minor", kind = "offensive", name = "Cripple", description = "Your attacks have a %d%% chance to paralyze the target for 10 seconds.", prices = {100, 150, 225}, bonuses = {6, 9, 12}, price = 100},
	{id = 7, category = "major", kind = "defensive", name = "Parry", description = "Each time you take damage, you have a %d%% chance to reflect it back to the aggressor.", prices = {400, 600, 2000}, bonuses = {5, 10, 11}, price = 400},
	{id = 8, category = "major", kind = "defensive", name = "Dodge", description = "Grants a %d%% chance to dodge an attack.", prices = {240, 360, 1200}, bonuses = {5, 10, 11}, price = 240},
	{id = 9, category = "minor", kind = "defensive", name = "Adrenaline Burst", description = "Each time you're hit you have a %d%% chance to trigger a burst of adrenaline, boosting your speed by 150%% for 10 seconds.", prices = {100, 150, 225}, bonuses = {6, 9, 12}, price = 100},
	{id = 10, category = "minor", kind = "defensive", name = "Numb", description = "After being attacked, you have a %d%% chance to paralyze the aggressor for 10 seconds.", prices = {100, 150, 225}, bonuses = {6, 9, 12}, price = 100},
	{id = 11, category = "minor", kind = "defensive", name = "Cleanse", description = "Each time you're hit, you have a %d%% chance to cleanse one random negative status effect and gain temporary immunity to it for 11 seconds.", prices = {100, 150, 225}, bonuses = {6, 9, 12}, price = 100},
	{id = 12, category = "minor", kind = "passive", name = "Bless", description = "Blesses you, reducing skill and experience loss by %d%% when killed by the chosen creature.", prices = {100, 150, 225}, bonuses = {6, 9, 12}, price = 100},
	{id = 13, category = "minor", kind = "passive", name = "Scavenge", description = "Increases your chance of successfully skinning/ dusting a skinnable/ dustable creature by %d%%.", prices = {100, 150, 225}, bonuses = {60, 90, 120}, price = 100},
	{id = 14, category = "minor", kind = "passive", name = "Gut", description = "Gutting the creature yields %d%% more creature products.", prices = {100, 150, 225}, bonuses = {6, 9, 12}, price = 100},
	{id = 15, category = "major", kind = "passive", name = "Low Blow", description = "Adds %d%% critical hit chance to attacks with critical hit weapons.", prices = {800, 1200, 4000}, bonuses = {4, 8, 9}, price = 800},
	{id = 16, category = "major", kind = "offensive", name = "Divine Wrath", description = "Your attacks have a %d%% chance to deal holy damage equal to 5%% of the target's initial hit points.", prices = {600, 900, 3000}, bonuses = {5, 10, 11}, percent = 5, price = 600},
	{id = 17, category = "minor", kind = "passive", name = "Vampiric Embrace", description = "Increases your current life leech by %.1f%%.", prices = {100, 150, 225}, bonuses = {1.6, 2.4, 3.2}, price = 100},
	{id = 18, category = "minor", kind = "passive", name = "Void's Call", description = "Increases your current mana leech by %.1f%%.", prices = {100, 150, 225}, bonuses = {0.8, 1.2, 1.6}, price = 100},
	{id = 19, category = "major", kind = "passive", name = "Savage Blow", description = "Adds %d%% critical extra damage to attacks with critical hit weapons.", prices = {800, 1200, 4000}, bonuses = {20, 40, 44}, price = 800},
	{id = 20, category = "minor", kind = "passive", name = "Fatal Hold", description = "Your attacks have a %d%% chance to prevent creatures from fleeing due to low health for 30 seconds.", prices = {100, 150, 225}, bonuses = {30, 45, 60}, price = 100},
	{id = 21, category = "minor", kind = "passive", name = "Void Inversion", description = "%d%% chance to gain mana instead of losing it when taking mana drain damage.", prices = {100, 150, 225}, bonuses = {20, 30, 40}, price = 100},
	{id = 22, category = "major", kind = "offensive", name = "Carnage", description = "Killing a monster has %d%% chance to deal physical damage equal to 15%% of its maximum health to all monsters in small radius.", prices = {600, 900, 3000}, bonuses = {10, 20, 22}, percent = 15, price = 600},
	{id = 23, category = "major", kind = "offensive", name = "Overpower", description = "Your attacks have a %d%% chance to deal damage equal to 5%% of your maximum health.", prices = {600, 900, 3000}, bonuses = {5, 10, 11}, percent = 5, price = 600},
	{id = 24, category = "major", kind = "offensive", name = "Overflux", description = "Your attacks have a %d%% chance to deal damage equal to 2.5%% of your maximum mana.", prices = {600, 900, 3000}, bonuses = {5, 10, 11}, percent = 2.5, price = 600}
}

local charmById = {}
for _, charm in ipairs(CustomBestiary.charmRunes) do
	charmById[charm.id] = charm
end
CustomBestiary.charmById = charmById

local elementMap = {}
if COMBAT_PHYSICALDAMAGE then elementMap[COMBAT_PHYSICALDAMAGE] = 0 end
if COMBAT_FIREDAMAGE then elementMap[COMBAT_FIREDAMAGE] = 1 end
if COMBAT_EARTHDAMAGE then elementMap[COMBAT_EARTHDAMAGE] = 2 end
if COMBAT_ENERGYDAMAGE then elementMap[COMBAT_ENERGYDAMAGE] = 3 end
if COMBAT_ICEDAMAGE then elementMap[COMBAT_ICEDAMAGE] = 4 end
if COMBAT_HOLYDAMAGE then elementMap[COMBAT_HOLYDAMAGE] = 5 end
if COMBAT_DEATHDAMAGE then elementMap[COMBAT_DEATHDAMAGE] = 6 end
if COMBAT_HEALING then elementMap[COMBAT_HEALING] = 7 end

local function clamp(value, minValue, maxValue)
	value = tonumber(value) or minValue
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function resolveItemType(value)
	if value == nil then
		return nil
	end
	local itemType = ItemType(value)
	if itemType and itemType:getId() ~= 0 then
		return itemType
	end
	return nil
end

local function collectLoot(maskLoot)
	local loot = {}
	if type(maskLoot) ~= "table" then
		return loot
	end

	for _, entry in ipairs(maskLoot) do
		local itemType = resolveItemType(entry.id or entry.name)
		if itemType then
			loot[#loot + 1] = {
				itemId = itemType:getClientId(),
				name = itemType:getName(),
				chance = tonumber(entry.chance) or 0,
				maxCount = clamp(entry.maxCount or entry.count or 1, 1, 255)
			}
		end
	end

	table.sort(loot, function(a, b)
		if a.chance == b.chance then
			return a.name < b.name
		end
		return a.chance > b.chance
	end)
	return loot
end

local function collectElements(maskElements)
	local elements = {}
	if type(maskElements) ~= "table" then
		return elements
	end

	for _, element in ipairs(maskElements) do
		local elementId = elementMap[element.type]
		if elementId then
			elements[#elements + 1] = {
				id = elementId,
				percent = clamp(100 + (tonumber(element.percent) or 0), 0, 65535)
			}
		end
	end

	table.sort(elements, function(a, b) return a.id < b.id end)
	return elements
end

local function splitLocations(locations)
	local result = {}
	if type(locations) ~= "string" or locations == "" then
		return result
	end

	for part in locations:gmatch("[^,]+") do
		local trimmedPart = part:gsub("^%s*(.-)%s*$", "%1")
		if trimmedPart ~= "" then
			result[#result + 1] = trimmedPart
		end
	end

	if #result == 0 then
		result[1] = locations
	end
	return result
end

local function callMonsterMethod(monsterType, methodName, defaultValue)
	local method = monsterType and monsterType[methodName]
	if type(method) ~= "function" then
		return defaultValue
	end

	local ok, value = pcall(method, monsterType)
	if ok and value ~= nil then
		return value
	end
	return defaultValue
end

local function normalizeOutfit(outfit)
	if type(outfit) ~= "table" then
		return {type = CustomBestiary.fallbackCreatureLookType, head = 0, body = 0, legs = 0, feet = 0, addons = 0}
	end

	local lookType = tonumber(outfit.lookType or outfit.type) or 0
	if lookType <= 0 or lookType > CustomBestiary.maxClientCreatureLookType then
		lookType = CustomBestiary.fallbackCreatureLookType
	end

	return {
		type = clamp(lookType, 1, CustomBestiary.maxClientCreatureLookType),
		head = clamp(outfit.lookHead or outfit.head or 0, 0, 0xFF),
		body = clamp(outfit.lookBody or outfit.body or 0, 0, 0xFF),
		legs = clamp(outfit.lookLegs or outfit.legs or 0, 0, 0xFF),
		feet = clamp(outfit.lookFeet or outfit.feet or 0, 0, 0xFF),
		addons = clamp(outfit.lookAddons or outfit.addons or 0, 0, 0xFF)
	}
end

local function addToClass(entry)
	local className = entry.class
	if not CustomBestiary.classes[className] then
		CustomBestiary.classes[className] = {}
		CustomBestiary.classOrder[#CustomBestiary.classOrder + 1] = className
	end
	local raceOrder = tonumber(entry.race) or 0
	if not CustomBestiary.classRace[className] or raceOrder < CustomBestiary.classRace[className] then
		CustomBestiary.classRace[className] = raceOrder
	end
	CustomBestiary.classes[className][#CustomBestiary.classes[className] + 1] = entry
	classesDirty = true
end

function CustomBestiary.registerMonster(monsterType, mask)
	if type(mask) ~= "table" or type(mask.Bestiary) ~= "table" then
		return false
	end

	local raceId = tonumber(mask.raceId) or callMonsterMethod(monsterType, "raceId", 0) or 0
	if raceId <= 0 then
		return false
	end

	local oldEntry = CustomBestiary.monstersByRaceId[raceId]
	if oldEntry and CustomBestiary.classes[oldEntry.class] then
		for index, entry in ipairs(CustomBestiary.classes[oldEntry.class]) do
			if entry.raceId == raceId then
				table.remove(CustomBestiary.classes[oldEntry.class], index)
				break
			end
		end
		if #CustomBestiary.classes[oldEntry.class] == 0 then
			CustomBestiary.classes[oldEntry.class] = nil
			CustomBestiary.classRace[oldEntry.class] = nil
			for index, className in ipairs(CustomBestiary.classOrder) do
				if className == oldEntry.class then
					table.remove(CustomBestiary.classOrder, index)
					break
				end
			end
		end
	end

	local bestiary = mask.Bestiary
	local className = tostring(bestiary.class or "Unknown")
	local toKill = clamp(bestiary.toKill, 1, 0xFFFF)
	local firstUnlock = clamp(bestiary.FirstUnlock, 1, toKill)
	local secondUnlock = clamp(bestiary.SecondUnlock, firstUnlock, toKill)
	if (tonumber(bestiary.SecondUnlock) or 0) < (tonumber(bestiary.FirstUnlock) or 0) then
		print("[CustomBestiary] Warning: SecondUnlock < FirstUnlock for raceId " .. raceId)
	end
	local entry = {
		raceId = raceId,
		name = tostring(mask.name or callMonsterMethod(monsterType, "name", "unknown") or "unknown"),
		class = className,
		race = tonumber(bestiary.race) or 0,
		toKill = toKill,
		firstUnlock = firstUnlock,
		secondUnlock = secondUnlock,
		charmPoints = clamp(bestiary.CharmsPoints, 0, 0xFFFF),
		stars = clamp(bestiary.Stars, 1, 5),
		occurrence = clamp((tonumber(bestiary.Occurrence) or 0) + 1, 1, 4),
		locations = splitLocations(bestiary.Locations),
		outfit = normalizeOutfit(mask.outfit),
		loot = collectLoot(mask.loot),
		elements = collectElements(mask.elements),
		health = clamp(mask.maxHealth or mask.health or callMonsterMethod(monsterType, "maxHealth", 0) or 0, 0, 0xFFFFFFFF),
		experience = clamp(mask.experience or callMonsterMethod(monsterType, "experience", 0) or 0, 0, 0xFFFFFFFF),
		baseSpeed = clamp(mask.speed or callMonsterMethod(monsterType, "baseSpeed", 0) or 0, 0, 0xFFFF),
		armor = clamp(type(mask.defenses) == "table" and mask.defenses.armor or callMonsterMethod(monsterType, "armor", 0) or 0, 0, 0xFFFF)
	}

	CustomBestiary.monstersByRaceId[raceId] = entry
	if Game.registerBestiaryMonsterData then
		Game.registerBestiaryMonsterData(entry.raceId, entry.name, entry.toKill, entry.firstUnlock, entry.secondUnlock,
			entry.charmPoints,
			entry.outfit.type or 0, entry.outfit.head or 0, entry.outfit.body or 0,
			entry.outfit.legs or 0, entry.outfit.feet or 0, entry.outfit.addons or 0)
	end
	local corpseId = tonumber(mask.corpse) or callMonsterMethod(monsterType, "corpseId", 0) or 0
	if corpseId > 0 then
		CustomBestiary.corpseRaceById[corpseId] = raceId
	end
	addToClass(entry)
	return true
end

function CustomBestiary.getMonster(raceId)
	return CustomBestiary.monstersByRaceId[tonumber(raceId) or 0]
end

function CustomBestiary.getClasses()
	if classesDirty then
		table.sort(CustomBestiary.classOrder, function(a, b)
			local orderA = CustomBestiary.classRace[a] or 0
			local orderB = CustomBestiary.classRace[b] or 0
			if orderA == orderB then
				return a < b
			end
			return orderA < orderB
		end)
		for _, className in ipairs(CustomBestiary.classOrder) do
			table.sort(CustomBestiary.classes[className], function(a, b)
				return a.name < b.name
			end)
		end
		classesDirty = false
	end
	return CustomBestiary.classOrder, CustomBestiary.classes
end

function CustomBestiary.getProgress(entry, kills)
	kills = tonumber(kills) or 0
	if not entry or kills <= 0 then
		return 0
	end
	if kills >= entry.toKill then
		return 4
	end
	if kills >= entry.secondUnlock then
		return 3
	end
	if kills >= entry.firstUnlock then
		return 2
	end
	return 1
end

function CustomBestiary.getLootTier(chance)
	chance = tonumber(chance) or 0
	if chance < 1000 then
		return 4
	elseif chance < 10000 then
		return 3
	elseif chance < 50000 then
		return 2
	elseif chance < 100000 then
		return 1
	end
	return 0
end

function CustomBestiary.getMonstersByStars(starFilter)
	local result = {}
	starFilter = tonumber(starFilter) or 0
	for _, entry in pairs(CustomBestiary.monstersByRaceId or {}) do
		if entry.stars == starFilter then
			result[#result + 1] = entry
		end
	end
	table.sort(result, function(a, b) return (a.name or ""):lower() < (b.name or ""):lower() end)
	return result
end

function Game.getMonstersByBestiaryStars(stars)
	if not CustomBestiary then
		return {}
	end
	return CustomBestiary.getMonstersByStars(stars)
end
