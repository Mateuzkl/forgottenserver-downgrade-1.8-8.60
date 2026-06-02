local mType = Game.createMonsterType("Percy Peacetinker")
local monster = {}

monster.name = "Percy Peacetinker"
monster.description = "Percy Peacetinker"
monster.experience = 0
monster.outfit = {
	lookType = 137,
	lookHead = 79,
	lookBody = 31,
	lookLegs = 101,
	lookFeet = 130,
	lookAddons = 3,
}

monster.health = 155000
monster.maxHealth = 155000
monster.race = "blood"
monster.corpse = 6081
monster.speed = 125
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 10,
}

monster.bosstiary = {
	bossRaceId = 2713,
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
	targetDistance = 5, -- Not confirmed
	runHealth = 0, -- Not confirmed
	healthHidden = false, -- Not confirmed
	isBlockable = false, -- Not confirmed
	canWalkOnEnergy = true,
	canWalkOnFire = true,
	canWalkOnPoison = true,
}

monster.light = {
	level = 0, -- Not confirmed
	color = 0, -- Not confirmed
}

monster.voices = {
	interval = 5000, -- Not confirmed
	chance = 10, -- Not confirmed
	{ text = "THE LOOT IS OURS!", yell = false },
	{ text = "You won't get away with this!", yell = false },
	{ text = "Let's see if you can dodge these holy arrows!", yell = false },
	{ text = "Time to stock up on diamond arrows!", yell = false },
}

monster.loot = {}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -380 }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 25, type = COMBAT_PHYSICALDAMAGE, minDamage = -400, maxDamage = -700, range = 7, shootEffect = CONST_ANI_ROYALSPEAR, effect = CONST_ME_EXPLOSIONHIT, target = true }, -- Not confirmed
	{ name = "combat", interval = 2500, chance = 18, type = COMBAT_HOLYDAMAGE, minDamage = -350, maxDamage = -600, range = 7, shootEffect = CONST_ANI_HOLY, effect = CONST_ME_HOLYDAMAGE, target = true }, -- Not confirmed
	{ name = "combat", interval = 3000, chance = 15, type = COMBAT_HOLYDAMAGE, minDamage = -500, maxDamage = -750, radius = 4, effect = CONST_ME_HOLYAREA, target = false }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 12, type = COMBAT_PHYSICALDAMAGE, minDamage = -450, maxDamage = -650, length = 6, spread = 2, effect = CONST_ME_EXPLOSIONHIT, target = false }, -- Not confirmed
}

monster.defenses = {
	defense = 82, -- Not confirmed
	armor = 82, -- Not confirmed
	mitigation = 2.15, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 15, type = COMBAT_HEALING, minDamage = 450, maxDamage = 750, effect = CONST_ME_MAGIC_BLUE, target = false }, -- Not confirmed
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
