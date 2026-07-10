-- BotBrain: engine adapter for the managed bot AI tick loop.
-- Decision logic lives in BotCore (data/scripts/lib/bot_core.lua); this file
-- only touches engine APIs, and never at file scope, so it loads under a bare
-- Lua interpreter regardless of lib load order.
BotBrain = BotBrain or {}

BotBrain.state = BotBrain.state or {}
BotBrain.running = BotBrain.running or false
BotBrain.eventId = BotBrain.eventId or nil

local slotMap

local function getConfig()
	BotBrain.config = BotCore.mergeDefaults(BotBrain.config, BotCore.defaults)
	return BotBrain.config
end

local function getSlotId(name)
	if not slotMap then
		slotMap = {
			backpack = CONST_SLOT_BACKPACK,
			armor = CONST_SLOT_ARMOR,
			legs = CONST_SLOT_LEGS,
			feet = CONST_SLOT_FEET,
			left = CONST_SLOT_LEFT,
			right = CONST_SLOT_RIGHT
		}
	end
	return slotMap[name]
end

local function logWarning(message)
	if logger and logger.warn then
		logger.warn(message)
	elseif logInfo then
		logInfo(message)
	else
		print(message)
	end
end

local function addToSlot(player, slot, itemId)
	if player:getSlotItem(slot) then
		return true
	end

	local item = player:addItem(itemId, 1, false, 1, slot)
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

	local vocation = player:getVocation()
	local vocationBase = BotCore.vocationBase(vocation and vocation.getId and vocation:getId() or 4)
	for _, entry in ipairs(BotCore.equipmentFor(vocationBase)) do
		local slot = getSlotId(entry.slot)
		if slot then
			addToSlot(player, slot, entry.itemId)
		end
	end

	local pos = player:getPosition()
	state.home = { x = pos.x, y = pos.y, z = pos.z }
	state.nextMoveAt = 0
	state.nextSayAt = os.time() + BotCore.firstSayDelay()
	state.configured = true
	return state
end

local function healIfNeeded(player, config)
	if not config.heal.enabled then
		return
	end

	local healthAmount = BotCore.computeHeal(player:getHealth(), player:getMaxHealth(), config.heal.health)
	if healthAmount then
		player:addHealth(healthAmount)
	end

	if player.getMana and player.getMaxMana and player.addMana then
		local manaAmount = BotCore.computeHeal(player:getMana(), player:getMaxMana(), config.heal.mana)
		if manaAmount then
			player:addMana(manaAmount)
		end
	end
end

local function validMonster(creature)
	return creature and not creature:isRemoved() and creature.isMonster and creature:isMonster() and
		creature:getHealth() > 0
end

local function findTarget(player, config)
	local position = player:getPosition()
	local spectators = Game.getSpectators(position, false, false, config.searchRangeX, config.searchRangeX,
		config.searchRangeY, config.searchRangeY)

	local candidates = {}
	for index, creature in ipairs(spectators) do
		if validMonster(creature) and player:canSeeCreature(creature) then
			local creaturePosition = creature:getPosition()
			candidates[#candidates + 1] = {
				index = index,
				position = { x = creaturePosition.x, y = creaturePosition.y, z = creaturePosition.z }
			}
		end
	end

	local chosen = BotCore.selectTarget({ x = position.x, y = position.y, z = position.z }, candidates)
	return chosen and spectators[chosen.index] or nil
end

local function walkPath(player, targetPos, minDistance, maxDistance)
	local path = player:getPathTo(targetPos, minDistance or 0, maxDistance or 1, true, true, 12)
	if not path or #path == 0 then
		return false
	end

	return player:move(path[1]) == RETURNVALUE_NOERROR
end

local function wander(player, state, config, nowMs)
	if nowMs < (state.nextMoveAt or 0) then
		return
	end
	state.nextMoveAt = nowMs + config.idleMoveInterval + math.random(0, 300)

	local pos = player:getPosition()
	local home = state.home or { x = pos.x, y = pos.y, z = pos.z }
	local dx, dy = BotCore.pickWanderOffset(config.wanderRadius)
	walkPath(player, Position(home.x + dx, home.y + dy, home.z), 0, 1)
end

local function handleCombat(player, state, config, nowMs)
	local target = player:getTarget()
	if not validMonster(target) then
		target = findTarget(player, config)
	end

	if not target then
		player:setTarget(nil)
		player:setFollowCreature(nil)
		return false
	end

	player:setTarget(target)

	local playerPosition = player:getPosition()
	local targetPosition = target:getPosition()
	if nowMs >= (state.nextMoveAt or 0) and
		BotCore.chebyshevDistance({ x = playerPosition.x, y = playerPosition.y, z = playerPosition.z },
			{ x = targetPosition.x, y = targetPosition.y, z = targetPosition.z }) > 1 then
		state.nextMoveAt = nowMs + config.combatMoveInterval
		walkPath(player, targetPosition, 1, 1)
	end
	return true
end

local function maybeSay(player, state)
	if os.time() < (state.nextSayAt or 0) then
		return
	end

	state.nextSayAt = os.time() + BotCore.nextSayDelay()
	player:say(BotCore.pickPhrase(), TALKTYPE_SAY)
end

function BotBrain.activate(player)
	if not player or player:isRemoved() or not player:isBot() then
		return false
	end

	configureBot(player)
	BotBrain.start()
	return true
end

function BotBrain.forget(guid)
	BotBrain.state[guid] = nil
end

function BotBrain.run()
	local config = getConfig()
	local bots = Game.getBots()
	local nowMs = os.mtime and os.mtime() or (os.time() * 1000)

	local seen = {}
	for _, player in ipairs(bots) do
		if player and not player:isRemoved() and player:isBot() then
			seen[player:getGuid()] = true
			-- One broken bot must not starve the rest of the fleet.
			local ok, err = pcall(function()
				local state = configureBot(player)
				healIfNeeded(player, config)
				if not handleCombat(player, state, config, nowMs) then
					wander(player, state, config, nowMs)
					maybeSay(player, state)
				end
			end)
			if not ok then
				logWarning("[BotBrain] bot tick failed: " .. tostring(err))
			end
		end
	end

	for guid in pairs(BotBrain.state) do
		if not seen[guid] then
			BotBrain.state[guid] = nil
		end
	end
end

function BotBrain.tick()
	BotBrain.eventId = nil
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

	BotBrain.eventId = addEvent(BotBrain.tick, getConfig().tickInterval)
end

function BotBrain.start()
	if BotBrain.running then
		return
	end
	if BotSystem and not BotSystem.isEnabled() then
		return
	end

	BotBrain.running = true
	BotBrain.eventId = addEvent(BotBrain.tick, getConfig().tickInterval)
end

function BotBrain.stop()
	BotBrain.running = false
	if BotBrain.eventId then
		stopEvent(BotBrain.eventId)
		BotBrain.eventId = nil
	end
end
