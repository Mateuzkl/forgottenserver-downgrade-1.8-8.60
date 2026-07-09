BotSystem = BotSystem or {}

local tablesReady = false

local function boolToNumber(value)
	return value and 1 or 0
end

local function normalizeName(name)
	name = tostring(name or "")
	name = name:gsub("^%s+", ""):gsub("%s+$", "")
	return name
end

local function parseVocation(value)
	value = tostring(value or ""):lower()
	if value == "" then
		return 4
	end

	local vocation = tonumber(value)
	if vocation then
		return math.floor(vocation)
	end

	local names = {
		sorcerer = 1,
		druid = 2,
		paladin = 3,
		knight = 4,
		ms = 5,
		ed = 6,
		rp = 7,
		ek = 8
	}
	return names[value] or 4
end

function BotSystem.isEnabled()
	if Game.isBotSystemEnabled then
		return Game.isBotSystemEnabled()
	end

	if configManager and configKeys and configKeys.BOT_SYSTEM_ENABLED then
		return configManager.getBoolean(configKeys.BOT_SYSTEM_ENABLED)
	end
	return true
end

function BotSystem.ensureTables()
	if tablesReady then
		return true
	end

	if not BotSystem.isEnabled() then
		return false
	end

	if Game.ensureBotTables then
		tablesReady = Game.ensureBotTables()
		return tablesReady
	end

	local resultId = db.storeQuery("SHOW TABLES LIKE " .. db.escapeString("bot_players"))
	if not resultId then
		return false
	end

	result.free(resultId)
	tablesReady = true
	return true
end

function BotSystem.getPlayerId(nameOrGuid)
	local value = normalizeName(nameOrGuid)
	if value == "" then
		return nil, nil
	end

	local numeric = tonumber(value)
	local query
	if numeric and numeric > 0 then
		query = "SELECT `id`, `name` FROM `players` WHERE `id` = " .. math.floor(numeric) .. " LIMIT 1"
	else
		query = "SELECT `id`, `name` FROM `players` WHERE LOWER(`name`) = LOWER(" .. db.escapeString(value) .. ") LIMIT 1"
	end

	local resultId = db.storeQuery(query)
	if not resultId then
		return nil, nil
	end

	local playerId = result.getNumber(resultId, "id")
	local playerName = result.getString(resultId, "name")
	result.free(resultId)
	return playerId, playerName
end

function BotSystem.isRegistered(playerId)
	if not playerId or not BotSystem.ensureTables() then
		return false
	end

	local resultId = db.storeQuery("SELECT `player_id` FROM `bot_players` WHERE `player_id` = " .. playerId .. " LIMIT 1")
	if not resultId then
		return false
	end

	result.free(resultId)
	return true
end

function BotSystem.register(nameOrGuid, autoSpawn, vocation)
	if not BotSystem.isEnabled() then
		return false, "Bot system is disabled in config.lua (botSystemEnabled = false)."
	end

	if not BotSystem.ensureTables() then
		return false, "Could not create bot tables."
	end

	local normalized = normalizeName(nameOrGuid)
	if normalized == "" then
		return false, "Bot name is required."
	end

	if Game.registerBot and not tonumber(normalized) then
		local ok, message = Game.registerBot(normalized, autoSpawn == true, true, parseVocation(vocation), PLAYERSEX_MALE)
		return ok, message
	end

	local playerId, playerName = BotSystem.getPlayerId(nameOrGuid)
	if not playerId then
		return false, "Player not found."
	end

	local now = os.time()
	local query = string.format(
		"INSERT INTO `bot_players` (`player_id`, `enabled`, `auto_spawn`, `created_at`, `updated_at`) VALUES (%d, 1, %d, %d, %d) " ..
		"ON DUPLICATE KEY UPDATE `enabled` = 1, `auto_spawn` = VALUES(`auto_spawn`), `updated_at` = VALUES(`updated_at`)",
		playerId, boolToNumber(autoSpawn), now, now
	)

	if not db.query(query) then
		return false, "Could not register bot."
	end
	return true, string.format("Bot '%s' registered%s.", playerName, autoSpawn and " with auto-spawn" or "")
end

function BotSystem.unregister(nameOrGuid)
	if not BotSystem.isEnabled() then
		return false, "Bot system is disabled in config.lua (botSystemEnabled = false)."
	end

	if not BotSystem.ensureTables() then
		return false, "Could not create bot tables."
	end

	local playerId, playerName = BotSystem.getPlayerId(nameOrGuid)
	if not playerId then
		return false, "Player not found."
	end
	if not BotSystem.isRegistered(playerId) then
		return false, "Bot is not registered. Use /bot add first."
	end

	if not db.query("DELETE FROM `bot_players` WHERE `player_id` = " .. playerId) then
		return false, "Could not unregister bot."
	end
	return true, string.format("Bot '%s' unregistered.", playerName)
