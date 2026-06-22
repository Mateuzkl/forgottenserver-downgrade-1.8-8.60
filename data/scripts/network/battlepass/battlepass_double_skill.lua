-- Applies the temporary Double Skill reward granted by the Battle Pass.
-- Artificial tries are excluded to avoid duplicating admin or scripted grants.
local event = Event()

function event.onGainSkillTries(player, skill, tries, artificial)
	if artificial then
		return tries
	end

	local untilTime = tonumber(player:getStorageValue(PlayerStorageKeys.battlePassDoubleSkillUntil)) or 0
	if untilTime <= os.time() then
		if untilTime > 0 then
			player:setStorageValue(PlayerStorageKeys.battlePassDoubleSkillUntil, -1)
		end
		return tries
	end

	return tries * 2
end

event:register(50)
