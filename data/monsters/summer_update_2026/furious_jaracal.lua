local mType = Game.createMonsterType("Furious Jaracal")
local monster = {}

monster.description = "a furious jaracal"
monster.experience = 0
monster.outfit = {
	lookType = 1961,
	lookHead = 109, -- VERIFY: violet wings. Winged Jaracal uses 113 (magenta-red) here.
	lookBody = 40,
	lookLegs = 95,
	lookFeet = 95,
	lookAddons = 1,
}

-- Outfit is Winged Jaracal's (lookType 1961) with the wing colour changed to violet.
-- The previous lookType 1967 was wrong - that is Radiant Inquisitor's outfit.
-- lookHead is the best candidate for the wing region; if the outfit tester shows the
-- wings unchanged, move the violet value to lookBody instead.

-- Quest creature (listed under Mamiferos de Quests). Not in the bestiary,
-- so no raceId / Bestiary block.
-- NOTE: the wiki page for this creature could not be read (the wiki database
-- was returning errors). Everything below is derived from the confirmed
-- Jaracal profile plus the original stub - re-verify when the page is back.

monster.health = 10000 -- UNSOURCED; value kept from the original file
monster.maxHealth = 10000
monster.race = "blood"
monster.corpse = 0 -- TODO: add corpse item to items.xml
monster.speed = 200 -- UNSOURCED; value kept from the original file (base Jaracal is 180)
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
	{ text = "Grrrrr.", yell = false },
}

-- Quest creature: no loot recorded.
monster.loot = {}

-- Attack set mirrors the base Jaracal (Physical + Life Drain), scaled up for
-- the quest variant. UNSOURCED - verify in game.
monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = -500, maxDamage = -750 },
	{ name = "combat", interval = 2000, chance = 20, cooldown = 4000, type = COMBAT_LIFEDRAIN, minDamage = -400, maxDamage = -600, range = 1, effect = CONST_ME_MAGIC_RED, target = false },
	{ name = "combat", interval = 2000, chance = 18, cooldown = 5000, type = COMBAT_PHYSICALDAMAGE, minDamage = -450, maxDamage = -650, radius = 2, effect = CONST_ME_HITAREA, target = false },
}

monster.defenses = {
	defense = 25, -- UNSOURCED; value kept from the original file
	armor = 22, -- UNSOURCED; value kept from the original file
}

-- Resistances copied from the confirmed Jaracal bestiary entry.
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
