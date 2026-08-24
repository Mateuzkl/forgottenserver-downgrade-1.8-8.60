local mType = Game.createMonsterType("Radiant Inquisitor")
local monster = {}

monster.description = "a radiant inquisitor"
monster.experience = 25200
monster.outfit = {
	lookType = 1967,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

monster.raceId = 2847
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

monster.health = 34800
monster.maxHealth = 34800
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
	{ name = "blue quiver", chance = 4200 },
	-- Summer 2026 loot omitted until these client items exist: golden belt, gilded bell.
}

-- Wiki states "Habilidades: Nenhuma" for this creature, which is almost certainly
-- undocumented data rather than a genuinely passive bestiary monster at 34800 HP.
-- Melee only, scaled against Radiant Acolyte (34600 HP). UNSOURCED - verify in game.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -1400, maxDamage = -1800 },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 5000, type = COMBAT_PHYSICALDAMAGE, minDamage = -1400, maxDamage = -2000, range = 7, radius = 3, shootEffect = CONST_ANI_ARROW, effect = CONST_ME_HITAREA, target = true },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 7000, type = COMBAT_HOLYDAMAGE, minDamage = -2200, maxDamage = -3000, length = 7, spread = 0, effect = CONST_ME_HOLYAREA, target = false },
}

monster.defenses = {
	defense = 110,
	armor = 110,
	mitigation = 4.63,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = -10 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 20 },
	{ type = COMBAT_EARTHDAMAGE, percent = 5 },
	{ type = COMBAT_FIREDAMAGE, percent = -5 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = -10 },
	{ type = COMBAT_HOLYDAMAGE, percent = 25 },
	{ type = COMBAT_DEATHDAMAGE, percent = -5 },
}

-- Wiki lists Paralysis only under Imunidades - this creature is NOT immune to invisibility,
-- unlike every other Radiant. Sense-invisible is off here.
monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = false },
	{ type = "bleed", condition = false },
}

mType:register(monster)
