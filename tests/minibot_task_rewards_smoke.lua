-- Reproducible smoke coverage for MiniBot Task experience and loot scaling.
-- Run from the repository root with Lua 5.1+ or LuaJIT.

local experienceCallbacks = {}

function Event()
	local event = {}
	function event:register(priority)
		experienceCallbacks[#experienceCallbacks + 1] = {
			priority = priority or 0,
			callback = self.onGainExperience,
		}
	end
	return event
end

function Condition()
	return {
		setTicks = function() end,
		setParameter = function() end,
	}
end

function CreatureEvent()
	return { register = function() end }
end

CONDITION_SOUL = 1
CONDITIONID_DEFAULT = 1
CONDITION_PARAM_SOULGAIN = 1
CONDITION_PARAM_SOULTICKS = 2
STORAGE_EXP_COLOR = 50100
PlayerStorageKeys = { expColor = STORAGE_EXP_COLOR }
ExperienceRateType = { STAMINA = 1, BASE = 2, LOW_LEVEL = 3, BONUS = 4 }
configKeys = { MODIFY_EXP_IN_K = 1, DEFAULT_EXP_COLOR = 2 }
configManager = {
	getBoolean = function() return false end,
	getNumber = function() return 1 end,
}
Game = {
	getExperienceStage = function() return 2 end,
	formatValueK = tostring,
}

local taskExperienceMultiplier = 0.20
AstraHelper = {
	getMiniBotExperienceMultiplier = function()
		return taskExperienceMultiplier
	end,
}

assert(loadfile("data/scripts/eventcallbacks/player/default_onGainExperience.lua"))()

local proficiencyExperience
WeaponProficiencySystem = {
	addExperience = function(_, _, exp)
		proficiencyExperience = exp
	end,
}
assert(loadfile("data/scripts/eventcallbacks/player/weapon_proficiency_onGainExperience.lua"))()

table.sort(experienceCallbacks, function(left, right)
	return left.priority < right.priority
end)

assert(experienceCallbacks[1].priority == 0, "standard formula must run first")
assert(experienceCallbacks[2].priority == 50, "Task multiplier must run at priority 50")
assert(experienceCallbacks[3].priority == 100, "Weapon Proficiency must run after Task")
assert(experienceCallbacks[4].priority == math.huge, "experience message must run last")

local vocation = {
	getMaxSoul = function() return 100 end,
	getSoulGainTicks = function() return 60 end,
}
local player = {
	getVocation = function() return vocation end,
	getSoul = function() return 100 end,
	getLevel = function() return 100 end,
	updateStamina = function() end,
	getExperienceRate = function() return 100 end,
	getXpBoostTime = function() return 0 end,
	getStamina = function() return 2520 end,
}
local source = {
	isPlayer = function() return false end,
	isMonster = function() return true end,
	isInfluenced = function() return false end,
	getName = function() return "Test Monster" end,
}

local function runExperienceCallbacks()
	local exp = 100
	for _, registered in ipairs(experienceCallbacks) do
		exp = registered.callback(player, source, exp, 100, false)
	end
	return exp
end

assert(runExperienceCallbacks() == 40, "Task must scale the post-formula experience")
assert(proficiencyExperience == 40, "Weapon Proficiency must observe Task-scaled experience")

taskExperienceMultiplier = 1
assert(runExperienceCallbacks() == 200, "experience multiplier 1 must preserve the baseline")
assert(proficiencyExperience == 200, "baseline Weapon Proficiency experience must be preserved")

local playerEventFile = assert(io.open("data/events/scripts/player.lua", "rb"))
local playerEventSource = playerEventFile:read("*a")
playerEventFile:close()
assert(
	not playerEventSource:find("getMiniBotExperienceMultiplier", 1, true),
	"Task experience must not be applied again after Event.onGainExperience"
)

