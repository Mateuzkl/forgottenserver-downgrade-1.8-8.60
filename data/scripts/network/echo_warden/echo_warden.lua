-- Echo Warden (Summer Update 2026), adapted to the Astra/TFS 8.60 stack.
-- The official 15.x portal sprite is not part of the current OTB, so a magic
-- field with a private action id is used without affecting ordinary fields.

EchoWarden = EchoWarden or {}

local PORTAL_ITEM_ID = 1387
local PORTAL_ACTION_ID = 54133
local PORTAL_ATTRIBUTE = "echoWardenKind"
local PORTAL_DELAY_MS = 30000
local PORTAL_TTL_MS = 120000
local SPAWN_CHANCE_NUMERATOR = 100
local SPAWN_CHANCE_DENOMINATOR = 200000
local WARDEN_HEALTH_MULTIPLIER = 4.0
local WARDEN_ATTACK_MULTIPLIER = 1.5
local MINION_HEALTH_MULTIPLIER = 1.5
local MINION_COUNT_MIN = 7
local MINION_COUNT_MAX = 12
local SPAWN_STEP_MS = 400
local AURA_RANGE = 5
local AURA_INTERVAL_MS = 2000
local STORAGE_IS_WARDEN = 54133
local STORAGE_IS_ECHO_SPAWN = 54134
local STORAGE_IS_MINION = 54135
local FIRST_KILL_STORAGE_BASE = 1000000
local CLIENT_EVENT_OPCODE = 0x75
local CLIENT_EVENT_ECHO_WARDEN = 14
local ECHOES_BY_STARS = { [0] = 1, [1] = 10, [2] = 15, [3] = 20, [4] = 25, [5] = 30 }

local activeWardens = {}

local function findBestiaryEntryByName(name)
	if not CustomBestiary or not CustomBestiary.monstersByRaceId then
		return nil
	end

	local needle = tostring(name or ""):lower()
	for _, entry in pairs(CustomBestiary.monstersByRaceId) do
		if tostring(entry.name or ""):lower() == needle then
			return entry
		end
	end
	return nil
end

local function sendRewardBanner(player, raceId, amount)
	if not player.isUsingAstraClient or not player:isUsingAstraClient() then
		return
	end

	local message = NetworkMessage(player)
	message:addByte(CLIENT_EVENT_OPCODE)
	message:addByte(CLIENT_EVENT_ECHO_WARDEN)
	message:addU16(math.max(0, math.min(0xFFFF, raceId or 0)))
	message:addU32(math.max(0, amount or 0))
	message:sendToPlayer(player)
end

local function findPortal(position)
	local tile = Tile(position)
	if not tile then
		return nil
	end

	for _, item in ipairs(tile:getItems() or {}) do
		if item:getId() == PORTAL_ITEM_ID and item:getActionId() == PORTAL_ACTION_ID then
			return item
		end
	end
	return nil
end

local function pickSpawnPosition(center, radius)
	radius = radius or 3
	for _ = 1, 12 do
		local position = Position(center.x + math.random(-radius, radius), center.y + math.random(-radius, radius), center.z)
		local tile = Tile(position)
		if tile and tile:getGround() and not tile:hasFlag(TILESTATE_BLOCKSOLID)
			and not tile:hasFlag(TILESTATE_PROTECTIONZONE) and not tile:hasFlag(TILESTATE_FLOORCHANGE)
			and not tile:hasFlag(TILESTATE_TELEPORT) then
			return position
		end
	end
	return center
end

local function makeMinion(monster)
	if not monster or monster:getStorageValue(STORAGE_IS_MINION, 0) == 1 then
		return
	end

	monster:setStorageValue(STORAGE_IS_MINION, 1)
	monster:setStorageValue(STORAGE_IS_ECHO_SPAWN, 1)
	monster:setMaxHealth(math.max(1, math.floor(monster:getMaxHealth() * MINION_HEALTH_MULTIPLIER)))
	monster:setHealth(monster:getMaxHealth())
	monster:setIcon("echo_minion", CreatureIconCategory_Modifications, CreatureIconModifications_Influenced, 0)
end

local function makeWarden(monster, kindName)
	if not monster or not monster:applyEchoWarden(WARDEN_HEALTH_MULTIPLIER, WARDEN_ATTACK_MULTIPLIER) then
		return false
	end

	monster:setStorageValue(STORAGE_IS_WARDEN, 1)
	monster:setStorageValue(STORAGE_IS_ECHO_SPAWN, 1)
	activeWardens[monster:getId()] = kindName
	return true
end

local function refreshWardenAura(wardenId, kindName)
	local warden = Monster(wardenId)
	if not warden or warden:isRemoved() or warden:getHealth() <= 0 then
		activeWardens[wardenId] = nil
		return
	end

	for _, spectator in ipairs(Game.getSpectators(warden:getPosition(), false, false,
		AURA_RANGE, AURA_RANGE, AURA_RANGE, AURA_RANGE) or {}) do
		local monster = spectator:getMonster()
		if monster and monster:getId() ~= wardenId and monster:getName():lower() == kindName:lower()
			and monster:getStorageValue(STORAGE_IS_WARDEN, 0) ~= 1 then
			makeMinion(monster)
		end
	end

	addEvent(refreshWardenAura, AURA_INTERVAL_MS, wardenId, kindName)
