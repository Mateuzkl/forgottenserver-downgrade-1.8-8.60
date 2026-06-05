local function secondsUntilMidnight()
	local now = os.time()
	local date = os.date("*t", now)
	date.hour = 0
	date.min = 0
	date.sec = 5
	date.day = date.day + 1
	return math.max(1, os.time(date) - now)
end

local function logBoostedBossError(message)
	if logger and logger.error then
		logger.error("%s", "[BoostedBoss] " .. message)
	else
		print("[BoostedBoss] " .. message)
	end
end

local function scheduleNextBoostedBoss()
	addEvent(function()
		if CustomBosstiary and CustomBosstiary.pickNewBoostedBoss then
			local ok, err = pcall(CustomBosstiary.pickNewBoostedBoss)
			if not ok then
				logBoostedBossError("CustomBosstiary.pickNewBoostedBoss failed: " .. tostring(err))
			end
		end
		scheduleNextBoostedBoss()
	end, secondsUntilMidnight() * 1000)
end

local boostedBossStartup = GlobalEvent("BoostedBossStartup")
function boostedBossStartup.onStartup()
	if not CustomBosstiary or not CustomBosstiary.loadBoostedBoss then
		logInfo(">> CustomBosstiary not available, skipping Boosted Boss load")
		return true
	end

	CustomBosstiary.loadBoostedBoss()
	scheduleNextBoostedBoss()
	return true
end
boostedBossStartup:register()
