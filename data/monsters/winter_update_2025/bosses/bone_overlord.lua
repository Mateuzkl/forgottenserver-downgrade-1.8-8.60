local mType = Game.createMonsterType("Bone Overlord")
local monster = {}

monster.name = "Bone Overlord"
monster.description = "Bone Overlord"
monster.experience = 0
monster.outfit = {
	lookTypeEx = 52831,
}

monster.bosstiary = {
	bossRaceId = 2771,
	bossRace = RARITY_ARCHFOE, -- Not confirmed
	storageCooldown = 82070, -- Not confirmed
}

monster.health = 500000
monster.maxHealth = 500000
monster.race = "undead"
monster.corpse = 44822 -- Not confirmed
monster.speed = 200
monster.manaCost = 0

monster.changeTarget = {
	interval = 3000, -- Not confirmed
	chance = 25, -- Not confirmed
}

monster.strategiesTarget = {
	nearest = 60, -- Not confirmed
	health = 15, -- Not confirmed
	damage = 15, -- Not confirmed
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
	canPushItems = false,
	canPushCreatures = false, -- Not confirmed
	staticAttackChance = 90, -- Not confirmed
	targetDistance = 0,
	runHealth = 0, -- Not confirmed
	healthHidden = false, -- Not confirmed
	isBlockable = false, -- Not confirmed
	canWalkOnEnergy = false,
	canWalkOnFire = false,
	canWalkOnPoison = false,
}

monster.light = {
	level = 4, -- Not confirmed
	color = 30, -- Not confirmed
}

monster.summon = {
	maxSummons = 4, -- Not confirmed
	summons = {
		{ name = "Spectral Eye", chance = 15, interval = 3000, count = 2 }, -- Not confirmed
		{ name = "Restless Spirit", chance = 12, interval = 4000, count = 2 }, -- Not confirmed
	},
}

monster.voices = {
	interval = 5000,
	chance = 15,
	{ text = "ALL DRAGON SOULS BELONG TO ME!", yell = true },
	{ text = "MY RULE OVER LIFE AND DEATH WILL BE UNCHALLENGED!", yell = true },
	{ text = "THE SOULS ARE MINE!", yell = true },
	{ text = "BY THE ELDRITCH POWERS THAT I COMMAND - I WILL CRUSH YOU!", yell = true },
	{ text = "BEHOLD THE LAST OF THE TRUE BONELORDS!", yell = true },
	{ text = "I DON'T NEED THE SHIRON'FAL!", yell = true },
	{ text = "YOU WILL SUFFER FOR THIS!", yell = true },
	{ text = "DEATH AWAITS THOSE WHO DEFY ME!", yell = true },
	{ text = "IN THE END VICTORY WILL BE MINE NONTHELESS!", yell = true },
	{ text = "MY VICTORY WILL BE GLORIOUS!", yell = true },
	{ text = "YOU PUNY CREATURES CAN'T STOP ME!", yell = true },
}

monster.loot = {
	-- { name = "ancient scales", chance = 15000 },
	-- { name = "soul trap", chance = 10000 },
	-- { name = "necromantic crypt rune", chance = 100000 },
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -1200 }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 25, type = COMBAT_DEATHDAMAGE, minDamage = -900, maxDamage = -1800, range = 7, shootEffect = CONST_ANI_DEATH, effect = CONST_ME_MORTAREA, target = true }, -- Not confirmed
	{ name = "combat", interval = 2500, chance = 22, type = COMBAT_ENERGYDAMAGE, minDamage = -800, maxDamage = -1600, radius = 6, effect = CONST_ME_ENERGYAREA, target = false }, -- Not confirmed
	{ name = "combat", interval = 3000, chance = 20, type = COMBAT_ICEDAMAGE, minDamage = -700, maxDamage = -1400, length = 8, spread = 3, effect = CONST_ME_ICEATTACK, target = false }, -- Not confirmed
	{ name = "combat", interval = 2800, chance = 18, type = COMBAT_DEATHDAMAGE, minDamage = -1000, maxDamage = -2000, radius = 5, effect = CONST_ME_DRAWBLOOD, target = false }, -- Not confirmed
	{ name = "combat", interval = 3500, chance = 15, type = COMBAT_LIFEDRAIN, minDamage = -600, maxDamage = -1200, radius = 4, effect = CONST_ME_MAGIC_RED, target = false }, -- Not confirmed
	{ name = "speed", interval = 4000, chance = 12, speedChange = -500, radius = 6, effect = CONST_ME_POFF, target = false, duration = 10000 }, -- Not confirmed
	{ name = "condition", type = CONDITION_CURSED, interval = 5000, chance = 10, minDamage = -200, maxDamage = -400, radius = 5, effect = CONST_ME_BLACKSMOKE, target = false }, -- Not confirmed
}

monster.defenses = {
	defense = 100, -- Not confirmed
	armor = 100, -- Not confirmed
	mitigation = 3.50, -- Not confirmed
	{ name = "combat", interval = 2500, chance = 20, type = COMBAT_HEALING, minDamage = 2000, maxDamage = 4000, effect = CONST_ME_MAGIC_BLUE, target = false }, -- Not confirmed
	{ name = "speed", interval = 5000, chance = 10, speedChange = 400, effect = CONST_ME_MAGIC_GREEN, target = false, duration = 5000 }, -- Not confirmed
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 100 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 100 },
	{ type = COMBAT_EARTHDAMAGE, percent = 100 },
	{ type = COMBAT_FIREDAMAGE, percent = 100 },
	{ type = COMBAT_LIFEDRAIN, percent = 100 }, -- Not confirmed
	{ type = COMBAT_MANADRAIN, percent = 0 }, -- Not confirmed
	{ type = COMBAT_DROWNDAMAGE, percent = 0 }, -- Not confirmed
	{ type = COMBAT_ICEDAMAGE, percent = 100 },
	{ type = COMBAT_HOLYDAMAGE, percent = 100 },
	{ type = COMBAT_DEATHDAMAGE, percent = 100 },
}

monster.immunities = {
	{ type = "paralyze", condition = true }, -- Not confirmed
	{ type = "outfit", condition = false }, -- Not confirmed
	{ type = "invisible", condition = true }, -- Not confirmed
	{ type = "bleed", condition = false }, -- Not confirmed
}

mType:register(monster)
