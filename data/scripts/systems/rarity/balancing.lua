-- Rarity System Balancing
-- All numeric balance values used by the C++ engine at runtime.
-- Values are written into items during rollRarity() via item:setRarityStat().
--
-- === CONSUMED BY C++ ===
-- spellScale.*  → combat.cpp getRaritySpellDamage():
--                  damage = (playerLevel * level + magicLevel * magic) / divisor + baseDmg
-- spells.*      → combat.cpp: dmgMin/dmgMax written to item, read by C++ as base damage roll
-- onKill.*      → game.cpp  processRarityOnKill():
--                  buffDuration, critChance, critAmount, maxHpPercent, maxMpPercent
--                  These are default values; items can override them via setRarityStat()

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

-- =============================================================================
-- TYPE REFERENCE
-- =============================================================================
-- All numeric values support floats (double). Pipeline: balancing → core.lua
-- (rollRarity) → item:setRarityStat() → C++ getRarityStat().
--
-- spellScale:
--   level   : number — multiplier for player level
--   magic   : number — multiplier for player magic level
--   divisor : number — result divisor (> 0)
--   Formula: damage = (playerLevel*level + magicLevel*magic) / divisor + baseDmg
--   C++:    combat.cpp getRaritySpellDamage()
--
-- spells.*:
--   dmgMin : number — minimum base damage (before scaling)
--   dmgMax : number — maximum base damage (before scaling)
--   Written to item as: statKey.."DmgMin" / statKey.."DmgMax"
--
-- onKill.*:
--   buffDuration   : number — ms (30000 = 30s)
--   critChance     : number — crit % on TFS SPECIALSKILL 10000-scale (1000 = 10%)
--   critAmount     : number — bonus crit damage on 10000-scale (5000 = +50%)
--   maxHpPercent   : number — % of max HP bonus
--   maxMpPercent   : number — % of max MP bonus
--   Written as defaults to items during rollRarity(); items can override.
--
-- Examples:
--   spellScale = { level = 2.5, magic = 3.0, divisor = 5.0 }
--   onKill = { buffDuration = 30000, critChance = 1000.5, critAmount = 5000 }
