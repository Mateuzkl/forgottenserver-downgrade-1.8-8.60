local mType = Game.createMonsterType("An Eye")
local monster = {}

monster.name = "An Eye"
monster.description = "an eye"
monster.experience = 0
monster.outfit = {
	lookType = 925,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0,
}

monster.health = 5000 -- Not confirmed
monster.maxHealth = 5000 -- Not confirmed
monster.race = "undead"
monster.corpse = 0
monster.speed = 0 -- Not confirmed
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 10,
}

monster.strategiesTarget = {
	nearest = 100, -- Not confirmed
}

monster.flags = {
	summonable = false, -- Not confirmed
	attackable = true, -- Not confirmed
	hostile = true, -- Not confirmed
	convinceable = false, -- Not confirmed
	pushable = false, -- Not confirmed
	rewardBoss = false, -- Not confirmed
	illusionable = false, -- Not confirmed
	canPushItems = false, -- Not confirmed
	canPushCreatures = false, -- Not confirmed
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
	level = 3, -- Not confirmed
	color = 180, -- Not confirmed
}

monster.loot = {}

-- Agony Explosion - deals 30% of player's max HP
monster.attacks = {
	{ name = "combat", interval = 3000, chance = 100, type = COMBAT_LIFEDRAIN, minDamage = -500, maxDamage = -1500, radius = 5, effect = CONST_ME_DRAWBLOOD, target = false }, -- Not confirmed
}

monster.defenses = {
	defense = 50, -- Not confirmed
	armor = 50, -- Not confirmed
	mitigation = 1.50, -- Not confirmed
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
