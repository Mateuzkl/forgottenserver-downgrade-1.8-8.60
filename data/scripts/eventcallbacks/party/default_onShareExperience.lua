local event = Event()

function event.onShareExperience(party, exp, rawExp)
	local partyVocationBonus = {
		[1] = 1.20,
		[2] = 1.35,
		[3] = 1.70,
		[4] = 2.00,
	}
	local sharedExperienceMultiplier = partyVocationBonus[1]
	local vocationsIds = {}

	local vocationId = party:getLeader():getVocation():getBase():getId()
	if vocationId ~= VOCATION_NONE then vocationsIds[1] = vocationId end

	for _, member in ipairs(party:getMembers()) do
		vocationId = member:getVocation():getBase():getId()
		if not table.contains(vocationsIds, vocationId) and vocationId ~=
			VOCATION_NONE then vocationsIds[#vocationsIds + 1] = vocationId end
	end

	local size = #vocationsIds
	sharedExperienceMultiplier = partyVocationBonus[size] or sharedExperienceMultiplier

	exp = math.ceil((exp * sharedExperienceMultiplier) / (#party:getMembers() + 1))
	return exp
end

event:register()
