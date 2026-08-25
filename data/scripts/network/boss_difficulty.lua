local OPCODE_REQUEST = 0xC2
local OPCODE_RESPONSE = 0x3E

local ACTION_START = 0
local ACTION_CANCEL = 1
local ACTION_SELECT = 2
local PROTOCOL_MAX_DIFFICULTY = 0xFFFF

BossDifficulty = BossDifficulty or {}
BossDifficulty.ACTION_START = ACTION_START
BossDifficulty.ACTION_CANCEL = ACTION_CANCEL
BossDifficulty.ACTION_SELECT = ACTION_SELECT

local sessionsByPlayer = {}
local groups = {}
local nextGroupId = 0

local function clampDifficulty(value, minimum, maximum)
	value = math.floor(tonumber(value) or minimum)
	return math.max(minimum, math.min(maximum, value))
end

local function formatPercent(value)
	if value == math.floor(value) then
		return tostring(math.floor(value))
	end
	return string.format("%.1f", value)
end

local function getPlayerByGuid(guid)
	return Player(guid)
end

function BossDifficulty.getModifiers(difficulty)
	difficulty = clampDifficulty(difficulty, 0, PROTOCOL_MAX_DIFFICULTY)
	if difficulty == 0 then
		return {
			"Practice difficulty grants no loot or Bosstiary progress.",
		}, {
			"Boss damage is reduced by 50% for practice.",
		}
	end

	return {
		string.format("All characters and their summons receive %d%% more damage.", difficulty * 8),
		string.format("Hit points of the boss increased by %d%%.", difficulty * 4),
	}, {
		string.format("%s%% more loot.", formatPercent(difficulty * 1.6)),
		"Moonsilver bad luck increases after a victory without Moonsilver loot.",
	}
end

function BossDifficulty.getProgress(player, raceId)
	local progress = {
		unlockedDifficulty = 1,
		highestDefeated = 0,
		selectedDifficulty = 1,
		badLuck = 0,
	}
	if not player or raceId <= 0 then
		return progress
	end

	local query = string.format(
		"SELECT `unlocked_difficulty`, `highest_defeated`, `selected_difficulty`, `bad_luck` " ..
		"FROM `player_boss_difficulty` WHERE `player_id` = %d AND `boss_race_id` = %d",
		player:getGuid(), raceId)
	local resultId = db.storeQuery(query)
	if resultId == false then
		return progress
	end

	progress.unlockedDifficulty = clampDifficulty(result.getDataInt(resultId, "unlocked_difficulty"), 1,
		PROTOCOL_MAX_DIFFICULTY)
	progress.highestDefeated = math.max(0, result.getDataInt(resultId, "highest_defeated"))
	progress.selectedDifficulty = clampDifficulty(result.getDataInt(resultId, "selected_difficulty"), 0,
		progress.unlockedDifficulty)
	progress.badLuck = math.max(0, result.getDataInt(resultId, "bad_luck"))
	result.free(resultId)
	return progress
end

