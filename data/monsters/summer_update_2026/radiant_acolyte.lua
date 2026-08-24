local mType = Game.createMonsterType("Radiant Acolyte")
local monster = {}

monster.description = "a radiant acolyte"
monster.experience = 25500
monster.outfit = {
	lookType = 1969,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

monster.raceId = 2843
monster.Bestiary = {
	class = "Human",
	race = BESTY_RACE_HUMAN,
	toKill = 5000,
	FirstUnlock = 250,
	SecondUnlock = 2500,
	CharmsPoints = 100,
	Stars = 5,
	Occurrence = 0,
	Locations = "Radiant Skyhold.",
}

monster.health = 34600
monster.maxHealth = 34600
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54419 is newer than this server's items database.
monster.speed = 150
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
	-- Summer 2026 loot omitted until the book of faith client item exists.
	{ name = "blank imbuement scroll", chance = 850 },
	-- Summer 2026 loot omitted until the gilded bell client item exists.
}

-- Wiki lists Physical + Ice abilities; exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -1400, maxDamage = -1800 },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 4000, type = COMBAT_ICEDAMAGE, minDamage = -1200, maxDamage = -1600, radius = 4, effect = CONST_ME_ICEAREA, target = false },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 4000, type = COMBAT_ICEDAMAGE, minDamage = -1000, maxDamage = -1400, range = 5, shootEffect = CONST_ANI_ICE, effect = CONST_ME_ICEATTACK, target = true },
	{ name = "combat", interval = 3000, chance = 13, cooldown = 9000, type = COMBAT_HOLYDAMAGE, minDamage = -1500, maxDamage = -2100, radius = 4, effect = CONST_ME_HOLYAREA, target = false },
}

monster.defenses = {
	defense = 95,
	armor = 95,
	mitigation = 4.87,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 5 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -5 },
	{ type = COMBAT_EARTHDAMAGE, percent = -5 },
	{ type = COMBAT_FIREDAMAGE, percent = 40 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 10 },
	{ type = COMBAT_HOLYDAMAGE, percent = 5 },
	{ type = COMBAT_DEATHDAMAGE, percent = 30 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
