CharacterTitles = CharacterTitles or {}
local titles = {
  { id = 1, name = "Gold Hoarder", description = "Earned at least 1,000,000 gold.", permanent = false, level = 0 },
  { id = 2, name = "Platinum Hoarder", description = "Earned at least 10,000,000 gold.", permanent = false, level = 0 },
  { id = 3, name = "Crystal Hoarder", description = "Earned at least 100,000,000 gold.", permanent = false, level = 0 },
  { id = 4, name = "Beaststrider (Grade 1)", description = "Unlocked 10 or more Mounts.", permanent = true, level = 0 },
  { id = 5, name = "Beaststrider (Grade 2)", description = "Unlocked 20 or more Mounts.", permanent = true, level = 0 },
  { id = 6, name = "Beaststrider (Grade 3)", description = "Unlocked 30 or more Mounts.", permanent = true, level = 0 },
  { id = 7, name = "Beaststrider (Grade 4)", description = "Unlocked 40 or more Mounts.", permanent = true, level = 0 },
  { id = 8, name = "Beaststrider (Grade 5)", description = "Unlocked 50 or more Mounts.", permanent = true, level = 0 },
  { id = 9, name = "Tibia's Topmodel (Grade 1)", description = "Unlocked 10 or more Outfits.", permanent = true, level = 0 },
  { id = 10, name = "Tibia's Topmodel (Grade 2)", description = "Unlocked 20 or more Outfits.", permanent = true, level = 0 },
  { id = 11, name = "Tibia's Topmodel (Grade 3)", description = "Unlocked 30 or more Outfits.", permanent = true, level = 0 },
  { id = 12, name = "Tibia's Topmodel (Grade 4)", description = "Unlocked 40 or more Outfits.", permanent = true, level = 0 },
  { id = 13, name = "Tibia's Topmodel (Grade 5)", description = "Unlocked 50 or more Outfits.", permanent = true, level = 0 },
  { id = 14, name = "Trolltrasher", description = "Reached level 50.", permanent = false, level = 50 },
  { id = 15, name = "Cyclopscamper", description = "Reached level 100.", permanent = false, level = 100 },
  { id = 16, name = "Dragondouser", description = "Reached level 200.", permanent = false, level = 200 },
  { id = 17, name = "Demondoom", description = "Reached level 300.", permanent = false, level = 300 },
  { id = 18, name = "Drakenbane", description = "Reached level 400.", permanent = false, level = 400 },
  { id = 19, name = "Silencer", description = "Reached level 500.", permanent = false, level = 500 },
  { id = 20, name = "Exalted", description = "Reached level 1000.", permanent = false, level = 1000 },
  { id = 21, name = "Apex Predator", description = "Highest Level on character's world.", permanent = false, level = 0 },
  { id = 22, name = "Big Boss", description = "Highest score of accumulated boss points on character's world.", permanent = false, level = 0 },
  { id = 23, name = "Jack of all Taints", description = "Highest score for killing Goshnar and his aspects on character's world.", permanent = false, level = 0 },
  { id = 24, name = "Legend of Fishing", description = "Highest fishing level on character's world.", permanent = false, level = 0 },
  { id = 25, name = "Legend of Magic", description = "Highest magic level on character's world.", permanent = false, level = 0 },
  { id = 26, name = "Legend of Marksmanship", description = "Highest distance level on character's world.", permanent = false, level = 0 },
  { id = 27, name = "Legend of the Axe", description = "Highest axe level on character's world.", permanent = false, level = 0 },
  { id = 28, name = "Legend of the Club", description = "Highest club level on character's world.", permanent = false, level = 0 },
  { id = 29, name = "Legend of the Fist", description = "Highest fist level on character's world.", permanent = false, level = 0 },
  { id = 30, name = "Legend of the Shield", description = "Highest shielding level on character's world.", permanent = false, level = 0 },
  { id = 31, name = "Legend of the Sword", description = "Highest sword level on character's world.", permanent = false, level = 0 },
  { id = 32, name = "Prince Charming", description = "Highest score of accumulated charm points on character's world.", permanent = false, level = 0 },
  { id = 33, name = "Reigning Drome Champion", description = "Finished most recent Tibiadrome rota ranked in the top 5.", permanent = false, level = 0 },
  { id = 34, name = "Bipedantic", description = "Unlocked All Humanoid Bestiary entries.", permanent = false, level = 0 },
  { id = 35, name = "Blood Moon Hunter", description = "Unlocked All Lycanthrope Bestiary entries.", permanent = false, level = 0 },
  { id = 36, name = "Coldblooded", description = "Unlocked All Amphibic Bestiary entries.", permanent = false, level = 0 },
  { id = 37, name = "Death from Below", description = "Unlocked all Bird Bestiary entries.", permanent = false, level = 0 },
  { id = 38, name = "Demonator", description = "Unlocked all Demon Bestiary entries.", permanent = false, level = 0 },
  { id = 39, name = "Dragonslayer", description = "Unlocked all Dragon Bestiary entries.", permanent = false, level = 0 },
  { id = 40, name = "Elementalist", description = "Unlocked all Elemental Bestiary entries.", permanent = false, level = 0 },
  { id = 41, name = "Exterminator", description = "Unlocked all Vermin Bestiary entries.", permanent = false, level = 0 },
  { id = 42, name = "Fey Swatter", description = "Unlocked all Fey Bestiary entries.", permanent = false, level = 0 },
  { id = 43, name = "Ghosthunter", description = "Unlocked all Undead Bestiary entries.", permanent = false, level = 0 },
  { id = 44, name = "Handyman", description = "Unlocked all Construct Bestiary entries.", permanent = false, level = 0 },
  { id = 45, name = "Huntsman", description = "Unlocked all Mammal Bestiary entries.", permanent = false, level = 0 },
  { id = 46, name = "Interdimensional Destroyer", description = "Unlocked all Extra Dimensional Bestiary entries.", permanent = false, level = 0 },
  { id = 47, name = "Manhunter", description = "Unlocked all Human Bestiary entries.", permanent = false, level = 0 },
  { id = 48, name = "Master of Illusion", description = "Unlocked all Magical Bestiary entries.", permanent = false, level = 0 },
  { id = 49, name = "Ooze Blues", description = "Unlocked all Slime Bestiary entries.", permanent = false, level = 0 },
  { id = 50, name = "Sea Bane", description = "Unlocked all Aquatic Bestiary entries.", permanent = false, level = 0 },
  { id = 51, name = "Snake Charmer", description = "Unlocked all Reptile Bestiary entries.", permanent = false, level = 0 },
  { id = 52, name = "Tumbler", description = "Unlocked all Giant Bestiary entries.", permanent = false, level = 0 },
  { id = 53, name = "Weedkiller", description = "Unlocked all Plant Bestiary entries.", permanent = false, level = 0 },
  { id = 54, name = "Ink Eraser", description = "Unlocked all Inkborn Bestiary entries.", permanent = false, level = 0 },
  { id = 55, name = "Executioner", description = "Unlocked all Bestiary entries.", permanent = false, level = 0 },
  { id = 56, name = "Boss Annihilator", description = "Unlocked all Nemesis bosses.", permanent = false, level = 0 },
  { id = 57, name = "Boss Destroyer", description = "Unlocked 10 or more Archfoe bosses.", permanent = true, level = 0 },
  { id = 58, name = "Boss Devastator", description = "Unlocked 10 or more Nemesis bosses.", permanent = true, level = 0 },
  { id = 59, name = "Boss Eraser", description = "Unlocked all Archfoe bosses.", permanent = false, level = 0 },
  { id = 60, name = "Boss Executioner", description = "Unlocked all bosses.", permanent = false, level = 0 },
  { id = 61, name = "Boss Hunter", description = "Unlocked 10 or more Bane bosses.", permanent = true, level = 0 },
  { id = 62, name = "Boss Obliterator", description = "Unlocked 40 or more Nemesis bosses.", permanent = true, level = 0 },
  { id = 63, name = "Boss Slayer", description = "Unlocked all Bane bosses.", permanent = false, level = 0 },
  { id = 64, name = "Boss Smiter", description = "Unlocked 40 or more Archfoe bosses.", permanent = true, level = 0 },
  { id = 65, name = "Boss Veteran", description = "Unlocked 40 or more Bane bosses.", permanent = true, level = 0 },
  { id = 66, name = "Creature of Habit (Grade 1)", description = "Reward Streak of at least 7 days of consecutive logins.", permanent = true, level = 0 },
  { id = 67, name = "Creature of Habit (Grade 2)", description = "Reward Streak of at least 30 days of consecutive logins.", permanent = true, level = 0 },
  { id = 68, name = "Creature of Habit (Grade 3)", description = "Reward Streak of at least 90 days of consecutive logins.", permanent = true, level = 0 },
  { id = 69, name = "Creature of Habit (Grade 4)", description = "Reward Streak of at least 180 days of consecutive logins.", permanent = true, level = 0 },
  { id = 70, name = "Creature of Habit (Grade 5)", description = "Reward Streak of at least 365 days of consecutive logins.", permanent = true, level = 0 },
  { id = 71, name = "Aspiring Huntsman", description = "Invested 160,000 tasks points.", permanent = true, level = 0 },
  { id = 72, name = "Competent Beastslayer", description = "Invested 320,000 tasks points.", permanent = true, level = 0 },
  { id = 73, name = "Feared Bountyhunter", description = "Invested 430,000 tasks points.", permanent = true, level = 0 },
  { id = 74, name = "Dedicated Entrepreneur", description = "Explored 50% of all the map areas.", permanent = false, level = 0 },
  { id = 75, name = "Globetrotter", description = "Explored all map areas.", permanent = false, level = 0 },
  { id = 76, name = "Guild Leader", description = "Leading a Guild.", permanent = false, level = 0 },
  { id = 77, name = "Proconsul of Iksupan", description = "Only a true devotee to the cause of the ancient Iks and their lost legacy may step up to the rank of proconsul.", permanent = true, level = 0 },
  { id = 78, name = "Admirer of the Crown", description = "Adjust your crown and handle it.", permanent = true, level = 0 },
  { id = 79, name = "Big Spender", description = "Unlocked the full Golden Outfit.", permanent = true, level = 0 },
  { id = 80, name = "Challenger of the Iks", description = "Challenged Ahau, guardian of Iksupan, in traditional Iks warrior attire.", permanent = true, level = 0 },
  { id = 81, name = "Royal Bounacean Advisor", description = "Called to the court of Bounac by Kesar the Younger himself.", permanent = true, level = 0 },
  { id = 82, name = "Aeternal", description = "Awarded exclusively to stalwart heroes keeping the faith under all circumstances.", permanent = true, level = 0 },
  { id = 83, name = "Robinson Crusoe", description = "Some discoveries are reserved to only the most experienced adventurers. Until the next frontier opens on the horizon.", permanent = true, level = 0 },
  { id = 84, name = "Chompmeister", description = "Awarded only to true connoisseurs undertaking even the most exotic culinary escapades.", permanent = true, level = 0 },
  { id = 85, name = "Bringer of Rain", description = "Forging through battle after battle like a true gladiator.", permanent = true, level = 0 },
  { id = 86, name = "Beastly", description = "Reached 2000 charm points. Quite beastly!", permanent = true, level = 0 },
  { id = 87, name = "Midnight Hunter", description = "When the hunter becomes the hunted, perseverance decides the game.", permanent = true, level = 0 },
  { id = 88, name = "Ratinator", description = "Killing some snarky cave rats is helpful, killing over ten thousand of them is a statement.", permanent = true, level = 0 },
  { id = 89, name = "Doomsday Nemesis", description = "Awarded for great help in the battle against Gaz'haragoth.", permanent = true, level = 0 },
  { id = 90, name = "Hero of Bounac", description = "You prevailed during the battle of Bounac and broke the siege that held Bounac's people in its firm grasp.", permanent = true, level = 0 },
  { id = 91, name = "King of Demon", description = "Defeat Morshabaal 5 times.", permanent = true, level = 0 },
  { id = 92, name = "Planegazer", description = "Followed the trail of the Planestrider to the end.", permanent = true, level = 0 },
  { id = 93, name = "Time Traveller", description = "Anywhere in time or space.", permanent = true, level = 0 },
  { id = 94, name = "Truly Boss", description = "Reach 15,000 boss points.", permanent = true, level = 0 },
  { id = 95, name = "Pensive Wanderer", description = "You have honoured the Merudri by visiting all of their shrines and completing the ancient Three-Fold Path.", permanent = true, level = 0 },
}

