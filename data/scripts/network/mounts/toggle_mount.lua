local handler = PacketHandler(0xD4)

function handler.onReceive(player, msg)
	if not NetworkGuard.cooldown(player, "mount-toggle", 150) then
		return
	end

	local value = NetworkGuard.readByte(msg)
	if value == nil then
		return
	end

	local mount = value ~= 0
	player:toggleMount(mount)
end

handler:register()
