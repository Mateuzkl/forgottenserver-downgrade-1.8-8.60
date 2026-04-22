-- Fight Mode Network Handler
-- Handles opcode 0xA0: fight stance, chase mode, secure mode
-- Packet: [0xA0][fightMode:u8][chaseMode:u8][secureMode:u8]

local handler = PacketHandler(0xA0)

handler.onReceive = function(player, msg)
	local fightMode  = msg:getByte()  -- 0=full, 1=balanced, 2=defensive
	local chaseMode  = msg:getByte()  -- 0=stand, 1=follow
	local secureMode = msg:getByte()  -- 0=off,  1=on

	if fightMode > 2 or chaseMode > 1 or secureMode > 1 then
		return
	end

	player:setFightMode(fightMode)
	player:setChaseMode(chaseMode)
	player:setSecureMode(secureMode)

	-- fire event so other scripts can react
	EventCallback.onFightModeChanged(player, fightMode, chaseMode, secureMode)
end

handler:register()
