local startup = GlobalEvent("BotManagerStartup")

function startup.onStartup()
	if not BotSystem.isEnabled() then
		logInfo("[BotManager] Bot system disabled by config.")
		return true
	end

	if not BotSystem.ensureTables() then
		logInfo("[BotManager] Could not create bot tables.")
		return true
	end

	addEvent(function()
		local spawned = BotSystem.spawnAuto()
		if spawned > 0 then
			logInfo(string.format("[BotManager] Auto-spawned %d bot(s).", spawned))
		end
		if BotBrain and BotBrain.start then
			BotBrain.start()
		end
	end, 1000)
	return true
end

startup:register()

local shutdown = GlobalEvent("BotManagerShutdown")

function shutdown.onShutdown()
	local despawned = Game.despawnAllBots(true)
	if despawned > 0 then
		logInfo(string.format("[BotManager] Despawned %d bot(s).", despawned))
	end
	return true
end

shutdown:register()
