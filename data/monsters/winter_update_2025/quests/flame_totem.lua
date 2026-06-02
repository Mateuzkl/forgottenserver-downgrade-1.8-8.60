local mType = Game.createMonsterType("Flame Totem")
local monster = {}

monster.name = "Flame Totem"
monster.description = "a flame totem"
monster.experience = 0
monster.outfit = {
	lookTypeEx = 51953,
}

monster.health = 15000 -- Not confirmed
monster.maxHealth = 15000 -- Not confirmed
monster.race = "fire"
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
	level = 0,
	color = 0,
}

monster.loot = {}

monster.attacks = {
	{ name = "combat", interval = 2000, chance = 100, type = COMBAT_FIREDAMAGE, minDamage = -600, maxDamage = -1000, radius = 6, effect = CONST_ME_HITBYFIRE, target = false }, -- Not confirmed
}

monster.defenses = {
	defense = 70, -- Not confirmed
	armor = 70, -- Not confirmed
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
	{ type = "paralyze", condition = true }, -- Not confirmed
	{ type = "outfit", condition = true }, -- Not confirmed
	{ type = "invisible", condition = true }, -- Not confirmed
	{ type = "bleed", condition = true }, -- Not confirmed
}

mType:register(monster)
