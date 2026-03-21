local leechChanceEffects = {
    [IMBUEMENT_TYPE_LIFE_LEECH] = SPECIALSKILL_LIFELEECHCHANCE,
    [IMBUEMENT_TYPE_MANA_LEECH] = SPECIALSKILL_MANALEECHCHANCE,
}

local function applyLeechChance(player, item, equip)
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
    applyLeechChance(player, item, equip)
    return true
end
ec:register()
