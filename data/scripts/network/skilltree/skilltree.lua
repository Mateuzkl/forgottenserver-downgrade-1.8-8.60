-- Skill Tree
--
-- A Ragnarok-style tree: nodes have levels, directed prerequisites, and a cost
-- that doubles every level. Points come from the highest level the character
-- ever reached (Player:getHighestLevel), so losing levels to death never costs
-- an allocation — only a character reset clears it.
--
-- Redistribution is free and unlimited by design: the client submits the WHOLE
-- allocation and the server validates it in isolation, so any legal end state is
-- reachable in one packet even when no intermediate state would be legal. A
-- future respec item needs no core change, only a gate on the save action.

local skillTreeConfigKey = configKeys and configKeys.SKILLTREE_SYSTEM_ENABLED or SKILLTREE_SYSTEM_ENABLED
if configManager and skillTreeConfigKey and not configManager.getBoolean(skillTreeConfigKey) then
	SkillTreeSystem = nil
	return
end

SkillTreeSystem = SkillTreeSystem or {}

local System = SkillTreeSystem

local NODES = dofile(DATA_DIRECTORY .. "/scripts/network/skilltree/skilltree_nodes.lua")

-- 0xBC is unclaimed inbound (neither the C++ switch nor another PacketHandler
-- takes it). 0xC1 is unclaimed outbound — note 0xC0 is NOT free, it is the
-- Astra-only quick-loot packet in luanetworkmessage.cpp.
local OPCODE_REQUEST = 0xBC
local OPCODE_WINDOW = 0xC1

local ACTION_OPEN = 0
local ACTION_SAVE = 1

-- Distinct from the wheel's 86061 so the two can coexist without one wiping the
-- other's condition.
local SKILLTREE_CONDITION_SUBID = 86062

local POINTS_START_LEVEL = 8
local POINTS_PER_LEVEL = 1
local MAX_TOTAL_POINTS = 1000

local MAX_NODES_PER_PACKET = 64

local function treeKV(player)
	return player:kv():scoped("skilltree")
end

---------------------------------------------------------------------------
-- Cost and budget
---------------------------------------------------------------------------

-- Cost of a single level. Doubles each step: baseCost, 2x, 4x, 8x...
local function costOf(node, level)
	return node.baseCost << (level - 1)
end

-- Everything paid to hold a node at `level`: baseCost * (2^level - 1).
local function totalCostFor(node, level)
	return node.baseCost * ((1 << level) - 1)
end

function System.getPointBudget(player)
	local highest = player:getHighestLevel()
	local points = math.max(0, (highest - POINTS_START_LEVEL) * POINTS_PER_LEVEL)
	return math.min(points, MAX_TOTAL_POINTS)
end

local function spentPoints(nodes)
	local total = 0
	for id, level in pairs(nodes) do
		local node = NODES[id]
		if node then
			total = total + totalCostFor(node, level)
		end
	end
	return total
end

function System.getNextLevelCost(nodeId, currentLevel)
	local node = NODES[nodeId]
	if not node or currentLevel >= node.maxLevel then
		return nil
	end
	return costOf(node, currentLevel + 1)
end

---------------------------------------------------------------------------
-- State
---------------------------------------------------------------------------

local function normalizeNodes(stored)
	local nodes = {}
	if type(stored) ~= "table" then
		return nodes
	end

	for id, level in pairs(stored) do
		local node = NODES[id]
		level = math.floor(tonumber(level) or 0)
		if node and level > 0 then
			nodes[id] = math.min(level, node.maxLevel)
		end
	end
	return nodes
end

local function loadNodes(player)
	return normalizeNodes(treeKV(player):get("nodes"))
end

local function saveNodes(player, nodes)
	treeKV(player):set("nodes", nodes)
	treeKV(player):set("spentPoints", spentPoints(nodes))
	treeKV(player):set("savedAt", os.time())
end

