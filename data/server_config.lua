-- Server-only configuration.
-- MyAAC reads config.lua, so keep advanced Lua values and server-only toggles here.

-- Astra/client protocol
-- NOTE: maxProtocolOutfits only applies to Cip client; OTC will use 255.
maxProtocolOutfits = 255
-- Minimum addons required to grant outfit attributes from data/XML/outfits.xml.
maxAddonAttributes = 3
dllCheckKick = true
dllCheckKickTime = 5
astraClientOnly = true

-- Dual Wielding
-- NOTE: dualWieldingSpeedRate = 200 means dual-wielding attacks twice as fast
-- dualWieldingDamageRate = 60 means each hit deals 60% of normal damage
-- dualWieldingMode = "allweapons" allows any melee weapon to dual-wield
-- dualWieldingMode = "itemxml" requires <attribute key="dualwielding" value="true"/> on weapons
allowDualWielding = false
dualWieldingSpeedRate = 200
dualWieldingDamageRate = 60
dualWieldingMode = "allweapons"

-- Reset System
-- Enable or disable the full reset system.
resetssystem = true

-- Visual display customization
modifyDamageInK = false
modifyExpInK = false
defaultExpColor = "white"
defaultHealthDisplay = "real"

-- Loot Grouping
-- When enabled, loot from multiple kills of the same monster type within 500ms
-- is grouped into a single message: Loot of a (3x) rat: 5 gold coins, 2 cheese.
lootGroupingEnabled = true
-- Raid spawn file generation
-- When a raid in data/raids/raids.xml has spawnFile="file.xml", successful
-- singlespawn/areaspawn monsters are exported to data/raids/file.xml.
-- spawntime value written to each generated <monster> node, in seconds.
-- Monsters created within this radius on the same floor are grouped into the same spawn block using x/y offsets.
-- Direction values: 0 = north, 1 = east, 2 = south, 3 = west.
raidSpawnFileEnabled = true
raidSpawnFileDirectory = "data/raids"
raidSpawnFileSpawntime = 60
raidSpawnFileRadius = 1
raidSpawnFileDirection = 2

-- Power-Law Skill System
-- Replaces exponential formula with slow-growth power-law above thresholds.
-- All skills and magic level share the same growth exponent.
powerlaw = false
powerLawSkillThreshold = 350
powerLawMagicThreshold = 200
powerLawExponent = 0.3
