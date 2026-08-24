local mType = Game.createMonsterType("Monk Rival")
local monster = {}

monster.description = "a monk rival"
monster.experience = 0
monster.outfit = {
	lookType = 1976,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

-- Quest creature (Make Believe, Pit Fighter arena). Not in the bestiary,
-- so no raceId / Bestiary block. Wiki flags resistance data as approximate.
-- Added in 15.30.a30dad.

monster.health = 31200
monster.maxHealth = 31200
monster.race = "blood"
monster.corpse = 0 -- TODO: add corpse item to items.xml
monster.speed = 220 -- not published by the wiki; value kept from the original file
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
}

-- Wiki: no loot recorded for this creature.
monster.loot = {}

-- Wiki lists Physical + Earth + Energy abilities with no published damage
-- values (the monk vocation element set). Values below are UNSOURCED, built
-- to the monk archetype (close range brawler, fast strikes) - verify in game.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -1000, maxDamage = -1400 },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 4000, type = COMBAT_EARTHDAMAGE, minDamage = -900, maxDamage = -1300, range = 4, shootEffect = CONST_ANI_EARTH, effect = CONST_ME_GREEN_RINGS, target = true },
	{ name = "combat", interval = 2000, chance = 18, cooldown = 4000, type = COMBAT_ENERGYDAMAGE, minDamage = -1000, maxDamage = -1500, radius = 3, effect = CONST_ME_ENERGYHIT, target = false },
	{ name = "combat", interval = 2000, chance = 25, cooldown = 5000, type = COMBAT_PHYSICALDAMAGE, minDamage = -800, maxDamage = -1200, radius = 2, effect = CONST_ME_HITAREA, target = false },
}

monster.defenses = {
	defense = 78, -- not published by the wiki; value kept from the original file
	armor = 69, -- not published by the wiki; value kept from the original file
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 0 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 0 },
	{ type = COMBAT_EARTHDAMAGE, percent = 0 },
	{ type = COMBAT_FIREDAMAGE, percent = 0 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 0 },
	{ type = COMBAT_HOLYDAMAGE, percent = 0 },
	{ type = COMBAT_DEATHDAMAGE, percent = 0 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