local CURRENT_TITLE_STORAGE = 50001000
local UNLOCKED_TITLE_STORAGE = 50000000

local function available(player, title)
    if title.id >= 1 and title.id <= 3 then
        local thresholds = { 1000000, 10000000, 100000000 }
        if player:getBankBalance() + player:getMoney() >= thresholds[title.id] then
            return true
        end
    end
    return title.level > 0 and player:getLevel() >= title.level
        or player:getStorageValue(UNLOCKED_TITLE_STORAGE + title.id) > 0
end

function CharacterTitles.grant(player, id)
    id = tonumber(id)
    if not player or not id or not titles[id] then
        return false
    end
    player:setStorageValue(UNLOCKED_TITLE_STORAGE + id, 1)
    CharacterTitles.send(player, 0)
    return true
end

function CharacterTitles.send(player, characterId)
    if not player or (characterId ~= 0 and characterId ~= player:getId() and characterId ~= player:getGuid()) then
        return
    end
    local current = math.max(0, player:getStorageValue(CURRENT_TITLE_STORAGE))
    if current > 0 and (not titles[current] or not available(player, titles[current])) then
        current = 0
        player:setStorageValue(CURRENT_TITLE_STORAGE, 0)
    end
    local msg = NetworkMessage(player)
    msg:addByte(0xDA)
    msg:addByte(11)
    msg:addByte(0)
    msg:addByte(current)
    msg:addByte(#titles)
    for _, title in ipairs(titles) do
        msg:addByte(title.id)
        msg:addString(title.name)
        msg:addString(title.description)
        msg:addByte(title.permanent and 1 or 0)
        msg:addByte(available(player, title) and 1 or 0)
    end
    msg:sendToPlayer(player)
end

local selection = PacketHandler(0x81)
function selection.onReceive(player, msg)
    if not player or not ((player.isUsingFonticakClient and player:isUsingFonticakClient())
        or (player.isUsingAstraClient and player:isUsingAstraClient())) then
        return
    end
    local action = NetworkGuard.readByte(msg)
    local id = NetworkGuard.readByte(msg)
    if action ~= 0x0E or id == nil then
        return
    end
    if id ~= 0 and (not titles[id] or not available(player, titles[id])) then
        return
    end
    player:setStorageValue(CURRENT_TITLE_STORAGE, id)
    CharacterTitles.send(player, 0)
end
selection:register()

