local mType = Game.createMonsterType("Clavius")
local monster = {}

monster.name = "Clavius"
monster.description = "Clavius"
monster.experience = 75000
monster.outfit = {
	lookType = 1902,
	lookHead = 94,
	lookBody = 19,
	lookLegs = 36,
	lookFeet = 56,
	lookAddons = 0,
	lookMount = 0,
}

monster.bosstiary = {
	bossRaceId = 2755,
	bossRace = RARITY_BANE,
}

monster.health = 40000
monster.maxHealth = 40000
monster.race = "undead"
monster.corpse = 52857
monster.speed = 175
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000, -- Not confirmed
	chance = 15, -- Not confirmed
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
	rewardBoss = true,
	illusionable = false, -- Not confirmed
	canPushItems = true,
	canPushCreatures = true, -- Not confirmed
	staticAttackChance = 90, -- Not confirmed
	targetDistance = 1,
	runHealth = 0, -- Not confirmed
	healthHidden = false, -- Not confirmed
	isBlockable = false, -- Not confirmed
	canWalkOnEnergy = true,
	canWalkOnFire = true,
	canWalkOnPoison = true,
}

monster.light = {
	level = 2, -- Not confirmed
	color = 45, -- Not confirmed
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "Never will I let your agression stand!", yell = false },
}

monster.loot = {
	{ name = "platinum coin", chance = 100000, maxCount = 50 },
	{ id = 3039, chance = 33070 }, -- red gem
	{ name = "yellow gem", chance = 29920 },
	{ name = "small ruby", chance = 23620, maxCount = 5 },
	{ name = "crystal coin", chance = 22830, maxCount = 10 },
	{ name = "small diamond", chance = 22050, maxCount = 5 },
	{ id = 6299, chance = 21260 }, -- death ring
	{ name = "small sapphire", chance = 20470, maxCount = 5 },
	{ name = "black pearl", chance = 18110 },
	-- { name = "cuirass plate", chance = 17320 },
	-- { name = "battle tactics", chance = 16540 },
	{ name = "green gem", chance = 15750 },
	{ name = "dark armor", chance = 14170 },
	{ name = "white pearl", chance = 12600 },
	{ name = "blue gem", chance = 11810 },
	{ name = "wand of cosmic energy", chance = 10240 },
	-- { name = "silver poniard", chance = 9450 },
	-- { name = "stag parchment", chance = 9450 },
	{ id = 3059, chance = 7870 }, -- spellbook
	{ name = "gold ingot", chance = 7090 },
	-- { name = "repair kit for boats", chance = 7090 },
	{ name = "strange helmet", chance = 5510 },
	{ name = "white gem", chance = 5510 },
	{ name = "wand of starstorm", chance = 4720 },
	{ name = "clerical mace", chance = 3940 },
	{ name = "terra hood", chance = 3940 },
	{ name = "magma monocle", chance = 2360 },
	{ name = "violet gem", chance = 2360 },
	{ name = "wooden spellbook", chance = 1570 },
	{ name = "lightning robe", chance = 790 },
	{ name = "shockwave amulet", chance = 790 },
	{ name = "magma amulet", chance = 790 }, -- Not confirmed
	-- { name = "salvaged iron parts", chance = 7740 },
	-- { name = "personal letter of clavius i", chance = 8330 },
	-- { name = "personal letter of clavius ii", chance = 4170 },
	-- { name = "personal letter of clavius iii", chance = 2080 },
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -950 }, -- Not confirmed
	{ name = "combat", interval = 2500, chance = 24, type = COMBAT_DEATHDAMAGE, minDamage = -600, maxDamage = -1100, range = 6, shootEffect = CONST_ANI_DEATH, effect = CONST_ME_MORTAREA, target = true }, -- Not confirmed
	{ name = "combat", interval = 3000, chance = 20, type = COMBAT_PHYSICALDAMAGE, minDamage = -700, maxDamage = -1200, radius = 4, effect = CONST_ME_GROUNDSHAKER, target = false }, -- Not confirmed
	{ name = "combat", interval = 2800, chance = 18, type = COMBAT_DEATHDAMAGE, minDamage = -550, maxDamage = -1000, length = 6, spread = 2, effect = CONST_ME_BLACKSMOKE, target = false }, -- Not confirmed
	{ name = "condition", type = CONDITION_CURSED, interval = 4000, chance = 12, minDamage = -150, maxDamage = -300, radius = 4, effect = CONST_ME_BLACKSMOKE, target = false }, -- Not confirmed
}

monster.defenses = {
	defense = 88, -- Not confirmed
	armor = 88, -- Not confirmed
	mitigation = 2.60, -- Not confirmed
	{ name = "combat", interval = 3000, chance = 15, type = COMBAT_HEALING, minDamage = 900, maxDamage = 1600, effect = CONST_ME_MAGIC_BLUE, target = false }, -- Not confirmed
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
