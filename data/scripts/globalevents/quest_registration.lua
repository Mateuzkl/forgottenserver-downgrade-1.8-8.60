local questRegistration = GlobalEvent("RegisterGlobalQuests")

function questRegistration.onStartup()
	if not Quests then
		logger.warn("[QuestRegistration] No Quests table found, skipping quest registration.")
		return true
	end

	local registered = 0
	for _, questData in pairs(Quests) do
		local quest = Game.createQuest(questData.name, {
			storageId = questData.startStorageId,
			storageValue = questData.startStorageValue,
			missions = questData.missions
		})
		if quest then
			quest:register()
			registered = registered + 1
		end
	end
	logger.info("[QuestRegistration] Registered {} quests.", registered)
	return true
end

questRegistration:register()
