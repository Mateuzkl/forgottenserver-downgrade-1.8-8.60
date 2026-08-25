local LEVER_AID = 8890
local EXIT_AID = 1004

local BOSS_NAME = "Gaz'Haragoth"
local BOSS_RACE_ID = 1003
local BOSS_POS = Position(1058, 955, 13)
local PLAYER_DEST = Position(1064, 955, 13)
local INSTANCE_FROM = Position(1045, 947, 13)
local INSTANCE_TO = Position(1068, 965, 13)
local ENCOUNTER_KEY = "boss-room:gaz-haragoth"

-- Boss Difficulty is a five-player feature. The old room exposed ten fields,
-- which did not fit the protocol and silently omitted half of the group.
local PLAYER_TILES = {
	Position(1091, 955, 13),
	Position(1092, 955, 13),
	Position(1093, 955, 13),
	Position(1094, 955, 13),
	Position(1095, 955, 13),
}

local COOLDOWN_TIME = 3600
local INSTANCE_TIMEOUT_MS = 30 * 60 * 1000
local instanceCounter = 30000
local activeInstances = {}

local function samePosition(left, right)
	return left.x == right.x and left.y == right.y and left.z == right.z
end

local function isInsideInstanceArea(position)
	return position.x >= INSTANCE_FROM.x and position.x <= INSTANCE_TO.x
		and position.y >= INSTANCE_FROM.y and position.y <= INSTANCE_TO.y
		and position.z >= INSTANCE_FROM.z and position.z <= INSTANCE_TO.z
end

local function getPlayersOnTiles()
	local players = {}
	for _, position in ipairs(PLAYER_TILES) do
		local tile = Tile(position)
		local creatures = tile and tile:getCreatures()
		if creatures then
			for _, creature in ipairs(creatures) do
				local player = creature:getPlayer()
				if player then
					players[#players + 1] = player
					break
				end
			end
		end
	end
	return players
end

local function getGuidSet(players)
	local guids = {}
	for _, player in ipairs(players) do
		guids[player:getGuid()] = true
	end
	return guids
end

local function validateBossEntry(leader, difficulty, expectedPlayers)
	local onField = false
	for _, position in ipairs(PLAYER_TILES) do
		if samePosition(leader:getPosition(), position) then
			onField = true
			break
		end
	end
	if not onField then
		leader:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Boss Room] Stand on a boss field before using the lever.")
		return nil
	end

	local players = getPlayersOnTiles()
	if #players == 0 or #players > 5 then
		leader:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Boss Room] This encounter requires one to five players.")
		return nil
	end

	local party = leader:getParty()
	if #players > 1 then
		if not party or not party:getLeader() or party:getLeader():getGuid() ~= leader:getGuid() then
			leader:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Boss Room] Only the party leader can configure the fight.")
			return nil
		end
		for _, player in ipairs(players) do
			if player:getParty() ~= party then
				leader:sendTextMessage(MESSAGE_EVENT_ORANGE,
					"[Boss Room] Every player on the fields must belong to the leader's party.")
				return nil
			end
		end
	elseif party and party:getLeader() and party:getLeader():getGuid() ~= leader:getGuid() then
		leader:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Boss Room] Only the party leader can configure the fight.")
		return nil
	end

	if expectedPlayers then
		if #players ~= #expectedPlayers then
			leader:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Boss Room] The group changed after difficulty selection.")
			return nil
		end
		local expectedGuids = getGuidSet(expectedPlayers)
		for _, player in ipairs(players) do
			if not expectedGuids[player:getGuid()] then
				leader:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Boss Room] The group changed after difficulty selection.")
				return nil
			end
		end
	end

	local now = os.time()
	for _, player in ipairs(players) do
		if player:getInstanceId() ~= 0 then
			leader:sendTextMessage(MESSAGE_EVENT_ORANGE, player:getName() .. " is already inside an instance.")
			return nil
		end
		if difficulty and difficulty > 0 then
			local cooldownEnd = player:getBossCooldown(BOSS_NAME)
			if cooldownEnd and cooldownEnd > now then
				local minutes = math.ceil((cooldownEnd - now) / 60)
				player:getPosition():sendMagicEffect(CONST_ME_TUTORIALARROW)
				leader:sendTextMessage(MESSAGE_EVENT_ORANGE,
					string.format("[Boss Room] %s must wait %d minute(s) before fighting again.", player:getName(), minutes))
				return nil
			end
		end
	end
	return players
