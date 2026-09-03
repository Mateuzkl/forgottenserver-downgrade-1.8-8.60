local FORGE_LEVER_AID = 7755
local FORGE_CONTAINER_ID = 37122
local FORGE_FIRST_ID = 37115
local FORGE_LAST_ID = 37159
local FORGE_CONTAINER_SLOTS = 2
local RESET_STONE_ID = 8302

local FORGE_ITEM_IDS = {
    dust = 37160,
    sliver = 37109,
    exaltedCore = 37110,
}

local FORGE_STORAGE = {
    dust = PlayerStorageKeys.forgeDust,
    dustLimit = PlayerStorageKeys.forgeDustLimit,
}

local FORGE_POSITIONS = {
    container = Position(1054, 974, 7),
}

local function getForgeContainerItem(player)
    local tile = Tile(FORGE_POSITIONS.container)
    if not tile then return nil end

    local instanceId = player:getInstanceId()
    local items = tile:getItems()
    if not items then return nil end

    for _, item in ipairs(items) do
        if item:getId() == FORGE_CONTAINER_ID and item:getInstanceId() == instanceId then
            return item
        end
    end
    return nil
end

local function isForgeEnabled()
    return configManager.getBoolean(configKeys.FORGE_SYSTEM_ENABLED)
end

local function isOtClient(player)
    return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function openClassicForgeContainer(player, clickedItem)
    local container = nil

    if clickedItem and clickedItem:getId() == FORGE_CONTAINER_ID then
        container = Container(clickedItem.uid)
    end

    if not container then
        local containerItem = getForgeContainerItem(player)
        if containerItem then
            container = Container(containerItem.uid)
        end
    end

    if not container then
        player:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Forge] Forge container not found.")
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    if not player:openContainer(container) then
        player:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Forge] Could not open the forge container.")
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    player:sendTextMessage(MESSAGE_EVENT_ORANGE, "[Forge] Place two equal items in the forge container and use the lever.")
    return true
end

local UPGRADE_CHANCES = {
    [0] = 50, [1] = 40, [2] = 35, [3] = 30, [4] = 25,
    [5] = 20, [6] = 15, [7] = 10, [8] = 8, [9] = 5,
}

local UPGRADE_COSTS = {
    [0] = 25000, [1] = 100000, [2] = 250000, [3] = 500000, [4] = 1000000,
    [5] = 2000000, [6] = 4000000, [7] = 8000000, [8] = 15000000, [9] = 30000000,
}

local DUST_COSTS = {
    [0] = 100, [1] = 100, [2] = 100, [3] = 100, [4] = 100,
    [5] = 150, [6] = 150, [7] = 200, [8] = 200, [9] = 250,
}

local EXALTED_CORE_REQUIRED_FROM_TIER = 5

local BONUS_PER_CORE = 15
local MAX_IMPROVE_CORES = 3
local REDUCE_RISK_PER_CORE = 5
local MAX_RISK_CORES = 2
local CHANCE_LOSS_TIER = 10

function Player:getForgeDust()
    if not isForgeEnabled() then return 0 end

    local v = self:getStorageValue(FORGE_STORAGE.dust)
    if not v or v < 0 then return 0 end
    return v
end

function Player:getForgeDustLimit()
    if not isForgeEnabled() then return 0 end

    local v = self:getStorageValue(FORGE_STORAGE.dustLimit)
    if not v or v <= 0 then
        self:setStorageValue(FORGE_STORAGE.dustLimit, 100)
        return 100
    end
    return v
end

function Player:sendForgeDustBalance()
    if not (self.isUsingOtClient and self:isUsingOtClient() or self.isUsingOtc and self:isUsingOtc()) then
        return false
    end

    local dust = self:getForgeDust()
    local limit = self:getForgeDustLimit()

    local out = NetworkMessage(self)
    out:addByte(0xEE)
    out:addByte(23)
    out:addU64(dust)
    out:sendToPlayer(self)

    local out20 = NetworkMessage(self)
    out20:addByte(0xEE)
    out20:addByte(20)
    out20:addU64(dust)
    out20:sendToPlayer(self)

    local limitOut = NetworkMessage(self)
    limitOut:addByte(0xEE)
    limitOut:addByte(88)
    limitOut:addU64(limit)
    limitOut:sendToPlayer(self)
end

function Player:addForgeDust(amount)
    if not isForgeEnabled() then return 0 end

    local cur = self:getForgeDust()
    local limit = self:getForgeDustLimit()
    local new = math.min(cur + amount, limit)
    self:setStorageValue(FORGE_STORAGE.dust, new)
    self:sendForgeDustBalance()
    return new - cur
end

