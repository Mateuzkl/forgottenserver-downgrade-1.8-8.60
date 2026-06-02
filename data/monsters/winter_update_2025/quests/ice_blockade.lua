local mType = Game.createMonsterType("Ice Blockade")
local monster = {}

monster.name = "Ice Blockade"
monster.description = "an ice blockade"
monster.experience = 0
monster.outfit = {
	lookTypeEx = 6707,
}

monster.health = 600
monster.maxHealth = 600
monster.race = "undead"
monster.corpse = 0
monster.speed = 0
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
	hostile = false, -- Not confirmed
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
	isBlockable = true, -- Not confirmed
	canWalkOnEnergy = true, -- Not confirmed
	canWalkOnFire = true, -- Not confirmed
	canWalkOnPoison = true, -- Not confirmed
}

monster.light = {
	level = 0, -- Not confirmed
	color = 0, -- Not confirmed
}

monster.loot = {}

monster.attacks = {} -- Not confirmed

monster.defenses = {
	defense = 100, -- Not confirmed
	armor = 100, -- Not confirmed
	mitigation = 3.00, -- Not confirmed
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