end

local function spawnMinions(kindName, center, remaining)
	if remaining <= 0 then
		return
	end

	local monster = Game.createMonster(kindName, pickSpawnPosition(center, 3), false, true, CONST_ME_NONE)
	if monster then
		makeMinion(monster)
	end

	addEvent(spawnMinions, SPAWN_STEP_MS, kindName, center, remaining - 1)
end

function EchoWarden.runRaid(kindName, center)
	if type(kindName) ~= "string" or kindName == "" then
		return false
	end

	local warden = Game.createMonster(kindName, center, false, true, CONST_ME_NONE)
	if not makeWarden(warden, kindName) then
		if warden then
			warden:remove()
		end
		return false
	end

	addEvent(refreshWardenAura, AURA_INTERVAL_MS, warden:getId(), kindName)
	spawnMinions(kindName, center, math.random(MINION_COUNT_MIN, MINION_COUNT_MAX))
	return true
end

function EchoWarden.spawnPortal(x, y, z, kindName)
	local position = Position(x, y, z)
	local tile = Tile(position)
	if not tile or not tile:getGround() or findPortal(position) then
		return
	end

	local portal = Game.createItem(PORTAL_ITEM_ID, 1, position)
	if not portal then
		return
	end

	portal:setActionId(PORTAL_ACTION_ID)
	portal:setCustomAttribute(PORTAL_ATTRIBUTE, kindName)
	position:sendMagicEffect(CONST_ME_TELEPORT)

	addEvent(function(px, py, pz)
		local expired = findPortal(Position(px, py, pz))
		if expired then
			expired:remove()
		end
	end, PORTAL_TTL_MS, x, y, z)
end

local function damagedPlayers(creature)
	local players = {}
	local seen = {}
	for creatureId in pairs(creature:getDamageMap() or {}) do
		local attacker = Creature(creatureId)
		if attacker then
			local player = attacker:isPlayer() and attacker:getPlayer() or nil
			local master = not player and attacker:getMaster() or nil
			if master and master:isPlayer() then
				player = master:getPlayer()
			end
			if player and not seen[player:getId()] then
				seen[player:getId()] = true
				players[#players + 1] = player
			end
		end
	end
	return players
end

local function grantFirstKillReward(player, kindName)
	local entry = findBestiaryEntryByName(kindName)
	if not entry or not entry.raceId then
		return false
	end

	local storage = FIRST_KILL_STORAGE_BASE + entry.raceId
	if player:getStorageValue(storage, 0) == 1 then
		return false
	end

	local amount = ECHOES_BY_STARS[entry.stars or 0] or ECHOES_BY_STARS[0]
	if not player:addMinorCharmEchoes(amount) then
		return false
	end

	player:setStorageValue(storage, 1)
	sendRewardBanner(player, entry.raceId, amount)
	return true
end

local deathEvent = CreatureEvent("EchoWardenDeath")

function deathEvent.onDeath(creature)
	if not creature or not creature:isMonster() then
		return true
	end

	if creature:getStorageValue(STORAGE_IS_WARDEN, 0) == 1 then
		local kindName = activeWardens[creature:getId()] or creature:getName()
		activeWardens[creature:getId()] = nil
		for _, player in ipairs(damagedPlayers(creature)) do
			grantFirstKillReward(player, kindName)
		end
		return true
	end

	if creature:getStorageValue(STORAGE_IS_ECHO_SPAWN, 0) == 1 or creature:getMaster() then
		return true
	end

	local monsterType = creature:getType()
	if not monsterType or monsterType:isBoss() or monsterType:isRewardBoss() then
		return true
	end

	local entry = findBestiaryEntryByName(creature:getName())
	if not entry or (entry.stars ~= 1 and entry.stars ~= 2) then
		return true
	end

	if math.random(1, SPAWN_CHANCE_DENOMINATOR) <= SPAWN_CHANCE_NUMERATOR then
		local position = creature:getPosition()
		addEvent(EchoWarden.spawnPortal, PORTAL_DELAY_MS, position.x, position.y, position.z, entry.name)
	end
	return true
end

deathEvent:register()

local portalEvent = MoveEvent()

function portalEvent.onStepIn(creature, item, position)
	local player = creature and creature:getPlayer()
	if not player then
		return true
	end

	local kindName = item:getCustomAttribute(PORTAL_ATTRIBUTE)
	item:remove()
	if type(kindName) == "string" and kindName ~= "" then
		position:sendMagicEffect(CONST_ME_AGONY)
		EchoWarden.runRaid(kindName, Position(position.x, position.y, position.z))
	end
	return true
end

portalEvent:type("stepin")
portalEvent:aid(PORTAL_ACTION_ID)
portalEvent:register()

local startupEvent = GlobalEvent("EchoWardenStartup")

function startupEvent.onStartup()
	local registered = 0
	for stars = 1, 2 do
		for _, entry in ipairs(Game.getMonstersByBestiaryStars(stars) or {}) do
			local monsterType = MonsterType(entry.name)
			if monsterType and monsterType:registerEvent("EchoWardenDeath") then
				registered = registered + 1
			end
		end
	end
	print(string.format("[EchoWarden] Registered %d common/uncommon monster types.", registered))
	return true
end

startupEvent:register()
