local mType = Game.createMonsterType("Druid Rival")
local monster = {}

monster.description = "a druid rival"
monster.experience = 0
monster.outfit = {
	lookType = 1974,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

-- Quest creature (Make Believe, Pit Fighter arena). Not in the bestiary,
-- so no raceId / Bestiary block. Wiki flags resistance data as approximate.

monster.health = 29800
monster.maxHealth = 29800
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
	{ text = "NATURE NEED LIGHT!", yell = true },
}

-- Wiki: no loot recorded for this creature.
monster.loot = {}

-- Wiki states "Habilidades: Nenhuma" - undocumented rather than passive.
-- Attacks below are UNSOURCED, built to the druid archetype (ice/earth caster,
-- keeps distance, self-heals). Verify in game.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -700, maxDamage = -1000 },
	{ name = "combat", interval = 2000, chance = 22, cooldown = 4000, type = COMBAT_ICEDAMAGE, minDamage = -1000, maxDamage = -1400, radius = 4, effect = CONST_ME_ICEAREA, target = false },
	{ name = "combat", interval = 2000, chance = 18, cooldown = 4000, type = COMBAT_EARTHDAMAGE, minDamage = -900, maxDamage = -1300, range = 7, shootEffect = CONST_ANI_POISON, effect = CONST_ME_GREEN_RINGS, target = true },
	{ name = "combat", interval = 3000, chance = 12, cooldown = 10000, type = COMBAT_HEALING, minDamage = 1500, maxDamage = 2500, effect = CONST_ME_MAGIC_BLUE, target = false },
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
