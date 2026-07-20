local root = (arg and arg[1]) or "."

local function loadProduction(relativePath)
	local chunk, err = loadfile(root .. "/" .. relativePath)
	assert(chunk, err)
	return chunk()
end

local function readProduction(relativePath)
	local file = assert(io.open(root .. "/" .. relativePath, "rb"))
	local contents = assert(file:read("*a"))
	file:close()
	return contents
end

local function contains(list, value)
	for _, candidate in ipairs(list) do
		if candidate == value then
			return true
		end
	end
	return false
end

table.contains = table.contains or contains

-- Exercise Training registry coverage.
SKILL_SWORD = "sword"
SKILL_AXE = "axe"
SKILL_CLUB = "club"
SKILL_SHIELD = "shield"
SKILL_MAGLEVEL = "magic"
SKILL_DISTANCE = "distance"
SKILL_FIST = "fist"
CONST_ANI_SMALLICE = "ice"
CONST_ANI_SIMPLEARROW = "arrow"
CONST_ANI_FIRE = "fire"
CONST_ANI_WHIRLWINDAXE = "fist-effect"
configKeys = {
	MAX_ALLOWED_ON_A_DUMMY = 1,
	RATE_MAGIC = 2,
	RATE_SKILL = 3
}
configManager = {
	getNumber = function()
		return 1
	end
}

loadProduction("data/lib/functions/exercise_training.lua")

local exerciseFamilies = {
	[SKILL_AXE] = { 40636, 35280, 40856, 28553, 35286, 40863, 40820, 40630, 40687 },
	[SKILL_DISTANCE] = { 40637, 35282, 40857, 28555, 35288, 40864, 40821, 40632, 40688 },
	[SKILL_CLUB] = { 40638, 35281, 40851, 28554, 35287, 40858, 40815, 40631, 40689 },
	[SKILL_MAGLEVEL] = {
		40639, 35283, 40852, 28556, 35289, 40859, 40816, 40633, 40690,
		40642, 35284, 40855, 28557, 35290, 40862, 40819, 40634, 40693
	},
	[SKILL_SHIELD] = { 40640, 44066, 40853, 44065, 44067, 40860, 40817, 40635, 40691 },
	[SKILL_SWORD] = { 40641, 35279, 40854, 28552, 35285, 40861, 40818, 40629, 40692 },
	[SKILL_FIST] = { 50294, 50293, 50295, 41021, 41022, 41023, 41024, 41025, 41026 }
}

local itemsXml = readProduction("data/items/items.xml")
local function itemXmlBlock(id)
	return itemsXml:match('<item%s+id="' .. id .. '"[^>]*>(.-)</item>')
end

local clientWeaponCount = 0
for skill, ids in pairs(exerciseFamilies) do
	for _, id in ipairs(ids) do
		clientWeaponCount = clientWeaponCount + 1
		local entry = assert(ExerciseWeaponsTable[id], "missing exercise weapon " .. id)
		assert(entry.skill == skill, "wrong exercise skill for " .. id)
		if skill == SKILL_DISTANCE or skill == SKILL_MAGLEVEL then
			assert(entry.allowFarUse == true and entry.effect ~= nil,
				"ranged exercise weapon is not usable at range: " .. id)
		else
			assert(entry.allowFarUse ~= true, "melee exercise weapon incorrectly allows far use: " .. id)
		end
		local itemBlock = assert(itemXmlBlock(id), "missing items.xml definition for exercise weapon " .. id)
		local charges = tonumber(itemBlock:match('<attribute%s+key="charges"%s+value="(%d+)"%s*/>'))
		assert(charges and charges > 0, "exercise weapon has no positive default charges: " .. id)
	end
end
assert(clientWeaponCount == 72, "client exercise weapon inventory changed")

for _, id in ipairs({ 28558, 28561, 28562, 40622, 40621, 28559, 28560,
	28563, 28564, 40648, 40647, 41259, 41260 }) do
	assert(contains(FreeDummies, id) or contains(HouseDummies, id),
		"missing Assistant training dummy " .. id)
	assert(itemXmlBlock(id), "missing items.xml definition for Assistant training dummy " .. id)
end

assert(itemXmlBlock(35563), "missing items.xml definition for Magic Shield Potion")

-- Minimal engine surfaces used to load and exercise the real revscripts.
CONDITION_MANASHIELD = 512
CONDITION_PARAM_TICKS = 1
CONST_ME_MAGIC_BLUE = 13
TALKTYPE_MONSTER_SAY = 19
configKeys.REMOVE_POTION_CHARGES = 4

local registeredSpell
local registeredAction
local removePotionCharges = true
local trackedSupplies = 0

function Condition(conditionType)
	local condition = { conditionType = conditionType, parameters = {} }
	function condition:setParameter(key, value)
		self.parameters[key] = value
	end
	return condition