end

local function getInstanceSpectators(instanceId)
	local area = Game.getInstanceArea(instanceId)
	if not area then
		return {}
	end
	local center = Position(math.floor((area.fromPos.x + area.toPos.x) / 2),
		math.floor((area.fromPos.y + area.toPos.y) / 2), area.fromPos.z)
	local rangeX = math.ceil((area.toPos.x - area.fromPos.x) / 2)
	local rangeY = math.ceil((area.toPos.y - area.fromPos.y) / 2)
	return Game.getSpectators(center, false, false, rangeX, rangeX, rangeY, rangeY)
end

local function closeInstance(instanceId, destination)
	if instanceId == 0 then
		return
	end
	local encounter = activeInstances[instanceId]
	if encounter and encounter.timeoutEvent then
		stopEvent(encounter.timeoutEvent)
		encounter.timeoutEvent = nil
	end

	local players = {}
	local monsters = {}
	for _, spectator in ipairs(getInstanceSpectators(instanceId)) do
		if spectator:getInstanceId() == instanceId then
			if spectator:isPlayer() then
				players[#players + 1] = spectator
			elseif spectator:isMonster() and not spectator:getMaster() then
				monsters[#monsters + 1] = spectator
			end
		end
	end
	for _, monster in ipairs(monsters) do
		monster:remove()
	end
	for _, player in ipairs(players) do
		local target = destination or player:getTown():getTemplePosition()
		player:setInstanceIdRaw(0)
		player:teleportTo(target)
	end
	activeInstances[instanceId] = nil
	Game.unregisterInstanceArea(instanceId)
end

local function startBossEncounter(leader, difficulty, expectedPlayers)
	local players = validateBossEntry(leader, difficulty, expectedPlayers)
	if not players then
		return false
	end

	difficulty = math.max(0, math.min(0xFFFF, math.floor(tonumber(difficulty) or 0)))
	instanceCounter = instanceCounter + 1
	local instanceId = instanceCounter
	Game.registerInstanceArea(instanceId, INSTANCE_FROM, INSTANCE_TO)

	local boss = Game.createMonster(BOSS_NAME, BOSS_POS, false, true, CONST_ME_TELEPORT, instanceId)
	if not boss then
		Game.unregisterInstanceArea(instanceId)
		leader:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Boss Room] The boss could not be created.")
		return false
	end
	if not boss.applyBossDifficulty or not boss:applyBossDifficulty(difficulty, BOSS_RACE_ID) then
		boss:remove()
		Game.unregisterInstanceArea(instanceId)
		leader:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Boss Room] The selected difficulty could not be applied.")
		return false
	end

	activeInstances[instanceId] = {
		difficulty = difficulty,
		raceId = BOSS_RACE_ID,
		players = getGuidSet(players),
	}
	activeInstances[instanceId].timeoutEvent = addEvent(function(expiredInstanceId)
		local encounter = activeInstances[expiredInstanceId]
		if not encounter then
			return
		end
		encounter.timeoutEvent = nil
		closeInstance(expiredInstanceId)
	end, INSTANCE_TIMEOUT_MS, instanceId)
	boss:registerEvent("BossRoomBossDeath")
	if BossDifficulty then
		BossDifficulty.closeEncounter(ENCOUNTER_KEY)
	end

	for _, player in ipairs(players) do
		player:setInstanceIdRaw(instanceId)
		local destination = player:getClosestFreePosition(PLAYER_DEST, false)
		if destination.x == 0 then
			destination = PLAYER_DEST
		end
		player:teleportTo(destination)
		player:sendTextMessage(MESSAGE_EVENT_ORANGE,
			string.format("[Boss Room] Difficulty %d started%s.", difficulty,
				difficulty == 0 and " in practice mode" or ""))
	end
	return true
end

