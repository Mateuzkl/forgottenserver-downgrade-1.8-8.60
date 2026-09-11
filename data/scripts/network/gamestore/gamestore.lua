-- GameStore Lua Delivery Bridge
-- Subsystems that exist only in Lua (BattlePass, Prey, TaskBoard, Hirelings)
-- are fulfilled here via the C++ StoreService::deliverViaLuaCallback bridge.
-- Network protocol handling (0xF8, 0xFA, 0xFB, 0xFC, 0xFD) is now natively executed in C++23.

local function logInfo(message)
	if logger and logger.info then
		logger.info(message)
	else
		print(message)
	end
end

local function logError(message)
	if logger and logger.error then
		logger.error(message)
	else
		print(message)
	end
end

--- Fulfill store purchase delivery for offers backed by Lua subsystems.
--- Called by C++ StoreService via lua_pcall.
--- @param player Player The player receiving the purchase.
--- @param offerType string Canonical offer type string.
--- @param value integer The numeric offer value from catalog XML.
--- @param displayId integer The client display eid/lookType.
--- @param extraName string Optional extra name (e.g. for hireling).
--- @param extraSex integer Optional extra sex (e.g. for hireling).
--- @return string|nil Error message on failure, or nil on success.
function StoreDeliverLuaOffer(player, offerType, value, displayId, extraName, extraSex)
	if not player then
		return "Player not found."
	end

	offerType = tostring(offerType or ""):lower()

	-- TaskBoard timed bounty kill boost
	if offerType == "bounty_kill_boost" then
		if not configManager.getBoolean(configKeys.TASK_HUNTING_SYSTEM_ENABLED) or
		   not configManager.getBoolean(configKeys.BOUNTY_TASKS_ENABLED) or
		   not TaskBoard or not TaskBoard.activateTimedBoost then
			return "Task Hunt system is not available."
		end
		if not TaskBoard.activateTimedBoost(player, TaskBoard.Storage.BOUNTY_KILL_BOOST_UNTIL, value) then
			return "Failed to activate the bounty kill boost."
		end
		return nil
	end

	-- TaskBoard timed weekly kill boost
	if offerType == "weekly_kill_boost" then
		if not configManager.getBoolean(configKeys.TASK_HUNTING_SYSTEM_ENABLED) or
		   not configManager.getBoolean(configKeys.WEEKLY_TASKS_ENABLED) or
		   not TaskBoard or not TaskBoard.activateTimedBoost then
			return "Task Hunt system is not available."
		end
		if not TaskBoard.activateTimedBoost(player, TaskBoard.Storage.WEEKLY_KILL_BOOST_UNTIL, value) then
			return "Failed to activate the weekly kill boost."
		end
		return nil
	end

	-- TaskBoard timed weekly reduced items
	if offerType == "weekly_reduced_items" then
		if not configManager.getBoolean(configKeys.TASK_HUNTING_SYSTEM_ENABLED) or
		   not configManager.getBoolean(configKeys.WEEKLY_TASKS_ENABLED) or
		   not TaskBoard or not TaskBoard.activateTimedBoost then
			return "Task Hunt system is not available."
		end
		if not TaskBoard.activateTimedBoost(player, TaskBoard.Storage.WEEKLY_REDUCED_ITEMS_UNTIL, value) then
			return "Failed to activate reduced weekly item amounts."
		end
		if _TASK_BOARD_WEEKLY_MODULE and _TASK_BOARD_WEEKLY_MODULE.applyReducedItems then
			_TASK_BOARD_WEEKLY_MODULE.applyReducedItems(player)
		end
		return nil
	end

	-- TaskBoard permanent weekly task expansion
	if offerType == "weekly_task_expansion" then
		if not configManager.getBoolean(configKeys.TASK_HUNTING_SYSTEM_ENABLED) or
		   not configManager.getBoolean(configKeys.WEEKLY_TASKS_ENABLED) then
			return "Task Hunt system is not available."
		end
		if player:hasWeeklyExpansion() then
			return "You already have the Permanent Weekly Task Expansion."
		end
		player:setWeeklyExpansion(true)
		if _TASK_BOARD_WEEKLY_MODULE and _TASK_BOARD_WEEKLY_MODULE.applyExpansion then
			_TASK_BOARD_WEEKLY_MODULE.applyExpansion(player)
		end
		return nil
	end

	-- BattlePass premium unlock
	if offerType == "battlepass" then
		if not configManager.getBoolean(configKeys.BATTLEPASS_SYSTEM_ENABLED) or
		   not BattlePassSystem or not BattlePassSystem.purchasePremium then
			return "Battle Pass system is not available."
		end
		return BattlePassSystem.purchasePremium(player, true)
	end

	-- Prey System wildcards
	if offerType == "prey_wildcard" then
		if not PreySystem or not PreySystem.addWildcards then
			return "Prey System is not available."
		end
		if value <= 0 then
			return "Invalid Prey Wildcard amount."
		end
		PreySystem.addWildcards(player, value)
		return nil
	end

	-- Hireling lamp creation
	if offerType == "hireling" then
		if not configManager.getBoolean(configKeys.HIRELING_SYSTEM_ENABLED) or
		   not configManager.getBoolean(configKeys.ASTRA_HIRELING_PROTOCOL_ENABLED) or
		   not player.addNewHireling then
			return "Hireling system is not available."
		end
		local hireling, err = player:addNewHireling(extraName or "", extraSex or 0)
		if not hireling then
			return err or "Failed to create hireling."
		end
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "Your hireling lamp was sent to your Store Inbox.")
		return nil
	end

	-- Hireling skill unlock
	if offerType == "hireling_skill" then
		if not configManager.getBoolean(configKeys.HIRELING_SYSTEM_ENABLED) or
		   not configManager.getBoolean(configKeys.ASTRA_HIRELING_PROTOCOL_ENABLED) then
			return "Hireling system is not available."
		end
		local skillId = value > 0 and value or displayId
		local skillName = GetHirelingSkillNameById and GetHirelingSkillNameById(skillId)
		if not skillName then
			return "Invalid hireling skill."
		end
		if player:hasHirelingSkill(skillName) then
			return "You already have this hireling skill."
		end
		player:enableHirelingSkill(skillName)
		return nil
	end

	-- Hireling dress unlock
	if offerType == "hireling_outfit" then
		if not configManager.getBoolean(configKeys.HIRELING_SYSTEM_ENABLED) or
		   not configManager.getBoolean(configKeys.ASTRA_HIRELING_PROTOCOL_ENABLED) then
			return "Hireling system is not available."
		end
		local dressId = value > 0 and value or displayId
		local outfitName = GetHirelingOutfitNameById and GetHirelingOutfitNameById(dressId)
		if not outfitName then
			return "Invalid hireling dress."
		end
		if player:hasHirelingOutfit(outfitName) then
			return "You already have this hireling dress."
		end
		player:enableHirelingOutfit(outfitName)
		return nil
	end

	return "Unsupported Lua offer type."
end

logInfo(">> Loaded GameStore Lua Delivery Bridge for native C++23 Store.")
