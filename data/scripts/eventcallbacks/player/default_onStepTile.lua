local event = Event()
event.onStepTile = function(self, fromPosition, toPosition, movementSessionFlags)
	local function isActive(flag)
		return movementSessionFlags % (flag * 2) >= flag
	end

	if isActive(MOVEMENT_SESSION_EXERCISE) then
		LeaveTraining(self:getId(), self)
		self:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You can't move while you train, the training has stopped.")
	end

	if isActive(MOVEMENT_SESSION_MARKET) and CustomMarket and CustomMarket.checkAccess then
		CustomMarket.checkAccess(self)
	end

	if isActive(MOVEMENT_SESSION_FORGE) and CustomForge and CustomForge.close then
		CustomForge.close(self)
	end

	if isActive(MOVEMENT_SESSION_IMBUING) and ImbuingWindow and ImbuingWindow.onStepTile then
		ImbuingWindow.onStepTile(self)
	end
end

event:register()
