local mType = Game.createMonsterType("Court Warlock")
local monster = {}

monster.name = "Court Warlock"
monster.description = "a court warlock"
monster.experience = 147000
monster.outfit = {
	lookType = 1903,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 1,
	lookMount = 0,
}

monster.bosstiary = {
	bossRaceId = 2741,
	bossRace = RARITY_BANE,
}

monster.health = 120000
monster.maxHealth = 120000
monster.race = "blood"
monster.corpse = 52696
monster.speed = 170
monster.manaCost = 0

monster.changeTarget = {
	interval = 3500, -- Not confirmed
	chance = 18, -- Not confirmed
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
	canPushItems = true,
	canPushCreatures = true, -- Not confirmed
	staticAttackChance = 85, -- Not confirmed
	targetDistance = 1,
	runHealth = 500, -- Not confirmed
	healthHidden = false, -- Not confirmed
	isBlockable = false, -- Not confirmed
	canWalkOnEnergy = true,
	canWalkOnFire = true,
	canWalkOnPoison = true,
}

monster.light = {
	level = 3, -- Not confirmed
	color = 60, -- Not confirmed
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "I welcome you, towitness the spectacular destruction of the outside menace that cost so many of your dear bretheren's lives!", yell = false },
	{ text = "You're just flies in my bed-chamber.", yell = false },
	{ text = "Oh? Maybe it shold get a little more COSY IN HERE!", yell = true },
	{ text = "Enough of this! Stop gawking and GET OVER THERE TO FINIFH IT!", yell = true },
	{ text = "DO I REALLY HAVE TO DO EVERYTHING... EEEVERYTHIIING MYSELF!", yell = true },
	{ text = "Your medding with my affairs was a grave mistake!", yell = false },
	{ text = "NO, not like this, not without... NOOO!!!", yell = true },
}

monster.loot = {
	{ name = "platinum coin", chance = 100000, maxCount = 100 },
	{ name = "crystal coin", chance = 100000, maxCount = 2 },
	{ id = 3039, chance = 56250, maxCount = 3 }, -- red gem
	{ name = "great spirit potion", chance = 50000, maxCount = 30 },
	{ name = "yellow gem", chance = 43750, maxCount = 4 },
	{ name = "blue gem", chance = 43750, maxCount = 2 },
	{ name = "ultimate mana potion", chance = 37500, maxCount = 39 },
	{ name = "great mana potion", chance = 37500, maxCount = 30 },
	{ name = "supreme health potion", chance = 37500, maxCount = 8 },
	{ name = "strong mana potion", chance = 31250, maxCount = 25 },
	{ name = "ultimate health potion", chance = 31250, maxCount = 16 },
	{ name = "wooden spellbook", chance = 31250 },
	{ id = 6299, chance = 31250 }, -- death ring
	{ name = "mind stone", chance = 25000 },
	{ name = "skull staff", chance = 18750 },
	{ name = "magma coat", chance = 18750 },
	{ name = "giant sapphire", chance = 18750 },
	{ name = "wand of defiance", chance = 18750 },
	{ name = "ultimate spirit potion", chance = 12500, maxCount = 11 },
	{ name = "giant ruby", chance = 12500 },
	{ name = "lightning robe", chance = 12500 },
	{ name = "might ring", chance = 12500 },
	{ name = "terra mantle", chance = 12500 },
	{ name = "glacier robe", chance = 6250 },
	{ name = "terra hood", chance = 6250 },
	{ name = "lightning headband", chance = 6250 },
	{ name = "giant amethyst", chance = 5000 }, -- Not confirmed
	{ name = "giant emerald", chance = 5000 }, -- Not confirmed
	{ id = 37335, chance = 5000 }, -- black skull (não confirmado)
	{ name = "magma monocle", chance = 5000 }, -- Not confirmed
	{ name = "glacier mask", chance = 5000 }, -- Not confirmed
	-- { name = "broken staff of mind control", chance = 5000 }, -- Not confirmed
	-- { name = "twisted marionette", chance = 5000 }, -- Not confirmed
	-- { name = "stag shinguards", chance = 3000 }, -- Not confirmed
	-- { name = "stag boots", chance = 3000 }, -- Not confirmed
	-- { name = "stag footwraps", chance = 3000 }, -- Not confirmed
	-- { name = "stag scrolls", chance = 3000 }, -- Not confirmed
	-- { name = "stag spellbook", chance = 3000 }, -- Not confirmed
	-- { name = "stag shield", chance = 3000 }, -- Not confirmed
	-- { name = "stag helmet", chance = 3000 }, -- Not confirmed
	-- { name = "stag legs", chance = 3000 }, -- Not confirmed
	-- { name = "stag plate", chance = 3000 }, -- Not confirmed
	-- { name = "stag robe", chance = 3000 }, -- Not confirmed
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -700 }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 28, type = COMBAT_FIREDAMAGE, minDamage = -700, maxDamage = -1300, range = 7, shootEffect = CONST_ANI_FIRE, effect = CONST_ME_FIREATTACK, target = true }, -- Not confirmed
	{ name = "combat", interval = 2500, chance = 25, type = COMBAT_ENERGYDAMAGE, minDamage = -650, maxDamage = -1200, range = 7, shootEffect = CONST_ANI_ENERGY, effect = CONST_ME_ENERGYHIT, target = true }, -- Not confirmed
	{ name = "combat", interval = 3000, chance = 22, type = COMBAT_DEATHDAMAGE, minDamage = -600, maxDamage = -1100, radius = 5, effect = CONST_ME_MORTAREA, target = false }, -- Not confirmed
	{ name = "combat", interval = 2800, chance = 18, type = COMBAT_FIREDAMAGE, minDamage = -800, maxDamage = -1400, length = 7, spread = 2, effect = CONST_ME_FIREAREA, target = false }, -- Not confirmed
	{ name = "combat", interval = 3500, chance = 15, type = COMBAT_ENERGYDAMAGE, minDamage = -700, maxDamage = -1250, radius = 4, effect = CONST_ME_ENERGYAREA, target = false }, -- Not confirmed
	{ name = "condition", type = CONDITION_FIRE, interval = 4000, chance = 12, minDamage = -200, maxDamage = -350, radius = 4, effect = CONST_ME_HITBYFIRE, target = false }, -- Not confirmed
}

monster.defenses = {
	defense = 75, -- Not confirmed
	armor = 80, -- Not confirmed
	mitigation = 2.40, -- Not confirmed
	{ name = "combat", interval = 3000, chance = 18, type = COMBAT_HEALING, minDamage = 1000, maxDamage = 1800, effect = CONST_ME_MAGIC_BLUE, target = false }, -- Not confirmed
	{ name = "invisible", interval = 6000, chance = 8, effect = CONST_ME_MAGIC_RED }, -- Not confirmed
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
