local mType = Game.createMonsterType("Moonstone Excavator")
local monster = {}

monster.description = "a moonstone excavator"
monster.experience = 4670
monster.outfit = {
	lookType = 1952,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 1,
}

monster.raceId = 2857
monster.Bestiary = {
	class = "Human",
	race = BESTY_RACE_HUMAN,
	toKill = 2500,
	FirstUnlock = 100,
	SecondUnlock = 1000,
	CharmsPoints = 50,
	Stars = 4,
	Occurrence = 0,
	Locations = "Moonstone Crater.",
}

monster.health = 5100
monster.maxHealth = 5100
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54342 is newer than this server's items database.
monster.speed = 120
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
	{ text = "I must keep digging!", yell = false },
	{ text = "No room for failure.", yell = false },
}

monster.loot = {
	{ name = "small diamond", chance = 42000, minCount = 1, maxCount = 2 },
	{ name = "platinum coin", chance = 42000, minCount = 1, maxCount = 7 },
	{ name = "small sapphire", chance = 9800, minCount = 1, maxCount = 2 },
	{ name = "small ruby", chance = 9800, minCount = 1, maxCount = 2 },
	{ name = "small emerald", chance = 9800, minCount = 1, maxCount = 2 },
	{ name = "small amethyst", chance = 9800, minCount = 1, maxCount = 2 },
	{ name = "small topaz", chance = 9800, minCount = 1, maxCount = 2 },
	{ name = "candlestick", chance = 4200 },
	{ name = "iron ore", chance = 4200 },
	{ id = 3040, chance = 850 }, -- gold nugget (name is non-unique: 3040 classic / 27488 duplicate)
	{ id = 3456, chance = 850 }, -- pick (name is non-unique: 3456 plain / 31613 enchanted / 31615 faded)
	{ name = "moonstone", chance = 850 },
}

-- Wiki lists Physical + Earth abilities; exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -240, maxDamage = -306 },
	{ name = "combat", interval = 2000, chance = 15, cooldown = 4000, type = COMBAT_EARTHDAMAGE, minDamage = -180, maxDamage = -250, radius = 3, effect = CONST_ME_GREEN_RINGS, target = false },
	{ name = "combat", interval = 3000, chance = 12, cooldown = 8000, type = COMBAT_EARTHDAMAGE, minDamage = -200, maxDamage = -280, radius = 4, effect = CONST_ME_GROUNDSHAKER, target = false },
}

monster.defenses = {
	defense = 47,
	armor = 47,
	mitigation = 2.09,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = -5 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -10 },
	{ type = COMBAT_EARTHDAMAGE, percent = 80 },
	{ type = COMBAT_FIREDAMAGE, percent = -10 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 15 },
	{ type = COMBAT_HOLYDAMAGE, percent = 10 },
	{ type = COMBAT_DEATHDAMAGE, percent = 20 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