end

function BotSystem.setEnabled(nameOrGuid, enabled)
	if not BotSystem.isEnabled() then
		return false, "Bot system is disabled in config.lua (botSystemEnabled = false)."
	end

	if not BotSystem.ensureTables() then
		return false, "Could not create bot tables."
	end

	local playerId, playerName = BotSystem.getPlayerId(nameOrGuid)
	if not playerId then
		return false, "Player not found."
	end
	if not BotSystem.isRegistered(playerId) then
		return false, "Bot is not registered. Use /bot add first."
	end

	local query = string.format("UPDATE `bot_players` SET `enabled` = %d, `updated_at` = %d WHERE `player_id` = %d",
		boolToNumber(enabled), os.time(), playerId)
	if not db.query(query) then
		return false, "Could not update bot."
	end
	return true, string.format("Bot '%s' %s.", playerName, enabled and "enabled" or "disabled")
end

function BotSystem.setAutoSpawn(nameOrGuid, enabled)
	if not BotSystem.isEnabled() then
		return false, "Bot system is disabled in config.lua (botSystemEnabled = false)."
	end

	if not BotSystem.ensureTables() then
		return false, "Could not create bot tables."
	end

	local playerId, playerName = BotSystem.getPlayerId(nameOrGuid)
	if not playerId then
		return false, "Player not found."
	end
	if not BotSystem.isRegistered(playerId) then
		return false, "Bot is not registered. Use /bot add first."
	end

	local query = string.format("UPDATE `bot_players` SET `auto_spawn` = %d, `updated_at` = %d WHERE `player_id` = %d",
		boolToNumber(enabled), os.time(), playerId)
	if not db.query(query) then
		return false, "Could not update bot auto-spawn."
	end
	return true, string.format("Bot '%s' auto-spawn %s.", playerName, enabled and "enabled" or "disabled")
end

function BotSystem.spawn(nameOrGuid, broadcast, requireMarked)
	if not BotSystem.isEnabled() then
		return false, "Bot system is disabled in config.lua (botSystemEnabled = false)."
	end

	local player, message = Game.spawnBot(nameOrGuid, broadcast == true, requireMarked ~= false)
	if player and BotBrain and BotBrain.activate then
		BotBrain.activate(player)
	end
	return player ~= nil, message, player
end

function BotSystem.despawn(nameOrGuid, save)
	return Game.despawnBot(nameOrGuid, save ~= false)
end

function BotSystem.setCast(nameOrGuid, enabled)
	if not BotSystem.isEnabled() then
		return false, "Bot system is disabled in config.lua (botSystemEnabled = false)."
	end

	return Game.setBotBroadcast(nameOrGuid, enabled == true)
end

function BotSystem.spawnAuto()
	if not BotSystem.isEnabled() then
		return 0
	end

	if not BotSystem.ensureTables() then
		return 0
	end

	local resultId = db.storeQuery(
		"SELECT p.`name` FROM `bot_players` bp INNER JOIN `players` p ON p.`id` = bp.`player_id` " ..
		"WHERE bp.`enabled` = 1 AND bp.`auto_spawn` = 1 ORDER BY p.`name` ASC"
	)
	if not resultId then
		return 0
	end

	local spawned = 0
	repeat
		local name = result.getString(resultId, "name")
		local ok = BotSystem.spawn(name, true, true)
		if ok then
			spawned = spawned + 1
		end
	until not result.next(resultId)
	result.free(resultId)
	return spawned
end

function BotSystem.getRegistered()
	if not BotSystem.ensureTables() then
		return {}
	end

	local resultId = db.storeQuery(
		"SELECT bp.`player_id`, bp.`enabled`, bp.`auto_spawn`, bp.`last_spawn`, bp.`last_despawn`, p.`name`, p.`level` " ..
		"FROM `bot_players` bp INNER JOIN `players` p ON p.`id` = bp.`player_id` ORDER BY p.`name` ASC"
	)
	if not resultId then
		return {}
	end

	local list = {}
	repeat
		list[#list + 1] = {
			playerId = result.getNumber(resultId, "player_id"),
			name = result.getString(resultId, "name"),
			level = result.getNumber(resultId, "level"),
			enabled = result.getNumber(resultId, "enabled") ~= 0,
			autoSpawn = result.getNumber(resultId, "auto_spawn") ~= 0,
			lastSpawn = result.getNumber(resultId, "last_spawn"),
			lastDespawn = result.getNumber(resultId, "last_despawn")
		}
	until not result.next(resultId)
	result.free(resultId)
	return list
end
