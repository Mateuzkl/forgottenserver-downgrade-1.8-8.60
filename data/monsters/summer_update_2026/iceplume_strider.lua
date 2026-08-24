local mType = Game.createMonsterType("Iceplume Strider")
local monster = {}

monster.description = "an iceplume strider"
monster.experience = 7500
monster.outfit = {
	lookType = 1950,
	lookHead = 0,
	lookBody = 51,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

monster.raceId = 2803
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

monster.health = 8500
monster.maxHealth = 8500
monster.race = "undead"
monster.corpse = 0 -- Summer 2026 corpse 54333 is newer than this server's items database.
monster.speed = 170
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
	{ name = "platinum coin", chance = 42000, minCount = 1, maxCount = 15 },
	{ name = "small sapphire", chance = 9800, minCount = 1, maxCount = 3 },
	{ name = "crystal coin", chance = 9800 },
	{ name = "blue gem", chance = 4200 },
	{ name = "northwind rod", chance = 4200 },
	-- Summer 2026 loot omitted until the ice shard client item exists.
	{ id = 3007, chance = 850 }, -- crystal ring (name is non-unique: 3007 wearable / 6093 engraved quest copy)
	{ name = "blue robe", chance = 850 },
}

-- Wiki lists Energy + Ice + Mana Drain and NO physical ability, so no melee entry.
-- Exact damage values are not published.
monster.attacks = {
	{ name = "combat", interval = 2000, chance = 25, cooldown = 4000, type = COMBAT_ICEDAMAGE, minDamage = -400, maxDamage = -510, radius = 4, effect = CONST_ME_ICEAREA, target = false },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 4000, type = COMBAT_ENERGYDAMAGE, minDamage = -350, maxDamage = -470, range = 5, shootEffect = CONST_ANI_ENERGY, effect = CONST_ME_ENERGYAREA, target = true },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 6000, type = COMBAT_MANADRAIN, minDamage = -250, maxDamage = -400, range = 5, effect = CONST_ME_MAGIC_BLUE, target = true },
	{ name = "combat", interval = 3000, chance = 12, cooldown = 8000, type = COMBAT_ICEDAMAGE, minDamage = -420, maxDamage = -540, ring = 4, effect = CONST_ME_ICEAREA, target = false },
	{ name = "combat", interval = 2000, chance = 10, cooldown = 6000, type = COMBAT_ICEDAMAGE, minDamage = -180, maxDamage = -260, range = 7, shootEffect = CONST_ANI_ICE, effect = CONST_ME_ICEATTACK, target = true },
}

monster.defenses = {
	defense = 75,
	armor = 75,
	mitigation = 3.68,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = -5 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 10 },
	{ type = COMBAT_EARTHDAMAGE, percent = -5 },
	{ type = COMBAT_FIREDAMAGE, percent = -5 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 10 },
	{ type = COMBAT_HOLYDAMAGE, percent = 10 },
	{ type = COMBAT_DEATHDAMAGE, percent = -5 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = true },
}

mType:register(monster)
