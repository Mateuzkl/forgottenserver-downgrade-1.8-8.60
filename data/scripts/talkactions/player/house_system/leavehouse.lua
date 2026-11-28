local talkaction = TalkAction("!leavehouse")
function talkaction.onSay(player, words, param)
    local position = player:getPosition()
    local tile = Tile(position)
    local house = tile and tile:getHouse()
    
    if not house then
        player:sendCancelMessage("You are not inside a house.")
        position:sendMagicEffect(CONST_ME_POFF)
        return false
    end

    local REQUIRED_ITEM_ID = ITEM_RECEIPT_SUCCESS
    local houseType = house:getType()

    if houseType == HOUSE_TYPE_NORMAL then
        local ownerGuid = house:getOwnerGuid()
        local playerGuid = player:getGuid()
        if ownerGuid == 0 then
            player:sendCancelMessage("This house has no owner.")
            position:sendMagicEffect(CONST_ME_POFF)
            return false
        end

        if ownerGuid ~= playerGuid then
            player:sendCancelMessage("You are not the owner of this house.")
            position:sendMagicEffect(CONST_ME_POFF)
            return false
        end

        if not house:setOwnerGuid(0, true, player) then
            player:sendCancelMessage("Could not leave this house right now. Please try again shortly.")
            position:sendMagicEffect(CONST_ME_POFF)
            return false
        end

    elseif houseType == HOUSE_TYPE_GUILDHALL then
        local guild = player:getGuild()
        if not guild then
            player:sendCancelMessage("Only Guild Leader or Vice-Leader of the owning guild can leave this guildhall.")
            position:sendMagicEffect(CONST_ME_POFF)
            return false
        end

        local houseOwnerGuildId = house:getOwnerGuild()
        local guildId = guild:getId()
        if houseOwnerGuildId == 0 then
            player:sendCancelMessage("This guildhall has no owner.")
            position:sendMagicEffect(CONST_ME_POFF)
            return false
        end

        if houseOwnerGuildId ~= guildId then
            player:sendCancelMessage("Your guild does not own this guildhall.")
            position:sendMagicEffect(CONST_ME_POFF)
            return false
        end

        local isLeader = player:isGuildLeader()
        local isVice = player:isGuildVice()
        if not (isLeader or isVice) then
            player:sendCancelMessage("Only Guild Leader or Vice-Leader of the owning guild can leave this guildhall.")
            position:sendMagicEffect(CONST_ME_POFF)
            return false
        end

        local function isValidReceipt(item)
            if not item or item:getId() ~= REQUIRED_ITEM_ID then
                return false
            end
            
            local guildIdAttr = item:getCustomAttribute("guild_id")
            local houseIdAttr = item:getCustomAttribute("house_id")
            local receiptAttr = item:getCustomAttribute("guildhall_receipt")
            
            return (receiptAttr == 1 and guildIdAttr == guildId and houseIdAttr == house:getId())
        end
        
        local function searchInContainer(container)
            if not container then
                return nil
            end
            
            for i = 0, container:getSize() - 1 do
                local item = container:getItem(i)
                if item then
                    if isValidReceipt(item) then
                        return item
                    end
                    
                    if item:isContainer() then
                        local foundItem = searchInContainer(item)
                        if foundItem then
                            return foundItem
                        end
                    end
                end
            end
            
            return nil
        end
        
        local validReceipt = nil
        local rightHand = player:getSlotItem(CONST_SLOT_RIGHT)
        local leftHand = player:getSlotItem(CONST_SLOT_LEFT)
        
        if isValidReceipt(rightHand) then
            validReceipt = rightHand
        elseif isValidReceipt(leftHand) then
            validReceipt = leftHand
        else
            for slot = CONST_SLOT_HEAD, CONST_SLOT_AMMO do
                local slotItem = player:getSlotItem(slot)
                if slotItem then
                    if isValidReceipt(slotItem) then
                        validReceipt = slotItem
                        break
                    end
                    
                    if slotItem:isContainer() then
                        local foundItem = searchInContainer(slotItem)
                        if foundItem then
                            validReceipt = foundItem
                            break
                        end
                    end
                end
            end
        end

        if not validReceipt then
            player:sendCancelMessage("You must have the original guild bank receipt for this guildhall in your inventory to leave it.")
            position:sendMagicEffect(CONST_ME_POFF)
            return false
        end

        if not house:setOwnerGuid(0, true, player) then
            player:sendCancelMessage("Could not leave this guildhall right now. Please try again shortly.")
            position:sendMagicEffect(CONST_ME_POFF)
            return false
        end
        validReceipt:remove(1)

    else
        player:sendCancelMessage("You cannot leave this property.")
        position:sendMagicEffect(CONST_ME_POFF)
        return false
    end

    player:setStorageValue(PlayerStorageKeys.houseProtectionBase + house:getId(), -1)
    player:sendTextMessage(MESSAGE_INFO_DESCR, "You have successfully left your house.")
    position:sendMagicEffect(CONST_ME_POFF)
    return false
end
talkaction:register()