function Player:removeForgeDust(amount)
    if not isForgeEnabled() then return false end

    local cur = self:getForgeDust()
    if cur < amount then return false end
    self:setStorageValue(FORGE_STORAGE.dust, cur - amount)
    self:sendForgeDustBalance()
    return true
end

local forgeFusion = Action()
function forgeFusion.onUse(player, item, fromPosition, target, toPosition, isHotkey)
    if not isForgeEnabled() then
        player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    local containerPos = FORGE_POSITIONS.container
    local containerItem = getForgeContainerItem(player)
    if not containerItem then
        player:sendTextMessage(MESSAGE_STATUS_WARNING, "[Forge] Forge container not found.")
        return true
    end

    local container = Container(containerItem.uid)
    if not container then
        player:sendTextMessage(MESSAGE_STATUS_WARNING, "[Forge] Error accessing the container.")
        return true
    end

    if container:getSize() ~= 2 then
        player:sendTextMessage(MESSAGE_STATUS_WARNING, "[Forge] Place exactly 2 items in the container.")
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    local item1 = container:getItem(0)
    local item2 = container:getItem(1)

    if not item1 or not item2 then
        player:sendTextMessage(MESSAGE_STATUS_WARNING, "[Forge] Place two items in the container.")
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    if item1:getId() ~= item2:getId() then
        player:sendTextMessage(MESSAGE_STATUS_WARNING, "[Forge] Both items must be of the same type.")
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    local itemId = item1:getId()
    local maxTier = item1:getClassification()
    if maxTier == 0 then
        player:sendTextMessage(MESSAGE_STATUS_WARNING, "[Forge] This item has no classification and cannot be forged.")
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    local tier1 = item1:getTier()
    local tier2 = item2:getTier()
    if tier1 ~= tier2 then
        player:sendTextMessage(MESSAGE_STATUS_WARNING,
            string.format("[Forge] Both items must have the same tier. (Tier %d and Tier %d)", tier1, tier2))
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    if tier1 >= maxTier then
        player:sendTextMessage(MESSAGE_STATUS_WARNING,
            string.format("[Forge] This item has already reached the maximum tier (%d).", maxTier))
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    -- Dust cost
    local dustCost = DUST_COSTS[tier1] or 100
    if player:getForgeDust() < dustCost then
        player:sendTextMessage(MESSAGE_INFO_DESCR,
            string.format("[Forge] You need %d Dust. You have %d.", dustCost, player:getForgeDust()))
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    -- Exalted Core (required from tier 5+)
    local coresNeeded = 0
    if tier1 >= EXALTED_CORE_REQUIRED_FROM_TIER then
        coresNeeded = 1
    end
    if coresNeeded > 0 and player:getItemCount(FORGE_ITEM_IDS.exaltedCore) < coresNeeded then
        player:sendTextMessage(MESSAGE_INFO_DESCR,
            string.format("[Forge] You need %d Exalted Core(s) for tier %d+ fusion.", coresNeeded, EXALTED_CORE_REQUIRED_FROM_TIER))
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    -- Gold cost
    local cost = UPGRADE_COSTS[tier1] or 0
    if cost > 0 then
        local totalGold = player:getMoney() + player:getBankBalance()
        if totalGold < cost then
            player:sendTextMessage(MESSAGE_INFO_DESCR,
                string.format("[Forge] You need %d gold coins.", cost))
            player:getPosition():sendMagicEffect(CONST_ME_POFF)
            return true
        end

        local bankBal = player:getBankBalance()
        if bankBal >= cost then
            player:setBankBalance(bankBal - cost)
        else
            local remaining = cost - bankBal
            player:setBankBalance(0)
            player:removeMoney(remaining)
        end
    end

    local successChance = UPGRADE_CHANCES[tier1] or 5
    local roll = math.random(1, 100)

    -- Consume dust
    player:removeForgeDust(dustCost)

    -- Consume exalted cores
    if coresNeeded > 0 then
        player:removeItem(FORGE_ITEM_IDS.exaltedCore, coresNeeded)
    end

    if roll <= successChance then
        item1:setTier(tier1 + 1)
        item2:remove(1)
        local chest = Game.createItem(37561, 1)
        if chest and chest:isContainer() then
            local container = Container(chest:getUniqueId() or chest.uid)
            if container then
                item1:moveTo(container)
            else
                item1:moveTo(player)
            end
            chest:moveTo(player)
        else
            item1:moveTo(player)
        end

        FORGE_POSITIONS.container:sendMagicEffect(CONST_ME_AVATAR_APPEAR)
        FORGE_POSITIONS.container:sendMagicEffect(CONST_ME_WATER_DROP)
        player:sendTextMessage(MESSAGE_INFO_DESCR,
            string.format("[Forge] Success! Tier %d. (Chance: %d%%, Gold: %d, Dust: %d)",
                tier1 + 1, successChance, cost, dustCost))
    else
        item2:remove(1)

        FORGE_POSITIONS.container:sendMagicEffect(CONST_ME_AGONY)
        player:sendTextMessage(MESSAGE_INFO_DESCR,
            string.format("[Forge] Failed! Sacrifice lost. (Chance: %d%%, Gold: %d, Dust: %d)",
                successChance, cost, dustCost))
    end

    return true
