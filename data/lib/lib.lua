-- Core API functions implemented in Lua
dofile(CORE_DIRECTORY .. '/lib/core/core.lua')

-- Compatibility library for our old Lua API
dofile(CORE_DIRECTORY .. '/lib/compat/compat.lua')

-- Debugging helper function for Lua developers
dofile(CORE_DIRECTORY .. '/lib/debugging/dump.lua')

dofile(CORE_DIRECTORY .. '/lib/functions/load.lua')
dofile(CORE_DIRECTORY .. "/lib/quests/quest.lua")
for _, file in ipairs({"bigfoot_burden", "demon_oak", "grimvale", "killing_in_the_name_of", "soul_war", "svargrond_arena", "the_cursed_crystal", "the_primal_ordeal", "the_queen_of_the_banshees", "their_masters_voice"}) do dofile(CORE_DIRECTORY .. "/lib/quests/" .. file .. ".lua") end
