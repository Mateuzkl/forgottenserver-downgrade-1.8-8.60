-- TEMP diagnostic: dump the C2S 0xF3 (243) payload to discover its format.
-- 0xF3 is a Cipsoft custom packet the server normally ignores. On this fork,
-- custom network packets are wired through PacketHandler under data/scripts/network.

local handler = PacketHandler(0xF3)

function handler.onReceive(player, msg)
	local remaining = NetworkGuard.remaining(msg)
	local parts = {}
	local count = math.min(remaining, 24)

	for _ = 1, count do
		local byte = NetworkGuard.readByte(msg)
		if not byte then
			break
		end
		parts[#parts + 1] = string.format("%02X", byte)
	end

	if logger and logger.info then
		logger.info("[F3-DUMP] player=%s remaining=%d bytes=%s", player:getName(), remaining, table.concat(parts, " "))
	end
end

handler:register()
