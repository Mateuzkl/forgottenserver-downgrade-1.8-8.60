local mType = Game.createMonsterType("Goldhanded Cultist Bride")
local monster = {}

monster.name = "Goldhanded Cultist Bride"
monster.description = "a goldhanded cultist bride"
monster.experience = 2000
monster.outfit = {
	lookType = 140,
	lookHead = 114,
	lookBody = 79,
	lookLegs = 62,
	lookFeet = 94,
	lookAddons = 2,
	lookMount = 0,
}

monster.raceId = 1482

monster.health = 3000
monster.maxHealth = 3000
monster.race = "blood"
monster.corpse = 4247
monster.speed = 300
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 10,
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = true,
	convinceable = false,
	pushable = false,
	rewardBoss = false,
	illusionable = false,
	canPushItems = true,
	canPushCreatures = false,
	staticAttackChance = 95,
	targetDistance = 1,
	runHealth = 0,
	healthHidden = false,
	ignoreSpawnBlock = false,
	canWalkOnEnergy = true,
	canWalkOnFire = true,
	canWalkOnPoison = false
}

monster.light = {
	level = 0,
	color = 0,
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "Your backpack looks very compelling!", yell = false },
	{ text = "Money, money, money!", yell = false },
	{ text = "You can't ever be rich enough!", yell = false },
}

monster.loot = {
	{ id = 3031, chance = 48680, maxCount = 250 }, -- gold coin
	{ name = "golden brush", chance = 25620 },
	{ name = "tiger eye", chance = 17240 },
	{ name = "opal", chance = 9820 },
	{ name = "yellow gem", chance = 4950 },
	{ id = 2863, chance = 3430 }, -- golden bag
	{ name = "golden backpack", chance = 3190 },
	{ name = "gold ingot", chance = 1360 },
	{ id = 3031, chance = 1200, maxCount = 10 }, -- platinum coin
	{ id = 3040, chance = 80 },
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -380 },
	{ name = "combat", interval = 1000, chance = 15, type = COMBAT_LIFEDRAIN, minDamage = 0, maxDamage = -250, range = 5, radius = 2, effect = CONST_ME_DRAWBLOOD, target = true },
}

monster.defenses = {
	defense = 20,
	armor = 30,
	{ name = "speed", interval = 2000, chance = 30, speed = 290, effect = CONST_ME_MAGIC_RED, target = false, duration = 6000 },
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 0 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 17 },
	{ type = COMBAT_EARTHDAMAGE, percent = 0 },
	{ type = COMBAT_FIREDAMAGE, percent = 10 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 15 },
	{ type = COMBAT_HOLYDAMAGE, percent = 0 },
	{ type = COMBAT_DEATHDAMAGE, percent = 15 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = false },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
