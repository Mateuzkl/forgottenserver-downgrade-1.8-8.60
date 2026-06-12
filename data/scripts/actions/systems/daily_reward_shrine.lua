local dailyRewardShrine = Action()

function dailyRewardShrine.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	DailyRewardSystem.openRewardWall(player, true)
	return true
end

dailyRewardShrine:id(25802, 25803)
dailyRewardShrine:register()
