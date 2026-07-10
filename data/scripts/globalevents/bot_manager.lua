local startup = GlobalEvent("BotManagerStartup")

function startup.onStartup()
	if not BotSystem or not BotSystem.isAvailable() then
		logInfo("[BotManager] Bot system bindings are not available in this build.")
		return true
	end

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
	-- Quiesce the brain before despawning so an already-queued tick exits at
	-- its guard instead of touching players mid-despawn or rescheduling.
	if BotBrain and BotBrain.stop then
		BotBrain.stop()
	end

	if Game.despawnAllBots then
		local despawned = Game.despawnAllBots(true)
		if despawned > 0 then
			logInfo(string.format("[BotManager] Despawned %d bot(s).", despawned))
		end
	end
	return true
end

shutdown:register()
