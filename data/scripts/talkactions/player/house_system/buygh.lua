local config = {
    onlyPremium = true
}

local talkaction = TalkAction("!buygh")
function talkaction.onSay(player, words, param)
    local housePrice = configManager.getNumber(configKeys.HOUSE_PRICE)
    if housePrice == -1 then
        return true
    end

    if player:getLevel() < configManager.getNumber(configKeys.HOUSE_LEVEL) then
        player:sendCancelMessage("You need level " .. configManager.getNumber(configKeys.HOUSE_LEVEL) .. " or higher to buy a guildhall.")
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
        player:sendCancelMessage("You have to be looking at the door of the guildhall you would like to buy.")
        return false
    end

    local house = tile:getHouse()
    if not house then
        player:sendCancelMessage("You have to be looking at the door of the guildhall you would like to buy.")
        return false
    end

    if house:getType() ~= HOUSE_TYPE_GUILDHALL then
        player:sendCancelMessage("This is a normal house. Use !buyhouse to buy it.")
        return false
    end

    if house:getOwnerGuild() > 0 then
        player:sendCancelMessage("This guildhall already has an owner.")
        return false
    end

    local guild = player:getGuild()
    if not guild then
        player:sendCancelMessage("You are not in a guild.")
        return false
    end

    local isLeader = player:isGuildLeader()
    local isVice = player:isGuildVice()
    if not (isLeader or isVice) then
        player:sendCancelMessage("Only Guild Leader or Vice-Leader can buy guildhalls.")
        return false
    end

    local price = house:getTileCount() * housePrice

    local paymentSource = house:purchaseGuildhall(player, price)
    if paymentSource == "insufficient funds" then
        player:sendCancelMessage("Neither the guild bank nor you can cover the price of " .. price .. " gold.")
        return false
    elseif paymentSource == "pending confirmation" then
        player:sendCancelMessage("The purchase could not be confirmed. Contact an administrator before trying again.")
        return false
    elseif not paymentSource then
        player:sendCancelMessage("Could not complete the guildhall purchase.")
        return false
    end

    local currentTime = os.time()
    local receipt = Game.createItem(ITEM_RECEIPT_SUCCESS, 1)
    if receipt then
        local purchaseDate = os.date("%d %B %Y, %H:%M:%S", currentTime)

        local town = house:getTown()
        local townName = town and town:getName() or "Unknown"

        local receiptText = string.format(
            "Guild Bank Receipt\n\n" ..
            "Date: %s\n" ..
            "Type: Guildhall Purchase\n" ..
            "Category: House Rent\n\n" ..
            "Property: %s\n" ..
            "City: %s\n" ..
            "Amount Paid: %d gold\n\n" ..
            "Guild: %s\n" ..
            "Purchased by: %s\n\n" ..
            "This receipt is required to leave the guildhall.\n" ..
            "Keep it safe!",
            purchaseDate,
            house:getName(),
            townName,
            price,
            guild:getName(),
            player:getName()
        )

        receipt:setAttribute(ITEM_ATTRIBUTE_TEXT, receiptText)
        receipt:setAttribute(ITEM_ATTRIBUTE_DESCRIPTION,
            "Guild: " .. guild:getName() ..
            " | Property: " .. house:getName() ..
            " | City: " .. townName ..
            " | Purchased: " .. purchaseDate
        )

        receipt:setCustomAttribute("guildhall_receipt", 1)
        receipt:setCustomAttribute("guild_id", guild:getId())
        receipt:setCustomAttribute("house_id", house:getId())
        receipt:setCustomAttribute("town_id", town and town:getId() or 0)
        receipt:setCustomAttribute("purchase_time", currentTime)
        receipt:setCustomAttribute("buyer_guid", player:getGuid())

        local backpack = player:getSlotItem(CONST_SLOT_BACKPACK)
        local ret = RETURNVALUE_NOTPOSSIBLE

        if backpack then
            ret = backpack:addItemEx(receipt, INDEX_WHEREEVER, FLAG_NOLIMIT)
        end

        if ret ~= RETURNVALUE_NOERROR then
            ret = player:addItemEx(receipt, INDEX_WHEREEVER, FLAG_NOLIMIT)
        end

        if ret ~= RETURNVALUE_NOERROR then
            local playerPos = player:getPosition()
            local groundTile = Tile(playerPos)
            if groundTile and groundTile:addItemEx(receipt, FLAG_NOLIMIT) == RETURNVALUE_NOERROR then
                player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The receipt has been placed on the floor because your inventory is full.")
            else
                player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Could not create receipt. Please contact an administrator.")
            end
        else
            player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The receipt has been added to your inventory.")
        end
    end

    player:saveAsync()
    player:sendTextMessage(MESSAGE_INFO_DESCR, "You have successfully bought this guildhall for " .. price .. " gold from " .. paymentSource .. ". Be sure to have the money for rent in the guild bank.")
    return false
end
talkaction:register()
