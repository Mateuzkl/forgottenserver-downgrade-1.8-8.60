local event = Event()

function event.onGainExperience(player, source, exp)
	if EquipmentProficiencySystem then
		EquipmentProficiencySystem.addExperience(player, source, exp)
	end
	return exp
end

event:register(100)
