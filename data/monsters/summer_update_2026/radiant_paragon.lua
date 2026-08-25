local mType = Game.createMonsterType("Radiant Paragon")
local monster = {}

monster.description = "a radiant paragon"
monster.experience = 27000
monster.outfit = {
	lookType = 1966,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 2,
}

monster.raceId = 2841
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

monster.health = 36000
monster.maxHealth = 36000
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54407 is newer than this server's items database.
monster.speed = 240
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
	staticAttackChance = 80,
	targetDistance = 4,
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
	{ name = "blue quiver", chance = 4200 },
	-- Summer 2026 loot omitted until these client items exist: broken arrow, gilded bell.
}

-- Wiki lists Physical + Fire + Holy abilities; exact damage values are not published.
-- Ranged attacker (drops a quiver and broken arrows), so targetDistance = 4.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -1200, maxDamage = -1600 },
	{ name = "combat", interval = 2000, chance = 25, cooldown = 4000, type = COMBAT_PHYSICALDAMAGE, minDamage = -1300, maxDamage = -1750, range = 7, shootEffect = CONST_ANI_ARROW, target = true },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 4000, type = COMBAT_FIREDAMAGE, minDamage = -1100, maxDamage = -1500, range = 7, shootEffect = CONST_ANI_BURSTARROW, effect = CONST_ME_FIREAREA, target = true },
	{ name = "combat", interval = 3000, chance = 18, cooldown = 4000, type = COMBAT_HOLYDAMAGE, minDamage = -1000, maxDamage = -1450, range = 7, shootEffect = CONST_ANI_HOLY, effect = CONST_ME_HOLYAREA, target = true },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 5000, type = COMBAT_PHYSICALDAMAGE, minDamage = -1400, maxDamage = -2000, range = 7, radius = 3, shootEffect = CONST_ANI_ARROW, effect = CONST_ME_HITAREA, target = true },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 7000, type = COMBAT_HOLYDAMAGE, minDamage = -2000, maxDamage = -2800, length = 6, spread = 3, effect = CONST_ME_HOLYAREA, target = false },
}

monster.defenses = {
	defense = 108,
	armor = 108,
	mitigation = 4.63,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 10 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -10 },
	{ type = COMBAT_EARTHDAMAGE, percent = -15 },
	{ type = COMBAT_FIREDAMAGE, percent = 30 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 5 },
	{ type = COMBAT_HOLYDAMAGE, percent = 0 },
	{ type = COMBAT_DEATHDAMAGE, percent = 20 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
