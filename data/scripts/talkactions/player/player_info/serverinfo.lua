local talkaction = TalkAction("!serverinfo")

local fmt = string.format

function talkaction.onSay(player, words, param)

	local desc = {"Server Info:\n"}

	-- Global Rates
	desc[#desc + 1] = fmt("Exp rate: %s", Game.getExperienceStage(player:getLevel()))
	desc[#desc + 1] = fmt("Fist rate: %s", Game.getSkillStage(player:getSkillLevel(SKILL_FIST)))
	desc[#desc + 1] = fmt("Club rate: %s", Game.getSkillStage(player:getSkillLevel(SKILL_CLUB)))
	desc[#desc + 1] = fmt("Sword rate: %s", Game.getSkillStage(player:getSkillLevel(SKILL_SWORD)))
	desc[#desc + 1] = fmt("Axe rate: %s", Game.getSkillStage(player:getSkillLevel(SKILL_AXE)))
	desc[#desc + 1] = fmt("Distance rate: %s", Game.getSkillStage(player:getSkillLevel(SKILL_DISTANCE)))
	desc[#desc + 1] = fmt("Shield rate: %s", Game.getSkillStage(player:getSkillLevel(SKILL_SHIELD)))
	desc[#desc + 1] = fmt("Fishing rate: %s", Game.getSkillStage(player:getSkillLevel(SKILL_FISHING)))
	desc[#desc + 1] = fmt("Magic rate: %s", Game.getMagicLevelStage(player:getMagicLevel()))
	desc[#desc + 1] = fmt("Loot rate: %s", configManager.getNumber(configKeys.RATE_LOOT))

	-- Player Rates
	desc[#desc + 1] = fmt("XP rate base: %+d%%", player:getExperienceRate(ExperienceRateType.BASE) - 100)
	desc[#desc + 1] = fmt("XP rate low level: %+d%%", player:getExperienceRate(ExperienceRateType.LOW_LEVEL) - 100)
	desc[#desc + 1] = fmt("XP rate bonus: %+d%%", player:getExperienceRate(ExperienceRateType.BONUS) - 100)
	desc[#desc + 1] = fmt("XP rate stamina: %+d%%", player:getExperienceRate(ExperienceRateType.STAMINA) - 100)

	player:popupFYI(table.concat(desc, "\n"))
	return false
end

talkaction:register()

