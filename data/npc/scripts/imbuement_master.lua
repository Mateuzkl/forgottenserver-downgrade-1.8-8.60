local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

local voices = {
	{text = "Imbuement scrolls and etchers! Everything for your equipment enchanting needs!"},
	{text = "Looking for imbuement scrolls? Just ask me for a trade!"},
}
npcHandler:addModule(VoiceModule:new(voices))

local shopModule = ShopModule:new()
npcHandler:addModule(shopModule)

keywordHandler:addKeyword({'scrolls'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'I sell {intricate} and {powerful} imbuement scrolls, plus the {etcher} tool. Say {trade} to see my full catalog!'
})
keywordHandler:addAliasKeyword({'offer'})
keywordHandler:addAliasKeyword({'stuff'})
keywordHandler:addAliasKeyword({'wares'})
keywordHandler:addAliasKeyword({'imbuement'})
keywordHandler:addAliasKeyword({'imbuements'})

keywordHandler:addKeyword({'etcher'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The etcher is needed to apply scrolls on a workbench. Place your equipment and scrolls on the workbench, then use the etcher on it.'
})

keywordHandler:addKeyword({'workbench'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'You need a workbench to apply imbuements. Place your equipment and the scrolls inside the workbench, then use the etcher on it.'
})

keywordHandler:addKeyword({'intricate'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Intricate scrolls are tier 2 imbuements. They cost 60,000 gold to apply. Say {trade} to see them!'
})

keywordHandler:addKeyword({'powerful'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Powerful scrolls are tier 3 imbuements - the strongest! They cost 250,000 gold to apply. Say {trade} to see them!'
})

-- ===== TOOL =====
shopModule:addBuyableItem({'etcher'}, 51443, 5000, 'etcher')

-- ===== INTRICATE SCROLLS (Tier 2) =====
-- Skill Boosts
shopModule:addBuyableItem({'intricate bash scroll'}, 51724, 15000, 'intricate bash scroll')
shopModule:addBuyableItem({'intricate blockade scroll'}, 51725, 15000, 'intricate blockade scroll')
shopModule:addBuyableItem({'intricate chop scroll'}, 51726, 15000, 'intricate chop scroll')
shopModule:addBuyableItem({'intricate epiphany scroll'}, 51731, 15000, 'intricate epiphany scroll')
shopModule:addBuyableItem({'intricate precision scroll'}, 51735, 15000, 'intricate precision scroll')
shopModule:addBuyableItem({'intricate punch scroll'}, 51736, 15000, 'intricate punch scroll')
shopModule:addBuyableItem({'intricate slash scroll'}, 51740, 15000, 'intricate slash scroll')

-- Elemental Damage
shopModule:addBuyableItem({'intricate electrify scroll'}, 51730, 15000, 'intricate electrify scroll')
shopModule:addBuyableItem({'intricate frost scroll'}, 51733, 15000, 'intricate frost scroll')
shopModule:addBuyableItem({'intricate reap scroll'}, 51738, 15000, 'intricate reap scroll')
shopModule:addBuyableItem({'intricate scorch scroll'}, 51739, 15000, 'intricate scorch scroll')
shopModule:addBuyableItem({'intricate venom scroll'}, 51745, 15000, 'intricate venom scroll')

-- Elemental Protection
shopModule:addBuyableItem({'intricate cloud fabric scroll'}, 51727, 15000, 'intricate cloud fabric scroll')
shopModule:addBuyableItem({'intricate demon presence scroll'}, 51728, 15000, 'intricate demon presence scroll')
shopModule:addBuyableItem({'intricate dragon hide scroll'}, 51729, 15000, 'intricate dragon hide scroll')
shopModule:addBuyableItem({'intricate lich shroud scroll'}, 51734, 15000, 'intricate lich shroud scroll')
shopModule:addBuyableItem({'intricate quara scale scroll'}, 51737, 15000, 'intricate quara scale scroll')
shopModule:addBuyableItem({'intricate snake skin scroll'}, 51741, 15000, 'intricate snake skin scroll')

-- Leech / Critical / Other
shopModule:addBuyableItem({'intricate strike scroll'}, 51742, 15000, 'intricate strike scroll')
shopModule:addBuyableItem({'intricate vampirism scroll'}, 51744, 15000, 'intricate vampirism scroll')
shopModule:addBuyableItem({'intricate void scroll'}, 51747, 15000, 'intricate void scroll')
shopModule:addBuyableItem({'intricate swiftness scroll'}, 51743, 15000, 'intricate swiftness scroll')
shopModule:addBuyableItem({'intricate featherweight scroll'}, 51732, 15000, 'intricate featherweight scroll')
shopModule:addBuyableItem({'intricate vibrancy scroll'}, 51746, 15000, 'intricate vibrancy scroll')

