-- Run from the repository root with Lua 5.4.
-- Exercise the registration path used by real monster/NPC definitions.
local function read(path)
	local file = assert(io.open(path, "rb"))
	local content = file:read("*a")
	file:close()
	return content
end
for name, id in read("src/luascript.cpp"):gmatch('registerGlobalVariable%("(SOUND_EFFECT_TYPE_[%w_]+)",%s*(%d+)') do
	_G[name] = tonumber(id)
end
local helper = read("data/scripts/lib/register_monster_type.lua")
local synthetic = 100000
for name in helper:gmatch("CONST_ANI_[%w_]+") do
	if _G[name] == nil then synthetic = synthetic + 1; _G[name] = synthetic end
end
for name in helper:gmatch("COMBAT_[%w_]+") do
	if _G[name] == nil then synthetic = synthetic + 1; _G[name] = synthetic end
end
MonsterType, NpcType, Charm = {}, {}, {}
local spellMethods = {}
function spellMethods:castSound(id) self.cast = id end
function spellMethods:impactSound(id) self.impact = id end
setmetatable(spellMethods, {__index = function() return function() end end})
function MonsterSpell() return setmetatable({}, {__index = spellMethods, __close = function() end}) end
local mtype = {attacks = {}, voices = {}}
function mtype:targetDistance() return 1 end
function mtype:addAttack(spell) self.attacks[#self.attacks + 1] = spell end
function mtype:deathSound(id) self.death = id end
function mtype:addSound(id, ticks, chance) self.voices[#self.voices + 1] = {id, ticks, chance} end
assert(load(helper, "register_monster_type"))()
local function spell(config)
	registerMonsterType.attacks(mtype, {attacks = {config}})
	return mtype.attacks[#mtype.attacks]
end
assert(spell({name = "combat", type = COMBAT_FIREDAMAGE, target = true}).impact == SOUND_EFFECT_TYPE_MONSTER_SPELL_SINGLE_TARGET_FIRE)
assert(spell({name = "combat", type = COMBAT_ICEDAMAGE, radius = 3}).impact == SOUND_EFFECT_TYPE_MONSTER_SPELL_SMALL_AREA_ICE)
assert(spell({name = "combat", type = COMBAT_DEATHDAMAGE, radius = 5}).impact == SOUND_EFFECT_TYPE_MONSTER_SPELL_LARGE_AREA_DEATH)
assert(spell({name = "combat", type = COMBAT_ENERGYDAMAGE, length = 8, spread = 3}).impact == SOUND_EFFECT_TYPE_MONSTER_SPELL_WAVE_ENERGY)
assert(spell({name = "combat", type = COMBAT_FIREDAMAGE, shootEffect = CONST_ANI_FIRE}).cast == SOUND_EFFECT_TYPE_MAGICAL_RANGE_ATK)
assert(spell({name = "combat", type = COMBAT_PHYSICALDAMAGE, shooteffect = CONST_ANI_BOLT}).cast == SOUND_EFFECT_TYPE_DIST_ATK_CROSSBOW)
assert(spell({name = "combat", type = COMBAT_FIREDAMAGE, soundCast = 123, soundImpact = 456}).impact == 456)
assert(spell({name = "combat", type = COMBAT_FIREDAMAGE, soundCast = 0, soundImpact = 0}).impact == 0)
assert(spell({name = "combat", type = COMBAT_FIREDAMAGE, impactCast = 789}).impact == 789)
assert(spell({name = "melee"}).impact ~= 0)
registerMonsterType.sounds(mtype, {Bestiary = {class = "Dragon"}})
assert(mtype.death == SOUND_EFFECT_TYPE_DRAGON_DEATH)
assert(mtype.voices[#mtype.voices][1] == SOUND_EFFECT_TYPE_DRAGON_BARK)
registerMonsterType.sounds(mtype, {sounds = {death = 0, ids = {123}, ticks = 6000, chance = 20}})
assert(mtype.death == 0)
assert(mtype.voices[#mtype.voices][2] == 6000)
dofile("data/scripts/lib/sound_configuration.lua")
local npc = setmetatable({ids = {}}, {__index = NpcType})
function npc:soundSpeedTicks(value) self.ticks = value end
function npc:soundChance(value) self.chance = value end
function npc:addSound(value) self.ids[#self.ids + 1] = value end
npc:sounds({ticks = 4000, chance = 0, ids = {2518, 2519}})
assert(npc.ticks == 4000 and npc.chance == 0 and #npc.ids == 2)
local charm = setmetatable({}, {__index = Charm})
function charm:castSound(value) self.cast = value end
function charm:impactSound(value) self.impact = value end
charm:sounds({castSound = 0, impactSound = 2002})
assert(charm.cast == 0 and charm.impact == 2002)
print("Sound registration regression checks passed")
