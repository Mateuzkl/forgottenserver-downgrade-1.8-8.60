-- data/scripts/network/prey_system/prey_monsters.lua

if not configManager.getBoolean(configKeys.PREY_SYSTEM_ENABLED) then
	PreyMonsters = nil
	return
end

PreyMonsters = PreyMonsters or {}

local monsterPool = {
	{ name = "Rat", minLevel = 1, maxLevel = 30 },
	{ name = "Cave Rat", minLevel = 1, maxLevel = 35 },
	{ name = "Bug", minLevel = 1, maxLevel = 35 },
	{ name = "Spider", minLevel = 1, maxLevel = 35 },
	{ name = "Snake", minLevel = 1, maxLevel = 35 },
	{ name = "Wolf", minLevel = 1, maxLevel = 40 },
	{ name = "Bear", minLevel = 1, maxLevel = 45 },
	{ name = "Goblin", minLevel = 1, maxLevel = 50 },
	{ name = "Poison Spider", minLevel = 1, maxLevel = 50 },
	{ name = "Skeleton", minLevel = 1, maxLevel = 50 },
	{ name = "Troll", minLevel = 1, maxLevel = 50 },
	{ name = "Orc", minLevel = 1, maxLevel = 50 },
	{ name = "Orc Spearman", minLevel = 1, maxLevel = 55 },
	{ name = "Orc Warrior", minLevel = 1, maxLevel = 60 },
	{ name = "Orc Shaman", minLevel = 1, maxLevel = 65 },
	{ name = "Dwarf", minLevel = 1, maxLevel = 60 },
	{ name = "Dwarf Soldier", minLevel = 1, maxLevel = 70 },
	{ name = "Amazon", minLevel = 1, maxLevel = 70 },
	{ name = "Valkyrie", minLevel = 1, maxLevel = 75 },
	{ name = "Smuggler", minLevel = 1, maxLevel = 75 },
	{ name = "Wild Warrior", minLevel = 1, maxLevel = 80 },
	{ name = "Hunter", minLevel = 1, maxLevel = 80 },
	{ name = "Minotaur Archer", minLevel = 1, maxLevel = 80 },
	{ name = "Minotaur Guard", minLevel = 1, maxLevel = 90 },
	{ name = "Minotaur Mage", minLevel = 1, maxLevel = 90 },
	{ name = "Cyclops", minLevel = 1, maxLevel = 60 },
	{ name = "Demon Skeleton", minLevel = 8, maxLevel = 60 },
	{ name = "Minotaur", minLevel = 8, maxLevel = 70 },
	{ name = "Rotworm", minLevel = 10, maxLevel = 80 },
	{ name = "Ghoul", minLevel = 12, maxLevel = 80 },
	{ name = "Witch", minLevel = 15, maxLevel = 80 },
	{ name = "Dragon", minLevel = 25, maxLevel = 100 },
	{ name = "Vampire", minLevel = 30, maxLevel = 100 },
	{ name = "Necromancer", minLevel = 30, maxLevel = 100 },
	{ name = "Beholder", minLevel = 35, maxLevel = 110 },
	{ name = "Medusa", minLevel = 40, maxLevel = 120 },
	{ name = "Warlock", minLevel = 50, maxLevel = 150 },
	{ name = "Dragon Lord", minLevel = 50, maxLevel = 150 },
	{ name = "Hydra", minLevel = 60, maxLevel = 160 },
	{ name = "Giant Spider", minLevel = 40, maxLevel = 160 },
	{ name = "Behemoth", minLevel = 60, maxLevel = 180 },
	{ name = "Serpent Spawn", minLevel = 60, maxLevel = 180 },
	{ name = "Demon", minLevel = 80, maxLevel = 999 },
	{ name = "Plaguesmith", minLevel = 80, maxLevel = 999 },
	{ name = "Hellhound", minLevel = 80, maxLevel = 999 },
	{ name = "Hellspawn", minLevel = 80, maxLevel = 999 },
	{ name = "Lost Soul", minLevel = 80, maxLevel = 999 },
	{ name = "Juggernaut", minLevel = 100, maxLevel = 999 },
	{ name = "Ice Witch", minLevel = 50, maxLevel = 160 },
	{ name = "Crystal Spider", minLevel = 50, maxLevel = 160 },
	{ name = "Frost Giant", minLevel = 50, maxLevel = 160 },
	{ name = "Stone Golem", minLevel = 20, maxLevel = 100 },
	{ name = "Gargoyle", minLevel = 20, maxLevel = 100 },
	{ name = "Bonebeast", minLevel = 35, maxLevel = 120 },
	{ name = "Banshee", minLevel = 40, maxLevel = 130 },
	{ name = "Dark Torturer", minLevel = 70, maxLevel = 999 },
	{ name = "Blightwalker", minLevel = 70, maxLevel = 999 },
	{ name = "Defiler", minLevel = 70, maxLevel = 999 },
	{ name = "Phantasm", minLevel = 80, maxLevel = 999 },
	{ name = "Nightmare", minLevel = 60, maxLevel = 999 },
	{ name = "Fury", minLevel = 60, maxLevel = 999 },
	{ name = "Lizard Templar", minLevel = 30, maxLevel = 120 },
	{ name = "Lizard Snakecharmer", minLevel = 30, maxLevel = 120 },
}

