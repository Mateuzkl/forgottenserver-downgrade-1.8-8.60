local config = {
    onlyPremium = true
}

local talkaction = TalkAction("!buyhouse")
function talkaction.onSay(player, words, param)
    local housePrice = configManager.getNumber(configKeys.HOUSE_PRICE)
    if housePrice == -1 then
        return true
    end

    if player:getLevel() < configManager.getNumber(configKeys.HOUSE_LEVEL) then
        player:sendCancelMessage("You need level " .. configManager.getNumber(configKeys.HOUSE_LEVEL) .. " or higher to buy a house.")
        return false
    end

    if config.onlyPremium and not player:isPremium() then
        player:sendCancelMessage("You need a premium account.")
        return false
    end

    local position = player:getPosition()
    position:getNextPosition(player:getDirection())

    local tile = Tile(position)
    if not tile then
        player:sendCancelMessage("You have to be looking at the door of the house you would like to buy.")
        return false
    end

    local house = tile:getHouse()
    if not house then
        player:sendCancelMessage("You have to be looking at the door of the house you would like to buy.")
        return false
    end

    if house:getType() ~= HOUSE_TYPE_NORMAL then
        player:sendCancelMessage("This is a guildhall. Use !buygh to buy it.")
        return false
    end

    if house:getOwnerGuid() > 0 then
        player:sendCancelMessage("This house already has an owner.")
        return false
    end

    if player:getHouse() then
        player:sendCancelMessage("You are already the owner of a house.")
        return false
    end

    local price = house:getTileCount() * housePrice

    local inventoryMoney = player:getMoney()
    local bankBalance = player:getBankBalance()
    local totalMoney = inventoryMoney + bankBalance

    if totalMoney < price then
        player:sendCancelMessage("You do not have enough money. Price: " .. price .. " gold. You have " .. totalMoney .. " gold (inventory: " .. inventoryMoney .. ", bank: " .. bankBalance .. ").")
        return false
    end

    local paidInventory = 0
    local paidBank = 0
    if inventoryMoney >= price then
        if not player:removeMoney(price) then
            player:sendCancelMessage("Error removing money from inventory.")
            return false
        end
        paidInventory = price
    else
        if inventoryMoney > 0 and not player:removeMoney(inventoryMoney) then
            player:sendCancelMessage("Error removing money from inventory.")
            return false
        end
        paidInventory = inventoryMoney
        paidBank = price - inventoryMoney
        player:setBankBalance(bankBalance - paidBank)
    end

    if not house:setOwnerGuid(player:getGuid()) then
        if paidBank > 0 then
            player:setBankBalance(player:getBankBalance() + paidBank)
        end
        if paidInventory > 0 then
            player:addMoney(paidInventory)
        end
        player:sendCancelMessage("Could not complete the house purchase. Your gold has been returned.")
        return false
    end
    player:saveAsync()
    player:sendTextMessage(MESSAGE_INFO_DESCR, "You have successfully bought this house for " .. price .. " gold. Be sure to have the money for rent in the bank.")
    return false
end
talkaction:register()

