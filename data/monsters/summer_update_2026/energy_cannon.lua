local mType = Game.createMonsterType("Energy Cannon")
local monster = {}

monster.description = "an energy cannon"
monster.experience = 0
monster.outfit = {
	lookTypeEx = 53678,
}

-- Quest construct. Not in the bestiary, so no raceId / Bestiary block.
-- NOTE: no wiki page could be read for this creature (the wiki database was
-- returning errors and it is not listed in any creature-class navbox reached
-- so far). Everything below is UNSOURCED - re-verify before going live.

monster.health = 10000 -- UNSOURCED; value kept from the original file
monster.maxHealth = 10000
monster.race = "energy" -- assumed: a cannon should not splash blood
monster.corpse = 0 -- TODO: add corpse item to items.xml
monster.speed = 0 -- assumed stationary emplacement; original file had 200
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
	canPushItems = false,
	canPushCreatures = false,
	staticAttackChance = 90,
	targetDistance = 4,
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

-- Quest creature: no loot recorded.
monster.loot = {}

-- UNSOURCED. Built to the name: a ranged energy emplacement.
monster.attacks = {
	{ name = "combat", interval = 2000, chance = 30, type = COMBAT_ENERGYDAMAGE, minDamage = -600, maxDamage = -900, range = 7, shootEffect = CONST_ANI_ENERGY, effect = CONST_ME_ENERGYHIT, target = true },
	{ name = "combat", interval = 3000, chance = 20, cooldown = 6000, type = COMBAT_ENERGYDAMAGE, minDamage = -700, maxDamage = -1000, length = 7, spread = 0, effect = CONST_ME_ENERGYHIT, target = false },
}

monster.defenses = {
	defense = 25, -- UNSOURCED; value kept from the original file
	armor = 22, -- UNSOURCED; value kept from the original file
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
	{ type = "bleed", condition = true },
}

mType:register(monster)