local leverAction = Action()
function leverAction.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local players = validateBossEntry(player)
	if not players then
		return true
	end
	if not BossDifficulty then
		player:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Boss Room] Boss Difficulty is unavailable.")
		return true
	end
	for _, member in ipairs(players) do
		if not member.isUsingAstraClient or not member:isUsingAstraClient() then
			player:sendTextMessage(MESSAGE_EVENT_ORANGE,
				"[Boss Room] Every group member must use an AstraClient version with Boss Difficulty support.")
			return true
		end
	end

	BossDifficulty.openGroup(player, players, {
		raceId = BOSS_RACE_ID,
		minimum = 0,
		encounterKey = ENCOUNTER_KEY,
		onAction = function(leader, action, selectedDifficulty, context, sessionPlayers)
			if action == BossDifficulty.ACTION_START then
				return startBossEncounter(leader, selectedDifficulty, sessionPlayers)
			end
			return true
		end,
	})
	return true
end
leverAction:aid(LEVER_AID)
leverAction:register()

local function closeSelectionOnFieldChange(creature)
	local player = creature:getPlayer()
	if player and BossDifficulty then
		BossDifficulty.close(player)
	end
	return true
end

for _, position in ipairs(PLAYER_TILES) do
	local stepIn = MoveEvent()
	stepIn:type("stepin")
	stepIn:position(position)
	function stepIn.onStepIn(creature, item, position, fromPosition)
		return closeSelectionOnFieldChange(creature)
	end
	stepIn:register()

	local stepOut = MoveEvent()
	stepOut:type("stepout")
	stepOut:position(position)
	function stepOut.onStepOut(creature, item, position, fromPosition)
		return closeSelectionOnFieldChange(creature)
	end
	stepOut:register()
end

local exitMovement = MoveEvent()
function exitMovement.onStepIn(creature, item, position, fromPosition)
	local player = creature:getPlayer()
	if not player or player:getInstanceId() == 0 then
		return true
	end
	closeInstance(player:getInstanceId(), Position(1054, 1000, 7))
	return true
end
exitMovement:aid(EXIT_AID)
exitMovement:type("stepin")
exitMovement:register()

local bossDeathEvent = CreatureEvent("BossRoomBossDeath")
function bossDeathEvent.onDeath(creature, corpse, killer, mostDamageKiller, lastHitUnjustified, mostDamageUnjustified)
	local instanceId = creature:getInstanceId()
	local encounter = activeInstances[instanceId]
	if not encounter then
		return true
	end

	local winners = {}
	local now = os.time()
	for _, spectator in ipairs(getInstanceSpectators(instanceId)) do
		if spectator:isPlayer() and spectator:getInstanceId() == instanceId
			and encounter.players[spectator:getGuid()] then
			winners[#winners + 1] = spectator
			if encounter.difficulty > 0 then
				spectator:setBossCooldown(BOSS_NAME, now + COOLDOWN_TIME)
				spectator:sendTextMessage(MESSAGE_EVENT_ORANGE,
					string.format("[Boss Room] Victory at difficulty %d. Difficulties through %d are now unlocked.",
						encounter.difficulty, math.min(0xFFFF, encounter.difficulty + 3)))
			else
				spectator:sendTextMessage(MESSAGE_EVENT_ORANGE,
					"[Boss Room] Practice completed. No cooldown, loot, or Bosstiary progress was granted.")
			end
		end
	end
	if encounter.difficulty > 0 and #winners > 0 then
		BossDifficulty.recordVictory(winners, encounter.raceId, encounter.difficulty)
	end
	return true
end
bossDeathEvent:type("death")
bossDeathEvent:register()

local logoutEvent = CreatureEvent("BossRoomLogout")
function logoutEvent.onLogout(player)
	if BossDifficulty then
		BossDifficulty.close(player)
	end
	local instanceId = player:getInstanceId()
	if instanceId ~= 0 and isInsideInstanceArea(player:getPosition()) then
		closeInstance(instanceId)
	end
	return true
end
logoutEvent:type("logout")
logoutEvent:register()

local loginEvent = CreatureEvent("BossRoomLogin")
function loginEvent.onLogin(player)
	player:registerEvent("BossRoomLogout")
	if isInsideInstanceArea(player:getPosition()) then
		player:setInstanceIdRaw(0)
		player:teleportTo(player:getTown():getTemplePosition())
	end
	return true
end
loginEvent:type("login")
loginEvent:register()
