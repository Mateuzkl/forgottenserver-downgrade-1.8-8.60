local mType = Game.createMonsterType("Moonspawn Juggernaut")
local monster = {}

monster.description = "a moonspawn juggernaut"
monster.experience = 10000
monster.outfit = {
	lookType = 1972,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

-- Raid creature (Make Believe Quest), Thalassara Surroundings, 30 minute respawn.
-- Not a bestiary creature: no raceId / Bestiary block.

monster.health = 60000
monster.maxHealth = 60000
monster.race = "venom"
monster.corpse = 0 -- Summer 2026 corpse 54660 is newer than this server's items database.
monster.speed = 200
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
	{ text = "<Druuuhhh>", yell = false },
	{ text = "<Gloooosh>", yell = false },
	{ text = "<Krrrshhhh>", yell = false },
}

-- Wiki: no loot recorded for this creature.
monster.loot = {}

-- Wiki lists Physical + Earth abilities; exact damage values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -300, maxDamage = -400 },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 4000, type = COMBAT_EARTHDAMAGE, minDamage = -250, maxDamage = -350, radius = 4, effect = CONST_ME_GREEN_RINGS, target = false },
	{ name = "combat", interval = 3000, chance = 15, cooldown = 7000, type = COMBAT_EARTHDAMAGE, minDamage = -450, maxDamage = -620, length = 6, spread = 3, effect = CONST_ME_GREEN_RINGS, target = false },
}

monster.defenses = {
	defense = 70,
	armor = 70,
	mitigation = 2.50,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 0 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 15 },
	{ type = COMBAT_EARTHDAMAGE, percent = 100 },
	{ type = COMBAT_FIREDAMAGE, percent = -5 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 15 },
	{ type = COMBAT_HOLYDAMAGE, percent = 0 },
	{ type = COMBAT_DEATHDAMAGE, percent = 35 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = true },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
