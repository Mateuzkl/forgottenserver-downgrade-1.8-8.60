local hirelingStartup = GlobalEvent("HirelingStartup")

function hirelingStartup.onStartup()
	if configManager.getBoolean(configKeys.HIRELING_SYSTEM_ENABLED) then
		HirelingsInit()
	end
	return true
end

hirelingStartup:register()

local hirelingShutdown = GlobalEvent("HirelingShutdown")

function hirelingShutdown.onShutdown()
	if configManager.getBoolean(configKeys.HIRELING_SYSTEM_ENABLED) then
		SaveHirelings()
	end
	return true
end

hirelingShutdown:register()
