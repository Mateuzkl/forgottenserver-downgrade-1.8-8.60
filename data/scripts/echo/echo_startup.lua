-- Reconfigure at script load as well as first startup. This makes /reload scripts
-- replace the old runtime state instead of leaving stale raids or callbacks.
if not configManager.getBoolean(configKeys.ECHO_RAID_SYSTEM_ENABLED)
	or not configManager.getBoolean(configKeys.BESTIARY_SYSTEM_ENABLED) then
	return
end

if not Game.configureEchoRaid(EchoConfig) then
	logError("[EchoRaid] Configuration validation failed; subsystem remains disabled.")
end
