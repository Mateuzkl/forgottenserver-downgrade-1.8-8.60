-- Echo Raid balance and data. Runtime/lifecycle lives in EchoRaidManager (C++).
-- Occurrence values here use CustomBestiary's normalized representation:
-- 1 = Common, 2 = Uncommon, 3 = Rare, 4 = Very Rare.
EchoConfig = {
	enabled = true,

	portal = {
		itemId = 54133,
		delayMs = 0, -- Test mode: create the portal on the next runtime tick.
		ttlMs = 2 * 60 * 1000,
	},

	eligibility = {
		occurrences = { 1, 2 },
	},

	spawn = {
		numerator = 1, -- Test mode: 1/1 means a 100% spawn chance.
		denominator = 1,
		radius = 3,
		intervalMs = 400, -- Crystal reference timing; configurable, not claimed as an official rate.
	},

	-- Exact Global probabilities are not public. These weights are deliberately
	-- configurable; completed Bestiary doubles only the Warden weight.
	outcomes = {
		normalWeight = 78,
		influencedWeight = 20,
		wardenWeight = 2,
		completedBestiaryWardenMultiplier = 2.0,
	},

	normal = {
		countMin = 5,
		countMax = 8,
	},

	influenced = {
		count = 4,
		levelMin = 1,
		levelMax = 5,
	},

	warden = {
		-- Community references document 3x health. Attack and dodge values are
		-- configurable because no official numerical values are published.
		healthMultiplier = 3.0,
		attackMultiplier = 1.5,
		minionCountMin = 7,
		minionCountMax = 12,
		auraRange = 5,
		auraIntervalMs = 2000,
		auraDodgeChancePercent = 10.0,
	},

	lifetimeMs = 10 * 60 * 1000,

	rewards = {
		-- First Warden per raceId: Harmless through Challenging.
		charmPointsByStars = { [0] = 1, [1] = 2, [2] = 5, [3] = 10, [4] = 15, [5] = 30 },
		basicScrollItemIds = {
			53751, 53752, 53753, 53754, 53755, 53756, 53757, 53758,
			53759, 53760, 53761, 53762, 53763, 53764, 53765, 53766,
			53767, 53768, 53769, 53770, 53771, 53772, 53773, 53774,
		},
		-- Catalyst rarity ratios are configurable because exact Global rates are
		-- not published. Each Warden receives exactly one catalyst roll.
		catalysts = {
			{ itemId = 54266, weight = 80 }, -- Lesser
			{ itemId = 51588, weight = 18 }, -- Proficiency
			{ itemId = 51589, weight = 2 },  -- Greater
		},
	},
}
