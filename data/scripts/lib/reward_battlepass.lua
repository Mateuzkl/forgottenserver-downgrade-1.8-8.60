-- Battle Pass season data.
--
-- This is the only file that should need changing when a new season starts:
-- 1. Change season.id (and rewards/missions as desired).
-- 2. Reload the server scripts or restart the server.
-- 3. Use /battlepass newseason as a GM.
--
-- All reward item ids below exist in this data pack. Replace them with the
-- season's final items before opening the season to players.

BattlePassConfig = {
	requirements = {
		minimumLevel = 8,
		requireVocation = true,
	},

	season = {
		id = "season-02",
		-- Leave startsAt at 0 to start when /battlepass newseason is used.
		-- A Unix timestamp can be used to schedule a fixed start.
		startsAt = 0,
		durationDays = 35,
		resetHour = 10,
		-- The existing Season 2 mission pool can award 6,775 points. Eighty
		-- points per step keeps the 80-level track attainable without changing
		-- the published mission values.
		maxStep = 80,
		pointsPerStep = 80,
	},

	deluxe = {
		price = 250,
	},

	reroll = {
		goldPerLevel = 800,
	},

	-- The shop unlocks when the player completes level 80. From then until the
	-- season ends, completed daily missions award their normal point value as
	-- shop points. `shopPoints` can be added to an individual daily mission to
	-- override that amount. All catalog data is seasonal and lives here.
	shop = {
		items = {
			{ id = 1, type = "mount", title = "Black Sheep Mount", description = "A dark and dependable companion.", price = 250, mountId = 4, looktype = 371 },
			{ id = 2, type = "mount", title = "Crystal Wolf Mount", description = "A fierce crystalline wolf mount.", price = 900, mountId = 16, looktype = 390 },
			{ id = 3, type = "mount", title = "Dragonling Mount", description = "Ride a young dragonling into battle.", price = 1500, mountId = 31, looktype = 506 },
			{ id = 4, type = "item", title = "Stamina Extension", description = "One stamina extension delivered to your Store Inbox.", price = 350, itemId = 36725, count = 1, repeatable = true },
			{ id = 5, type = "item", title = "Training Dummy", description = "A training dummy delivered to your Store Inbox.", price = 700, itemId = 28558, count = 1, repeatable = true },
			{ id = 6, type = "item", title = "Durable Exercise Weapon", description = "A durable exercise weapon for your training.", price = 900, itemId = 35279, count = 1, repeatable = true },
			{ id = 7, type = "prey", title = "Prey Wildcards", description = "Receive two Prey wildcards.", price = 200, count = 2, repeatable = true },
			{ id = 8, type = "charms", title = "Charm Points", description = "Receive 50 charm points.", price = 600, count = 50, repeatable = true },
			{ id = 9, type = "outfit", title = "Dragon Slayer Outfit", description = "Unlock the Dragon Slayer outfit with the first addon.", price = 1800, male = { { looktype = 1289, name = "Dragon Slayer" } }, female = { { looktype = 1289, name = "Dragon Slayer" } }, addons = 1 },
		},
	},

	dailyMissions = {
		free = {
			{ id = "daily_rotworm", name = "Rotworm Cleanup", description = "Kill 40 rotworms or carrion worms.", maxProgress = 40, points = 25, targets = { "rotworm", "carrion worm" } },
			{ id = "daily_troll", name = "Troll Patrol", description = "Kill 40 trolls.", maxProgress = 40, points = 25, targets = { "troll", "swamp troll", "frost troll" } },
			{ id = "daily_orc", name = "Orc Skirmish", description = "Kill 40 orcs.", maxProgress = 40, points = 25, targets = { "orc", "orc spearman", "orc warrior", "orc berserker", "orc leader" } },
			{ id = "daily_cyclops", name = "One-Eyed Trouble", description = "Kill 20 cyclops.", maxProgress = 20, points = 25, targets = { "cyclops", "cyclops smith", "cyclops drone" } },
			{ id = "daily_dragon", name = "Dragon Pressure", description = "Kill 10 dragons or dragon lords.", maxProgress = 10, points = 25, targets = { "dragon", "dragon lord" } },
		},
		deluxe = {
			{ id = "daily_deluxe_any", name = "Daily Hunter", description = "Kill 80 creatures.", maxProgress = 80, points = 40, targets = "*" },
			{ id = "daily_deluxe_minotaur", name = "Minotaur Sweep", description = "Kill 60 minotaurs.", maxProgress = 60, points = 40, targets = { "minotaur", "minotaur archer", "minotaur guard", "minotaur mage" } },
			{ id = "daily_deluxe_undead", name = "Restless Dead", description = "Kill 50 undead creatures.", maxProgress = 50, points = 40, targets = { "skeleton", "ghoul", "crypt shambler", "mummy", "vampire" } },
			{ id = "daily_deluxe_dragon", name = "Dragon Daily", description = "Kill 25 dragons.", maxProgress = 25, points = 40, targets = { "dragon", "dragon lord", "wyrm" } },
			{ id = "daily_deluxe_demon", name = "Demonic Daily", description = "Kill 10 demons.", maxProgress = 10, points = 40, targets = { "demon", "demon outcast" } },
		},
	},

	-- General missions use unlockWeek 1..5, matching the Season 2 timeline.
	generalMissions = {
		{ id = "bronze_any_150", tier = "bronze", unlockWeek = 1, name = "Fresh Start", description = "Kill 150 creatures during the season.", maxProgress = 150, points = 100, targets = "*" },
		{ id = "bronze_rotworm_120", tier = "bronze", unlockWeek = 1, name = "Tunnel Sweep", description = "Kill 120 rotworms.", maxProgress = 120, points = 100, targets = { "rotworm", "carrion worm" } },
		{ id = "silver_cyclops_100", tier = "silver", unlockWeek = 1, name = "Cyclops Camp", description = "Kill 100 cyclops.", maxProgress = 100, points = 200, targets = { "cyclops", "cyclops smith", "cyclops drone" } },
		{ id = "silver_dragon_80", tier = "silver", unlockWeek = 1, name = "Dragon Hunter", description = "Kill 80 dragons.", maxProgress = 80, points = 200, targets = { "dragon" } },

		{ id = "bronze_troll_120", tier = "bronze", unlockWeek = 2, name = "Troll Breaker", description = "Kill 120 trolls.", maxProgress = 120, points = 100, targets = { "troll", "swamp troll", "frost troll" } },
		{ id = "bronze_goblin_120", tier = "bronze", unlockWeek = 2, name = "Goblin Control", description = "Kill 120 goblins.", maxProgress = 120, points = 100, targets = { "goblin", "goblin assassin", "goblin leader", "goblin scavenger" } },
		{ id = "silver_giant_spider_40", tier = "silver", unlockWeek = 2, name = "Web Cleaner", description = "Kill 40 giant spiders.", maxProgress = 40, points = 200, targets = { "giant spider" } },
		{ id = "silver_vampire_60", tier = "silver", unlockWeek = 2, name = "Night Watch", description = "Kill 60 vampires.", maxProgress = 60, points = 200, targets = { "vampire", "vampire bride", "vampire viscount" } },

		{ id = "bronze_minotaur_120", tier = "bronze", unlockWeek = 3, name = "Maze Breaker", description = "Kill 120 minotaurs.", maxProgress = 120, points = 100, targets = { "minotaur", "minotaur archer", "minotaur guard", "minotaur mage" } },
		{ id = "bronze_orc_150", tier = "bronze", unlockWeek = 3, name = "Orc Campaign", description = "Kill 150 orcs.", maxProgress = 150, points = 100, targets = { "orc", "orc spearman", "orc warrior", "orc berserker", "orc leader", "orc warlord" } },
		{ id = "bronze_dwarf_120", tier = "bronze", unlockWeek = 3, name = "Dwarf Advance", description = "Kill 120 dwarves.", maxProgress = 120, points = 100, targets = { "dwarf", "dwarf soldier", "dwarf guard", "dwarf geomancer" } },
		{ id = "silver_necromancer_60", tier = "silver", unlockWeek = 3, name = "Necromancer Hunt", description = "Kill 60 necromancers or priests.", maxProgress = 60, points = 200, targets = { "necromancer", "priestess", "blood priest" } },
		{ id = "silver_hero_50", tier = "silver", unlockWeek = 3, name = "Hero Trial", description = "Kill 50 heroes or black knights.", maxProgress = 50, points = 200, targets = { "hero", "black knight" } },
		{ id = "gold_demon_25", tier = "gold", unlockWeek = 3, name = "Demon Contract", description = "Kill 25 demons.", maxProgress = 25, points = 300, targets = { "demon" } },

		{ id = "bronze_amazon_100", tier = "bronze", unlockWeek = 4, name = "Amazon Trail", description = "Kill 100 amazons or valkyries.", maxProgress = 100, points = 100, targets = { "amazon", "valkyrie" } },
		{ id = "bronze_undead_150", tier = "bronze", unlockWeek = 4, name = "Undead Campaign", description = "Kill 150 undead creatures.", maxProgress = 150, points = 100, targets = { "skeleton", "ghoul", "crypt shambler", "mummy", "vampire", "lich" } },
		{ id = "silver_beholder_80", tier = "silver", unlockWeek = 4, name = "Evil Eyes", description = "Kill 80 beholders.", maxProgress = 80, points = 200, targets = { "beholder", "elder beholder", "bonelord", "elder bonelord" } },
		{ id = "silver_dragon_lord_40", tier = "silver", unlockWeek = 4, name = "Dragon Lord Hunt", description = "Kill 40 dragon lords.", maxProgress = 40, points = 200, targets = { "dragon lord" } },
		{ id = "silver_hydra_30", tier = "silver", unlockWeek = 4, name = "Hydra Heads", description = "Kill 30 hydras.", maxProgress = 30, points = 200, targets = { "hydra" } },
		{ id = "gold_dragon_family_200", tier = "gold", unlockWeek = 4, name = "Wyrm Scale", description = "Kill 200 dragons, dragon lords or wyrms.", maxProgress = 200, points = 300, targets = { "dragon", "dragon lord", "wyrm" } },

		{ id = "bronze_larva_120", tier = "bronze", unlockWeek = 5, name = "Desert Nest", description = "Kill 120 larvas or scarabs.", maxProgress = 120, points = 100, targets = { "larva", "scarab", "ancient scarab" } },
		{ id = "bronze_slime_80", tier = "bronze", unlockWeek = 5, name = "Slime Splitter", description = "Kill 80 slimes.", maxProgress = 80, points = 100, targets = { "slime" } },
		{ id = "silver_serpent_30", tier = "silver", unlockWeek = 5, name = "Serpent Strike", description = "Kill 30 serpent spawns.", maxProgress = 30, points = 200, targets = { "serpent spawn" } },
		{ id = "silver_any_350", tier = "silver", unlockWeek = 5, name = "Battle Routine", description = "Kill 350 creatures during the season.", maxProgress = 350, points = 200, targets = "*" },
		{ id = "gold_strong_120", tier = "gold", unlockWeek = 5, name = "Stronghold Breaker", description = "Kill 120 strong creatures.", maxProgress = 120, points = 300, targets = { "warlock", "demon", "hydra", "serpent spawn", "frost dragon", "behemoth" } },
		{ id = "gold_any_800", tier = "gold", unlockWeek = 5, name = "Season Veteran", description = "Kill 800 creatures during the season.", maxProgress = 800, points = 300, targets = "*" },
	},

	-- Supported reward types: item, randomItem, randomMount, exercise,
	-- doubleSkill, level, prey, xpBoost, regeneration, charms, outfit,
	-- extraSkill, multiItem and choiceItem. All item rewards go to the server's
	-- persistent Store Inbox, which is used as the Battle Pass reward inbox.
	rewards = {},
}

