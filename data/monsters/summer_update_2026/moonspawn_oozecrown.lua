local mType = Game.createMonsterType("Moonspawn Oozecrown")
local monster = {}

monster.description = "a moonspawn oozecrown"
monster.experience = 7320
monster.outfit = {
	lookType = 1971,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

monster.raceId = 2852
monster.Bestiary = {
	class = "Plant",
	race = BESTY_RACE_PLANT,
	toKill = 2500,
	FirstUnlock = 100,
	SecondUnlock = 1000,
	CharmsPoints = 50,
	Stars = 4,
	Occurrence = 0,
	Locations = "Thalassara Surroundings.",
}

monster.health = 9200
monster.maxHealth = 9200
monster.race = "venom"
monster.corpse = 0 -- Summer 2026 corpse 54443 is newer than this server's items database.
monster.speed = 162
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 10,
}

monster.strategiesTarget = {
	nearest = 70,
	health = 10,
	damage = 10,
	random = 10,
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = true,
	convinceable = false,
	pushable = false,
	rewardBoss = false,
	illusionable = false,
	canPushItems = true,
	canPushCreatures = false,
	staticAttackChance = 90,
	targetDistance = 1,
	runHealth = 0,
	healthHidden = false,
	isBlockable = false,
	canWalkOnEnergy = true,
	canWalkOnFire = true,
	canWalkOnPoison = true,
}

monster.light = {
	level = 0,
	color = 0,
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "<Blrrrpp>", yell = false },
	{ text = "<Pffshhht>", yell = false },
	{ text = "<Shlooom>", yell = false },
}

monster.loot = {
	{ name = "platinum coin", chance = 42000, minCount = 1, maxCount = 8 },
	{ name = "mushroom pie", chance = 9800 },
	-- Summer 2026 loot omitted until the moonspawn tentacle client item exists.
	{ name = "yellow gem", chance = 4200 },
	{ name = "wand of decay", chance = 4200 },
	{ name = "dark mushroom", chance = 4200, minCount = 1, maxCount = 3 },
	{ name = "magic sulphur", chance = 850 },
	{ name = "moonstone", chance = 850 },
	{ name = "mycological bow", chance = 210 },
}

-- Wiki lists Physical + Earth + Death abilities and confirms it causes drunkenness.
-- Exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -430, maxDamage = -552 },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 4000, type = COMBAT_EARTHDAMAGE, minDamage = -350, maxDamage = -450, radius = 4, effect = CONST_ME_GREEN_RINGS, target = false },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 4000, type = COMBAT_DEATHDAMAGE, minDamage = -300, maxDamage = -400, range = 5, shootEffect = CONST_ANI_SUDDENDEATH, effect = CONST_ME_MORTAREA, target = true },
	{ name = "drunk", interval = 8000, chance = 10, cooldown = 8000, radius = 4, effect = CONST_ME_SOUND_PURPLE, target = false, duration = 8000 },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 7000, type = COMBAT_EARTHDAMAGE, minDamage = -450, maxDamage = -620, length = 6, spread = 3, effect = CONST_ME_GREEN_RINGS, target = false },
}

monster.defenses = {
	defense = 75,
	armor = 75,
	mitigation = 3.45,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = -5 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 5 },
	{ type = COMBAT_EARTHDAMAGE, percent = 18 },
	{ type = COMBAT_FIREDAMAGE, percent = -8 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = -6 },
	{ type = COMBAT_HOLYDAMAGE, percent = -6 },
	{ type = COMBAT_DEATHDAMAGE, percent = 10 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
