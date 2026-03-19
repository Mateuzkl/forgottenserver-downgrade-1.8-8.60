local mType = Game.createMonsterType("Sineater Inferniarch")
local monster = {}

monster.name = "Sineater Inferniarch"
monster.description = "a sineater inferniarch"
monster.experience = 7250
monster.outfit = {
	lookType = 1795,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0,
}

monster.raceId = 2602

monster.health = 9150
monster.maxHealth = 9150
monster.race = "undead"
monster.corpse = 50002
monster.speed = 320
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 0,
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
	staticAttackChance = 70,
	targetDistance = 1,
	runHealth = 0,
	healthHidden = false,
	ignoreSpawnBlock = false,
	canWalkOnEnergy = true,
	canWalkOnFire = true,
	canWalkOnPoison = true,
	isPreyable = true
}

monster.light = {
	level = 0,
	color = 0,
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "Bahrrr... Bharush!", yell = true },
	{ text = "Kah ... Thul ... GROAR!", yell = true },
}

monster.loot = {
	{ name = "Platinum Coin", chance = 26500, maxCount = 40 },
	{ name = "Great Mana Potion", chance = 24500, maxCount = 5 },
	{ id = 3030, chance = 24500, maxCount = 5 },
	{ name = "Demonic Core Essence", chance = 1500, maxCount = 1 },
	{ name = "Sineater Wing", chance = 5500, maxCount = 1 },
	{ name = "Wand of Defiance", chance = 12000, maxCount = 1 },
	{ name = "Ruby Necklace", chance = 10000, maxCount = 1 },
	{ name = "Mummified Demon Finger", chance = 1500, maxCount = 1 },
	{ name = "Demonic Matter", chance = 1500, maxCount = 1 },
	{ name = "Wooden Spellbook", chance = 3500, maxCount = 1 },
	{ name = "broodrider inferniarch soul core", chance = 100 },
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 200, maxDamage = -255 },
	{ name = "combat", interval = 2000, chance = 20, type = COMBAT_FIREDAMAGE, minDamage = -350, maxDamage = -415, range = 7, radius = 4, shootEffect = CONST_ANI_FIRE, effect = CONST_ME_FIREAREA, target = true },
	{ name = "combat", interval = 2000, chance = 10, type = COMBAT_DEATHDAMAGE, minDamage = -350, maxDamage = -450, length = 4, spread = 5, effect = CONST_ME_WHITE_ENERGY_SPARK, target = false },
	{ name = "combat", interval = 2700, chance = 37, type = COMBAT_DEATHDAMAGE, minDamage = -350, maxDamage = -405, radius = 1, effect = CONST_ME_BIG_SCRATCH, range = 1, target = true },
	{ name = "firefield", interval = 2000, chance = 15, range = 7, radius = 1, shootEffect = CONST_ANI_FIRE, effect = CONST_ME_FIREAREA, target = true },
}

monster.defenses = {
	defense = 68,
	armor = 68,
	{ name = "combat", interval = 2000, chance = 25, type = COMBAT_HEALING, minDamage = 200, maxDamage = 400, effect = CONST_ME_MAGIC_RED, target = false },
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = -5 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -10 },
	{ type = COMBAT_EARTHDAMAGE, percent = 0 },
	{ type = COMBAT_FIREDAMAGE, percent = 100 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = -5 },
	{ type = COMBAT_HOLYDAMAGE, percent = -10 },
	{ type = COMBAT_DEATHDAMAGE, percent = 10 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
