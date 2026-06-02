local mType = Game.createMonsterType("Tamed Frazzlemaw")
local monster = {}

monster.name = "Tamed Frazzlemaw"
monster.description = "a tamed frazzlemaw"
monster.experience = 0
monster.outfit = {
	lookType = 594,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0,
}

monster.health = 1000
monster.maxHealth = 1000
monster.race = "blood"
monster.corpse = 0
monster.speed = 165 -- Not confirmed
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 10,
}

monster.strategiesTarget = {
	nearest = 70,
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = false,
	convinceable = false,
	pushable = false,
	rewardBoss = false,
	illusionable = false,
	canPushItems = false,
	canPushCreatures = false,
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
	level = 0, -- Not confirmed
	color = 0, -- Not confirmed
}

monster.loot = {}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -800 }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 25, type = COMBAT_PHYSICALDAMAGE, minDamage = -500, maxDamage = -900, radius = 4, effect = CONST_ME_HITAREA, target = false }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 20, type = COMBAT_LIFEDRAIN, minDamage = -400, maxDamage = -700, range = 1, effect = CONST_ME_DRAWBLOOD, target = true }, -- Not confirmed
}

monster.defenses = {
	defense = 90, -- Not confirmed
	armor = 90, -- Not confirmed
	mitigation = 2.60, -- Not confirmed
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
	{ type = "paralyze", condition = true }, -- Not confirmed
	{ type = "outfit", condition = true }, -- Not confirmed
	{ type = "invisible", condition = true }, -- Not confirmed
	{ type = "bleed", condition = true }, -- Not confirmed
}

mType:register(monster)