local reward = BattlePassConfig.rewards
local exerciseWeapons = { 28552, 28553, 28554, 28555, 28556, 28557, 50293 }
local durableWeapons = { 35279, 35280, 35281, 35282, 35283, 35284, 50294 }
local lastingWeapons = { 35285, 35286, 35287, 35288, 35289, 35290, 50295 }

local function item(itemId, count, charges)
	return { type = "item", itemId = itemId, count = count or 1, charges = charges or 0 }
end

local function randomItem(items, count)
	return { type = "randomItem", items = items, count = count or 1 }
end

local function exercise(items, charges, boosted)
	return { type = boosted and "boostedExercise" or "exercise", choices = items, count = 1, charges = charges }
end

-- Free rewards follow the published Season 2 cadence. Deluxe has one reward
-- per step. These defaults are intentionally made from server-valid ids, so
-- the season is playable immediately and can be customised safely here.
reward[1] = { deluxe = { type = "outfit", male = { { looktype = 128, name = "Citizen" } }, female = { { looktype = 136, name = "Citizen" } }, addons = 1 } }
reward[2] = { deluxe = { type = "charms", count = 50 } }
reward[3] = { free = item(36725), deluxe = { type = "randomMount", mounts = { { id = 1, looktype = 368, name = "Widow Queen" }, { id = 2, looktype = 369, name = "Racing Bird" }, { id = 3, looktype = 370, name = "War Bear" } } } }
reward[4] = { deluxe = { type = "prey", count = 4 } }
reward[5] = { deluxe = exercise(durableWeapons, 3600, true) }
reward[6] = { free = { type = "randomMount", mounts = { { id = 4, looktype = 371, name = "Black Sheep" }, { id = 5, looktype = 372, name = "Midnight Panther" }, { id = 6, looktype = 373, name = "Draptor" } } }, deluxe = item(36725) }
reward[7] = { deluxe = { type = "regeneration", durationHours = 6 } }
reward[8] = { deluxe = { type = "prey", count = 3 } }
reward[9] = { free = exercise(exerciseWeapons, 3600), deluxe = { type = "doubleSkill", durationHours = 6 } }
reward[10] = { deluxe = { type = "outfit", male = { { looktype = 130, name = "Mage" } }, female = { { looktype = 138, name = "Mage" } }, addons = 2 } }
reward[11] = { deluxe = exercise(exerciseWeapons, 3600) }
reward[12] = { free = { type = "doubleSkill", durationHours = 6 }, deluxe = { type = "prey", count = 4 } }
reward[13] = { deluxe = { type = "charms", count = 50 } }
reward[14] = { deluxe = randomItem({ 3043, 2160 }, 1) }
reward[15] = { free = { type = "level", count = 1 }, deluxe = { type = "outfit", male = { { looktype = 130, name = "Mage" } }, female = { { looktype = 138, name = "Mage" } }, addons = 1 } }
reward[16] = { deluxe = { type = "randomMount", mounts = { { id = 16, looktype = 390, name = "Crystal Wolf" }, { id = 17, looktype = 392, name = "War Horse" }, { id = 18, looktype = 401, name = "Kingly Deer" } } } }
reward[17] = { deluxe = { type = "level", count = 1 } }
reward[18] = { free = { type = "prey", count = 3 }, deluxe = { type = "regeneration", durationHours = 3 } }
reward[19] = { deluxe = { type = "charms", count = 100 } }
reward[20] = { deluxe = item(36725) }
reward[21] = { free = randomItem({ 3043, 2160 }, 2), deluxe = item(36725) }
reward[22] = { deluxe = randomItem({ 3043, 2160 }, 1) }
reward[23] = { deluxe = { type = "multiItem", items = { { itemId = 3043, count = 5 }, { itemId = 2160, count = 2 } } } }
reward[24] = { free = randomItem({ 3043, 2160 }, 1), deluxe = { type = "prey", count = 3 } }
reward[25] = { deluxe = { type = "outfit", male = { { looktype = 130, name = "Mage" } }, female = { { looktype = 138, name = "Mage" } }, addons = 2 } }
reward[26] = { deluxe = { type = "multiItem", items = { { itemId = 28558, count = 1 }, { itemId = 3043, count = 3 } } } }
reward[27] = { free = { type = "xpBoost", durationHours = 2, percent = 50 }, deluxe = { type = "xpBoost", durationHours = 2, percent = 50 } }
reward[28] = { deluxe = exercise(durableWeapons, 3600, true) }
reward[29] = { deluxe = { type = "regeneration", durationHours = 18 } }
reward[30] = { free = { type = "regeneration", durationHours = 12 }, deluxe = { type = "choiceItem", choices = exerciseWeapons, count = 1, charges = 3600 } }
reward[31] = { deluxe = item(36725) }
reward[32] = { deluxe = exercise(durableWeapons, 3600, true) }
reward[33] = { free = exercise(lastingWeapons, 5400), deluxe = item(28558) }
reward[34] = { deluxe = randomItem({ 3043, 2160 }, 1) }
reward[35] = { deluxe = { type = "randomMount", mounts = { { id = 29, looktype = 502, name = "Ironblight" }, { id = 30, looktype = 503, name = "Magma Crawler" }, { id = 31, looktype = 506, name = "Dragonling" } } } }
reward[36] = { free = { type = "doubleSkill", durationHours = 2 }, deluxe = exercise(lastingWeapons, 5400) }
reward[37] = { deluxe = item(36726) }
reward[38] = { deluxe = item(28558) }
reward[39] = { free = item(28558), deluxe = { type = "prey", count = 5 } }
reward[40] = { deluxe = { type = "multiItem", items = { { itemId = 36725, count = 1 }, { itemId = 36726, count = 1 } } } }
reward[41] = { deluxe = { type = "extraSkill", count = 10, durationHours = 3 } }
reward[42] = { free = item(36726), deluxe = { type = "regeneration", durationHours = 6 } }
reward[43] = { deluxe = { type = "prey", count = 3 } }
reward[44] = { deluxe = { type = "xpBoost", durationHours = 3, percent = 50 } }
reward[45] = { free = { type = "prey", count = 3 }, deluxe = item(36725) }
reward[46] = { deluxe = randomItem({ 3043, 2160, 36725 }, 1) }
reward[47] = { deluxe = item(36726) }
reward[48] = { free = { type = "doubleSkill", durationHours = 12 }, deluxe = { type = "doubleSkill", durationHours = 12 } }
reward[49] = { free = { type = "xpBoost", durationHours = 2, percent = 50 }, deluxe = item(36725) }
reward[50] = { free = { type = "level", count = 2 }, deluxe = { type = "outfit", male = { { looktype = 1289, name = "Dragon Slayer" } }, female = { { looktype = 1289, name = "Dragon Slayer" } }, addons = 3 } }

