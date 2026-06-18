local magicWallStress = TalkAction("/mw")

function magicWallStress.onSay(player, words, param)
	local count = tonumber(param) or 30
	if count > 200 then
		count = 200
	end

	local playerPos = player:getPosition()
	local minX = playerPos.x - 8
	local maxX = playerPos.x + 9
	local minY = playerPos.y - 6
	local maxY = playerPos.y + 7

	local created = 0
	local attempts = 0
	local maxAttempts = count * 10

	while created < count and attempts < maxAttempts do
		attempts = attempts + 1
		local x = math.random(minX, maxX)
		local y = math.random(minY, maxY)
		local pos = Position(x, y, playerPos.z)

		local item = Game.createItem(2129, 1, pos)
		if item then
			item:decay()
			created = created + 1
		end
	end

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
		string.format("Created %d magic walls in %d attempts.", created, attempts))
	return false
end

magicWallStress:separator(" ")
magicWallStress:accountType(6)
magicWallStress:access(true)
magicWallStress:register()
