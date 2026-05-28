-- Rarity System Initialization
-- Entry point loaded at server startup.

if not RARITY_SYSTEM_ENABLED then
	return
end

dofile("data/scripts/systems/rarity/config.lua")
dofile("data/scripts/systems/rarity/balancing.lua")
dofile("data/scripts/systems/rarity/helpers.lua")
dofile("data/scripts/systems/rarity/core.lua")
dofile("data/scripts/systems/rarity/combat.lua")
dofile("data/scripts/systems/rarity/events.lua")

print("[Rarity System] Loaded successfully")
