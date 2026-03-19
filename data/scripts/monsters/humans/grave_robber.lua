local mType = Game.createMonsterType("Grave Robber")
local monster = {}

monster.name = "Grave Robber"
monster.description = "a grave robber"
monster.experience = 65
monster.outfit = {
	lookType = 146,
	lookHead = 57,
	lookBody = 95,
	lookLegs = 57,
	lookFeet = 19,
	lookAddons = 3,
	lookMount = 0,
}

monster.raceId = 867

monster.health = 165
monster.maxHealth = 165
monster.race = "blood"
monster.corpse = 18130
monster.speed = 190
monster.manaCost = 435

monster.changeTarget = {
	interval = 4000,
	chance = 10,
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = true,
	convinceable = true,
	pushable = true,
	rewardBoss = false,
	illusionable = false,
	canPushItems = false,
	canPushCreatures = false,
	staticAttackChance = 90,
	targetDistance = 1,
	runHealth = 15,
	healthHidden = false,
	ignoreSpawnBlock = false,
	canWalkOnEnergy = false,
	canWalkOnFire = false,
	canWalkOnPoison = false
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
	{ name = "gold coin", chance = 54260, maxCount = 38 },
	{ name = "axe", chance = 2500 },
	{ name = "mace", chance = 1700 },
	{ name = "iron helmet", chance = 500 },
	{ name = "brass armor", chance = 1700 },
	{ name = "steel shield", chance = 500 },
	{ name = "nomad parchment", chance = 170 },
	{ name = "potato", chance = 5680, maxCount = 3 },
	{ name = "dirty turban", chance = 1700 },
	{ name = "rope belt", chance = 7510 },
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -90 },
	-- poison
	{ name = "condition", interval = 2000, chance = 15, target = true, condition =
	{ type = CONDITION_POISON, minDamage = -100, maxDamage = -160, range = 7, radius = 1, shootEffect = CONST_ANI_POISON } },
}

monster.defenses = {
	defense = 15,
	armor = 7,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = -10 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 0 },
	{ type = COMBAT_EARTHDAMAGE, percent = 0 },
	{ type = COMBAT_FIREDAMAGE, percent = 20 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = -10 },
	{ type = COMBAT_HOLYDAMAGE, percent = 20 },
	{ type = COMBAT_DEATHDAMAGE, percent = -10 },
}

monster.immunities = {
	{ type = "paralyze", condition = false },
	{ type = "outfit", condition = false },
	{ type = "invisible", condition = false },
	{ type = "bleed", condition = false },
}

mType:register(monster)
