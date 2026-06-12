-- Soulpit Arena Exit: use-position action to leave arena
-- Ported from Crystal Server.

if not configManager or not configManager.getBoolean or not configManager.getBoolean(configKeys.SOULPIT_SYSTEM_ENABLED) then
	return
end

if not SoulPit then
	dofile("data/lib/others/soulpit.lua")
end
if not SoulPit then
	return
end

local arenaExit = Action()

function arenaExit.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if not player then
		return false
	end

	-- Teleport player to safe exit position outside the arena
	player:teleportTo(SoulPit.exitDestination)
	SoulPit.exitDestination:sendMagicEffect(CONST_ME_TELEPORT)

	return true
end

-- Register at the exit position inside the arena (adjust to your map!)
arenaExit:position(SoulPit.playerExitDestination)
arenaExit:register()
