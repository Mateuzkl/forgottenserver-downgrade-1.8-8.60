local mType = Game.createMonsterType("Moonspawn Blightspitter")
local monster = {}

monster.description = "a moonspawn blightspitter"
monster.experience = 6670
monster.outfit = {
	lookType = 1970,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

monster.raceId = 2851
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

monster.health = 8500
monster.maxHealth = 8500
monster.race = "venom"
monster.corpse = 0 -- Summer 2026 corpse 54439 is newer than this server's items database.
monster.speed = 168
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
	{ text = "<Splorch>", yell = false },
	{ text = "<Slushhhh>", yell = false },
	{ text = "<Glrrrkk>", yell = false },
}

monster.loot = {
	{ name = "platinum coin", chance = 42000, minCount = 1, maxCount = 8 },
	{ name = "mushroom pie", chance = 9800 },
	{ name = "terra mantle", chance = 4200 },
	{ name = "terra legs", chance = 4200 },
	{ name = "terra boots", chance = 4200 },
	{ name = "terra amulet", chance = 4200 },
	{ name = "green gem", chance = 4200 },
	{ name = "green mushroom", chance = 4200, minCount = 1, maxCount = 3 },
	{ name = "muck rod", chance = 4200 },
	-- Summer 2026 loot omitted until the moonspawn headpiece client item exists.
	{ name = "moonstone", chance = 850 },
	{ name = "mushroom backpack", chance = 210 },
}

-- Wiki lists Physical + Earth + Death abilities; exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -400, maxDamage = -510 },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 4000, type = COMBAT_EARTHDAMAGE, minDamage = -320, maxDamage = -420, radius = 4, effect = CONST_ME_GREEN_RINGS, target = false },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 4000, type = COMBAT_DEATHDAMAGE, minDamage = -280, maxDamage = -380, range = 5, shootEffect = CONST_ANI_SUDDENDEATH, effect = CONST_ME_MORTAREA, target = true },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 7000, type = COMBAT_EARTHDAMAGE, minDamage = -450, maxDamage = -620, length = 6, spread = 3, effect = CONST_ME_GREEN_RINGS, target = false },
}

monster.defenses = {
	defense = 80,
	armor = 80,
	mitigation = 3.22,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = -5 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 15 },
	{ type = COMBAT_EARTHDAMAGE, percent = 25 },
	{ type = COMBAT_FIREDAMAGE, percent = -12 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 0 },
	{ type = COMBAT_HOLYDAMAGE, percent = -2 },
	{ type = COMBAT_DEATHDAMAGE, percent = 15 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
