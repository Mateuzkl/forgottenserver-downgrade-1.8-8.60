-- EventCallback: onFightModeChanged
-- Called whenever a player changes their fight/chase/secure mode via packet 0xA0
-- Scripts that need to react to fight mode changes should hook here.

local ec = EventCallback

function ec.onFightModeChanged(player, fightMode, chaseMode, secureMode)
	-- Example: log changes (disable in production)
	-- print(string.format('[FightMode] %s -> fight=%d chase=%d secure=%d',
	--     player:getName(), fightMode, chaseMode, secureMode))

	-- Sync client (send the same packet back so the UI stays in sync
	-- in case the server clamped any value)
	local msg = NetworkMessage()
	msg:addByte(0xA0)
	msg:addByte(player:getFightMode())
	msg:addByte(player:getChaseMode())
	msg:addByte(player:getSecureMode())
	msg:sendToPlayer(player)
end

ec:register()
