local mType = Game.createMonsterType("The Gravedigger")
local monster = {}

monster.name = "The Gravedigger"
monster.description = "The Gravedigger"
monster.experience = 750000
monster.outfit = {
	lookType = 1880,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0,
}

monster.bosstiary = {
	bossRaceId = 2721,
	bossRace = RARITY_ARCHFOE,
}

monster.health = 65000
monster.maxHealth = 65000
monster.race = "undead"
monster.corpse = 44836 -- Not confirmed
monster.speed = 180
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
	level = 0,
	color = 0,
}

monster.summon = {
	maxSummons = 3, -- Not confirmed
	summons = {
		{ name = "Undead Minion", chance = 12, interval = 5000, count = 3 }, -- Not confirmed
	},
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "I am death!", yell = true },
	{ text = "You shouldn't have meddeled with our affairs!", yell = false },
	{ text = "Ahhhh! ... MORE... POWER!", yell = true },
	{ text = "The master will be pleased with your demise!", yell = false },
}

monster.loot = {
	{ name = "crystal coin", chance = 100000, maxCount = 54 },
	{ name = "yellow gem", chance = 50000, maxCount = 9 }, -- Not confirmed
	{ name = "strong mana potion", chance = 50000, maxCount = 96 }, -- Not confirmed
	{ name = "great mana potion", chance = 40000, maxCount = 42 }, -- Not confirmed
	{ name = "ultimate mana potion", chance = 40000, maxCount = 24 }, -- Not confirmed
	{ name = "ultimate spirit potion", chance = 30000, maxCount = 20 }, -- Not confirmed
	{ name = "ultimate health potion", chance = 30000, maxCount = 37 }, -- Not confirmed
	{ name = "supreme health potion", chance = 30000, maxCount = 17 }, -- Not confirmed
	{ id = 3039, chance = 25000, maxCount = 4 }, -- red gem (não confirmado)
	{ name = "blue gem", chance = 15000, maxCount = 2 }, -- Not confirmed
	{ name = "giant ruby", chance = 10000, maxCount = 2 }, -- Not confirmed
	{ name = "guardian gem", chance = 5000 }, -- Not confirmed
	{ name = "marksman gem", chance = 5000 }, -- Not confirmed
	{ name = "mystic gem", chance = 5000 }, -- Not confirmed
	{ name = "sage gem", chance = 5000 }, -- Not confirmed
	{ name = "spiritualist gem", chance = 5000 }, -- Not confirmed
	{ name = "greater guardian gem", chance = 3000 }, -- Not confirmed
	{ name = "greater marksman gem", chance = 3000 }, -- Not confirmed
	{ name = "greater mystic gem", chance = 3000 }, -- Not confirmed
	{ name = "greater sage gem", chance = 3000 }, -- Not confirmed
	{ name = "greater spiritualist gem", chance = 3000 }, -- Not confirmed
	{ name = "small flask of eyedrops", chance = 15000, maxCount = 2 }, -- Not confirmed
	{ name = "bonelord eye", chance = 10000 }, -- Not confirmed
	{ id = 3457, chance = 10000 }, -- shovel (não confirmado)
	{ name = "terra rod", chance = 10000 }, -- Not confirmed
	{ name = "necrotic rod", chance = 10000 }, -- Not confirmed
	{ name = "bonelord shield", chance = 8000 }, -- Not confirmed
	-- { name = "shrunken head", chance = 5000 }, -- Not confirmed
	-- { name = "cryptic fossil", chance = 5000 }, -- Not confirmed
	-- { name = "fetid heart", chance = 5000 }, -- Not confirmed
	-- { name = "deathly crypt rune", chance = 100000 },
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -1050 }, -- Not confirmed
	{ name = "combat", interval = 2500, chance = 25, type = COMBAT_DEATHDAMAGE, minDamage = -700, maxDamage = -1300, range = 6, shootEffect = CONST_ANI_DEATH, effect = CONST_ME_MORTAREA, target = true }, -- Not confirmed
	{ name = "combat", interval = 3000, chance = 22, type = COMBAT_EARTHDAMAGE, minDamage = -600, maxDamage = -1150, radius = 5, effect = CONST_ME_POISONAREA, target = false }, -- Not confirmed
	{ name = "combat", interval = 2800, chance = 20, type = COMBAT_DEATHDAMAGE, minDamage = -650, maxDamage = -1200, length = 7, spread = 2, effect = CONST_ME_BLACKSMOKE, target = false }, -- Not confirmed
	{ name = "combat", interval = 3500, chance = 18, type = COMBAT_EARTHDAMAGE, minDamage = -550, maxDamage = -1100, range = 7, shootEffect = CONST_ANI_POISON, effect = CONST_ME_GREEN_RINGS, target = true }, -- Not confirmed
	{ name = "condition", type = CONDITION_CURSED, interval = 4000, chance = 12, minDamage = -180, maxDamage = -350, radius = 4, effect = CONST_ME_BLACKSMOKE, target = false }, -- Not confirmed
	{ name = "condition", type = CONDITION_POISON, interval = 4500, chance = 10, minDamage = -150, maxDamage = -300, radius = 5, effect = CONST_ME_POISONAREA, target = false }, -- Not confirmed
}

monster.defenses = {
	defense = 90, -- Not confirmed
	armor = 90, -- Not confirmed
	mitigation = 2.70, -- Not confirmed
	{ name = "combat", interval = 3000, chance = 17, type = COMBAT_HEALING, minDamage = 1000, maxDamage = 1800, effect = CONST_ME_MAGIC_BLUE, target = false }, -- Not confirmed
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
