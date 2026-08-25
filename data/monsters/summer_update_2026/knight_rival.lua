local mType = Game.createMonsterType("Knight Rival")
local monster = {}

monster.description = "a knight rival"
monster.experience = 0
monster.outfit = {
	lookType = 1975,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

-- Quest creature (Make Believe, Pit Fighter arena). Not in the bestiary,
-- so no raceId / Bestiary block. Wiki flags resistance data as approximate.

monster.health = 32500
monster.maxHealth = 32500
monster.race = "blood"
monster.corpse = 0 -- TODO: add corpse item to items.xml
monster.speed = 200 -- not published by the wiki; value kept from the original file
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
	{ text = "FEEL MY STRENGTH!", yell = true },
}

-- Wiki: no loot recorded for this creature.
monster.loot = {}

-- Wiki states "Habilidades: Nenhuma", which for a 32500 HP arena duelist is
-- almost certainly undocumented rather than genuinely passive.
-- Melee below is UNSOURCED (knight-style: high physical, no spells) - verify in game.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -1200, maxDamage = -1600 },
}

monster.defenses = {
	defense = 25, -- not published by the wiki; value kept from the original file
	armor = 22, -- not published by the wiki; value kept from the original file
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