-- Validates a whole allocation on its own terms. Prerequisites are checked
-- against the submitted table, never against what is stored, which is what makes
-- a one-packet respec legal.
local function validate(player, nodes)
	local budget = System.getPointBudget(player)
	local spent = 0

	for id, level in pairs(nodes) do
		local node = NODES[id]
		if not node then
			return false, "Unknown skill."
		end
		if level < 1 or level > node.maxLevel then
			return false, "Invalid skill level."
		end
		spent = spent + totalCostFor(node, level)
	end

	if spent > budget then
		return false, "Not enough skill points."
	end

	for id in pairs(nodes) do
		for _, requirement in ipairs(NODES[id].requires or {}) do
			if (nodes[requirement.node] or 0) < requirement.level then
				return false, "Missing skill requirement."
			end
		end
	end

	return true
end

System.validate = validate

---------------------------------------------------------------------------
-- Applying bonuses
---------------------------------------------------------------------------

local function setConditionBonus(condition, parameter, value)
	if value and value ~= 0 then
		condition:setParameter(parameter, value)
		return true
	end
	return false
end

local function collectBonuses(nodes)
	local attributes = {}
	local modifiers = {}
	local taught = {}

	for id, level in pairs(nodes) do
		local node = NODES[id]

		for key, perLevel in pairs(node.attributes or {}) do
			attributes[key] = (attributes[key] or 0) + perLevel * level
		end

		for _, modifier in ipairs(node.modifiers or {}) do
			local amount = (modifier.value or 0) + (modifier.perLevel or 0) * level
			if modifier.spell and modifier.type and amount ~= 0 then
				modifiers[#modifiers + 1] = {
					spell = modifier.spell,
					type = modifier.type,
					value = amount,
				}
			end
		end

		if node.teaches then
			taught[node.teaches] = true
		end
	end

	return attributes, modifiers, taught
end

local function removeBonuses(player)
	player:removeCondition(CONDITION_ATTRIBUTES, CONDITIONID_DEFAULT, SKILLTREE_CONDITION_SUBID, true)
	if player.clearSkillTreeSpellAugments then
		player:clearSkillTreeSpellAugments()
	end
end

