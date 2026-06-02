local mType = Game.createMonsterType("Enthralled Dragon")
local monster = {}

monster.name = "Enthralled Dragon"
monster.description = "an enthralled dragon"
monster.experience = 0
monster.outfit = {
	lookType = 231,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0,
}

monster.health = 40000 -- Not confirmed
monster.maxHealth = 40000 -- Not confirmed
monster.race = "blood"
monster.corpse = 0
monster.speed = 160 -- Not confirmed
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
	level = 0, -- Not confirmed
	color = 0, -- Not confirmed
}

monster.loot = {}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -800 }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 20, type = COMBAT_FIREDAMAGE, minDamage = -700, maxDamage = -1200, length = 8, spread = 3, effect = CONST_ME_FIREATTACK, target = false }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 15, type = COMBAT_FIREDAMAGE, minDamage = -600, maxDamage = -1000, radius = 5, effect = CONST_ME_FIREAREA, target = false }, -- Not confirmed
}

monster.defenses = {
	defense = 70, -- Not confirmed
	armor = 70, -- Not confirmed
	mitigation = 2.50, -- Not confirmed
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
