local mType = Game.createMonsterType("Yorik Youngbook")
local monster = {}

monster.name = "Yorik Youngbook"
monster.description = "Yorik Youngbook"
monster.experience = 0
monster.outfit = {
	lookType = 268,
	lookHead = 57,
	lookBody = 77,
	lookLegs = 79,
	lookFeet = 114,
	lookAddons = 3,
}

monster.health = 155000
monster.maxHealth = 155000
monster.race = "blood"
monster.corpse = 6081
monster.speed = 135
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 10,
}

monster.bosstiary = {
	bossRaceId = 2712,
	bossRace = RARITY_ARCHFOE, -- Not confirmed
}

monster.strategiesTarget = {
	nearest = 70, -- Not confirmed
	health = 10, -- Not confirmed
	damage = 10, -- Not confirmed
	random = 10, -- Not confirmed
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = true,
	convinceable = false,
	pushable = false,
	rewardBoss = false, -- Not confirmed
	illusionable = false, -- Not confirmed
	canPushItems = true,
	canPushCreatures = true, -- Not confirmed
	staticAttackChance = 90, -- Not confirmed
	targetDistance = 4, -- Not confirmed
	runHealth = 0, -- Not confirmed
	healthHidden = false, -- Not confirmed
	isBlockable = false, -- Not confirmed
	canWalkOnEnergy = true,
	canWalkOnFire = true,
	canWalkOnPoison = true,
}

monster.light = {
	level = 0,
	color = 0,
}

monster.voices = {
	interval = 5000, -- Not confirmed
	chance = 10, -- Not confirmed
	{ text = "What? How did they get in?", yell = false },
	{ text = "Stand together, team!", yell = false },
	{ text = "It's time to get serious!", yell = false },
	{ text = "You swing like a rookie!", yell = false },
	{ text = "Sorry, my guild needs me!", yell = false },
}

monster.loot = {}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -300 }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 22, type = COMBAT_FIREDAMAGE, minDamage = -400, maxDamage = -700, range = 7, shootEffect = CONST_ANI_FIRE, effect = CONST_ME_FIREATTACK, target = true }, -- Not confirmed
	{ name = "combat", interval = 2500, chance = 18, type = COMBAT_ENERGYDAMAGE, minDamage = -450, maxDamage = -750, length = 6, spread = 3, effect = CONST_ME_ENERGYHIT, target = false }, -- Not confirmed
	{ name = "combat", interval = 3000, chance = 15, type = COMBAT_FIREDAMAGE, minDamage = -550, maxDamage = -850, radius = 5, effect = CONST_ME_FIREAREA, target = false }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 12, type = COMBAT_ENERGYDAMAGE, minDamage = -350, maxDamage = -550, radius = 4, effect = CONST_ME_PURPLEENERGY, target = false }, -- Not confirmed
}

monster.defenses = {
	defense = 70, -- Not confirmed
	armor = 70, -- Not confirmed
	mitigation = 1.90, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 12, type = COMBAT_HEALING, minDamage = 300, maxDamage = 550, effect = CONST_ME_MAGIC_BLUE, target = false }, -- Not confirmed
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
