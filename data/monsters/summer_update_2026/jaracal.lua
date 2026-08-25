local mType = Game.createMonsterType("Jaracal")
local monster = {}

monster.description = "a jaracal"
monster.experience = 2460
monster.outfit = {
	lookType = 1961,
	lookHead = 82,
	lookBody = 97,
	lookLegs = 76,
	lookFeet = 76,
	lookAddons = 0,
}

-- TODO: raceId + Bestiary block require the client bestiary race id for 15.30.a30dad.
-- Bestiary data confirmed: class Mammal, Hard, 2500 kills, Common occurrence, 50 charm points.

monster.health = 3200
monster.maxHealth = 3200
monster.race = "blood"
monster.corpse = 0 -- TODO: add corpse item to items.xml
monster.speed = 180
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 10,
}

monster.strategiesTarget = {
	nearest = 70,
	health = 10,
	damage = 10,
	random = 10,
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
	staticAttackChance = 90,
	targetDistance = 1,
	runHealth = 0,
	healthHidden = false,
	isBlockable = false,
	canWalkOnEnergy = true,
	canWalkOnFire = true,
	canWalkOnPoison = true,
}

monster.light = {
	level = 0,
	color = 0,
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "Roarrr!", yell = false },
	{ text = "Rawww!", yell = false },
	{ text = "Grrrrr.", yell = false },
}

monster.loot = {
	{ name = "platinum coin", chance = 42000, minCount = 1, maxCount = 5 },
	{ name = "strong health potion", chance = 9800 },
	{ name = "beastslayer axe", chance = 4200 },
	{ name = "ham", chance = 4200, minCount = 1, maxCount = 2 },
	{ name = "furry club", chance = 4200 },
}

-- Wiki lists Physical + Life Drain abilities; exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -170, maxDamage = -215 },
	{ name = "combat", interval = 2000, chance = 15, cooldown = 4000, type = COMBAT_LIFEDRAIN, minDamage = -120, maxDamage = -180, range = 1, effect = CONST_ME_MAGIC_RED, target = false },
	{ name = "combat", interval = 2000, chance = 12, cooldown = 6000, type = COMBAT_PHYSICALDAMAGE, minDamage = -150, maxDamage = -200, radius = 2, effect = CONST_ME_HITAREA, target = false },
}

monster.defenses = {
	defense = 65,
	armor = 65,
	mitigation = 3.08,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 5 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -5 },
	{ type = COMBAT_EARTHDAMAGE, percent = -5 },
	{ type = COMBAT_FIREDAMAGE, percent = 15 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = -10 },
	{ type = COMBAT_HOLYDAMAGE, percent = 10 },
	{ type = COMBAT_DEATHDAMAGE, percent = 10 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
