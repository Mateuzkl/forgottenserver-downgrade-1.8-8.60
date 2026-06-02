local mType = Game.createMonsterType("Ice Crawler")
local monster = {}

monster.name = "Ice Crawler"
monster.description = "an ice crawler"
monster.experience = 0
monster.outfit = {
	lookType = 263,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0,
}

monster.health = 12000 -- Not confirmed
monster.maxHealth = 12000 -- Not confirmed
monster.race = "undead"
monster.corpse = 0
monster.speed = 160
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 10,
}

monster.strategiesTarget = {
	nearest = 70, -- Not confirmed
	health = 10, -- Not confirmed
	damage = 10, -- Not confirmed
	random = 10, -- Not confirmed
}

monster.flags = {
	summonable = false, -- Not confirmed
	attackable = true, -- Not confirmed
	hostile = true, -- Not confirmed
	convinceable = false, -- Not confirmed
	pushable = false, -- Not confirmed
	rewardBoss = false, -- Not confirmed
	illusionable = false, -- Not confirmed
	canPushItems = true, -- Not confirmed
	canPushCreatures = true, -- Not confirmed
	staticAttackChance = 90, -- Not confirmed
	targetDistance = 1, -- Not confirmed
	runHealth = 0, -- Not confirmed
	healthHidden = false, -- Not confirmed
	isBlockable = false, -- Not confirmed
	canWalkOnEnergy = true, -- Not confirmed
	canWalkOnFire = true, -- Not confirmed
	canWalkOnPoison = true, -- Not confirmed
}

monster.light = {
	level = 0,
	color = 0,
}

monster.loot = {}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -500 }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 20, type = COMBAT_ICEDAMAGE, minDamage = -400, maxDamage = -700, range = 5, shootEffect = CONST_ANI_ICE, effect = CONST_ME_ICEATTACK, target = true }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 15, type = COMBAT_ICEDAMAGE, minDamage = -500, maxDamage = -800, radius = 4, effect = CONST_ME_ICEAREA, target = false }, -- Not confirmed
}

monster.defenses = {
	defense = 55, -- Not confirmed
	armor = 55, -- Not confirmed
	mitigation = 2.00, -- Not confirmed
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 0 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 0 },
	{ type = COMBAT_EARTHDAMAGE, percent = 0 },
	{ type = COMBAT_FIREDAMAGE, percent = 0 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 }, -- Not confirmed
	{ type = COMBAT_MANADRAIN, percent = 0 }, -- Not confirmed
	{ type = COMBAT_DROWNDAMAGE, percent = 0 }, -- Not confirmed
	{ type = COMBAT_ICEDAMAGE, percent = 0 },
	{ type = COMBAT_HOLYDAMAGE, percent = 0 },
	{ type = COMBAT_DEATHDAMAGE, percent = 0 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true }, -- Not confirmed
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = true }, -- Not confirmed
}

mType:register(monster)
