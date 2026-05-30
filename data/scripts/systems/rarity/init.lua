-- Rarity System Initialization
-- Entry point loaded at server startup (Main Interface).
-- Only loads pure data/config files. Event registrations that require
-- Scripts Interface are loaded by data/scripts/rarity_register.lua.

if not RARITY_SYSTEM_ENABLED then
	return
end

dofile("data/scripts/systems/rarity/config.lua")
dofile("data/scripts/systems/rarity/balancing.lua")
dofile("data/scripts/systems/rarity/helpers.lua")
dofile("data/scripts/systems/rarity/core.lua")

print("[Rarity System] Config loaded successfully")