local function applyBonuses(player)
	local nodes = loadNodes(player)

	-- Revalidate on every apply, not just on save. The wheel skips this, so a
	-- character reset there leaves the old allocation applied forever. Anything
	-- that no longer fits the budget is dropped rather than silently kept.
	local valid = validate(player, nodes)
	if not valid then
		nodes = {}
		saveNodes(player, nodes)
	end

	removeBonuses(player)

	local attributes, modifiers, taught = collectBonuses(nodes)

	local condition = Condition(CONDITION_ATTRIBUTES, CONDITIONID_DEFAULT)
	condition:setParameter(CONDITION_PARAM_SUBID, SKILLTREE_CONDITION_SUBID)
	condition:setParameter(CONDITION_PARAM_TICKS, -1)

	local hasBonus = false
	hasBonus = setConditionBonus(condition, CONDITION_PARAM_STAT_MAXHITPOINTS, attributes.health) or hasBonus
	hasBonus = setConditionBonus(condition, CONDITION_PARAM_STAT_MAXMANAPOINTS, attributes.mana) or hasBonus
	hasBonus = setConditionBonus(condition, CONDITION_PARAM_STAT_CAPACITY, attributes.capacity) or hasBonus
	hasBonus = setConditionBonus(condition, CONDITION_PARAM_STAT_MAGICPOINTS, attributes.magic) or hasBonus
	hasBonus = setConditionBonus(condition, CONDITION_PARAM_SKILL_MELEE, attributes.melee) or hasBonus
	hasBonus = setConditionBonus(condition, CONDITION_PARAM_SKILL_DISTANCE, attributes.distance) or hasBonus
	hasBonus = setConditionBonus(condition, CONDITION_PARAM_SKILL_FIST, attributes.fist) or hasBonus
	hasBonus = setConditionBonus(condition, CONDITION_PARAM_SKILL_SHIELD, attributes.shielding) or hasBonus

	if hasBonus then
		player:addCondition(condition)
	end

	if player.addSkillTreeSpellAugment then
		for _, modifier in ipairs(modifiers) do
			player:addSkillTreeSpellAugment(modifier.spell, modifier.type, modifier.value)
		end
	end

	---------------------------------------------------------------------
	-- Taught spells
	---------------------------------------------------------------------
	-- Tracked separately so a respec can take back exactly what the tree gave
	-- and nothing else.
	local previous = treeKV(player):get("grantedSpells")
	if type(previous) == "table" then
		for _, spellName in pairs(previous) do
			if type(spellName) == "string" and not taught[spellName] then
				player:forgetSpell(spellName)
			end
		end
	end

	local grantedList = {}
	for spellName in pairs(taught) do
		grantedList[#grantedList + 1] = spellName
		if not player:hasLearnedSpell(spellName) then
			player:learnSpell(spellName)
		end
	end
	table.sort(grantedList)
	treeKV(player):set("grantedSpells", grantedList)

	-- forgetSpell above is blind to the other source of permanent spells, so let
	-- equipment proficiency re-assert anything the player mastered through gear.
	if EquipmentProficiencySystem and EquipmentProficiencySystem.refreshEquippedSpells then
		EquipmentProficiencySystem.refreshEquippedSpells(player)
	elseif player.reloadData then
		player:reloadData()
	end

	return nodes
end

System.applyBonuses = applyBonuses

---------------------------------------------------------------------------
-- Public API
---------------------------------------------------------------------------

function System.getNodes(player)
	return loadNodes(player)
end

-- Validates and commits a whole allocation. Returns false plus a reason when the
-- allocation is not legal, leaving the stored one untouched.
function System.setNodes(player, nodes)
	nodes = normalizeNodes(nodes)

	local valid, reason = validate(player, nodes)
	if not valid then
		return false, reason
	end

	saveNodes(player, nodes)
	applyBonuses(player)
	return true
end

function System.getSpentPoints(player)
	return spentPoints(loadNodes(player))
end

function System.getAvailablePoints(player)
	return System.getPointBudget(player) - System.getSpentPoints(player)
end

-- Wipes points and allocation. Called by the character reset, which restarts the
-- progression axis along with the highest-level watermark.
function System.clearProfile(player)
	if not player then
		return
	end

	removeBonuses(player)

	local previous = treeKV(player):get("grantedSpells")
	if type(previous) == "table" then
		for _, spellName in pairs(previous) do
			if type(spellName) == "string" then
				player:forgetSpell(spellName)
			end
		end
	end

	local store = treeKV(player)
	store:remove("nodes")
	store:remove("grantedSpells")
	store:remove("spentPoints")
	store:remove("savedAt")
end

---------------------------------------------------------------------------
-- Protocol
---------------------------------------------------------------------------

local function supportsCustomNetwork(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function sendWindow(player)
	if not supportsCustomNetwork(player) then
		return false
	end

	local nodes = loadNodes(player)
	local ids = {}
	for id in pairs(NODES) do
		ids[#ids + 1] = id
	end
	table.sort(ids)

	local out = NetworkMessage(player)
	out:addByte(OPCODE_WINDOW)
	out:addU32(System.getPointBudget(player))
	out:addU32(spentPoints(nodes))
	out:addU16(#ids)
	for _, id in ipairs(ids) do
		local node = NODES[id]
		local level = nodes[id] or 0
		out:addString(id)
		out:addString(node.name)
		out:addByte(node.maxLevel)
		out:addByte(level)
		out:addU32(level < node.maxLevel and costOf(node, level + 1) or 0)
	end
	return out:sendToPlayer(player)
end

System.sendWindow = sendWindow

local function parseSave(player, msg)
	if msg:len() - msg:tell() < 1 then
		return
	end

	local count = msg:getByte()
	if count > MAX_NODES_PER_PACKET then
		return
	end

	local nodes = {}
	for _ = 1, count do
		if msg:len() - msg:tell() < 2 then
			return
		end
		local id = msg:getString()
		local level = msg:getByte()
		if NODES[id] and level > 0 then
			nodes[id] = level
		end
	end

	local ok, reason = System.setNodes(player, nodes)
	if not ok then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, reason)
	end
	sendWindow(player)
end

local requestHandler = PacketHandler(OPCODE_REQUEST)

function requestHandler.onReceive(player, msg)
	if not supportsCustomNetwork(player) or msg:len() - msg:tell() < 1 then
		return
	end

	local action = msg:getByte()
	if action == ACTION_OPEN then
		sendWindow(player)
	elseif action == ACTION_SAVE then
		parseSave(player, msg)
	end
end

requestHandler:register()

local loginEvent = CreatureEvent("SkillTreeLogin")

function loginEvent.onLogin(player)
	applyBonuses(player)
	return true
end

loginEvent:register()