-- Levels 51-80 keep the same supported reward types and only use ids that
-- exist in this data pack. Change this table freely for a later season.
local extendedRewards = {
	{ free = item(3043, 5), deluxe = item(36725) },
	{ deluxe = { type = "prey", count = 3 } },
	{ free = exercise(exerciseWeapons, 3600), deluxe = exercise(durableWeapons, 3600, true) },
	{ deluxe = { type = "charms", count = 50 } },
	{ free = { type = "doubleSkill", durationHours = 3 }, deluxe = { type = "regeneration", durationHours = 6 } },
	{ deluxe = item(28558) },
	{ free = randomItem({ 3043, 2160 }, 2), deluxe = { type = "xpBoost", durationHours = 2, percent = 50 } },
	{ deluxe = { type = "randomMount", mounts = { { id = 4, looktype = 371, name = "Black Sheep" }, { id = 16, looktype = 390, name = "Crystal Wolf" }, { id = 31, looktype = 506, name = "Dragonling" } } } },
	{ free = { type = "prey", count = 2 }, deluxe = item(36726) },
	{ deluxe = { type = "multiItem", items = { { itemId = 36725, count = 1 }, { itemId = 3043, count = 3 } } } },
}

for step = 51, 80 do
	reward[step] = extendedRewards[((step - 51) % #extendedRewards) + 1]
end

reward[80] = {
	free = { type = "level", count = 2 },
	deluxe = { type = "charms", count = 250 },
}
