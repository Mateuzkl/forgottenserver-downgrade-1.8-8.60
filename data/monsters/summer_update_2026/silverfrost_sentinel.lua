local mType = Game.createMonsterType("Silverfrost Sentinel")
local monster = {}

monster.description = "a silverfrost sentinel"
monster.experience = 7700
monster.outfit = {
	lookType = 1951,
	lookHead = 81,
	lookBody = 113,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

monster.raceId = 2804
monster.Bestiary = {
	class = "Construct",
	race = BESTY_RACE_CONSTRUCT,
	toKill = 2500,
	FirstUnlock = 100,
	SecondUnlock = 1000,
	CharmsPoints = 50,
	Stars = 4,
	Occurrence = 0,
	Locations = "Asura Citadel.",
}

monster.health = 9500
monster.maxHealth = 9500
monster.race = "undead"
monster.corpse = 0 -- Summer 2026 corpse 54337 is newer than this server's items database.
monster.speed = 140
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
	{ name = "platinum coin", chance = 42000, minCount = 1, maxCount = 20 },
	{ name = "small sapphire", chance = 9800, minCount = 1, maxCount = 3 },
	{ name = "crystal coin", chance = 9800 },
	{ name = "glacier mask", chance = 4200 },
	{ name = "blue gem", chance = 4200 },
	{ name = "ice rapier", chance = 4200 },
	{ name = "diamond sceptre", chance = 4200 },
	-- Summer 2026 loot omitted until the ice shard client item exists.
	{ id = 3007, chance = 850 }, -- crystal ring (name is non-unique: 3007 wearable / 6093 engraved quest copy)
}

-- Wiki lists Physical + Energy + Ice abilities; exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -450, maxDamage = -570 },
	{ name = "combat", interval = 2000, chance = 22, cooldown = 4000, type = COMBAT_ICEDAMAGE, minDamage = -400, maxDamage = -520, radius = 4, effect = CONST_ME_ICEAREA, target = false },
	{ name = "combat", interval = 2000, chance = 18, cooldown = 4000, type = COMBAT_ENERGYDAMAGE, minDamage = -350, maxDamage = -480, range = 5, shootEffect = CONST_ANI_ENERGY, effect = CONST_ME_ENERGYAREA, target = true },
	{ name = "combat", interval = 3000, chance = 14, cooldown = 8000, type = COMBAT_ICEDAMAGE, minDamage = -450, maxDamage = -580, length = 6, spread = 3, effect = CONST_ME_ICEAREA, target = false },
}

monster.defenses = {
	defense = 110,
	armor = 110,
	mitigation = 2.99,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = -10 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 10 },
	{ type = COMBAT_EARTHDAMAGE, percent = -10 },
	{ type = COMBAT_FIREDAMAGE, percent = -5 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 15 },
	{ type = COMBAT_HOLYDAMAGE, percent = 10 },
	{ type = COMBAT_DEATHDAMAGE, percent = -10 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = true },
}

mType:register(monster)
