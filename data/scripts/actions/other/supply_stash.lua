local SUPPLY_STASH_ITEM_ID = ITEM_SUPPLY_STASH or 28750

local action = Action()
function action.onUse(player, item, fromPosition, target, toPosition, isHotkey)
    if not player:isUsingOtClient() then
        player:sendCancelMessage("The supply stash is only available on OTClient.")
        return true
    end

    if not CustomSupplyStash then
        player:sendCancelMessage("The supply stash is not available.")
        return true
    end

    if not CustomSupplyStash.open then
        player:sendCancelMessage("The supply stash is not available.")
        return true
    end

    local depotId = 0
    if player.getLastDepotId then
        local ok, value = pcall(function()
            return player:getLastDepotId()
        end)
        depotId = tonumber(ok and value or nil) or 0
    end

    local ok, err = pcall(function()
        CustomSupplyStash.open(player, depotId)
    end)

    if not ok then
        player:sendCancelMessage("Error opening supply stash.")
        return true
    end

    return true
end
action:id(SUPPLY_STASH_ITEM_ID)
action:register()
