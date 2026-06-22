local event = Event()

function event.onShareExperience(party, exp, rawExp)
	local partyVocationBonus = {
		[1] = 1.20, -- same vocation: +20%
		[2] = 1.35, -- 2 different vocations: +35%
		[3] = 1.70, -- 3 different vocations: +70%
		[4] = 2.00, -- 4 different vocations: +100%
	}
	local vocationsIds = {}

	local vocationId = party:getLeader():getVocation():getBase():getId()
	if vocationId ~= VOCATION_NONE then vocationsIds[1] = vocationId end

	for _, member in ipairs(party:getMembers()) do
		vocationId = member:getVocation():getBase():getId()
		if not table.contains(vocationsIds, vocationId) and vocationId ~=
			VOCATION_NONE then vocationsIds[#vocationsIds + 1] = vocationId end
	end

	local size = #vocationsIds
	local sharedExperienceMultiplier = partyVocationBonus[size] or partyVocationBonus[4]

	exp = math.ceil((exp * sharedExperienceMultiplier) / (#party:getMembers() + 1))
	return exp
end

event:register()
