ResetStages = {}

function ResetStages.getMultiplier(resetCount)
	if not ResetBonusConfig or not ResetBonusConfig.xpStages then
		return 1.0
	end

	for _, stage in ipairs(ResetBonusConfig.xpStages) do
		local max = stage.maxReset == 0 and resetCount or stage.maxReset
		if resetCount >= stage.minReset and resetCount <= max then
			return stage.multiplier
		end
	end
	return 1.0
end
