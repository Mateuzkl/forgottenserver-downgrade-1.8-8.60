local mType = Game.createMonsterType("Radiant Warden")
local monster = {}

monster.description = "a radiant warden"
monster.experience = 27500
monster.outfit = {
	lookType = 1964,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

monster.raceId = 2839
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

monster.health = 38500
monster.maxHealth = 38500
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54399 is newer than this server's items database.
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
	{ id = 7385, chance = 4200 }, -- crimson sword (name is non-unique: 7385 real atk28/def20 / 860 notchy atk18 variant)
	-- Summer 2026 loot omitted until these client items exist: tatty robe, gilded bell.
}

-- Wiki lists Physical + Fire + Holy + Ice abilities; exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -1500, maxDamage = -1950 },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 4000, type = COMBAT_FIREDAMAGE, minDamage = -1200, maxDamage = -1600, radius = 4, effect = CONST_ME_FIREAREA, target = false },
	{ name = "combat", interval = 3000, chance = 18, cooldown = 4000, type = COMBAT_HOLYDAMAGE, minDamage = -1100, maxDamage = -1500, range = 5, shootEffect = CONST_ANI_HOLY, effect = CONST_ME_HOLYAREA, target = true },
	{ name = "combat", interval = 2000, chance = 15, cooldown = 4000, type = COMBAT_ICEDAMAGE, minDamage = -1000, maxDamage = -1400, range = 5, shootEffect = CONST_ANI_ICE, effect = CONST_ME_ICEATTACK, target = true },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 7000, type = COMBAT_HOLYDAMAGE, minDamage = -2000, maxDamage = -2800, length = 6, spread = 3, effect = CONST_ME_HOLYAREA, target = false },
}

monster.defenses = {
	defense = 130,
	armor = 130,
	mitigation = 4.63,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 10 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -10 },
	{ type = COMBAT_EARTHDAMAGE, percent = -5 },
	{ type = COMBAT_FIREDAMAGE, percent = 30 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 10 },
	{ type = COMBAT_HOLYDAMAGE, percent = -10 },
	{ type = COMBAT_DEATHDAMAGE, percent = 20 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
