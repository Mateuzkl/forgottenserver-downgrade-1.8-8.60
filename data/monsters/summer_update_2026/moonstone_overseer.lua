local mType = Game.createMonsterType("Moonstone Overseer")
local monster = {}

monster.description = "a moonstone overseer"
monster.experience = 4920
monster.outfit = {
	lookType = 1956,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

monster.raceId = 2858
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

monster.health = 5450
monster.maxHealth = 5450
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54368 is newer than this server's items database.
monster.speed = 125
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
	{ text = "The holy mission will not be delayed!", yell = false },
	{ text = "The moonsilver must flow!", yell = false },
}

monster.loot = {
	{ name = "platinum coin", chance = 42000, minCount = 1, maxCount = 8 },
	{ name = "yellow gem", chance = 9800 },
	{ name = "green gem", chance = 9800 },
	{ id = 3039, chance = 9800 }, -- red gem (name is non-unique: 3039 classic / 36706 duplicate)
	{ name = "blue gem", chance = 9800 },
	{ name = "plate armor", chance = 9800 },
	{ name = "spike sword", chance = 9800 },
	{ name = "iron ore", chance = 850 },
	{ name = "diamond", chance = 850 },
	{ name = "moonstone", chance = 850 },
}

-- Wiki lists Physical + Holy abilities; exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -255, maxDamage = -327 },
	{ name = "combat", interval = 2000, chance = 18, cooldown = 4000, type = COMBAT_HOLYDAMAGE, minDamage = -200, maxDamage = -280, range = 5, shootEffect = CONST_ANI_HOLY, effect = CONST_ME_HOLYAREA, target = true },
	{ name = "combat", interval = 3000, chance = 12, cooldown = 8000, type = COMBAT_EARTHDAMAGE, minDamage = -220, maxDamage = -300, radius = 4, effect = CONST_ME_GROUNDSHAKER, target = false },
}

monster.defenses = {
	defense = 45,
	armor = 45,
	mitigation = 2.44,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = -5 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -10 },
	{ type = COMBAT_EARTHDAMAGE, percent = 30 },
	{ type = COMBAT_FIREDAMAGE, percent = -10 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 5 },
	{ type = COMBAT_HOLYDAMAGE, percent = 15 },
	{ type = COMBAT_DEATHDAMAGE, percent = 25 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
