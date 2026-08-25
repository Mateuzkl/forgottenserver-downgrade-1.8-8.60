local OPCODE_REQUEST = 0xC2
local OPCODE_RESPONSE = 0x3E

local ACTION_START = 0
local ACTION_CANCEL = 1
local ACTION_SELECT = 2
local MAX_DIFFICULTY = 25

BossDifficulty = BossDifficulty or {}

local sessions = {}

local function clampDifficulty(value, minimum, maximum)
	value = math.floor(tonumber(value) or minimum)
	return math.max(minimum, math.min(maximum, value))
end

local function addStrings(message, values)
	local count = math.min(type(values) == "table" and #values or 0, 0xFF)
	message:addByte(count)
	for index = 1, count do
		message:addString(tostring(values[index] or ""))
	end
end

local function sendClose(player)
	local message = NetworkMessage(player)
	message:addByte(OPCODE_RESPONSE)
	message:addByte(1)
	return message:sendToPlayer(player)
end

local function sendUpdate(player, difficulty, negativeModifiers, positiveModifiers)
	local message = NetworkMessage(player)
	message:addByte(OPCODE_RESPONSE)
	message:addByte(2)
	message:addU16(difficulty)
	addStrings(message, negativeModifiers)
	addStrings(message, positiveModifiers)
	return message:sendToPlayer(player)
end

function BossDifficulty.open(player, options)
	if not player or not player.isUsingAstraClient or not player:isUsingAstraClient() or type(options) ~= "table" then
		return false
	end

	local minimum = clampDifficulty(options.minimum or 0, 0, MAX_DIFFICULTY)
	local maximum = clampDifficulty(options.maximum or MAX_DIFFICULTY, minimum, MAX_DIFFICULTY)
	local difficulty = clampDifficulty(options.difficulty or minimum, minimum, maximum)
	local banners = type(options.banners) == "table" and options.banners
		or type(options.players) == "table" and options.players
		or {}

	local session = {
		minimum = minimum,
		maximum = maximum,
		difficulty = difficulty,
		raceId = math.max(0, math.min(0xFFFF, math.floor(tonumber(options.raceId) or 0))),
		personalMaximum = clampDifficulty(options.personalMaximum or maximum, minimum, MAX_DIFFICULTY),
		badLuck = math.max(0, math.min(0xFFFFFFFF, math.floor(tonumber(options.badLuck) or 0))),
		banners = banners,
		negativeModifiers = options.negativeModifiers or {},
		positiveModifiers = options.positiveModifiers or {},
		spinnerEnabled = options.spinnerEnabled ~= false,
		onAction = options.onAction,
	}
	sessions[player:getGuid()] = session

	local message = NetworkMessage(player)
	message:addByte(OPCODE_RESPONSE)
	message:addByte(0)
	message:addU32(session.minimum)
	message:addByte(session.spinnerEnabled and 0 or 1)
	message:addU16(session.raceId)
	message:addU16(session.difficulty)
	message:addU16(session.maximum)
	message:addU16(session.personalMaximum)
	message:addU32(session.badLuck)
	for index = 1, 5 do
		message:addString(tostring(session.banners[index] or ""))
	end
	message:addU16(session.difficulty)
	addStrings(message, session.negativeModifiers)
	addStrings(message, session.positiveModifiers)
	return message:sendToPlayer(player)
end

function BossDifficulty.update(player, difficulty, negativeModifiers, positiveModifiers)
	if not player then
		return false
	end

	local session = sessions[player:getGuid()]
	if not session then
		return false
	end

	session.difficulty = clampDifficulty(difficulty, session.minimum, session.maximum)
	session.negativeModifiers = negativeModifiers or session.negativeModifiers
	session.positiveModifiers = positiveModifiers or session.positiveModifiers
	return sendUpdate(player, session.difficulty, session.negativeModifiers, session.positiveModifiers)
end

function BossDifficulty.close(player)
	if not player then
		return false
	end

	sessions[player:getGuid()] = nil
	return sendClose(player)
end

local requestHandler = PacketHandler(OPCODE_REQUEST)

function requestHandler.onReceive(player, message)
	if not player or not player.isUsingAstraClient or not player:isUsingAstraClient()
		or message:len() - message:tell() < 5 then
		return
	end

	local context = message:getU32()
	local action = message:getByte()
	local session = sessions[player:getGuid()]
	if not session then
		return
	end

	local difficulty = session.difficulty
	if (action == ACTION_START or action == ACTION_SELECT) and message:len() - message:tell() >= 2 then
		difficulty = clampDifficulty(message:getU16(), session.minimum, session.maximum)
	end

	if action == ACTION_SELECT then
		session.difficulty = difficulty
		local negativeModifiers = session.negativeModifiers
		local positiveModifiers = session.positiveModifiers
		if type(session.onAction) == "function" then
			local accepted, negative, positive = session.onAction(player, action, difficulty, context)
			if accepted == false then
				return
			end
			negativeModifiers = negative or negativeModifiers
			positiveModifiers = positive or positiveModifiers
		end
		BossDifficulty.update(player, difficulty, negativeModifiers, positiveModifiers)
		return
	end

	local accepted = true
	if type(session.onAction) == "function" then
		accepted = session.onAction(player, action, difficulty, context) ~= false
	end

	if accepted and (action == ACTION_START or action == ACTION_CANCEL) then
		BossDifficulty.close(player)
	end
end

requestHandler:register()

local logout = CreatureEvent("BossDifficultyLogout")

function logout.onLogout(player)
	sessions[player:getGuid()] = nil
	return true
end

logout:register()

local testCommand = TalkAction("/bossdiff")

function testCommand.onSay(player, words, param)
	local arguments = {}
	for value in tostring(param or ""):gmatch("%d+") do
		arguments[#arguments + 1] = tonumber(value)
	end

	local raceId = arguments[1] or 100
	local difficulty = arguments[2] or 5
	if #arguments == 1 and arguments[1] <= MAX_DIFFICULTY then
		-- Convenient shorthand: /bossdiff 2 selects difficulty 2 on the
		-- reference race. The original /bossdiff <raceId> <difficulty> form
		-- remains available when two arguments are supplied.
		raceId = 100
		difficulty = arguments[1]
	end

	return BossDifficulty.open(player, {
		raceId = raceId,
		difficulty = difficulty,
		minimum = 0,
		maximum = 25,
		personalMaximum = 5,
		badLuck = 20, -- per-mille: displayed as 2%
		banners = { "Yvara", "Gryllan", "Frost Walker", "Drift Reaper", "Eradrel" },
		negativeModifiers = {
			"All characters and their summons receive 40% more damage.",
			"Hit points of monsters increased by 20%.",
		},
		positiveModifiers = {
			"8% more loot.",
			"Radiant attire scraps added to loot pool.",
		},
	})
end

testCommand:separator(" ")
testCommand:accountType(6)
testCommand:access(true)
testCommand:register()
