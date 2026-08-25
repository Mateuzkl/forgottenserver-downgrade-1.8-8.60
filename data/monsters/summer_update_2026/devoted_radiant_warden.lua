local mType = Game.createMonsterType("Devoted Radiant Warden")
local monster = {}

monster.description = "a devoted radiant warden"
monster.experience = 33000
monster.outfit = {
	lookType = 1964,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 1,
}

monster.raceId = 2840
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

-- Spawn behaviour (wiki): appears either by using a Gilded Bell in the area
-- (invasion lasts ~20 seconds), or automatically after ~2000 creatures are killed in
-- the area (invasion lasts ~1 hour). That needs a separate raid/invasion script.
-- NOTE: the wiki gives Radiant Skyhold as the fixed location but names Radiant
-- Ascendancy in the Gilded Bell note - one of the two is a wiki error.
monster.health = 44275
monster.maxHealth = 44275
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54399 is newer than this server's items database.
monster.speed = 220
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

-- Wiki also lists an Uncommon and a Semi-Rare tier, both still recorded as "?".
monster.loot = {
	-- Summer 2026 loot omitted until these client items exist: cloud familiar, lunar ascension orb.
}

-- Wiki lists one ability: a Root debuff. "root" is NOT a monsters.cpp spell name,
-- but it IS a Lua revscript already present at
-- data-global/scripts/spells/monster/root.lua (CONDITION_ROOTED, 3000 ticks,
-- CONST_ME_ROOTS + CONST_ANI_LEAFSTAR). It needs target = true.
-- No custom spell required - 7 upstream monsters use this exact call.
-- Melee is UNSOURCED, scaled against Radiant Warden (38500 HP) - verify in game.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -1700, maxDamage = -2150 },
	{ name = "root", interval = 9000, cooldown = 9000, chance = 10, target = true },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 8000, type = COMBAT_HOLYDAMAGE, minDamage = -2800, maxDamage = -3800, range = 7, shootEffect = CONST_ANI_HOLY, effect = CONST_ME_HOLYAREA, target = true },
	{ name = "combat", interval = 3000, chance = 12, cooldown = 9000, type = COMBAT_MANADRAIN, minDamage = -400, maxDamage = -700, radius = 4, effect = CONST_ME_MAGIC_BLUE, target = false },
}

monster.defenses = {
	defense = 140,
	armor = 140,
	mitigation = 5.12,
}

-- Wiki has no resistance data for this creature. Values mirrored from the base
-- Radiant Warden, following the pattern the sourced Devoted files already use
-- (Devoted Templar and Devoted Zealot are element-identical to their base creature).
monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 10 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -10 },
	{ type = COMBAT_EARTHDAMAGE, percent = -5 },
	{ type = COMBAT_FIREDAMAGE, percent = 30 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 10 },
	{ type = COMBAT_HOLYDAMAGE, percent = -10 },
	{ type = COMBAT_DEATHDAMAGE, percent = 20 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