end
forgeFusion:aid(FORGE_LEVER_AID)
forgeFusion:register()

local forgeWindow = Action()
function forgeWindow.onUse(player, item, fromPosition, target, toPosition, isHotkey)
    if not isForgeEnabled() then
        player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    if isOtClient(player) and CustomForge and CustomForge.open(player) then
        return true
    end

    return openClassicForgeContainer(player, item)
end
for id = FORGE_FIRST_ID, FORGE_LAST_ID do
    forgeWindow:id(id)
end
forgeWindow:register()

local resetStone = Action()
function resetStone.onUse(player, item, fromPosition, target, toPosition, isHotkey)
    if not isForgeEnabled() then
        player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    if not target or not target:isItem() then
        player:sendTextMessage(MESSAGE_STATUS_WARNING, "[Forge] Use the reset stone on an equipment item.")
        return true
    end

    local tier = target:getTier()
    if tier == 0 then
        player:sendTextMessage(MESSAGE_STATUS_WARNING, "[Forge] This item has no tier to reset.")
        target:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    target:setTier(0)
    target:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
    player:sendTextMessage(MESSAGE_STATUS_WARNING,
        string.format("[Forge] Tier reset! (was Tier %d)", tier))
    item:remove(1)
    return true
end
resetStone:id(RESET_STONE_ID)
resetStone:register()

-- ====== Conversions: /forge dust | /forge sliver | /forge info ======
local forgeTalk = TalkAction("/forge", "!forge")
function forgeTalk.onSay(player, words, param)
    if not isForgeEnabled() then
        player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
        return false
    end

    param = param:lower():trim()

    if param == "info" then
        player:popupFYI(string.format(
            "[Forge]\n\nDust: %d/%d\nSliver: %d\nExalted Core: %d",
            player:getForgeDust(), player:getForgeDustLimit(),
            player:getItemCount(FORGE_ITEM_IDS.sliver),
            player:getItemCount(FORGE_ITEM_IDS.exaltedCore)))
        return false
    end

    if param == "dust" then
        -- Convert 60 dust -> 3 slivers
        local currentDust = player:getForgeDust()
        local batches = math.floor(currentDust / 60)
        if batches == 0 then
            player:popupFYI("[Forge]\n\nYou need at least 60 Dust to convert.")
            return false
        end

        local dustUsed = batches * 60
        local sliversGained = batches * 3

        player:removeForgeDust(dustUsed)
        player:addItem(FORGE_ITEM_IDS.sliver, sliversGained)
        player:popupFYI(string.format("[Forge]\n\nConverted %d Dust into %d sliver(s).", dustUsed, sliversGained))
        return false
    end

    if param == "sliver" then
        -- Convert 50 sliver -> 1 exalted core
        local sliver = player:getItemCount(FORGE_ITEM_IDS.sliver)
        local cores = math.floor(sliver / 50)
        if cores == 0 then
            player:popupFYI("[Forge]\n\nYou need at least 50 sliver to convert.")
            return false
        end
        player:removeItem(FORGE_ITEM_IDS.sliver, cores * 50)
        player:addItem(FORGE_ITEM_IDS.exaltedCore, cores)
        player:popupFYI(string.format("[Forge]\n\nConverted %d sliver into %d Exalted Core(s).", cores * 50, cores))
        return false
    end

    player:popupFYI("[Forge]\n\nCommands:\n/forge info\n/forge dust\n/forge sliver")
    return false
end
forgeTalk:separator(" ")
forgeTalk:register()

local forgeStation = GlobalEvent("ForgeStation")
function forgeStation.onStartup()
    if not isForgeEnabled() then
        return true
    end

    local pos = FORGE_POSITIONS.container
    local tile = Tile(pos)
    if tile then
        local existing = tile:getItemById(FORGE_CONTAINER_ID)
        if existing then
            local container = Container(existing.uid)
            if not container then
                existing:remove()
                Game.createContainer(FORGE_CONTAINER_ID, FORGE_CONTAINER_SLOTS, pos)
            end
        else
            Game.createContainer(FORGE_CONTAINER_ID, FORGE_CONTAINER_SLOTS, pos)
        end
    end
    return true
end
forgeStation:register()