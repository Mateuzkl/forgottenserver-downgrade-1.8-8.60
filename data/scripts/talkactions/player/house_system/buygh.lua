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

    local guildHouseQuery = db.storeQuery("SELECT `id` FROM `houses` WHERE `owner` = " .. guild:getId() .. " LIMIT 1")
    if guildHouseQuery then
        result.free(guildHouseQuery)
        player:sendCancelMessage("Your guild already owns a guildhall.")
        return false
    end

    local isLeader = player:isGuildLeader()
    local isVice = player:isGuildVice()
    if not (isLeader or isVice) then
        player:sendCancelMessage("Only Guild Leader or Vice-Leader can buy guildhalls.")
        return false
    end

    local price = house:getTileCount() * housePrice

    local query = db.storeQuery("SELECT `balance` FROM `guilds` WHERE `id` = " .. guild:getId())
    local guildBalance = 0
    if query then
        guildBalance = result.getNumber(query, "balance")
        result.free(query)
    end

    local fromGuild = false
    local fromPlayer = false
    local playerInventoryMoney = player:getMoney()
    local playerBankBalance = player:getBankBalance()
    local playerTotalMoney = playerInventoryMoney + playerBankBalance

    if guildBalance >= price then
        fromGuild = true
    elseif playerTotalMoney >= price then
        fromPlayer = true
    else
        player:sendCancelMessage("Neither your guild bank nor you have enough money. Guild bank: " .. guildBalance .. " gold. You have " .. playerTotalMoney .. " gold (inventory: " .. playerInventoryMoney .. ", bank: " .. playerBankBalance .. "). Price: " .. price .. " gold.")
        return false
    end

    if fromGuild then
        local newBalance = guildBalance - price
        db.query("UPDATE `guilds` SET `balance` = " .. newBalance .. " WHERE `id` = " .. guild:getId())

        local currentTime = os.time()
        db.query(string.format(
            "INSERT INTO `guild_transactions` (`guild_id`, `player_associated`, `type`, `category`, `balance`, `time`) VALUES (%d, %d, 'WITHDRAW', 'RENT', %d, %d)",
            guild:getId(),
            player:getGuid(),
            price,
            currentTime
        ))
    elseif fromPlayer then
        if playerInventoryMoney >= price then
            if not player:removeMoney(price) then
                player:sendCancelMessage("Error removing money from inventory.")
                return false
            end
        else
            if not player:removeMoney(playerInventoryMoney) then
                player:sendCancelMessage("Error removing money from inventory.")
                return false
            end
            local remaining = price - playerInventoryMoney
            player:setBankBalance(playerBankBalance - remaining)
        end
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
            if groundTile then
                groundTile:addItem(receipt)
                player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The receipt has been placed on the floor because your inventory is full.")
            else
                player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Could not create receipt. Please contact an administrator.")
            end
        else
            player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The receipt has been added to your inventory.")
        end
    end

    if not house:setOwnerGuid(player:getGuid()) then
        if fromGuild then
            db.query("UPDATE `guilds` SET `balance` = `balance` + " .. price .. " WHERE `id` = " .. guild:getId())
        elseif fromPlayer then
            if playerInventoryMoney >= price then
                player:addMoney(price)
            else
                if playerInventoryMoney > 0 then
                    player:addMoney(playerInventoryMoney)
                end
                local remaining = price - playerInventoryMoney
                player:setBankBalance(player:getBankBalance() + remaining)
            end
        end
        if receipt then
            receipt:remove(1)
        end
        player:sendCancelMessage("Could not complete the guildhall purchase. Your payment has been returned.")
        return false
    end
    player:saveAsync()
    local paymentSource = fromGuild and "guild bank" or "your personal funds"
    player:sendTextMessage(MESSAGE_INFO_DESCR, "You have successfully bought this guildhall for " .. price .. " gold from " .. paymentSource .. ". Be sure to have the money for rent in the guild bank.")
    return false
end
talkaction:register()
