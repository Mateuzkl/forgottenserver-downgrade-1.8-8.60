local event = Event()

function event.onGainExperience(player, source, exp)
	if WeaponProficiencySystem then
		WeaponProficiencySystem.addExperience(player, source, exp)
	end
	return exp
end

event:register(100)
