local mType = Game.createMonsterType("Abyssal Calamary")
local monster = {}

monster.name = "Abyssal Calamary"
monster.description = "an abyssal calamary"
monster.experience = 200
monster.outfit = {
	lookType = 451,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0,
}

monster.raceId = 1105

monster.health = 300
monster.maxHealth = 300
monster.race = "blood"
monster.corpse = 13853
monster.speed = 280
monster.manaCost = 0

monster.changeTarget = {
	interval = 2000,
	chance = 0,
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = true,
	convinceable = false,
	pushable = false,
	rewardBoss = false,
	illusionable = true,
	canPushItems = false,
	canPushCreatures = false,
	staticAttackChance = 90,
	targetDistance = 1,
	runHealth = 89,
	healthHidden = false,
	ignoreSpawnBlock = false,
	canWalkOnEnergy = false,
	canWalkOnFire = true,
	canWalkOnPoison = true
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
	{ id = 3578, chance = 10660 }, -- fish
	{ name = "shrimp", chance = 9960 },
	{ name = "black pearl", chance = 1340 },
	{ name = "small amethyst", chance = 580 },
	{ name = "white pearl", chance = 520 },
	{ name = "small ruby", chance = 520 },
	{ name = "small sapphire", chance = 270 },
}

monster.attacks = {}

monster.defenses = {
	defense = 13,
	armor = 12,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = -5 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -5 },
	{ type = COMBAT_EARTHDAMAGE, percent = 100 },
	{ type = COMBAT_FIREDAMAGE, percent = 100 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 100 },
	{ type = COMBAT_ICEDAMAGE, percent = 0 },
	{ type = COMBAT_HOLYDAMAGE, percent = 0 },
	{ type = COMBAT_DEATHDAMAGE, percent = 0 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = false },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
