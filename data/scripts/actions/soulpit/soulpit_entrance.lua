-- Soulpit Arena Entrance/Exit MoveEvents
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

-- Entrance: teleport into arena
local enterArena = MoveEvent()

function enterArena.onStepIn(creature, item, position, fromPosition)
	if not creature or not creature:isPlayer() then
		return true
	end

	-- Teleport to arena
	creature:teleportTo(SoulPit.entrancePos[1].toPos)
	SoulPit.entrancePos[1].toPos:sendMagicEffect(CONST_ME_TELEPORT)

	return true
end

-- Register for each entrance position (adjust to your map!)
for _, entry in ipairs(SoulPit.entrancePos) do
	enterArena:position(entry.fromPos)
end
enterArena:type("stepin")
enterArena:register()

-- Exit: teleport out of arena
local exitArena = MoveEvent()

function exitArena.onStepIn(creature, item, position, fromPosition)
	if not creature or not creature:isPlayer() then
		return true
	end

	creature:teleportTo(SoulPit.exitDestination)
	SoulPit.exitDestination:sendMagicEffect(CONST_ME_TELEPORT)

	return true
end

exitArena:position(SoulPit.exitPos)
exitArena:type("stepin")
exitArena:register()
