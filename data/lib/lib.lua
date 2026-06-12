-- Core API functions implemented in Lua
dofile(CORE_DIRECTORY .. '/lib/core/core.lua')

-- Compatibility library for our old Lua API
dofile(CORE_DIRECTORY .. '/lib/compat/compat.lua')

-- Debugging helper function for Lua developers
dofile(CORE_DIRECTORY .. '/lib/debugging/dump.lua')

dofile(CORE_DIRECTORY .. '/lib/functions/load.lua')

-- Hireling system
dofile(CORE_DIRECTORY .. '/lib/hireling_lib.lua')
dofile(CORE_DIRECTORY .. '/lib/hireling_items.lua')
