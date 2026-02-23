-- ============================================================
-- EQUIPMENT REGISTER - TFS 1.5+ RevScript
-- Centralized file for equipment (Equip/DeEquip)
-- made by condinBlack. Thanks for the script converter, similar to this spell converter:
-- https://otland.net/threads/1-3-spell-converter.280247/
-- ============================================================

local equipment = {
    -- Boots
    {id = 3079, slot = "feet"}, -- boots of haste
    {id = 3549, slot = "feet"}, -- soft boots
    {id = 6529, slot = "feet"}, -- soft boots (worn)
    {id = 813, slot = "feet", level = 35, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- fur boots
    {id = 818, slot = "feet", level = 35, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- terra boots
    {id = 819, slot = "feet", level = 35, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- firewalker boots
    {id = 820, slot = "feet", level = 35, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- glacier kilt
    {id = 9018, slot = "feet", level = 130}, -- firewalker boots
    {id = 9019, slot = "feet", level = 130}, -- firewalker boots
    {id = 10200, slot = "feet", level = 70, vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- guardian boots
    {id = 10201, slot = "feet", level = 70, vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- guardian boots
    {id = 10323, slot = "feet", level = 70, vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- guardian boots
    {id = 10386, slot = "feet"}, -- boots of haste
    {id = 4033, slot = "feet", level = 80, vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- draken boots

    -- Amulets
    {id = 3045, slot = "necklace"}, -- blue crystal amulet
    {id = 3054, slot = "necklace"}, -- silver amulet
    {id = 3056, slot = "necklace"}, -- bronze amulet
    {id = 3057, slot = "necklace"}, -- garlic necklace
    {id = 3081, slot = "necklace"}, -- stone skin amulet
    {id = 3082, slot = "necklace"}, -- protection amulet
    {id = 3083, slot = "necklace"}, -- strange talisman
    {id = 3084, slot = "necklace"}, -- elven amulet
    {id = 3085, slot = "necklace"}, -- dragon necklace
    {id = 814, slot = "necklace", level = 60}, -- platinum amulet
    {id = 815, slot = "necklace", level = 60}, -- lightning pendant
    {id = 816, slot = "necklace", level = 60}, -- glacier amulet
    {id = 817, slot = "necklace", level = 60}, -- terra amulet
    {id = 7532, slot = "necklace"}, -- spirit amulet
    {id = 9301, slot = "necklace", level = 80}, -- sacred tree amulet
    {id = 9302, slot = "necklace", level = 80}, -- shockwave amulet
    {id = 9303, slot = "necklace", level = 80}, -- leviathan's amulet
    {id = 9304, slot = "necklace", level = 80}, -- volcano amulet
    {id = 10457, slot = "necklace"}, -- bronze amulet

    -- Rings
    {id = 3048, slot = "ring"}, -- might ring
    {id = 3049, slot = "ring"}, -- stealth ring
    {id = 3050, slot = "ring"}, -- power ring
    {id = 3051, slot = "ring"}, -- energy ring
    {id = 3052, slot = "ring"}, -- life ring
    {id = 3053, slot = "ring"}, -- time ring
    {id = 3086, slot = "ring"}, -- ring of healing
    {id = 3087, slot = "ring"}, -- dwarven ring
    {id = 3088, slot = "ring"}, -- ring of the sky
    {id = 3089, slot = "ring"}, -- sword ring
    {id = 3090, slot = "ring"}, -- axe ring
    {id = 3091, slot = "ring"}, -- club ring
    {id = 3092, slot = "ring"}, -- ornamented brooch
    {id = 3093, slot = "ring"}, -- death ring
    {id = 3094, slot = "ring"}, -- sword ring
    {id = 3095, slot = "ring"}, -- axe ring
    {id = 3096, slot = "ring"}, -- club ring
    {id = 3097, slot = "ring"}, -- dwarven ring
    {id = 3098, slot = "ring"}, -- warrior's ring
    {id = 3099, slot = "ring"}, -- ring of healing
    {id = 3100, slot = "ring"}, -- dragon scale mail
    {id = 6299, slot = "ring"}, -- death ring
    {id = 6300, slot = "ring"}, -- death ring
    {id = 9386, slot = "ring", level = 100}, -- elite draken mail
    {id = 9394, slot = "ring", level = 100}, -- prismatic ring
    {id = 10323, slot = "ring", level = 10}, -- ring of healing

    -- Helmets
    {id = 3210, slot = "head", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- hat of the mad
    {id = 3230, slot = "head"}, -- chain helmet
    {id = 3368, slot = "head"}, -- mystic turban
    {id = 3396, slot = "head"}, -- demon helmet
    {id = 3575, slot = "head", vocation = {"Paladin", "Royal Paladin"}}, -- ranger's cloak
    {id = 5460, slot = "head"}, -- pirate hat
    {id = 7459, slot = "head"}, -- pumpkinhead
    {id = 827, slot = "head", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- spirit cloak
    {id = 828, slot = "head", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- glacier robe
    {id = 829, slot = "head", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- lightning robe
    {id = 830, slot = "head", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- magma coat
    {id = 7992, slot = "head", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- velvet mantle
    {id = 8864, slot = "head", level = 80, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- yalahari mask
    {id = 9103, slot = "head", level = 50, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- witch hat
    {id = 9653, slot = "head", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- bonelord helmet
    {id = 10385, slot = "head", vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- zaoan helmet
    {id = 10451, slot = "head", level = 60, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- dragon scale helmet
    {id = 11674, slot = "head", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- winged helmet
    {id = 11689, slot = "head", level = 100, vocation = {"Paladin", "Royal Paladin"}}, -- royal helmet

    -- Armors
    {id = 3360, slot = "armor", vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- golden armor
    {id = 3366, slot = "armor", vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- magic plate armor
    {id = 3370, slot = "armor", vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- knight armor
    {id = 3381, slot = "armor", vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- crown armor
    {id = 3386, slot = "armor", vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- dragon scale mail
    {id = 3394, slot = "armor", level = 60, vocation = {"Paladin", "Royal Paladin"}}, -- royal scale robe
    {id = 3397, slot = "armor"}, -- dwarven armor
    {id = 3571, slot = "armor", vocation = {"Paladin", "Royal Paladin"}}, -- noble armor
    {id = 811, slot = "armor", level = 50, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- focus cape
    {id = 824, slot = "armor", level = 50, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- spirit cloak
    {id = 825, slot = "armor", level = 50, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- glacier robe
    {id = 826, slot = "armor", level = 50, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- lightning robe
    {id = 7991, slot = "armor", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- velvet mantle
    {id = 7993, slot = "armor", level = 50}, -- rainbow shield
    {id = 8037, slot = "armor", level = 65, vocation = {"Sorcerer", "Master Sorcerer"}}, -- fireborn giant armor
    {id = 8038, slot = "armor", level = 75, vocation = {"Druid", "Elder Druid"}}, -- frozen plate
    {id = 8039, slot = "armor", level = 75, vocation = {"Sorcerer", "Master Sorcerer"}}, -- fireborn giant armor
    {id = 8040, slot = "armor", level = 75, vocation = {"Sorcerer", "Master Sorcerer"}}, -- fireborn giant armor
    {id = 8041, slot = "armor", level = 75, vocation = {"Druid", "Elder Druid"}}, -- earthborn titan armor
    {id = 8042, slot = "armor", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- windborn colossus armor
    {id = 8043, slot = "armor", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- fireborn giant armor
    {id = 8044, slot = "armor", vocation = {"Paladin", "Royal Paladin"}}, -- fireproof adventure backpack
    {id = 8049, slot = "armor", level = 60, vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- firewalker boots
    {id = 8050, slot = "armor", level = 60, vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- fireproof adventure backpack
    {id = 8051, slot = "armor", level = 60, vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- fireborn giant armor
    {id = 8052, slot = "armor", level = 60, vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- frozen plate
    {id = 8053, slot = "armor", level = 100, vocation = {"Knight", "Elite Knight"}}, -- master archer's armor
    {id = 8054, slot = "armor", level = 100, vocation = {"Knight", "Elite Knight"}}, -- fireborn giant armor
    {id = 8055, slot = "armor", level = 100, vocation = {"Knight", "Elite Knight"}}, -- frozen plate
    {id = 8056, slot = "armor", level = 100, vocation = {"Knight", "Elite Knight"}}, -- earthborn titan armor
    {id = 8057, slot = "armor", level = 75, vocation = {"Paladin", "Royal Paladin"}}, -- paladin armor
    {id = 8058, slot = "armor", level = 75, vocation = {"Paladin", "Royal Paladin"}}, -- fireproof adventure backpack
    {id = 8059, slot = "armor", level = 75, vocation = {"Paladin", "Royal Paladin"}}, -- frozen plate
    {id = 8060, slot = "armor", level = 100, vocation = {"Paladin", "Royal Paladin"}}, -- royal crossbow
    {id = 8061, slot = "armor", level = 85, vocation = {"Knight", "Elite Knight"}}, -- skullcracker armor
    {id = 8062, slot = "armor", level = 100, vocation = {"Sorcerer", "Master Sorcerer"}}, -- fireborn giant armor
    {id = 8063, slot = "armor", vocation = {"Paladin", "Royal Paladin"}}, -- composite hornbow
    {id = 8064, slot = "armor", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- wand of voodoo
    {id = 8862, slot = "armor", level = 80, vocation = {"Knight", "Elite Knight"}}, -- elite draken mail
    {id = 10384, slot = "armor", level = 50, vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- zaoan armor
    {id = 10438, slot = "armor", level = 60, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- gill gugel
    {id = 10439, slot = "armor", level = 60, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- gill coat
    {id = 11651, slot = "armor", level = 100, vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- heroic armor
    {id = 11686, slot = "armor", level = 100, vocation = {"Knight", "Elite Knight"}}, -- demon armor
    {id = 11687, slot = "armor", level = 100, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- draconian robe

    -- Legs
    {id = 3364, slot = "legs", vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- golden legs
    {id = 3371, slot = "legs", vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- knight legs
    {id = 3382, slot = "legs", vocation = {"Knight", "Elite Knight", "Paladin", "Royal Paladin"}}, -- crown legs
    {id = 3398, slot = "legs"}, -- dwarven legs
    {id = 812, slot = "legs", level = 40, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- spirit pants
    {id = 821, slot = "legs", level = 40, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- glacier kilt
    {id = 822, slot = "legs", level = 40, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- lightning legs
    {id = 823, slot = "legs", level = 40, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- magma legs
    {id = 8095, slot = "legs", vocation = {"Paladin", "Royal Paladin"}}, -- ranger's legs
    {id = 8863, slot = "legs", level = 80, vocation = {"Paladin", "Royal Paladin"}}, -- yalahari leg piece
    {id = 10387, slot = "legs"}, -- zaoan legs

    -- Shields
    {id = 3059, slot = "shield", vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- spellbook
    {id = 8072, slot = "shield", level = 30, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- spellbook of enlightenment
    {id = 8073, slot = "shield", level = 40, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- spellbook of warding
    {id = 8074, slot = "shield", level = 50, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- spellbook of mind control
    {id = 8075, slot = "shield", level = 60, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- spellbook of lost souls
    {id = 8076, slot = "shield", level = 70, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- spellscroll of prophecies
    {id = 8077, slot = "shield", level = 100, vocation = {"Knight", "Elite Knight"}}, -- ornate shield
    {id = 8078, slot = "shield", level = 100, vocation = {"Knight", "Elite Knight"}}, -- master archer's armor
    {id = 8079, slot = "shield", level = 100, vocation = {"Knight", "Elite Knight"}}, -- fireborn giant armor
    {id = 8080, slot = "shield", level = 100, vocation = {"Knight", "Elite Knight"}}, -- frozen plate
    {id = 8081, slot = "shield", level = 100, vocation = {"Knight", "Elite Knight"}}, -- earthborn titan armor
    {id = 8090, slot = "shield", level = 80, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}}, -- spellbook of ancient arcana
    {id = 11688, slot = "shield", level = 80, vocation = {"Knight", "Elite Knight"}}, -- draken elite helmet
    {id = 11691, slot = "shield", level = 100, vocation = {"Sorcerer", "Master Sorcerer", "Druid", "Elder Druid"}} -- umbral master spellbook
}

for _, equip in ipairs(equipment) do
    local equipEvent = MoveEvent()
    
    equipEvent.onEquip = function(player, item, slot, isCheck)
        return true
    end
    
    equipEvent:type("equip")
    equipEvent:id(equip.id)
    equipEvent:slot(equip.slot)
    
    if equip.level then
        equipEvent:level(equip.level)
    end
    
    if equip.vocation then
        for _, voc in ipairs(equip.vocation) do
            equipEvent:vocation(voc)
        end
    end
    equipEvent:register()
    
    local deEquipEvent = MoveEvent()
    deEquipEvent.onDeEquip = function(player, item, slot, isCheck)
        return true
    end
    
    deEquipEvent:type("deequip")
    deEquipEvent:id(equip.id)
    deEquipEvent:slot(equip.slot)
    deEquipEvent:register()
end
