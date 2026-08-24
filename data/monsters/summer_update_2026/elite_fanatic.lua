local mType = Game.createMonsterType("Elite Fanatic")
local monster = {}

monster.description = "an elite fanatic"
monster.experience = 0
monster.outfit = {
	lookType = 1972,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
}

-- Quest creature (Make Believe). Not in the bestiary, so no raceId / Bestiary block.
--
-- IMPORTANT: the wiki notes its HP figure is only approximate because at roughly
-- 4000 damage taken it PETRIFIES and becomes a "Petrified Fanatic" instead of dying.
-- That transformation is not expressed here - it needs a creaturescript on health
-- change that swaps this monster for the Petrified Fanatic monster type.
-- Petrified Fanatic is not in the stub folder and has no file yet.

monster.health = 6000 -- wiki calls this approximate; see petrification note above
monster.maxHealth = 6000
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

-- Wiki lists two sounds but the second is unedited template placeholder text
-- ("I'm just dummy text. Edit me!"), so only the real line is used.
monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "For Phosphorus!", yell = false },
}

-- Wiki: no loot recorded for this creature.
monster.loot = {}

-- Wiki lists Physical + Healing. Exact damage/heal values are not published.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -450, maxDamage = -600 },
	{ name = "combat", interval = 2000, chance = 15, cooldown = 8000, type = COMBAT_HEALING, minDamage = 400, maxDamage = 700, effect = CONST_ME_MAGIC_BLUE, target = false },
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
