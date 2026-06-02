local mType = Game.createMonsterType("Adventurer Group")
local monster = {}

monster.name = "Adventurer Group"
monster.description = "an adventurer group"
monster.experience = 100000
monster.outfit = {
	lookTypeEx = 52836,
}

monster.bosstiary = {
	bossRaceId = 2773,
	bossRace = RARITY_ARCHFOE, -- Not confirmed
}

monster.health = 80000
monster.maxHealth = 80000
monster.race = "blood"
monster.corpse = 52836 -- Not confirmed
monster.speed = 175
monster.manaCost = 0

monster.changeTarget = {
	interval = 3000, -- Not confirmed
	chance = 20, -- Not confirmed
}

monster.strategiesTarget = {
	nearest = 60, -- Not confirmed
	health = 20, -- Not confirmed
	damage = 10, -- Not confirmed
	random = 10, -- Not confirmed
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = true,
	convinceable = false,
	pushable = false,
	rewardBoss = true,
	illusionable = false, -- Not confirmed
	canPushItems = true,
	canPushCreatures = true, -- Not confirmed
	staticAttackChance = 85, -- Not confirmed
	targetDistance = 1,
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
	{ text = "Together we are unstoppable!", yell = true }, -- Not confirmed
	{ text = "Cover me, allies!", yell = false }, -- Not confirmed
	{ text = "For glory and treasure!", yell = true }, -- Not confirmed
	{ text = "Work together, team!", yell = false }, -- Not confirmed
	{ text = "No beast can defeat us united!", yell = true }, -- Not confirmed
}

monster.loot = {
	{ name = "platinum coin", chance = 100000, maxCount = 60 }, -- Not confirmed
	{ name = "crystal coin", chance = 35000, maxCount = 5 }, -- Not confirmed
	{ name = "gold ingot", chance = 25000, maxCount = 3 }, -- Not confirmed
	{ name = "blue gem", chance = 18000 }, -- Not confirmed
	{ name = "green gem", chance = 16000 }, -- Not confirmed
	{ name = "violet gem", chance = 14000 }, -- Not confirmed
	{ name = "yellow gem", chance = 12000 }, -- Not confirmed
	{ id = 3039, chance = 10000 }, -- red gem (não confirmado)
	{ name = "crown armor", chance = 5000 }, -- Not confirmed
	{ name = "crown shield", chance = 5500 }, -- Not confirmed
	{ name = "crown helmet", chance = 4500 }, -- Not confirmed
	{ name = "crown legs", chance = 4000 }, -- Not confirmed
	{ name = "mastermind shield", chance = 2000 }, -- Not confirmed
	{ name = "great shield", chance = 1500 }, -- Not confirmed
	{ name = "giant sword", chance = 3000 }, -- Not confirmed
	{ name = "magic longsword", chance = 2500 }, -- Not confirmed
	{ name = "small diamond", chance = 30000, maxCount = 5 }, -- Not confirmed
	{ name = "small ruby", chance = 28000, maxCount = 5 }, -- Not confirmed
	{ name = "small sapphire", chance = 26000, maxCount = 5 }, -- Not confirmed
	{ name = "small emerald", chance = 24000, maxCount = 5 },
	-- { name = "ancient crypt rune", chance = 100000 }, -- Not confirmed
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -1000 }, -- Not confirmed
	{ name = "combat", interval = 2500, chance = 22, type = COMBAT_PHYSICALDAMAGE, minDamage = -700, maxDamage = -1300, range = 5, shootEffect = CONST_ANI_ARROW, target = true }, -- Not confirmed
	{ name = "combat", interval = 3000, chance = 20, type = COMBAT_FIREDAMAGE, minDamage = -600, maxDamage = -1100, range = 7, shootEffect = CONST_ANI_FIRE, effect = CONST_ME_FIREATTACK, target = true }, -- Not confirmed
	{ name = "combat", interval = 2800, chance = 18, type = COMBAT_HOLYDAMAGE, minDamage = -500, maxDamage = -900, radius = 4, effect = CONST_ME_HOLYAREA, target = false }, -- Not confirmed
	{ name = "combat", interval = 3500, chance = 15, type = COMBAT_ICEDAMAGE, minDamage = -550, maxDamage = -1000, length = 6, spread = 2, effect = CONST_ME_ICEAREA, target = false }, -- Not confirmed
}

monster.defenses = {
	defense = 90, -- Not confirmed
	armor = 90, -- Not confirmed
	mitigation = 2.80, -- Not confirmed
	{ name = "combat", interval = 2500, chance = 18, type = COMBAT_HEALING, minDamage = 1000, maxDamage = 2000, effect = CONST_ME_MAGIC_BLUE, target = false }, -- Not confirmed
	{ name = "speed", interval = 4000, chance = 12, speedChange = 350, effect = CONST_ME_MAGIC_GREEN, target = false, duration = 6000 }, -- Not confirmed
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
	{ type = "outfit", condition = false }, -- Not confirmed
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false }, -- Not confirmed
}

mType:register(monster)
