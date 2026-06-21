-- Soul Seal Network Handler.
--
-- Soulseal data uses Task Board 0x53/subtype 0x03 and fight requests use
-- Task Board action 0x5F/19. Native Task Hunting exclusively owns 0xBA/0xBB.

if not configManager or not configManager.getBoolean
	or not configManager.getBoolean(configKeys.TASK_HUNTING_SYSTEM_ENABLED)
	or not configManager.getBoolean(configKeys.SOULSEALS_SYSTEM_ENABLED) then
	return
end

if not SoulPit then
	dofile("data/lib/others/soulpit.lua")
end

local protocol
local SoulSealHandler = {}

local function isNearSoulpitObelisk(player)
	local playerPosition = player and player:getPosition()
	local obeliskPosition = SoulPit and SoulPit.obeliskPos
	if not playerPosition or not obeliskPosition or playerPosition.z ~= obeliskPosition.z then
		return false
	end

	return math.abs(playerPosition.x - obeliskPosition.x) <= 1
		and math.abs(playerPosition.y - obeliskPosition.y) <= 1
end

local function isAstraPlayer(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

function SoulSealHandler.sendSoulSealsData(player)
	if not protocol or not player then
		return false
	end
	if not isAstraPlayer(player) then
		return false
	end
	if not isNearSoulpitObelisk(player) then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Stand next to the Soulpit obelisk to open Soulseals.")
		return false
	end

	local entries = SoulPit.buildSoulsealEntries()
	if #entries == 0 then
		return false
	end

	TaskBoard.sendResourceBalance(player, TaskBoard.Resources.SOULSEALS_POINTS)
	return protocol.sendSoulSealsData(player, entries, player:getSoulsealsPoints())
end

function SoulSealHandler.startFight(player, raceId)
	if not SoulPit or not isAstraPlayer(player) then
		return false
	end
	if not isNearSoulpitObelisk(player) then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Stand next to the Soulpit obelisk to start a fight.")
		return false
	end

	raceId = tonumber(raceId) or 0
	if raceId <= 0 then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Invalid creature selected.")
		return false
	end
	if not CustomBestiary or not CustomBestiary.getMonster then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Bestiary system is not available.")
		return false
	end

	local monster = CustomBestiary.getMonster(raceId)
	if not monster or not monster.name or monster.name == "" then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Unknown creature selected.")
		return false
	end

	local cost = SoulPit.getSoulsealCost(raceId)
	if not cost or cost <= 0 then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Cannot determine soulseal cost for this creature.")
		return false
	end
	local soulsealBalance = player:getSoulsealsPoints()
	if soulsealBalance < cost then
		player:sendTextMessage(MESSAGE_INFO_DESCR, string.format(
			"You need %d soulseal points to fight %s. You have %d.", cost, monster.name, soulsealBalance))
		return false
	end
	if not MonsterType(monster.name) then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "This creature does not exist: " .. monster.name)
		return false
	end
	if not player:removeSoulsealsPoints(cost) then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "Failed to deduct soulseal points.")
		return false
	end

	if protocol and protocol.sendResourceBalance then
		protocol.sendResourceBalance(player, protocol.RESOURCE_SOULSEALS_POINTS, player:getSoulsealsPoints())
	end

	local ok, started, err = pcall(SoulPit.startEncounter, player, monster.name)
	if not ok then
		err = started
		started = false
	end
	if not started then
		player:addSoulsealsPoints(cost)
		if protocol and protocol.sendResourceBalance then
			protocol.sendResourceBalance(player, protocol.RESOURCE_SOULSEALS_POINTS, player:getSoulsealsPoints())
		end
		player:sendTextMessage(MESSAGE_INFO_DESCR, tostring(err or "Failed to start Soulpit encounter."))
		return false
	end

	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format(
		"Soulpit encounter started! Fighting %s for %d soulseal points.", monster.name, cost))
	return true
end

function SoulSealHandler.setProtocol(value)
	protocol = value
end

return SoulSealHandler
