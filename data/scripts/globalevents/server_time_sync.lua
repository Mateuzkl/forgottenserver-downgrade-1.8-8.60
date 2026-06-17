local serverTimeSync = GlobalEvent("ServerTimeSync")

local function sendServerTime(player, hour, minute)
	local msg<close> = NetworkMessage()
	msg:addByte(0xEF)
	msg:addByte(hour)
	msg:addByte(minute)
	msg:sendToPlayer(player)
end

function serverTimeSync.onThink(interval)
	local worldTime = getWorldTime()
	local hour = math.floor(worldTime / 60)
	local minute = worldTime % 60

	for _, player in ipairs(Game.getPlayers()) do
		if player.isUsingAstraClient and player:isUsingAstraClient() then
			sendServerTime(player, hour, minute)
		end
	end
	return true
end

serverTimeSync:interval(1000)
serverTimeSync:register()