local dropCallbacks = {}
function Event()
	local event = {}
	function event:register()
		dropCallbacks[#dropCallbacks + 1] = self.onDropLoot
	end
	return event
end

configKeys = {
	RATE_LOOT = 1,
	STAMINA_SYSTEM = 2,
	BOOSTED_LOOT_MULTIPLIER = 3,
	LOOT_GROUPING_ENABLED = 4,
	COLORIZED_LOOT_VALUE = 5,
	SOULPIT_SYSTEM_ENABLED = 6,
}
configManager = {
	getNumber = function(key)
		return key == configKeys.RATE_LOOT and 1 or 0
	end,
	getFloat = function() return 1 end,
	getBoolean = function(key)
		return key == configKeys.LOOT_GROUPING_ENABLED or key == configKeys.SOULPIT_SYSTEM_ENABLED
	end,
}

local taskLootMultiplier = 1
AstraHelper = {
	getMiniBotLootMultiplier = function()
		return taskLootMultiplier
	end,
}

local owner = {
	getStamina = function() return 2520 end,
	getDropBonus = function() return 0 end,
}
function Player(id)
	return id == 1 and owner or nil
end

Game = { getBoostedCreature = function() return nil end }
PreySystem = {
	BONUS_LOOT = 7,
	getBonus = function() return 7, 100 end,
}
TaskBoard = { getBountyTalismanBonus = function() return 10000 end }

local monsterType = {
	raceId = function() return 1 end,
	getName = function() return "Test Monster" end,
	getNameDescription = function() return "a test monster" end,
	getLoot = function()
		return {
			{ itemId = 100, chance = 100000, maxCount = 1, minCount = 1, childLoot = {} },
		}
	end,
}
local monster = {
	getType = function() return monsterType end,
	getName = function() return "Test Monster" end,
}

local function newCorpse()
	local created = 0
	return {
		getCorpseOwner = function() return 1 end,
		createLootItem = function()
			created = created + 1
			return {}
		end,
		getCreated = function() return created end,
	}
end

assert(loadfile("data/scripts/eventcallbacks/monster/default_onDropLoot.lua"))()
local defaultDrop = dropCallbacks[#dropCallbacks]
local originalRandom = math.random
local randomFloat = 0.5
local randomInteger = 1
math.random = function(first)
	if first == nil then
		return randomFloat
	end
	if first == 100000 then
		return randomInteger
	end
	return 1
end

local corpse = newCorpse()
taskLootMultiplier = 1
defaultDrop(monster, corpse)
assert(corpse:getCreated() == 3, "baseline normal/Prey/Bounty loot must be preserved")

corpse = newCorpse()
taskLootMultiplier = 0
defaultDrop(monster, corpse)
assert(corpse:getCreated() == 0, "zero multiplier must suppress normal/Prey/Bounty loot")

corpse = newCorpse()
taskLootMultiplier = 0.2
randomFloat = 0.1
defaultDrop(monster, corpse)
assert(corpse:getCreated() == 3, "Task roll must cover normal/Prey/Bounty success")

corpse = newCorpse()
randomFloat = 0.3
defaultDrop(monster, corpse)
assert(corpse:getCreated() == 0, "Task roll must cover normal/Prey/Bounty rejection")

SoulPit = {
	SoulCoresConfiguration = {
		chanceToDropSoulCore = 5,
		chanceToGetSameMonsterSoulCore = 100,
		chanceToDropSoulPrism = 4,
	},
	itemIds = { soulPrism = 49164 },
	getSoulCoreItems = function() return {} end,
}
ItemType = function()
	return { getId = function() return 50000 end }
end

local position = { sendMagicEffect = function() end }
local fiendishType = {
	getName = function() return "Fiendish Test" end,
	raceId = function() return 2 end,
}
local fiendish = {
	isFiendish = function() return true end,
	getType = function() return fiendishType end,
	getPosition = function() return position end,
}
CONST_ME_MAGIC_GREEN = 1

assert(loadfile("data/scripts/quests/soulpit/ondroploot_soul_core.lua"))()
local soulDrop = dropCallbacks[#dropCallbacks]
local function newSoulCorpse()
	local added = 0
	return {
		getCorpseOwner = function() return 1 end,
		addItem = function()
			added = added + 1
			return {}
		end,
		getAdded = function() return added end,
	}
end

local soulCorpse = newSoulCorpse()
taskLootMultiplier = 1
soulDrop(fiendish, soulCorpse)
assert(soulCorpse:getAdded() == 2, "Soul Core/Prism multiplier 1 must preserve the baseline")

soulCorpse = newSoulCorpse()
taskLootMultiplier = 0
soulDrop(fiendish, soulCorpse)
assert(soulCorpse:getAdded() == 0, "zero multiplier must suppress Soul Core/Prism drops")

soulCorpse = newSoulCorpse()
taskLootMultiplier = 0.2
randomInteger = 1
soulDrop(fiendish, soulCorpse)
assert(soulCorpse:getAdded() == 2, "Task roll must cover Soul Core/Prism success")

soulCorpse = newSoulCorpse()
randomInteger = 1001
soulDrop(fiendish, soulCorpse)
assert(soulCorpse:getAdded() == 0, "Task roll must cover Soul Core/Prism rejection")

local creatureEvents = {}
function GlobalEvent()
	return {
		interval = function() end,
		register = function() end,
	}
end
function CreatureEvent(name)
	local event = {}
	function event:register()
		creatureEvents[name] = self
	end
	return event
end

configKeys.FORGE_SYSTEM_ENABLED = 7
configManager.getBoolean = function(key)
	return key == configKeys.FORGE_SYSTEM_ENABLED
end
PlayerStorageKeys.influencedSpawnTime = 60000
MESSAGE_INFO_DESCR = 1
MESSAGE_EXPERIENCE = 2
AstraHelper.getMiniBotExperienceMultiplier = function()
	return taskExperienceMultiplier
end

assert(loadfile("data/scripts/globalevents/influenced_spawn.lua"))()
local influencedDeath = assert(creatureEvents.InfluencedDeath)

local function newForgePlayer()
	local forgeDust = 0
	local experience = 0
	local messages = {}
	local forgePlayer = {}

	function forgePlayer:addForgeDust(amount)
		forgeDust = forgeDust + amount
	end

	function forgePlayer:getForgeDust()
		return forgeDust
	end

	function forgePlayer:getExperience()
		return experience
	end

	function forgePlayer:addExperience(exp)
		for _, registered in ipairs(experienceCallbacks) do
			if registered.priority < math.huge then
				exp = registered.callback(self, nil, exp, exp, false)
			end
		end
		experience = experience + math.floor(exp)
		return true
	end

	function forgePlayer:sendTextMessage(messageType, text)
		messages[#messages + 1] = { messageType = messageType, text = text }
	end

	function forgePlayer:getMessage(messageType)
		for index = #messages, 1, -1 do
			if messages[index].messageType == messageType then
				return messages[index].text
			end
		end
		return nil
	end

	return forgePlayer
end

local forgeType = {
	experience = function() return 1000 end,
	isBoss = function() return false end,
	isRewardBoss = function() return false end,
	isAttackable = function() return true end,
	isHostile = function() return true end,
}
local forgeMonster = {
	isMonster = function() return true end,
	getMonster = function(self) return self end,
	getType = function() return forgeType end,
	getMaster = function() return nil end,
	getName = function() return "Forge Test" end,
	isFiendish = function() return true end,
	isInfluenced = function() return false end,
	getInfluencedLevel = function() return 1 end,
}

local forgeFloat = 0.5
local forgeChanceRoll = 1
local forgeZeroArgumentRolls = 0
math.random = function(first, second)
	if first == nil then
		forgeZeroArgumentRolls = forgeZeroArgumentRolls + 1
		return forgeFloat
	end
	if first == 10 and second == 25 then
		return 12
	end
	if first == 1 and second == 10000 then
		return forgeChanceRoll
	end
	if first == 1 and second == 3 then
		return 2
	end
	return 1
end

local function killForgeMonster(forgePlayer, deathMonster, deathCorpse)
	local killer = {
		isPlayer = function() return true end,
		getPlayer = function() return forgePlayer end,
	}
	return influencedDeath.onDeath(deathMonster, deathCorpse, killer, killer, false, false)
end

taskLootMultiplier = 1
taskExperienceMultiplier = 1
forgeZeroArgumentRolls = 0
local forgePlayer = newForgePlayer()
assert(killForgeMonster(forgePlayer, forgeMonster, nil) == true)
assert(forgePlayer:getForgeDust() == 12, "Forge Dust multiplier 1 must preserve the baseline amount")
assert(forgeZeroArgumentRolls == 0, "Forge Dust multiplier 1 must not add an RNG call")
assert(forgePlayer:getExperience() == 2000, "Fiendish baseline extra experience must be preserved")
assert(
	forgePlayer:getMessage(MESSAGE_EXPERIENCE):find("2000 extra experience", 1, true),
	"Fiendish baseline experience message must report the awarded value"
)

taskLootMultiplier = 0.2
taskExperienceMultiplier = 0.2
forgeFloat = 0.1
forgePlayer = newForgePlayer()
assert(killForgeMonster(forgePlayer, forgeMonster, nil) == true)
assert(forgePlayer:getForgeDust() == 12, "Task Forge Dust roll must support success")
assert(forgePlayer:getExperience() == 400, "Task must scale Fiendish extra experience")
assert(
	forgePlayer:getMessage(MESSAGE_EXPERIENCE):find("400 extra experience", 1, true),
	"Fiendish Task message must report Task-scaled experience"
)

forgeFloat = 0.3
forgePlayer = newForgePlayer()
assert(killForgeMonster(forgePlayer, forgeMonster, nil) == true)
assert(forgePlayer:getForgeDust() == 0, "Task Forge Dust roll must support rejection")

forgeMonster.isFiendish = function() return false end
forgeMonster.isInfluenced = function() return true end
local function newSliverCorpse()
	local itemId
	local count = 0
	return {
		isContainer = function() return true end,
		addItem = function(_, addedItemId, addedCount)
			itemId = addedItemId
			count = addedCount
			return {}
		end,
		getItemId = function() return itemId end,
		getCount = function() return count end,
	}
end

taskLootMultiplier = 1
forgeZeroArgumentRolls = 0
forgeChanceRoll = 7000
local sliverCorpse = newSliverCorpse()
forgePlayer = newForgePlayer()
assert(killForgeMonster(forgePlayer, forgeMonster, sliverCorpse) == true)
assert(sliverCorpse:getItemId() == 37109 and sliverCorpse:getCount() == 2,
	"Forge Sliver multiplier 1 must preserve the baseline roll")
assert(forgeZeroArgumentRolls == 0, "Forge Sliver multiplier 1 must not add an RNG call")

taskLootMultiplier = 0.2
forgeChanceRoll = 1400
sliverCorpse = newSliverCorpse()
assert(killForgeMonster(forgePlayer, forgeMonster, sliverCorpse) == true)
assert(sliverCorpse:getCount() == 2, "Task Forge Sliver roll must support success")

forgeChanceRoll = 1600
sliverCorpse = newSliverCorpse()
assert(killForgeMonster(forgePlayer, forgeMonster, sliverCorpse) == true)
assert(sliverCorpse:getCount() == 0, "Task Forge Sliver roll must support rejection")

math.random = originalRandom
print("minibot task rewards smoke: OK")
