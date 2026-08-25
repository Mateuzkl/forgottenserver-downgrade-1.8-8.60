local mType = Game.createMonsterType("Radiant Templar")
local monster = {}

monster.description = "a radiant templar"
monster.experience = 26800
monster.outfit = {
	lookType = 1965,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

monster.raceId = 2845
monster.Bestiary = {
	class = "Human",
	race = BESTY_RACE_HUMAN,
	toKill = 5000,
	FirstUnlock = 250,
	SecondUnlock = 2500,
	CharmsPoints = 100,
	Stars = 5,
	Occurrence = 0,
	Locations = "Radiant Ascendancy.",
}

monster.health = 37000
monster.maxHealth = 37000
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54403 is newer than this server's items database.
monster.speed = 220
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
}

monster.loot = {
	-- Summer 2026 loot omitted until these client items exist: ceremonial bowl, gilded bell.
}

-- Wiki lists Physical + Energy + Holy + Life Drain abilities; exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -1500, maxDamage = -1900 },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 4000, type = COMBAT_ENERGYDAMAGE, minDamage = -1200, maxDamage = -1600, radius = 4, effect = CONST_ME_ENERGYAREA, target = false },
	{ name = "combat", interval = 3000, chance = 18, cooldown = 4000, type = COMBAT_HOLYDAMAGE, minDamage = -1100, maxDamage = -1500, range = 5, shootEffect = CONST_ANI_HOLY, effect = CONST_ME_HOLYAREA, target = true },
	{ name = "combat", interval = 2000, chance = 15, cooldown = 4000, type = COMBAT_LIFEDRAIN, minDamage = -900, maxDamage = -1300, range = 5, effect = CONST_ME_MAGIC_RED, target = true },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 7000, type = COMBAT_HOLYDAMAGE, minDamage = -2200, maxDamage = -3000, length = 7, spread = 0, effect = CONST_ME_HOLYAREA, target = false },
}

monster.defenses = {
	defense = 125,
	armor = 125,
	mitigation = 4.87,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 5 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 20 },
	{ type = COMBAT_EARTHDAMAGE, percent = 5 },
	{ type = COMBAT_FIREDAMAGE, percent = -10 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = -10 },
	{ type = COMBAT_HOLYDAMAGE, percent = 25 },
	{ type = COMBAT_DEATHDAMAGE, percent = -5 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
