-- BotSystem: registry and spawn wrapper for the managed bot system.
-- Registration/spawn/despawn are implemented once, in C++ (BotManager); this
-- file intentionally has no SQL fallbacks for those paths so the logic cannot
-- drift. SQL remains only where it is the canonical implementation (registry
-- listing and per-bot flags). No engine global is read at file scope.
BotSystem = BotSystem or {}

local tablesReady = false

local function boolToNumber(value)
	return value and 1 or 0
end

local DISABLED_MESSAGE = "Bot system is disabled in config.lua (botSystemEnabled = false)."
local UNAVAILABLE_MESSAGE = "Bot system requires a server build with BotManager (Game.spawnBot missing)."

function BotSystem.isAvailable()
	return Game.spawnBot ~= nil and Game.registerBot ~= nil and Game.despawnBot ~= nil and
		Game.ensureBotTables ~= nil and Game.isBotSystemEnabled ~= nil
end

function BotSystem.isEnabled()
	return BotSystem.isAvailable() and Game.isBotSystemEnabled()
end

-- Returns false plus the reason when the system cannot act.
local function guard()
	if not BotSystem.isAvailable() then
		return false, UNAVAILABLE_MESSAGE
	end
	if not Game.isBotSystemEnabled() then
		return false, DISABLED_MESSAGE
	end
	return true
end

function BotSystem.ensureTables()
	if tablesReady then
		return true
	end

	local ok = guard()
	if not ok then
		return false
	end

	tablesReady = Game.ensureBotTables()
	return tablesReady
end

function BotSystem.getPlayerId(nameOrGuid)
	local value = BotCore.trim(nameOrGuid)
	if value == "" then
		return nil, nil
	end

	local numeric = tonumber(value)
	local query
	if numeric and numeric > 0 then
		query = "SELECT `id`, `name` FROM `players` WHERE `id` = " .. math.floor(numeric) .. " LIMIT 1"
	else
		query = "SELECT `id`, `name` FROM `players` WHERE LOWER(`name`) = LOWER(" .. db.escapeString(value) ..
			") LIMIT 1"
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

	local resultId = db.storeQuery(
		"SELECT `player_id` FROM `bot_players` WHERE `player_id` = " .. playerId .. " LIMIT 1")
	if not resultId then
		return false
	end

	result.free(resultId)
	return true
end

function BotSystem.register(nameOrGuid, autoSpawn, vocation)
	local ok, message = guard()
	if not ok then
		return false, message
	end

	if not BotSystem.ensureTables() then
		return false, "Could not create bot tables."
	end

	local normalized = BotCore.trim(nameOrGuid)
	if normalized == "" then
		return false, "Bot name is required."
	end

	-- Numeric input refers to an existing player id; resolve it to the name
	-- so the single C++ registration path handles both forms.
	if tonumber(normalized) then
		local playerId, playerName = BotSystem.getPlayerId(normalized)
		if not playerId then
			return false, "Player not found."
		end
		normalized = playerName
	end

	local registered, registerMessage = Game.registerBot(normalized, autoSpawn == true, true,
		BotCore.normalizeVocation(vocation), PLAYERSEX_MALE)
	return registered, registerMessage
end

function BotSystem.unregister(nameOrGuid)
	local ok, message = guard()
	if not ok then
		return false, message
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

local function setRegistryFlag(nameOrGuid, column, enabled, label)
	local ok, message = guard()
	if not ok then
		return false, message
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

	local query = string.format("UPDATE `bot_players` SET `%s` = %d, `updated_at` = %d WHERE `player_id` = %d",
		column, boolToNumber(enabled), os.time(), playerId)
	if not db.query(query) then
		return false, string.format("Could not update bot %s.", label)
	end
	return true, string.format("Bot '%s' %s %s.", playerName, label, enabled and "enabled" or "disabled")
end

function BotSystem.setEnabled(nameOrGuid, enabled)
	return setRegistryFlag(nameOrGuid, "enabled", enabled, "status")
end

function BotSystem.setAutoSpawn(nameOrGuid, enabled)
	return setRegistryFlag(nameOrGuid, "auto_spawn", enabled, "auto-spawn")
end

function BotSystem.spawn(nameOrGuid, broadcast, requireMarked)
	local ok, message = guard()
	if not ok then
		return false, message
	end

	local player, spawnMessage = Game.spawnBot(nameOrGuid, broadcast == true, requireMarked ~= false)
	if player and BotBrain and BotBrain.activate then
		BotBrain.activate(player)
	end
	return player ~= nil, spawnMessage, player
end

function BotSystem.despawn(nameOrGuid, save)
	if not BotSystem.isAvailable() then
		return false, UNAVAILABLE_MESSAGE
	end

	-- Despawn stays allowed while the system is disabled so operators can
	-- always clean up; only spawn/register are gated.
	if BotBrain and BotBrain.forget then
		local playerId = BotSystem.getPlayerId(nameOrGuid)
		if playerId then
			BotBrain.forget(playerId)
		end
	end
	return Game.despawnBot(nameOrGuid, save ~= false)
end

function BotSystem.setCast(nameOrGuid, enabled)
	local ok, message = guard()
	if not ok then
		return false, message
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
		"WHERE bp.`enabled` = 1 AND bp.`auto_spawn` = 1 ORDER BY p.`name` ASC")
	if not resultId then
		return 0
	end

	local spawned = 0
	repeat
		local name = result.getString(resultId, "name")
		-- Auto-spawned bots always open their cast; use "/bot cast <name>, off"
		-- to close it after spawn.
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
		"FROM `bot_players` bp INNER JOIN `players` p ON p.`id` = bp.`player_id` ORDER BY p.`name` ASC")
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
