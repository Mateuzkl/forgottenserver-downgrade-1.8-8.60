BotBrain = BotBrain or {}

BotBrain.config = {
	tickInterval = 500,
	idleMoveInterval = 850,
	combatMoveInterval = 700,
	searchRangeX = 7,
	searchRangeY = 5,
	wanderRadius = 5
}

BotBrain.state = BotBrain.state or {}
BotBrain.running = BotBrain.running or false

local cardinalDirections = {
	DIRECTION_NORTH,
	DIRECTION_EAST,
	DIRECTION_SOUTH,
	DIRECTION_WEST
}

local starterEquipment = {
	common = {
		{ slot = CONST_SLOT_BACKPACK, itemId = 2854 },
		{ slot = CONST_SLOT_ARMOR, itemId = 3359 },
		{ slot = CONST_SLOT_LEGS, itemId = 3372 },
		{ slot = CONST_SLOT_FEET, itemId = 3552 }
	},
	vocations = {
		[1] = {
			{ slot = CONST_SLOT_LEFT, itemId = 3074 }
		},
		[2] = {
			{ slot = CONST_SLOT_LEFT, itemId = 3066 }
		},
		[3] = {
			{ slot = CONST_SLOT_LEFT, itemId = 3277 },
			{ slot = CONST_SLOT_RIGHT, itemId = 3411 }
		},
		[4] = {
			{ slot = CONST_SLOT_LEFT, itemId = 3271 },
			{ slot = CONST_SLOT_RIGHT, itemId = 3411 }
		}
	}
}

local function logWarning(message)
	if logger and logger.warn then
		logger.warn(message)
	else
		logInfo(message)
	end
end

local function distance(a, b)
	if not a or not b or a.z ~= b.z then
		return 999
	end
	return math.max(math.abs(a.x - b.x), math.abs(a.y - b.y))
end

local function vocationBase(player)
	local vocation = player:getVocation()
	if not vocation or not vocation.getId then
		return 4
	end

	local id = vocation:getId()
	if id > 4 then
		id = id - 4
	end
	if id < 1 or id > 4 then
		return 4
	end
	return id
end

local function addToSlot(player, slot, itemId, count)
	if player:getSlotItem(slot) then
		return true
	end

	local item = player:addItem(itemId, count or 1, false, count or 1, slot)
	return item ~= nil
end

local function configureBot(player)
	local guid = player:getGuid()
	local state = BotBrain.state[guid]
	if not state then
		state = {}
		BotBrain.state[guid] = state
	end

	if state.configured then
		return state
	end

	if player.setDropLoot then
		player:setDropLoot(false)
	end
	if player.setSkillLoss then
		player:setSkillLoss(false)
	end
	if player.setFightMode then
		player:setFightMode(FIGHTMODE_ATTACK, false, true)
	end

	for _, entry in ipairs(starterEquipment.common) do
		addToSlot(player, entry.slot, entry.itemId, entry.count)
	end
	for _, entry in ipairs(starterEquipment.vocations[vocationBase(player)] or starterEquipment.vocations[4]) do
		addToSlot(player, entry.slot, entry.itemId, entry.count)
	end

	local pos = player:getPosition()
	state.home = { x = pos.x, y = pos.y, z = pos.z }
	state.nextMoveAt = 0
	state.nextSayAt = os.time() + math.random(40, 120)
	state.configured = true
	return state
end

local function healIfNeeded(player)
	local missingHealth = player:getMaxHealth() - player:getHealth()
	if missingHealth > 0 and player:getHealth() * 100 <= player:getMaxHealth() * 65 then
		player:addHealth(math.min(missingHealth, math.max(25, math.floor(player:getMaxHealth() * 0.15))))
	end

	if player.getMana and player.getMaxMana and player.addMana then
		local maxMana = player:getMaxMana()
		local missingMana = maxMana - player:getMana()
		if missingMana > 0 and player:getMana() * 100 <= maxMana * 50 then
			player:addMana(math.min(missingMana, math.max(20, math.floor(maxMana * 0.20))))
		end
	end
end

local function validMonster(creature)
	return creature and not creature:isRemoved() and creature.isMonster and creature:isMonster() and creature:getHealth() > 0
