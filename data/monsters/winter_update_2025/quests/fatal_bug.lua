local mType = Game.createMonsterType("Fatal Bug")
local monster = {}

monster.name = "Fatal Bug"
monster.description = "a fatal bug"
monster.experience = 668000
monster.outfit = {
	lookType = 79,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0,
}

monster.health = 250000
monster.maxHealth = 250000
monster.race = "venom"
monster.corpse = 0
monster.speed = 130 -- Not confirmed
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 10,
}

monster.strategiesTarget = {
	nearest = 70, -- Not confirmed
	health = 10, -- Not confirmed
	damage = 10, -- Not confirmed
	random = 10, -- Not confirmed
}

monster.flags = {
	summonable = false, -- Not confirmed
	attackable = true, -- Not confirmed
	hostile = true, -- Not confirmed
	convinceable = false, -- Not confirmed
	pushable = false,
	rewardBoss = false, -- Not confirmed
	illusionable = false, -- Not confirmed
	canPushItems = true,
	canPushCreatures = true, -- Not confirmed
	staticAttackChance = 90, -- Not confirmed
	targetDistance = 1, -- Not confirmed
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
	{ text = "BZZT", yell = false },
	{ text = "BRRRRMM", yell = false },
}

monster.loot = {
	{ name = "crystal coin", chance = 100000, maxCount = 64 }, -- Not confirmed (chance)
	{ name = "yellow gem", chance = 100000, maxCount = 10 }, -- Not confirmed (chance)
	{ id = 3039, chance = 100000, maxCount = 10 }, -- red gem (não confirmado (chance))
	{ name = "blue gem", chance = 100000, maxCount = 2 }, -- Not confirmed (chance)
	{ name = "giant amethyst", chance = 100000 }, -- Not confirmed (chance)
	{ name = "giant sapphire", chance = 100000, maxCount = 3 }, -- Not confirmed (chance)
	{ name = "strong mana potion", chance = 100000, maxCount = 92 }, -- Not confirmed (chance)
	{ name = "great mana potion", chance = 100000, maxCount = 67 }, -- Not confirmed (chance)
	{ name = "great spirit potion", chance = 100000, maxCount = 56 }, -- Not confirmed (chance)
	{ name = "ultimate mana potion", chance = 100000, maxCount = 25 }, -- Not confirmed (chance)
	{ name = "ultimate spirit potion", chance = 100000, maxCount = 25 }, -- Not confirmed (chance)
	{ name = "ultimate health potion", chance = 100000, maxCount = 35 }, -- Not confirmed (chance)
	{ name = "supreme health potion", chance = 100000, maxCount = 11 }, -- Not confirmed (chance)
	{ name = "guardian gem", chance = 100000 }, -- Not confirmed (chance)
	{ name = "marksman gem", chance = 100000 }, -- Not confirmed (chance)
	{ name = "mystic gem", chance = 100000 }, -- Not confirmed (chance)
	{ name = "sage gem", chance = 100000 }, -- Not confirmed (chance)
	{ name = "spiritualist gem", chance = 100000 }, -- Not confirmed (chance)
	{ name = "greater guardian gem", chance = 100000 }, -- Not confirmed (chance)
	{ name = "greater marksman gem", chance = 100000 }, -- Not confirmed (chance)
	{ name = "greater mystic gem", chance = 100000 }, -- Not confirmed (chance)
	{ name = "greater sage gem", chance = 100000 }, -- Not confirmed (chance)
	{ name = "greater spiritualist gem", chance = 100000 }, -- Not confirmed (chance)
	{ name = "berserk potion", chance = 100000 }, -- Not confirmed (chance)
	{ name = "bullseye potion", chance = 100000 }, -- Not confirmed (chance)
	{ name = "transcendence potion", chance = 100000 }, -- Not confirmed (chance)
	-- { name = "cryptic fossil", chance = 100000 }, -- Not confirmed (chance)
	-- { name = "fetid heart", chance = 100000 }, -- Not confirmed (chance)
	{ name = "wand of everblazing", chance = 100000 }, -- Not confirmed (chance)
	{ name = "composite hornbow", chance = 100000 }, -- Not confirmed (chance)
	{ name = "muck rod", chance = 100000 }, -- Not confirmed (chance)
	-- { name = "worn guide book", chance = 100000 }, -- Not confirmed (chance)
	-- { name = "ancient crypt rune", chance = 100000 }, -- Not confirmed (chance)
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -400 }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 20, type = COMBAT_EARTHDAMAGE, minDamage = -300, maxDamage = -500, range = 5, shootEffect = CONST_ANI_POISON, effect = CONST_ME_POISONAREA, target = true }, -- Not confirmed
	{ name = "combat", interval = 2000, chance = 15, type = COMBAT_EARTHDAMAGE, minDamage = -400, maxDamage = -600, radius = 3, effect = CONST_ME_GREEN_RINGS, target = false }, -- Not confirmed
	{ name = "condition", interval = 2000, chance = 15, type = CONDITION_POISON, totalDamage = 200, effect = CONST_ME_HITBYPOISON, target = true }, -- Not confirmed
}

monster.defenses = {
	defense = 40, -- Not confirmed
	armor = 40, -- Not confirmed
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
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true }, -- Not confirmed
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = true }, -- Not confirmed
}

mType:register(monster)
