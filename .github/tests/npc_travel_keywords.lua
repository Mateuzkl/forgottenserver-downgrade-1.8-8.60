local handlerScript = assert(arg[1], 'missing handler.lua')
local requirementsScript = assert(arg[2], 'missing requirements.lua')
local modulesScript = assert(arg[3], 'missing modules.lua')
KEYWORDS_GREET, MESSAGES_GREET = { 'hi' }, { 'Hello.' }
KEYWORDS_FAREWELL, MESSAGES_FAREWELL = { 'bye' }, { 'Bye.' }
RevNpcSysResolveNpcName = function(name) return name end
getNpcCid = function() return 1 end
assert(loadfile(handlerScript))()
assert(loadfile(requirementsScript))()
assert(loadfile(modulesScript))()

local handler = NpcsHandler('Test Captain')
local greet = handler:keyword(handler.greetWords)
local trip = greet:keyword('trip')
trip:respond('Original trip menu.')
local position = { x = 733, y = 973, z = 6 }
handler:travelTo({
  trip = { position = position },
  travel = { position = position },
  destination = { position = position },
  timberport = { position = position }
})
assert(greet:isKeyword('trip') == trip and trip:getResponse() == 'Original trip menu.',
  'a city replaced an existing greeting-level menu')
local travelMenu = greet:isKeyword('travel')
assert(travelMenu == greet:isKeyword('destination') and travelMenu.resetTalkstate,
  'a reserved city name replaced the travel menu')
assert(travelMenu:isKeyword('trip'):isKeyword('yes').teleportPosition.position == position,
  'the colliding destination lost its travel node')
assert(greet:isKeyword('timberport') == travelMenu:isKeyword('timberport'),
  'a normal direct destination stopped working')

-- Preserve the established default: equipped quest/travel items are protected
-- unless the destination explicitly opts in to removing them.
local function requirement()
  return setmetatable({}, { __index = NpcRequirements })
end
MESSAGE_LIST = { item = 'Not enough eligible items.' }
REQUIREMENTS = { removeItem = 9 }
string.replaceTags = function(text) return text end
ItemType = function(id)
  return { getId = function() return id end, getName = function() return 'ticket' end }
end
local removed
local player = {
  getItemCount = function(_, _, _, ignoreEquipped) return ignoreEquipped and 0 or 1 end,
  removeItem = function(_, _, _, _, ignoreEquipped) removed = ignoreEquipped; return true end
}
local req = requirement()
req:removeItem(100)
assert(req.requireRemoveItem[1].ignoreEquipped == true)
assert(not req:init(player) and removed == nil, 'default removed an equipped item')
req:removeItem(100, 1, -1, false)
assert(req:init(player) and removed == false, 'explicit false could not remove an equipped item')
req:removeItems({ { item = 100, count = 1, subType = -1 },
  { item = 100, count = 1, subType = -1, ignoreEquipped = false } })
assert(req.requireRemoveItem[1].ignoreEquipped == true and
  req.requireRemoveItem[2].ignoreEquipped == false, 'multi-item defaults are inconsistent')
print('NPC travel keyword and equipped-item policy: passed')