end

local function findTarget(player)
	local position = player:getPosition()
	local spectators = Game.getSpectators(position, false, false, BotBrain.config.searchRangeX, BotBrain.config.searchRangeX, BotBrain.config.searchRangeY, BotBrain.config.searchRangeY)
	local closest
	local closestDistance = 999

	for _, creature in ipairs(spectators) do
		if validMonster(creature) and player:canSeeCreature(creature) then
			local creatureDistance = distance(position, creature:getPosition())
			if creatureDistance < closestDistance then
				closest = creature
				closestDistance = creatureDistance
			end
		end
	end
	return closest, closestDistance
end

local function walkPath(player, targetPos, minDistance, maxDistance)
	local path = player:getPathTo(targetPos, minDistance or 0, maxDistance or 1, true, true, 12)
	if not path or #path == 0 then
		return false
	end

	local ret = player:move(path[1])
	return ret == RETURNVALUE_NOERROR or ret == true or ret == 0
end

local function randomNearbyPosition(pos, radius)
	local dx = math.random(-radius, radius)
	local dy = math.random(-radius, radius)
	if dx == 0 and dy == 0 then
		local direction = cardinalDirections[math.random(#cardinalDirections)]
		if direction == DIRECTION_NORTH then
			dy = -1
		elseif direction == DIRECTION_SOUTH then
			dy = 1
		elseif direction == DIRECTION_EAST then
			dx = 1
		else
			dx = -1
		end
	end
	return Position(pos.x + dx, pos.y + dy, pos.z)
end

local function wander(player, state, nowMs)
	if nowMs < (state.nextMoveAt or 0) then
		return
	end
	state.nextMoveAt = nowMs + BotBrain.config.idleMoveInterval + math.random(0, 300)

	local pos = player:getPosition()
	local target = randomNearbyPosition(state.home or pos, BotBrain.config.wanderRadius)
	walkPath(player, target, 0, 1)
end

local function handleCombat(player, state, nowMs)
	local target = player:getTarget()
	if not validMonster(target) then
		target = findTarget(player)
	end

	if not target then
		player:setTarget(nil)
		player:setFollowCreature(nil)
		return false
	end

	player:setTarget(target)

	if nowMs >= (state.nextMoveAt or 0) and distance(player:getPosition(), target:getPosition()) > 1 then
		state.nextMoveAt = nowMs + BotBrain.config.combatMoveInterval
		walkPath(player, target:getPosition(), 1, 1)
	end
	return true
end

local function maybeSay(player, state)
	if os.time() < (state.nextSayAt or 0) then
		return
	end

	state.nextSayAt = os.time() + math.random(90, 240)
	local phrases = {
		"hi",
		"hunt?",
		"need cap",
		"refill soon",
		"exura"
	}
	player:say(phrases[math.random(#phrases)], TALKTYPE_SAY)
end

function BotBrain.activate(player)
	if not player or player:isRemoved() or not player:isBot() then
		return false
	end

	configureBot(player)
	BotBrain.start()
	return true
end

function BotBrain.run()
	local bots = Game.getBots()
	local nowMs = os.mtime and os.mtime() or (os.time() * 1000)

	for _, player in ipairs(bots) do
		if player and not player:isRemoved() and player:isBot() then
			local state = configureBot(player)
			healIfNeeded(player)
			if not handleCombat(player, state, nowMs) then
				wander(player, state, nowMs)
				maybeSay(player, state)
			end
		end
	end
end

function BotBrain.tick()
	if not BotBrain.running then
		return
	end

	if BotSystem and not BotSystem.isEnabled() then
		BotBrain.running = false
		return
	end

	local ok, err = pcall(BotBrain.run)
	if not ok then
		logWarning("[BotBrain] tick failed: " .. tostring(err))
	end

	addEvent(function()
		BotBrain.tick()
	end, BotBrain.config.tickInterval)
end

function BotBrain.start()
	if BotBrain.running then
		return
	end
	if BotSystem and not BotSystem.isEnabled() then
		return
	end

	BotBrain.running = true
	addEvent(function()
		BotBrain.tick()
	end, BotBrain.config.tickInterval)
end

function BotBrain.stop()
	BotBrain.running = false
end