-- ===== POWERFUL SCROLLS (Tier 3) =====
-- Skill Boosts
shopModule:addBuyableItem({'powerful bash scroll'}, 51444, 50000, 'powerful bash scroll')
shopModule:addBuyableItem({'powerful blockade scroll'}, 51445, 50000, 'powerful blockade scroll')
shopModule:addBuyableItem({'powerful chop scroll'}, 51446, 50000, 'powerful chop scroll')
shopModule:addBuyableItem({'powerful epiphany scroll'}, 51451, 50000, 'powerful epiphany scroll')
shopModule:addBuyableItem({'powerful precision scroll'}, 51455, 50000, 'powerful precision scroll')
shopModule:addBuyableItem({'powerful punch scroll'}, 51456, 50000, 'powerful punch scroll')
shopModule:addBuyableItem({'powerful slash scroll'}, 51460, 50000, 'powerful slash scroll')

-- Elemental Damage
shopModule:addBuyableItem({'powerful electrify scroll'}, 51450, 50000, 'powerful electrify scroll')
shopModule:addBuyableItem({'powerful frost scroll'}, 51453, 50000, 'powerful frost scroll')
shopModule:addBuyableItem({'powerful reap scroll'}, 51458, 50000, 'powerful reap scroll')
shopModule:addBuyableItem({'powerful scorch scroll'}, 51459, 50000, 'powerful scorch scroll')
shopModule:addBuyableItem({'powerful venom scroll'}, 51465, 50000, 'powerful venom scroll')

-- Elemental Protection
shopModule:addBuyableItem({'powerful cloud fabric scroll'}, 51447, 50000, 'powerful cloud fabric scroll')
shopModule:addBuyableItem({'powerful demon presence scroll'}, 51448, 50000, 'powerful demon presence scroll')
shopModule:addBuyableItem({'powerful dragon hide scroll'}, 51449, 50000, 'powerful dragon hide scroll')
shopModule:addBuyableItem({'powerful lich shroud scroll'}, 51454, 50000, 'powerful lich shroud scroll')
shopModule:addBuyableItem({'powerful quara scale scroll'}, 51457, 50000, 'powerful quara scale scroll')
shopModule:addBuyableItem({'powerful snake skin scroll'}, 51461, 50000, 'powerful snake skin scroll')

-- Leech / Critical / Other
shopModule:addBuyableItem({'powerful strike scroll'}, 51462, 50000, 'powerful strike scroll')
shopModule:addBuyableItem({'powerful vampirism scroll'}, 51464, 50000, 'powerful vampirism scroll')
shopModule:addBuyableItem({'powerful void scroll'}, 51467, 50000, 'powerful void scroll')
shopModule:addBuyableItem({'powerful swiftness scroll'}, 51463, 50000, 'powerful swiftness scroll')
shopModule:addBuyableItem({'powerful featherweight scroll'}, 51452, 50000, 'powerful featherweight scroll')
shopModule:addBuyableItem({'powerful vibrancy scroll'}, 51466, 50000, 'powerful vibrancy scroll')

-- ===== IMBUEMENT MATERIALS (like CrystalServer) =====
-- Critical
shopModule:addBuyableItem({'protective charm'}, 11444, 60, 'protective charm')
shopModule:addBuyableItem({'sabretooth'}, 10311, 400, 'sabretooth')
shopModule:addBuyableItem({'vexclaw talon'}, 22728, 1100, 'vexclaw talon')

-- Life Leech
shopModule:addBuyableItem({'vampire teeth'}, 9685, 275, 'vampire teeth')
shopModule:addBuyableItem({'bloody pincers'}, 9633, 100, 'bloody pincers')
shopModule:addBuyableItem({'piece of dead brain'}, 9663, 420, 'piece of dead brain')

-- Mana Leech
shopModule:addBuyableItem({'rope belt'}, 11492, 66, 'rope belt')
shopModule:addBuyableItem({'silencer claws'}, 20200, 390, 'silencer claws')
shopModule:addBuyableItem({'some grimeleech wings'}, 22730, 1200, 'some grimeleech wings')

-- Elemental Damage
shopModule:addBuyableItem({'pile of grave earth'}, 11484, 25, 'pile of grave earth')
shopModule:addBuyableItem({'demonic skeletal hand'}, 9647, 80, 'demonic skeletal hand')
shopModule:addBuyableItem({'petrified scream'}, 10420, 250, 'petrified scream')

shopModule:addBuyableItem({'swamp grass'}, 9686, 20, 'swamp grass')
shopModule:addBuyableItem({'poisonous slime'}, 9640, 50, 'poisonous slime')
shopModule:addBuyableItem({'slime heart'}, 21194, 160, 'slime heart')

shopModule:addBuyableItem({'rorc feather'}, 18993, 70, 'rorc feather')
shopModule:addBuyableItem({'peacock feather fan'}, 21975, 350, 'peacock feather fan')
shopModule:addBuyableItem({'energy vein'}, 23508, 270, 'energy vein')

shopModule:addBuyableItem({'fiery heart'}, 9636, 375, 'fiery heart')
shopModule:addBuyableItem({'green dragon scale'}, 5920, 100, 'green dragon scale')
shopModule:addBuyableItem({'demon horn'}, 5954, 1000, 'demon horn')