function BossDifficulty.saveSelected(players, raceId, difficulty)
	if type(players) ~= "table" or #players == 0 or raceId <= 0 then
		return false
	end

	local values = {}
	for _, player in ipairs(players) do
		values[#values + 1] = string.format("(%d, %d, 1, 0, %d, 0)", player:getGuid(), raceId, difficulty)
	end
	db.asyncQuery("INSERT INTO `player_boss_difficulty` " ..
		"(`player_id`, `boss_race_id`, `unlocked_difficulty`, `highest_defeated`, `selected_difficulty`, `bad_luck`) VALUES " ..
		table.concat(values, ",") .. " ON DUPLICATE KEY UPDATE `selected_difficulty` = VALUES(`selected_difficulty`)")
	return true
end

function BossDifficulty.recordVictory(players, raceId, difficulty)
	difficulty = clampDifficulty(difficulty, 0, PROTOCOL_MAX_DIFFICULTY)
	if difficulty == 0 or type(players) ~= "table" or #players == 0 or raceId <= 0 then
		return false
	end

	local unlocked = math.min(PROTOCOL_MAX_DIFFICULTY, difficulty + 3)
	local values = {}
	for _, player in ipairs(players) do
		values[#values + 1] = string.format("(%d, %d, %d, %d, %d, 0)", player:getGuid(), raceId, unlocked,
			difficulty, difficulty)
	end
	db.query("INSERT INTO `player_boss_difficulty` " ..
		"(`player_id`, `boss_race_id`, `unlocked_difficulty`, `highest_defeated`, `selected_difficulty`, `bad_luck`) VALUES " ..
		table.concat(values, ",") .. " ON DUPLICATE KEY UPDATE " ..
		"`unlocked_difficulty` = GREATEST(`unlocked_difficulty`, VALUES(`unlocked_difficulty`)), " ..
		"`highest_defeated` = GREATEST(`highest_defeated`, VALUES(`highest_defeated`)), " ..
		"`selected_difficulty` = LEAST(`unlocked_difficulty`, VALUES(`selected_difficulty`))")
	return true
end

local function addStrings(message, values)
	local count = math.min(type(values) == "table" and #values or 0, 0xFF)
	message:addByte(count)
	for index = 1, count do
		message:addString(tostring(values[index] or ""))
	end
end

local function sendClose(player)
	if not player then
		return false
	end
	local message = NetworkMessage(player)
	message:addByte(OPCODE_RESPONSE)
	message:addByte(1)
	return message:sendToPlayer(player)
end

local function sendUpdate(player, difficulty, negativeModifiers, positiveModifiers)
	if not player then
		return false
	end
	local message = NetworkMessage(player)
	message:addByte(OPCODE_RESPONSE)
	message:addByte(2)
	message:addU16(difficulty)
	addStrings(message, negativeModifiers)
	addStrings(message, positiveModifiers)
	return message:sendToPlayer(player)
end

local function closeGroup(group, sendPacket)
	if not group or group.closed then
		return false
	end
	group.closed = true
	groups[group.id] = nil
	for _, participant in ipairs(group.participants) do
		if sessionsByPlayer[participant.guid] == group then
			sessionsByPlayer[participant.guid] = nil
		end
		if sendPacket then
			sendClose(getPlayerByGuid(participant.guid))
		end
	end
	return true
end

local function getLivePlayers(group)
	local players = {}
	for _, participant in ipairs(group.participants) do
		local player = getPlayerByGuid(participant.guid)
		if player then
			players[#players + 1] = player
		end
	end
	return players
end

local function broadcastUpdate(group)
	local negative, positive = BossDifficulty.getModifiers(group.difficulty)
	group.negativeModifiers = negative
	group.positiveModifiers = positive
	for _, player in ipairs(getLivePlayers(group)) do
		sendUpdate(player, group.difficulty, negative, positive)
	end
end

local function sendOpen(group, participant)
	local player = getPlayerByGuid(participant.guid)
	if not player then
		return false
	end

	local message = NetworkMessage(player)
	message:addByte(OPCODE_RESPONSE)
	message:addByte(0)
	message:addU32(group.minimum)
	message:addByte(participant.guid == group.leaderGuid and 0 or 1)
	message:addU16(group.raceId)
	message:addU16(group.difficulty)
	message:addU16(group.maximum)
	message:addU16(participant.progress.unlockedDifficulty)
	message:addU32(participant.progress.badLuck)
	for index = 1, 5 do
		message:addString(group.playerNames[index] or "")
	end
	message:addU16(group.difficulty)
	addStrings(message, group.negativeModifiers)
	addStrings(message, group.positiveModifiers)
	return message:sendToPlayer(player)
end

function BossDifficulty.openGroup(leader, players, options)
	if not leader or type(players) ~= "table" or type(options) ~= "table" or #players == 0 or #players > 5 then
		return false
	end
	if not leader.isUsingAstraClient or not leader:isUsingAstraClient() then
		return false
	end

	local ordered = { leader }
	local seen = { [leader:getGuid()] = true }
	for _, player in ipairs(players) do
		if player and not seen[player:getGuid()] then
			if not player.isUsingAstraClient or not player:isUsingAstraClient() then
				return false
			end
			seen[player:getGuid()] = true
			ordered[#ordered + 1] = player
		end
	end
	if #ordered ~= #players then
		return false
	end

	for _, player in ipairs(ordered) do
		local oldGroup = sessionsByPlayer[player:getGuid()]
		if oldGroup then
			closeGroup(oldGroup, true)
		end
	end

	local raceId = clampDifficulty(options.raceId or 0, 0, 0xFFFF)
	local minimum = clampDifficulty(options.minimum or 0, 0, PROTOCOL_MAX_DIFFICULTY)
	local participants = {}
	local playerNames = {}
	local maximum = PROTOCOL_MAX_DIFFICULTY
	for _, player in ipairs(ordered) do
		local progress = BossDifficulty.getProgress(player, raceId)
		if options.personalMaximum ~= nil then
			progress.unlockedDifficulty = clampDifficulty(options.personalMaximum, 1, PROTOCOL_MAX_DIFFICULTY)
		end
		maximum = math.min(maximum, progress.unlockedDifficulty)
		participants[#participants + 1] = {
			guid = player:getGuid(),
			progress = progress,
		}
		playerNames[#playerNames + 1] = player:getName()
	end
	maximum = math.max(minimum, maximum)
	local leaderProgress = participants[1].progress
	local difficulty = clampDifficulty(options.difficulty or leaderProgress.selectedDifficulty, minimum, maximum)
	local negative, positive = BossDifficulty.getModifiers(difficulty)

	nextGroupId = nextGroupId + 1
	local group = {
		id = nextGroupId,
		leaderGuid = leader:getGuid(),
		participants = participants,
		playerNames = playerNames,
		raceId = raceId,
		minimum = minimum,
		maximum = maximum,
		difficulty = difficulty,
		negativeModifiers = negative,
		positiveModifiers = positive,
		encounterKey = options.encounterKey,
		onAction = options.onAction,
		closed = false,
	}
	groups[group.id] = group
	for _, participant in ipairs(participants) do
		sessionsByPlayer[participant.guid] = group
		sendOpen(group, participant)
	end
	return true
end

function BossDifficulty.open(player, options)
	return BossDifficulty.openGroup(player, { player }, options)
end

function BossDifficulty.close(player)
	return player and closeGroup(sessionsByPlayer[player:getGuid()], true) or false
end

function BossDifficulty.closeEncounter(encounterKey)
	local pending = {}
	for _, group in pairs(groups) do
		if group.encounterKey == encounterKey then
			pending[#pending + 1] = group
		end
	end
	for _, group in ipairs(pending) do
		closeGroup(group, true)
	end
end

local requestHandler = PacketHandler(OPCODE_REQUEST)

function requestHandler.onReceive(player, message)
	if not player or not player.isUsingAstraClient or not player:isUsingAstraClient()
		or message:len() - message:tell() < 5 then
		return
	end

	local context = message:getU32()
	local action = message:getByte()
	local group = sessionsByPlayer[player:getGuid()]
	if not group or group.closed then
		return
	end

	if action == ACTION_CANCEL then
		closeGroup(group, true)
		return
	end
	if player:getGuid() ~= group.leaderGuid then
		return
	end

	local difficulty = group.difficulty
	if (action == ACTION_START or action == ACTION_SELECT) and message:len() - message:tell() >= 2 then
		difficulty = clampDifficulty(message:getU16(), group.minimum, group.maximum)
	end
	if action == ACTION_SELECT then
		group.difficulty = difficulty
		broadcastUpdate(group)
		return
	end
	if action ~= ACTION_START then
		return
	end

	local players = getLivePlayers(group)
	if #players ~= #group.participants then
		closeGroup(group, true)
		return
	end
	local accepted = type(group.onAction) ~= "function" or
		group.onAction(player, action, difficulty, context, players) ~= false
	if accepted then
		BossDifficulty.saveSelected(players, group.raceId, difficulty)
		closeGroup(group, true)
	end
end

requestHandler:register()

local logout = CreatureEvent("BossDifficultyLogout")
function logout.onLogout(player)
	local group = sessionsByPlayer[player:getGuid()]
	if group then
		closeGroup(group, true)
	end
	return true
end
logout:register()

local testCommand = TalkAction("/bossdiff")
function testCommand.onSay(player, words, param)
	local difficulty = clampDifficulty(tonumber(param) or 1, 0, PROTOCOL_MAX_DIFFICULTY)
	return BossDifficulty.open(player, {
		raceId = 1003,
		difficulty = difficulty,
		minimum = 0,
		personalMaximum = math.max(1, difficulty),
		onAction = function(sessionPlayer, action, selectedDifficulty)
			if action == ACTION_START then
				sessionPlayer:sendTextMessage(MESSAGE_EVENT_ADVANCE,
					string.format("Boss difficulty diagnostic selected %d.", selectedDifficulty))
			end
			return true
		end,
	})
end
testCommand:separator(" ")
testCommand:accountType(6)
testCommand:access(true)
testCommand:register()
