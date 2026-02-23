local serverstartup = GlobalEvent("serverstartup")
function serverstartup.onStartup()
	logInfo(">> Loading map attributes")

	-- Sign table
	-- loadLuaMapSign(SignTable)
	-- logInfo("Loaded " .. (#SignTable) .. " signs in the map")

	-- Action and unique tables
	-- Chest table
	loadLuaMapAction(ChestAction)
	loadLuaMapUnique(ChestUnique)
	-- -- Corpse table
	loadLuaMapAction(CorpseAction)
	loadLuaMapUnique(CorpseUnique)

	-- -- Doors key table
	loadLuaMapAction(KeyDoorAction)

	-- -- Doors level table
	loadLuaMapAction(LevelDoorAction)

	-- -- Doors quest table
	loadLuaMapAction(QuestDoorAction)
	loadLuaMapUnique(QuestDoorUnique)
	-- -- Item table
	loadLuaMapAction(ItemAction)
	loadLuaMapUnique(ItemUnique)

	-- -- Item daily reward table
	-- This is temporary disabled > loadLuaMapAction(DailyRewardAction)

	-- -- Item unmoveable table
	loadLuaMapAction(ItemUnmoveableAction)
	-- -- Lever table
	loadLuaMapAction(LeverAction)
	loadLuaMapUnique(LeverUnique)

	-- -- Teleport (magic forcefields) table
	loadLuaMapAction(TeleportAction)
	loadLuaMapUnique(TeleportUnique)

	-- -- Teleport item table
	loadLuaMapAction(TeleportItemAction)
	loadLuaMapUnique(TeleportItemUnique)

	-- -- Tile table
	loadLuaMapAction(TileAction)
	loadLuaMapUnique(TileUnique)

	-- -- Tile pick table
	loadLuaMapAction(TilePickAction)

	-- -- Create new item on map
	CreateMapItem(CreateItemOnMap)

	-- -- Update old quest storage keys
	-- updateKeysStorage(QuestKeysUpdate)

	logInfo(">> Loaded all actions in the map")
	logInfo(">> Loaded all uniques in the map")

	-- load map trainers
	--Game.loadMap("data/world/trainers/trainers-custom.otbm")
	logInfo(">> Loaded map trainers")

	-- for i = 1, #startupGlobalStorages do
	-- 	Game.setStorageValue(startupGlobalStorages[i], 0)
	-- end

	local time = os.time()
	db.asyncQuery('TRUNCATE TABLE `players_online`')
	db.asyncQuery("DELETE FROM `guild_wars` WHERE `status` = 0")
	db.asyncQuery("DELETE FROM `players` WHERE `deletion` != 0 AND `deletion` < " ..
		              os.time())
	db.asyncQuery("DELETE FROM `ip_bans` WHERE `expires_at` != 0 AND `expires_at` <= " ..
		              os.time())

	local resultId = db.storeQuery(
		                 "SELECT * FROM `account_bans` WHERE `expires_at` != 0 AND `expires_at` <= " ..
			                 os.time())
	if resultId ~= false then
		repeat
			local accountId = result.getNumber(resultId, "account_id")
			db.asyncQuery(
				"INSERT INTO `account_ban_history` (`account_id`, `reason`, `banned_at`, `expired_at`, `banned_by`) VALUES (" ..
					accountId .. ", " .. db.escapeString(result.getString(resultId, "reason")) .. ", " ..
					result.getNumber(resultId, "banned_at") .. ", " ..
					result.getNumber(resultId, "expires_at") .. ", " ..
					result.getNumber(resultId, "banned_by") .. ")")
			db.asyncQuery("DELETE FROM `account_bans` WHERE `account_id` = " .. accountId)
		until not result.next(resultId)
		result.free(resultId)
	end

	local resultId = db.storeQuery(
		                 "SELECT `id`, `highest_bidder`, `last_bid`, (SELECT `balance` FROM `players` WHERE `players`.`id` = `highest_bidder`) AS `balance` FROM `houses` WHERE `owner` = 0 AND `bid_end` != 0 AND `bid_end` < " ..
			                 os.time())
	if resultId ~= false then
		repeat
			local house = House(result.getNumber(resultId, "id"))
			if house then
				local highestBidder = result.getNumber(resultId, "highest_bidder")
				local balance = result.getNumber(resultId, "balance")
				local lastBid = result.getNumber(resultId, "last_bid")
				if balance >= lastBid then
					db.query("UPDATE `players` SET `balance` = " .. (balance - lastBid) ..
						         " WHERE `id` = " .. highestBidder)
					house:setOwnerGuid(highestBidder)
				end
				db.asyncQuery(
					"UPDATE `houses` SET `last_bid` = 0, `bid_end` = 0, `highest_bidder` = 0, `bid` = 0 WHERE `id` = " ..
						house:getId())
			end
		until not result.next(resultId)
		result.free(resultId)
	end

	db.query("TRUNCATE TABLE `towns`")
	for i, town in ipairs(Game.getTowns()) do
		local position = town:getTemplePosition()
		db.query("INSERT INTO `towns` (`id`, `name`, `posx`, `posy`, `posz`) VALUES (" ..
			         town:getId() .. ", " .. db.escapeString(town:getName()) .. ", " .. position.x ..
			         ", " .. position.y .. ", " .. position.z .. ")")
	end
	
	-- check for duplicate storages
	if configManager.getBoolean(configKeys.CHECK_DUPLICATE_STORAGE_KEYS) then
		local variableNames = {"AccountStorageKeys", "PlayerStorageKeys", "GlobalStorageKeys", "actionIds", "uniqueIds"}
		for _, variableName in ipairs(variableNames) do
			local duplicates = checkDuplicateStorageKeys(variableName)
			if duplicates then
				local message = "Duplicate keys found: " .. table.concat(duplicates, ", ")
				print(">> Checking " .. variableName .. ": " .. message)
			else
				print(">> Checking " .. variableName .. ": No duplicate keys found.")
			end
		end
	end
end
serverstartup:register()
