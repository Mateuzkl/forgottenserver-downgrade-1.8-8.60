local mType = Game.createMonsterType("Paladin Rival")
local monster = {}

monster.description = "a paladin rival"
monster.experience = 0
monster.outfit = {
	lookType = 1977,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

-- Quest creature (Make Believe, Pit Fighter arena). Not in the bestiary,
-- so no raceId / Bestiary block. Wiki flags resistance data as approximate.
-- Added in 15.30.a30dad.

monster.health = 30600 -- wiki value; the original file had a placeholder 10000
monster.maxHealth = 30600
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
	staticAttackChance = 80,
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

-- Wiki: no loot recorded for this creature.
monster.loot = {}

-- Wiki states "Habilidades: Nenhuma", which for a 30600 HP arena duelist is
-- almost certainly undocumented rather than genuinely passive.
-- Attacks below are UNSOURCED, built to the paladin archetype (distance
-- physical shots, holy magic) - verify in game.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -600, maxDamage = -900 },
	{ name = "combat", interval = 2000, chance = 30, type = COMBAT_PHYSICALDAMAGE, minDamage = -1100, maxDamage = -1500, range = 7, shootEffect = CONST_ANI_ROYALSPEAR, target = true },
	{ name = "combat", interval = 2000, chance = 18, cooldown = 4000, type = COMBAT_HOLYDAMAGE, minDamage = -1000, maxDamage = -1400, range = 7, shootEffect = CONST_ANI_HOLY, effect = CONST_ME_HOLYAREA, target = true },
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