shopModule:addBuyableItem({'frosty heart'}, 9661, 280, 'frosty heart')
shopModule:addBuyableItem({'seacrest hair'}, 21801, 260, 'seacrest hair')
shopModule:addBuyableItem({'polar bear paw'}, 9650, 30, 'polar bear paw')

-- Elemental Protection
shopModule:addBuyableItem({'flask of embalming fluid'}, 11466, 30, 'flask of embalming fluid')
shopModule:addBuyableItem({'gloom wolf fur'}, 22007, 70, 'gloom wolf fur')
shopModule:addBuyableItem({'mystical hourglass'}, 9660, 700, 'mystical hourglass')

shopModule:addBuyableItem({'piece of swampling wood'}, 17823, 30, 'piece of swampling wood')
shopModule:addBuyableItem({'snake skin'}, 9694, 400, 'snake skin')
shopModule:addBuyableItem({'brimstone fangs'}, 11702, 380, 'brimstone fangs')

shopModule:addBuyableItem({'wyvern talisman'}, 9644, 265, 'wyvern talisman')
shopModule:addBuyableItem({'crawler head plating'}, 14079, 210, 'crawler head plating')
shopModule:addBuyableItem({'wyrm scale'}, 9665, 400, 'wyrm scale')

shopModule:addBuyableItem({'green dragon leather'}, 5877, 100, 'green dragon leather')
shopModule:addBuyableItem({'blazing bone'}, 16131, 610, 'blazing bone')
shopModule:addBuyableItem({'draken sulphur'}, 11658, 550, 'draken sulphur')

shopModule:addBuyableItem({'cultish robe'}, 9639, 150, 'cultish robe')
shopModule:addBuyableItem({'cultish mask'}, 9638, 280, 'cultish mask')
shopModule:addBuyableItem({'hellspawn tail'}, 10304, 475, 'hellspawn tail')

shopModule:addBuyableItem({'winter wolf fur'}, 10295, 20, 'winter wolf fur')
shopModule:addBuyableItem({'thick fur'}, 10307, 150, 'thick fur')
shopModule:addBuyableItem({'deepling warts'}, 14012, 180, 'deepling warts')

-- Skill Boosts
shopModule:addBuyableItem({'orc tooth'}, 10196, 150, 'orc tooth')
shopModule:addBuyableItem({'battle stone'}, 11447, 290, 'battle stone')
shopModule:addBuyableItem({'moohtant horn'}, 21200, 140, 'moohtant horn')

shopModule:addBuyableItem({'cyclops toe'}, 9657, 55, 'cyclops toe')
shopModule:addBuyableItem({'ogre nose ring'}, 22189, 210, 'ogre nose ring')
shopModule:addBuyableItem({"warmaster's wristguards"}, 10405, 200, "warmaster's wristguards")

shopModule:addBuyableItem({'elven scouting glass'}, 11464, 50, 'elven scouting glass')
shopModule:addBuyableItem({'elven hoof'}, 18994, 115, 'elven hoof')
shopModule:addBuyableItem({'metal spike'}, 10298, 320, 'metal spike')

shopModule:addBuyableItem({"lion's mane"}, 9691, 60, "lion's mane")
shopModule:addBuyableItem({"mooh'tah shell"}, 21202, 110, "mooh'tah shell")
shopModule:addBuyableItem({'war crystal'}, 9654, 460, 'war crystal')

shopModule:addBuyableItem({'elvish talisman'}, 9635, 45, 'elvish talisman')
shopModule:addBuyableItem({'broken shamanic staff'}, 11452, 35, 'broken shamanic staff')
shopModule:addBuyableItem({'strand of medusa hair'}, 10309, 600, 'strand of medusa hair')

shopModule:addBuyableItem({'piece of scarab shell'}, 9641, 45, 'piece of scarab shell')
shopModule:addBuyableItem({'brimstone shell'}, 11703, 210, 'brimstone shell')
shopModule:addBuyableItem({'frazzle skin'}, 20199, 400, 'frazzle skin')

-- Speed / Capacity / Paralysis
shopModule:addBuyableItem({'damselfly wing'}, 17458, 20, 'damselfly wing')
shopModule:addBuyableItem({'compass'}, 10302, 45, 'compass')
shopModule:addBuyableItem({'waspoid wing'}, 14081, 190, 'waspoid wing')

shopModule:addBuyableItem({'fairy wings'}, 25694, 200, 'fairy wings')
shopModule:addBuyableItem({'little bowl of myrrh'}, 25702, 500, 'little bowl of myrrh')
shopModule:addBuyableItem({'goosebump leather'}, 20205, 650, 'goosebump leather')

shopModule:addBuyableItem({'wereboar hooves'}, 22053, 175, 'wereboar hooves')
shopModule:addBuyableItem({'crystallized anger'}, 23507, 400, 'crystallized anger')
shopModule:addBuyableItem({'quill'}, 28567, 1100, 'quill')

function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then return false end
	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
