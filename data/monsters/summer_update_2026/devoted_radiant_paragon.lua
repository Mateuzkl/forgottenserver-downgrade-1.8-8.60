local mType = Game.createMonsterType("Devoted Radiant Paragon")
local monster = {}

monster.description = "a devoted radiant paragon"
monster.experience = 32400
monster.outfit = {
	lookType = 1966,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 1,
}

monster.raceId = 2842
monster.Bestiary = {
	class = "Human",
	race = BESTY_RACE_HUMAN,
	toKill = 5000,
	FirstUnlock = 250,
	SecondUnlock = 2500,
	CharmsPoints = 100,
	Stars = 5,
	Occurrence = 0,
	Locations = "Radiant Skyhold.",
}

-- Spawn behaviour (wiki): appears either by using a Gilded Bell in Radiant Skyhold
-- (invasion lasts ~20 seconds), or automatically after ~2000 creatures are killed in
-- the area (invasion lasts ~1 hour). That needs a separate raid/invasion script.
monster.health = 41400
monster.maxHealth = 41400
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54407 is newer than this server's items database.
monster.speed = 220 -- wiki shows 0 (field not filled in); value kept from the original file
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

-- Wiki's own page lists only the Lunar Ascension Orb, but the Summer Update 2026
-- item table credits Cloud Familiar to all six Devoted Radiants, so it is included.
monster.loot = {
	-- Summer 2026 loot omitted until these client items exist: cloud familiar, lunar ascension orb.
}

-- Wiki lists one ability: a Fear debuff. "fear" is native (monsters.cpp:203,
-- creates CONDITION_FEARED). It needs target = true, matching every upstream user.
-- Ranged attacker like the base Radiant Paragon (targetDistance 4) - INFERRED, not sourced.
-- Melee/ranged damage is UNSOURCED, scaled against Radiant Paragon (36000 HP).
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -1350, maxDamage = -1750 },
	{ name = "combat", interval = 2000, chance = 25, cooldown = 4000, type = COMBAT_PHYSICALDAMAGE, minDamage = -1450, maxDamage = -1900, range = 7, shootEffect = CONST_ANI_ARROW, target = true },
	{ name = "fear", interval = 10000, chance = 10, cooldown = 20000, target = true },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 8000, type = COMBAT_HOLYDAMAGE, minDamage = -2800, maxDamage = -3800, range = 7, shootEffect = CONST_ANI_HOLY, effect = CONST_ME_HOLYAREA, target = true },
	{ name = "combat", interval = 3000, chance = 12, cooldown = 9000, type = COMBAT_MANADRAIN, minDamage = -400, maxDamage = -700, radius = 4, effect = CONST_ME_MAGIC_BLUE, target = false },
}

monster.defenses = {
	defense = 103, -- wiki shows 0 (field not filled in); value kept from the original file
	armor = 92, -- wiki shows 0 (field not filled in); value kept from the original file
	mitigation = 4.63, -- wiki has none; mirrored from base Radiant Paragon
}

-- Wiki has no resistance data for this creature. Values mirrored from the base
-- Radiant Paragon, following the pattern the sourced Devoted files already use
-- (Devoted Templar and Devoted Zealot are element-identical to their base creature).
monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 10 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -10 },
	{ type = COMBAT_EARTHDAMAGE, percent = -15 },
	{ type = COMBAT_FIREDAMAGE, percent = 30 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 5 },
	{ type = COMBAT_HOLYDAMAGE, percent = 0 },
	{ type = COMBAT_DEATHDAMAGE, percent = 20 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
