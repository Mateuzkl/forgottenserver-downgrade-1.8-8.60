local mType = Game.createMonsterType("Devoted Radiant Inquisitor")
local monster = {}

monster.description = "a devoted radiant inquisitor"
monster.experience = 32700
monster.outfit = {
	lookType = 1967,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 3,
}

monster.raceId = 2848
monster.Bestiary = {
	class = "Human",
	race = BESTY_RACE_HUMAN,
	toKill = 5000,
	FirstUnlock = 250,
	SecondUnlock = 2500,
	CharmsPoints = 100,
	Stars = 5,
	Occurrence = 1,
	Locations = "Radiant Ascendancy.",
}

-- Spawn behaviour (wiki): appears either by using a Gilded Bell (invasion lasts
-- ~20 seconds), or automatically after ~2000 creatures are killed in the area
-- (invasion lasts ~1 hour). That needs a separate raid/invasion script.
-- NOTE: wiki gives Radiant Ascendancy as the fixed location but names Radiant Skyhold
-- in the Gilded Bell note - same contradiction as devoted_radiant_warden.lua.
monster.health = 40020
monster.maxHealth = 40020
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54411 is newer than this server's items database.
monster.speed = 215
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
	-- Summer 2026 loot omitted until the golden belt client item exists.
	{ name = "blue quiver", chance = 4200 },
	-- Summer 2026 loot omitted until these client items exist: cloud familiar, lunar ascension orb.
}

-- Wiki lists three debuffs and no damage spells: Hex, Fear, Root.
-- "fear" is native (monsters.cpp:203, creates CONDITION_FEARED, duration defaults to 6000).
-- "root" is NOT a monsters.cpp name - it is a Lua revscript already present at
-- data-global/scripts/spells/monster/root.lua (CONDITION_ROOTED, 3000 ticks).
-- Both need target = true, matching every upstream user.
-- Hex has no engine name and no upstream script - still needs a custom Lua spell.
-- Melee is UNSOURCED, scaled against Radiant Inquisitor (34800 HP) - verify in game.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -1550, maxDamage = -2000 },
	{ name = "fear", interval = 10000, chance = 10, cooldown = 20000, target = true },
	{ name = "root", interval = 9000, cooldown = 9000, chance = 10, target = true },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 8000, type = COMBAT_HOLYDAMAGE, minDamage = -2800, maxDamage = -3800, range = 7, shootEffect = CONST_ANI_HOLY, effect = CONST_ME_HOLYAREA, target = true },
	{ name = "combat", interval = 3000, chance = 12, cooldown = 9000, type = COMBAT_MANADRAIN, minDamage = -400, maxDamage = -700, radius = 4, effect = CONST_ME_MAGIC_BLUE, target = false },
}

monster.defenses = {
	defense = 120,
	armor = 120,
	mitigation = 6.10,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = -10 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 25 },
	{ type = COMBAT_EARTHDAMAGE, percent = 10 },
	{ type = COMBAT_FIREDAMAGE, percent = -5 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = -5 },
	{ type = COMBAT_HOLYDAMAGE, percent = 25 },
	{ type = COMBAT_DEATHDAMAGE, percent = -10 },
}

-- Unlike the base Radiant Inquisitor, this one IS immune to invisibility.
monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
