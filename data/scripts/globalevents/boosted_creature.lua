local function getCurrentDay()
	return math.floor(os.time() / 86400)
end

local function secondsUntilMidnight()
	local now = os.time()
	local midnight = now - (now % 86400) + 86400
	return midnight - now
end

local function pickNewBoosted()
	local idx = math.random(1, #BOOSTED_MONSTER_LIST)
	local selected = BOOSTED_MONSTER_LIST[idx]
	Game.setBoostedCreature(selected)

	Game.setStorageValue(GlobalStorageKeys.boostedCreatureIndex, idx)
	Game.setStorageValue(GlobalStorageKeys.boostedCreatureDay, getCurrentDay())

	local msg = string.format(
		"[Boosted Creature] Today's boosted creature is: %s! It yields double experience, double loot, and spawns twice as fast!",
		selected
	)
	for _, player in ipairs(Game.getPlayers()) do
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, msg)
	end

	logInfo(">> Boosted Creature changed to: " .. selected)
end

local boostedStartup = GlobalEvent("BoostedCreatureStartup")
function boostedStartup.onStartup()
	local savedIdx = Game.getStorageValue(GlobalStorageKeys.boostedCreatureIndex)
	local savedDay = Game.getStorageValue(GlobalStorageKeys.boostedCreatureDay)
	local today = getCurrentDay()

	if savedIdx and savedDay and savedDay == today and savedIdx >= 1 and savedIdx <= #BOOSTED_MONSTER_LIST then
		local restored = BOOSTED_MONSTER_LIST[savedIdx]
		Game.setBoostedCreature(restored)
		logInfo(">> Boosted Creature restored: " .. restored)
	else
		pickNewBoosted()
	end

	addEvent(function()
		pickNewBoosted()
		addEvent(pickNewBoosted, 86400 * 1000)
	end, secondsUntilMidnight() * 1000)

	return true
end
boostedStartup:register()

local loginBoosted = CreatureEvent("BoostedCreatureLogin")
function loginBoosted.onLogin(player)
	local boosted = Game.getBoostedCreature()
	if boosted ~= "" then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE,
			string.format("Today's boosted creature is: %s. Double XP, double loot, spawns twice as fast!", boosted))
	end

	-- Boosted Boss is persisted by CustomBosstiary and sent to Astra through login and Bosstiary protocols.
	if player.isUsingAstraClient and player:isUsingAstraClient() and CustomBosstiary and CustomBosstiary.getBoostedBoss then
		local boostedBoss = CustomBosstiary.getBoostedBoss()
		if boostedBoss and boostedBoss.name then
			player:sendTextMessage(MESSAGE_EVENT_ADVANCE,
				string.format("Today's boosted boss is: %s. Extra loot and extra Bosstiary kills!", boostedBoss.name))
		end
	end
	return true
end
loginBoosted:register()
