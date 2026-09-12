local leechChanceEffects = {
    [IMBUEMENT_TYPE_LIFE_LEECH] = SPECIALSKILL_LIFELEECHCHANCE,
    [IMBUEMENT_TYPE_MANA_LEECH] = SPECIALSKILL_MANALEECHCHANCE,
}

local function applyLeechChance(player, item, equip)
    if not player or not item then
        return
    end

    if not item:isItem() or not item:hasImbuements() then
        return
    end

    for imbuementType, skill in pairs(leechChanceEffects) do
        if item:hasImbuementType(imbuementType) then
            player:addSpecialSkill(skill, equip and 10000 or -10000)
        end
    end
end

local ec = Event()
function ec.onUpdateInventory(player, item, slot, equip)
    if not player or not item then
        return true
    end

    applyLeechChance(player, item, equip)

    if player.wheelSendSkillStats then
        player:wheelSendSkillStats()
    end
    if player.cyclopediaSendCharacterOffence then
        player:cyclopediaSendCharacterOffence()
    end

    if slot == CONST_SLOT_LEFT or slot == CONST_SLOT_RIGHT then
        local playerGuid = player:getGuid()

        addEvent(function(guid)
            local freshPlayer = Player(guid)
            if not freshPlayer then
                return
            end

            if WeaponProficiencySystem
                and WeaponProficiencySystem.refreshEquippedPerks then
                WeaponProficiencySystem.refreshEquippedPerks(freshPlayer)
            end
        end, 100, playerGuid)
    end

    return true
end
ec:register()