local mType = Game.createMonsterType("Winged Jaracal")
local monster = {}

monster.description = "a winged jaracal"
monster.experience = 7830
monster.outfit = {
	lookType = 1961,
	lookHead = 113,
	lookBody = 40,
	lookLegs = 95,
	lookFeet = 95,
	lookAddons = 1,
}

monster.raceId = 2802
monster.Bestiary = {
	class = "Mammal",
	race = BESTY_RACE_MAMMAL,
	toKill = 2500,
	FirstUnlock = 100,
	SecondUnlock = 1000,
	CharmsPoints = 50,
	Stars = 4,
	Occurrence = 0,
	Locations = "Asura Citadel.",
}
monster.health = 8200
monster.maxHealth = 8200
monster.race = "blood"
monster.corpse = 0 -- Summer 2026 corpse 54647 is newer than this server's items database.
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
	{ text = "Grrrr!", yell = false },
}

monster.loot = {
	{ name = "platinum coin", chance = 42000, minCount = 1, maxCount = 8 },
	{ name = "strong health potion", chance = 9800 },
	{ name = "crystal coin", chance = 9800 },
	{ name = "furry club", chance = 9800 },
	{ id = 3049, chance = 4200 }, -- stealth ring (name is non-unique: 3049 inactive lootable / 3086 active invisible copy)
	{ name = "beastslayer axe", chance = 4200 },
	{ name = "dark shield", chance = 4200 },
	{ name = "ham", chance = 4200, minCount = 1, maxCount = 2 },
	-- Summer 2026 loot omitted until the catnip client item exists.
	{ name = "fur armor", chance = 850 },
	{ name = "skullcracker armor", chance = 210 },
}

-- Wiki lists Physical + Death abilities; exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -380, maxDamage = -470 },
	{ name = "combat", interval = 2000, chance = 18, cooldown = 4000, type = COMBAT_DEATHDAMAGE, minDamage = -300, maxDamage = -400, radius = 3, effect = CONST_ME_MORTAREA, target = false },
	{ name = "combat", interval = 2000, chance = 15, cooldown = 6000, type = COMBAT_PHYSICALDAMAGE, minDamage = -340, maxDamage = -430, radius = 2, effect = CONST_ME_HITAREA, target = false },
}

monster.defenses = {
	defense = 75,
	armor = 75,
	mitigation = 3.92,
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
