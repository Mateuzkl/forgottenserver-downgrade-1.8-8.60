local networkGuardCleanup = CreatureEvent("NetworkGuardCleanup")

function networkGuardCleanup.onLogout(player)
	NetworkGuard.clearPlayer(player)
	return true
end

networkGuardCleanup:register()
