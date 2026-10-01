local helperScript = assert(arg[1], 'missing astra_helper.lua')
local legacyScript = assert(arg[2], 'missing npchandler.lua')
local focusScript = assert(arg[3], 'missing focus.lua')
local eventsScript = assert(arg[4], 'missing events.lua')
local now, packets, shopsClosed, farewells = 1000, {}, 0, 0
os.time = function() return now end
local astra = true
local function position() return { x = 100, y = 100, z = 7 } end
local npc = {
  getId = function() return 1 end, getName = function() return 'Captain' end,
  getPosition = position, setFocus = function() end
}
local player = {
  getId = function() return 7 end,
  getName = function() return 'Player' end,
  getPosition = position,
  isUsingAstraClient = function() return astra end,
  sendExtendedOpcode = function(_, opcode, buffer)
    packets[#packets + 1] = { opcode = opcode, buffer = buffer }
    return true
  end
}
Player = function(id) return id == 7 and player or nil end
Npc = function(id) return (id == nil or id == 1) and npc or nil end
getNpcCid = function() return 1 end
closeShopWindow = function() shopsClosed = shopsClosed + 1 end
doNpcSetCreatureFocus = function() end
stopEvent = function() end
addEvent = function() return 1 end
selfSay = function() farewells = farewells + 1 end
Game = { getFormattedWorldTime = function() return '12:00' end }
shop_cost = {}
assert(loadfile(helperScript))()
assert(loadfile(legacyScript))()

-- Run the real legacy idle-time path, shared by crystalcompat handlers.
local handler = NpcHandler:new({ reset = function() end })
handler.isInRange = function() return true end
local state = handler:getNpcState()
state.focuses[1], state.talkStart[7] = 7, now - handler.idleTime - 1
handler:onThink()
assert(not handler:isFocused(7) and shopsClosed == 1, 'legacy timeout retained NPC focus/shop')
assert(#packets == 1 and packets[1].opcode == 213 and packets[1].buffer == 'Captain',
  'legacy timeout did not signal the correct NPC to Astra')
handler:releaseFocus(7)
assert(#packets == 1, 'legacy duplicate release emitted another signal')

FOCUS = { time = 60, distance = 5 }
assert(loadfile(focusScript))()
assert(loadfile(eventsScript))()
local revisioned = {
  farewellResponses = { 'Good bye.' },
  setTalkState = function() end, resetData = function() end
}
NpcsHandler = function() return revisioned end
NpcVoices = function() return {} end
NpcTalkQueue = function() return { processQueue = function() end } end
getDistanceTo = function() return 1 end
string.replaceTags = function(text) return text end
local focus = NpcFocus(npc)
focus:addFocus(player)
NpcEvents.onThink(npc)
assert(#packets == 1 and focus:isFocused(player), 'an active conversation timed out early')
now = now + FOCUS.time + 1
NpcEvents.onThink(npc)
assert(not focus:isFocused(player) and shopsClosed == 2 and farewells == 1,
  'revisioned timeout did not end focus/shop and say farewell')
assert(#packets == 2 and packets[2].opcode == 213 and packets[2].buffer == 'Captain',
  'revisioned timeout did not signal the correct NPC')
focus:removeFocus(player)
assert(#packets == 2, 'revisioned duplicate release emitted another signal')

-- Old clients must retain their packet layout, and vanished players are safe.
astra = false
focus:addFocus(player)
now = now + FOCUS.time + 1
NpcEvents.onThink(npc)
assert(#packets == 2, 'a non-Astra client received the custom signal')
focus.focus[99] = now - 1
NpcEvents.onThink(npc)
assert(focus.focus[99] == nil and #packets == 2, 'disconnected-player cleanup failed')
print('NPC conversation timeout signaling: passed')