local validMonsterCache = {}

local function isValidPreyMonster(name)
	if not name then
		return false
	end

	local cached = validMonsterCache[name]
	if cached ~= nil then
		return cached
	end

	local monsterType = MonsterType(name)
	if not monsterType then
		validMonsterCache[name] = false
		return false
	end

	if monsterType:experience() <= 0 then
		validMonsterCache[name] = false
		return false
	end

	if monsterType:isBoss() or monsterType:isRewardBoss() then
		validMonsterCache[name] = false
		return false
	end

	validMonsterCache[name] = true
	return true
end

for _, monster in ipairs(monsterPool) do
	isValidPreyMonster(monster.name)
end

local function shuffle(list)
	for i = #list, 2, -1 do
		local j = math.random(1, i)
		list[i], list[j] = list[j], list[i]
	end
end

local function addCandidate(eligible, seen, excluded, monster, playerLevel, strictLevel, ignoreMaxLevel)
	local monsterName = monster.name
	local lowerName = monsterName:lower()
	if seen[lowerName] or excluded[lowerName] then
		return
	end

	if strictLevel and (playerLevel < monster.minLevel or playerLevel > monster.maxLevel) then
		return
	end

	if not strictLevel and not ignoreMaxLevel and playerLevel > monster.maxLevel then
		return
	end

	if not isValidPreyMonster(monsterName) then
		return
	end

	seen[lowerName] = true
	table.insert(eligible, monsterName)
end

function PreyMonsters.generateList(playerLevel, excludeNames)
	excludeNames = excludeNames or {}
	local excluded = {}
	for _, name in ipairs(excludeNames) do
		excluded[name:lower()] = true
	end

	local eligible = {}
	local seen = {}
	for _, monster in ipairs(monsterPool) do
		addCandidate(eligible, seen, excluded, monster, playerLevel, true)
	end

	if #eligible < 9 then
		for _, monster in ipairs(monsterPool) do
			addCandidate(eligible, seen, excluded, monster, playerLevel, false)
		end
	end

	if #eligible < 9 then
		for _, monster in ipairs(monsterPool) do
			addCandidate(eligible, seen, excluded, monster, playerLevel, false, true)
		end
	end

	shuffle(eligible)

	local result = {}
	for i = 1, math.min(9, #eligible) do
		result[i] = eligible[i]
	end
	return result
end

function PreyMonsters.getAllNames(playerLevel, excludeNames)
	playerLevel = playerLevel or 0
	excludeNames = excludeNames or {}
	local excluded = {}
	for _, name in ipairs(excludeNames) do
		excluded[name:lower()] = true
	end

	local eligible = {}
	local seen = {}
	for _, monster in ipairs(monsterPool) do
		addCandidate(eligible, seen, excluded, monster, playerLevel, false, true)
	end

	table.sort(eligible, function(a, b)
		return a:lower() < b:lower()
	end)
	return eligible
end
