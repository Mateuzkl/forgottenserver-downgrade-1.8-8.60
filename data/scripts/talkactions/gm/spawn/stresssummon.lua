local talkaction = TalkAction("/stresssummon")

function talkaction.onSay(player, words, param)
	if param == "" then
		player:sendCancelMessage("Usage: /stresssummon name, amount, radius[, tries]")
		return false
	end

	local split = param:split(",")
	local monsterName = split[1] and split[1]:trim() or ""
	if monsterName == "" then
		player:sendCancelMessage("Usage: /stresssummon name, amount, radius[, tries]")
		return false
	end

	local amount = math.max(1, math.min(tonumber(split[2]) or 50, 500))
	local radius = math.max(1, math.min(tonumber(split[3]) or 8, 30))
	local triesPerMonster = math.max(1, math.min(tonumber(split[4]) or 20, 100))

	local center = player:getPosition()
	local placed = 0

	for i = 1, amount do
		for t = 1, triesPerMonster do
			local pos = Position(
				center.x + math.random(-radius, radius),
				center.y + math.random(-radius, radius),
				center.z
			)

			local monster = Game.createMonster(monsterName, pos, false, false, CONST_ME_TELEPORT, player:getInstanceId())
			if monster then
				if player:addSummon(monster) then
					placed = placed + 1
				else
					monster:remove()
				end
				break
			end
		end
	end

	if placed > 0 then
		center:sendMagicEffect(CONST_ME_MAGIC_RED)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
			string.format("Stress summoned %d/%d %s as summons in radius %d.", placed, amount, monsterName, radius))
	else
		player:sendCancelMessage("Could not summon " .. monsterName .. ". Check the name or area.")
		center:sendMagicEffect(CONST_ME_POFF)
	end

	return false
end

talkaction:separator(" ")
talkaction:access(true)
talkaction:register()