end

local function metadataMethod(name)
	return function(self, ...)
		self.metadata[name] = { ... }
		return self
	end
end

function Spell(kind)
	local spell = { kind = kind, metadata = {} }
	for _, name in ipairs({ "group", "id", "name", "words", "level", "mana",
		"isSelfTarget", "cooldown", "groupCooldown", "needLearn", "isAggressive", "vocation" }) do
		spell[name] = metadataMethod(name)
	end
	function spell:register()
		registeredSpell = self
	end
	return spell
end

function Action()
	local action = { metadata = {} }
	action.id = metadataMethod("id")
	function action:register()
		registeredAction = self
	end
	return action
end

configManager.getBoolean = function()
	return removePotionCharges
end
function sendSupplyTracker()
	trackedSupplies = trackedSupplies + 1
end

loadProduction("data/scripts/spells/support/cancel_magic_shield.lua")
assert(registeredSpell and registeredSpell.kind == "instant", "Cancel Magic Shield was not registered")
assert(registeredSpell.metadata.id[1] == 245, "Cancel Magic Shield id mismatch")
assert(registeredSpell.metadata.words[1] == "exana vita", "Cancel Magic Shield words mismatch")
assert(registeredSpell.metadata.level[1] == 14 and registeredSpell.metadata.mana[1] == 50,
	"Cancel Magic Shield requirements mismatch")
assert(registeredSpell.metadata.cooldown[1] == 2000 and registeredSpell.metadata.groupCooldown[1] == 2000,
	"Cancel Magic Shield cooldown mismatch")

local spellEffect
local removedCondition
local caster = {
	removeCondition = function(_, conditionType)
		removedCondition = conditionType
		return true
	end,
	getPosition = function()
		return {
			sendMagicEffect = function(_, effect, instanceId)
				spellEffect = { effect, instanceId }
			end
		}
	end,
	getInstanceId = function()
		return 77
	end
}
assert(registeredSpell.onCastSpell(caster, {}) == true, "Cancel Magic Shield cast failed")
assert(removedCondition == CONDITION_MANASHIELD, "Cancel Magic Shield did not remove the condition")
assert(spellEffect[1] == CONST_ME_MAGIC_BLUE and spellEffect[2] == 77,
	"Cancel Magic Shield effect mismatch")

loadProduction("data/scripts/actions/others/magic_shield_potion.lua")
assert(registeredAction and registeredAction.metadata.id[1] == 35563,
	"Magic Shield Potion action was not registered")

local function makePlayer(level, vocation)
	local player = { addedConditions = {}, messages = {}, effects = {}, achievements = 0 }
	function player:getLevel()
		return level
	end
	function player:getVocation()
		return { getId = function() return vocation end }
	end
	function player:say(message)
		self.messages[#self.messages + 1] = message
	end
	function player:addCondition(condition)
		self.addedConditions[#self.addedConditions + 1] = condition
	end
	function player:addAchievementProgress()
		self.achievements = self.achievements + 1
	end
	function player:getPosition()
		return {
			sendMagicEffect = function(_, effect, instanceId)
				player.effects[#player.effects + 1] = { effect, instanceId }
			end
		}
	end
	function player:getInstanceId()
		return 91
	end
	return player
end

local function makeItem()
	local item = { removed = 0 }
	function item:remove(count)
		self.removed = self.removed + count
		return true
	end
	return item
end

local invalidPlayer = makePlayer(13, 1)
local invalidItem = makeItem()
assert(registeredAction.onUse(invalidPlayer, invalidItem, {}, invalidPlayer, {}, true) == true)
assert(#invalidPlayer.addedConditions == 0 and invalidItem.removed == 0,
	"Magic Shield Potion bypassed its level requirement")

local validPlayer = makePlayer(14, 2)
local validItem = makeItem()
assert(registeredAction.onUse(validPlayer, validItem, {}, validPlayer, {}, true) == true)
assert(#validPlayer.addedConditions == 1, "Magic Shield Potion did not apply the condition")
assert(validPlayer.addedConditions[1].conditionType == CONDITION_MANASHIELD and
	validPlayer.addedConditions[1].parameters[CONDITION_PARAM_TICKS] == 180000,
	"Magic Shield Potion duration mismatch")
assert(validItem.removed == 1 and trackedSupplies == 1,
	"Magic Shield Potion consumption/tracking mismatch")

removePotionCharges = false
local reusableItem = makeItem()
assert(registeredAction.onUse(makePlayer(100, 5), reusableItem, {}, nil, {}, true) == true)
assert(reusableItem.removed == 0 and trackedSupplies == 2,
	"Magic Shield Potion ignored the server charge setting")

print("minibot dependencies smoke: OK (72 training weapons, 13 dummies, mana shield spell/potion)")
