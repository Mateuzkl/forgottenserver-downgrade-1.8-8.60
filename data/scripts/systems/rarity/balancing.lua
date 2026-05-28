-- Rarity System Balancing
-- All numeric balance values used by the C++ engine at runtime.
-- Values are written into items during rollRarity() via setRarityStat().

rarityBalancing = {
	-- Spell damage scaling formula: (level * levelMul + magicLevel * magicMul) / divisor
	spellScale = {
		level = 2,
		magic = 3,
		divisor = 5,
	},

	-- Base damage ranges per spell type
	spells = {
		onAttackFireStrike    = { dmgMin = 20, dmgMax = 45 },
		onAttackIceStrike     = { dmgMin = 15, dmgMax = 35 },
		onAttackTerraStrike   = { dmgMin = 15, dmgMax = 35 },
		onAttackDeathStrike   = { dmgMin = 15, dmgMax = 40 },
		onAttackEnergyStrike  = { dmgMin = 20, dmgMax = 50 },
		onAttackDivineMissile = { dmgMin = 20, dmgMax = 40 },

		onHitFireStrike       = { dmgMin = 20, dmgMax = 45 },
		onHitIceStrike        = { dmgMin = 15, dmgMax = 35 },
		onHitTerraStrike      = { dmgMin = 15, dmgMax = 35 },
		onHitDeathStrike      = { dmgMin = 15, dmgMax = 40 },
		onHitEnergyStrike     = { dmgMin = 20, dmgMax = 50 },
		onHitDivineMissile    = { dmgMin = 20, dmgMax = 40 },
	},

	-- On-kill effect values
	onKill = {
		buffDuration   = 30000,   -- ms (30 seconds)
		critChance     = 1000,    -- 10% (base 10000)
		critAmount     = 5000,    -- +50% crit damage
		maxHpPercent   = 5,       -- % of max HP
		maxMpPercent   = 5,       -- % of max MP
	},
}
