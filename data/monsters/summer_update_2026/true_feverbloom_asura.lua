local mType = Game.createMonsterType("True Feverbloom Asura")
local monster = {}

monster.description = "a true feverbloom asura"
monster.experience = 7850
monster.outfit = {
	lookType = 1068,
	lookHead = 62,
	lookBody = 61,
	lookLegs = 114,
	lookFeet = 15,
	lookAddons = 1,
}

monster.raceId = 2805
monster.Bestiary = {
	class = "Human",
	race = BESTY_RACE_HUMAN,
	toKill = 2500,
	FirstUnlock = 100,
	SecondUnlock = 1000,
	CharmsPoints = 50,
	Stars = 4,
	Occurrence = 0,
	Locations = "Asura Citadel.",
}

monster.health = 8600
monster.maxHealth = 8600
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54659 is newer than this server's items database.
monster.speed = 175
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
	{ name = "platinum coin", chance = 42000, minCount = 1, maxCount = 8 },
	{ name = "seeds", chance = 9800, minCount = 1, maxCount = 3 },
	{ name = "demonic essence", chance = 9800, minCount = 1, maxCount = 2 },
	{ name = "flask of demonic blood", chance = 9800 },
	{ name = "golden lotus brooch", chance = 9800 },
	{ name = "peacock feather fan", chance = 9800 },
	{ name = "wild flowers", chance = 9800 },
	-- Summer 2026 loot omitted until the pyrophyte seed pod client item exists.
	{ name = "crystal coin", chance = 4200 },
	{ name = "muck rod", chance = 4200 },
	{ name = "oriental shoes", chance = 4200 },
}

-- Wiki lists Physical + Earth + Fire + Mana Drain; exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -420, maxDamage = -540 },
	{ name = "combat", interval = 2000, chance = 22, cooldown = 4000, type = COMBAT_FIREDAMAGE, minDamage = -380, maxDamage = -500, radius = 4, effect = CONST_ME_FIREAREA, target = false },
	{ name = "combat", interval = 2000, chance = 18, cooldown = 4000, type = COMBAT_EARTHDAMAGE, minDamage = -330, maxDamage = -460, range = 5, shootEffect = CONST_ANI_POISON, effect = CONST_ME_GREEN_RINGS, target = true },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 6000, type = COMBAT_MANADRAIN, minDamage = -250, maxDamage = -400, range = 5, effect = CONST_ME_MAGIC_BLUE, target = true },
}

monster.defenses = {
	defense = 90,
	armor = 90,
	mitigation = 3.45,
}

-- Fire is listed as -50% on the wiki: this creature is HEALED by fire damage.
-- percent = 150 produces negative damage (healing) in the engine.
monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 0 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -5 },
	{ type = COMBAT_EARTHDAMAGE, percent = 15 },
	{ type = COMBAT_FIREDAMAGE, percent = 150 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = -10 },
	{ type = COMBAT_HOLYDAMAGE, percent = -5 },
	{ type = COMBAT_DEATHDAMAGE, percent = -10 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
