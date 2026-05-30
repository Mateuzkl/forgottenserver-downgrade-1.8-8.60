-- Rarity System Event Registrations
-- Loaded from data/scripts/ via Scripts Interface.
-- CreatureEvents and Event() callbacks require this interface.

if not RARITY_SYSTEM_ENABLED then
	return
end

dofile("data/scripts/systems/rarity/combat.lua")
dofile("data/scripts/systems/rarity/events.lua")
dofile("data/scripts/systems/rarity/callbacks.lua")

print("[Rarity System] Events loaded successfully")
