local mType = Game.createMonsterType("Devoted Radiant Acolyte")
local monster = {}

monster.description = "a devoted radiant acolyte"
monster.experience = 30600
monster.outfit = {
	lookType = 1969,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 3,
}

monster.raceId = 2844
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

monster.health = 39790
monster.maxHealth = 39790
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54419 is newer than this server's items database.
monster.speed = 260
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

monster.loot = {
	-- Summer 2026 loot omitted until these client items exist: book of faith, cloud familiar, lunar ascension orb.
}

-- Wiki lists only one ability: a Hex debuff. There is no "hex" spell name in this engine
-- (monsters.cpp supports: melee combat speed outfit invisible drunk fear firefield
-- poisonfield energyfield condition strength effect), so Hex needs a custom Lua spell.
-- Melee below is UNSOURCED, scaled against Radiant Warden (38500 HP) - verify in game.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -1500, maxDamage = -1950 },
	{ name = "combat", interval = 4000, chance = 10, cooldown = 15000, type = COMBAT_HEALING, minDamage = 1800, maxDamage = 2600, effect = CONST_ME_HOLYAREA, target = false },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 8000, type = COMBAT_HOLYDAMAGE, minDamage = -2800, maxDamage = -3800, range = 7, shootEffect = CONST_ANI_HOLY, effect = CONST_ME_HOLYAREA, target = true },
	{ name = "combat", interval = 3000, chance = 12, cooldown = 9000, type = COMBAT_MANADRAIN, minDamage = -400, maxDamage = -700, radius = 4, effect = CONST_ME_MAGIC_BLUE, target = false },
}

monster.defenses = {
	defense = 105,
	armor = 105,
	mitigation = 5.36,
}

-- Wiki has no resistance data for this creature. Values mirrored from the base
-- Radiant Acolyte, following the pattern the sourced Devoted files already use
-- (Devoted Templar and Devoted Zealot are element-identical to their base creature).
monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 5 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -5 },
	{ type = COMBAT_EARTHDAMAGE, percent = -5 },
	{ type = COMBAT_FIREDAMAGE, percent = 40 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 10 },
	{ type = COMBAT_HOLYDAMAGE, percent = 5 },
	{ type = COMBAT_DEATHDAMAGE, percent = 30 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
