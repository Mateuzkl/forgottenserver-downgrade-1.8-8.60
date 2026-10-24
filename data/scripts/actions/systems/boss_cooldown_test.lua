-- Temporary BossLever used to verify Fonticak boss cooldown updates.
-- Add a lever item at Position(1000, 967, 7) in the map editor.

local TEST_LEVER_POSITION = Position(1000, 967, 7)
local TEST_PLAYER_POSITION = Position(999, 967, 7)
local TEST_ARENA_FROM = Position(1001, 968, 7)
local TEST_ARENA_TO = Position(1001, 969, 7)
local TEST_BOSS_POSITION = Position(1001, 968, 7)
local TEST_EXIT_POSITION = Position(999, 967, 7)

BossLever({
	boss = {
		name = "Black Knight",
		position = TEST_BOSS_POSITION,
	},
	playerPositions = {
		{ pos = TEST_PLAYER_POSITION, teleport = Position(1001, 969, 7) },
		{ pos = Position(1001, 967, 7), teleport = Position(1001, 969, 7) },
		{ pos = Position(1000, 966, 7), teleport = Position(1001, 969, 7) },
		{ pos = Position(1000, 968, 7), teleport = Position(1001, 969, 7) },
	},
	specPos = {
		from = TEST_ARENA_FROM,
		to = TEST_ARENA_TO,
	},
	exit = TEST_EXIT_POSITION,
	requiredLevel = 0,
	minPlayers = 1,
	timeToFightAgain = 180,
	timeToDefeat = 300,
	timeAfterKill = 20,
	onUseExtra = function(player)
		if player:getAccountType() < ACCOUNT_TYPE_GAMEMASTER then
			player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "This is an admin-only boss cooldown test.")
			return false
		end
		return true
	end,
}):position(TEST_LEVER_POSITION):register()
