local npc = Game.createNpcType("Banker")
npc:speechBubble(SPEECHBUBBLE_TRADE)
npc:outfit({ lookType = 472, lookHead = 40, lookBody = 95, lookLegs = 114, lookFeet = 27, addons = 1 })
npc:defaultBehavior()

local handler = NpcsHandler(npc)
local greet = handler:keyword(handler.greetWords)
greet:setGreetResponse("Welcome to the bank, |PLAYERNAME|! Need some help with your {bank account}?")

local help = greet:keyword("help")
help:respond(
"You can check the {balance} of your bank account, {deposit} money or {withdraw} it. You can {transfer} money to other characters, provided that they have a vocation, or {change} the coins in your inventory.")

local bankAccount = greet:keyword("bank account")
bankAccount:respond(
"Would you like to know more about the {basic} functions of your bank account, the {advanced} functions, or are you already bored, perhaps?")

local basic = bankAccount:keyword({ "basic", "functions", "job" })
basic:respond("I work in this bank. I can {change} money for you and help you with your {bank account}.")

basic.keywords["balance"] = greet.keywords["balance"]
basic.keywords["deposit"] = greet.keywords["deposit"]
basic.keywords["withdraw"] = greet.keywords["withdraw"]
basic.keywords["transfer"] = greet.keywords["transfer"]
basic.keywords["change"] = greet.keywords["change"]

bankAccount.keywords["balance"] = greet.keywords["balance"]
bankAccount.keywords["deposit"] = greet.keywords["deposit"]
bankAccount.keywords["withdraw"] = greet.keywords["withdraw"]
bankAccount.keywords["transfer"] = greet.keywords["transfer"]
bankAccount.keywords["change"] = greet.keywords["change"]

local advanced = bankAccount:keyword("advanced")
advanced:respond(
"Your bank account will be used automatically when you want to {rent} a house or place an offer on an item on the {market}. Let me know if you want to know about how either one works.")

local rent = advanced:keyword("rent")
rent:respond(
"Once you have acquired a house the rent will be charged automatically from your {bank account} every month.")

local market = advanced:keyword("market")
market:respond(
"If you buy an item from the market, the required gold will be deducted from your bank account automatically. On the other hand, money you earn for selling items via the market will be added to your account. It's easy!")

market.keywords["rent"] = rent

-- Bank Balance
local balance = greet:keyword("balance")
function balance:callback(npc, player, message, handler)
    local balance = player:getBankBalance()
    if balance >= 100000000 then
        return true,
            "I think you must be one of the richest inhabitants in the world! Your account balance is " ..
            balance .. " gold."
    elseif balance >= 10000000 then
        return true, "You have made ten millions and it still grows! Your account balance is " .. balance .. " gold."
    elseif balance >= 1000000 then
        return true,
            "Wow, you have reached the magic number of a million gp!!! Your account balance is " .. balance .. " gold!"
    elseif balance >= 100000 then
        return true, "You certainly have made a pretty penny. Your account balance is " .. balance .. " gold."
    end
    return true, "Your account balance is " .. balance .. " gold."
end

-- Bank Deposit
local deposit = greet:keyword("deposit")
deposit:respond("How much money would you like to deposit?")
local answer = deposit:onAnswer()
function answer:callback(npc, player, message, handler)
    local money = 0
    if message == "all" then
        money = player:getMoney()
    else
        money = tonumber(message)
    end
    local valid = isValidMoney(money)
    if valid then
        if player:getMoney() < money then
            return false, "You don't have enough money to deposit " .. money .. " gold."
        end
        handler:addData(player, "money", money)
        return true, "You want to deposit " .. money .. " gold coins into your bank account?"
    end
    return false, "I'm sorry, but you can't deposit a negative amount of money or no money at all."
end

local accept = answer:keyword({ "yes" })
function accept:callback(npc, player, message, handler)
    local money = handler:getData(player, "money")
    if player:getMoney() < money then
        return false, "You don't have enough money to deposit " .. money .. " gold."
    end
    player:depositMoney(money)
    handler:resetData(player)
    return true, "You have deposited " .. money .. " gold coins into your bank account."
end

local decline = answer:keyword({ "no" })
decline:respond("Ok then, not.")

-- Bank Withdraw
local withdraw = greet:keyword("withdraw")
withdraw:respond("How much money would you like to withdraw?")
local answer = withdraw:onAnswer()
function answer:callback(npc, player, message, handler)
    local money = 0
    if message == "all" then
        money = player:getBankBalance()
    else
        money = tonumber(message)
    end
    local valid = isValidMoney(money)
    if valid then
        if player:getBankBalance() < money then
            return false, "You don't have enough money to withdraw " .. money .. " gold."
        end
        if not player:canCarryMoney(money) then
            return false, "You can't carry that much money."
        end
        handler:addData(player, "money", money)
        return true, "You want to withdraw " .. money .. " gold coins from your bank account?"
    end
    return false, "I'm sorry, but you can't withdraw a negative amount of money or no money at all."
end

local accept = answer:keyword({ "yes" })
function accept:callback(npc, player, message, handler)
    local money = handler:getData(player, "money")
    player:withdrawMoney(money)
    handler:resetData(player)
    return true, "You have withdrawn " .. money .. " gold coins from your bank account."
end

local decline = answer:keyword({ "no" })
decline:respond("Ok then, not.")

-- Bank Transfer
local transfer = greet:keyword("transfer")
transfer:respond("You want to transfer money to another player? Please tell me the amount and the name of the player.")
local answer = transfer:onAnswer()
function answer:callback(npc, player, message, handler)
    local data = string.split(message, " ")
    local money = 0
    local playerName = ""
    for i = 1, #data do
        if tonumber(data[i]) then
            money = tonumber(data[i])
        else
            playerName = playerName ~= "" and playerName .. " " .. data[i] or data[i]
        end
    end
    local receiver = Player.getPlayerDatabaseInfo(playerName)
    if not receiver then
        return false, "There is no one named like " .. playerName
    end
    if receiver.vocation == VOCATION_NONE or player:getVocation() == VOCATION_NONE then
        return false, "You can't transfer money to or from a player without a vocation."
    end
    if receiver.name == player:getName() then
        return false, "You can't transfer money to yourself."
    end
    if not isValidMoney(money) then
        return false, "You can't transfer a negative amount of money or no money at all."
    end
    if player:getBankBalance() < money then
        return false, "You don't have enough money to transfer " .. money .. " gold coins to " .. playerName
    end
    handler:addData(player, "money", money)
    handler:addData(player, "playerName", playerName)
    return true, "You want to transfer money to " .. playerName .. " in the amount of " .. money .. " gold coins?"
end

local accept = answer:keyword({ "yes" })
function accept:callback(npc, player, message, handler)
    local money = handler:getData(player, "money")
    local playerName = handler:getData(player, "playerName")
    local receiver = Player.getPlayerDatabaseInfo(playerName)
    if not player:transferMoneyTo(receiver, money) then
        return false, "You don't have enough money to transfer " .. money .. " gold coins to " .. playerName
    end
    handler:resetData(player)
    return true, "You have transferred " .. money .. " gold coins to " .. playerName
end

local decline = answer:keyword({ "no" })
decline:respond("Ok then, not.")

-- Bank Change
local change = greet:keyword("change")
change:respond(
"Would you like to change your coins? You can exchange gold, platinum, crystal, and spectral gold currencies.")

local function registerCurrencyExchange(parent, words, prompt, sourceId, resultId, sourceLabel, resultLabel, operation)
    local exchange = parent:keyword(words)
    exchange:respond(prompt)

    local answer = exchange:onAnswer()
    function answer:callback(npc, player, message, handler)
        local multiple = operation == "divide" and 100 or nil
        local sourceCount = CurrencyConversion.parseAmount(message, multiple)
        local resultCount = sourceCount and (operation == "divide"
            and CurrencyConversion.divideExact(sourceCount, 100)
            or CurrencyConversion.multiply(sourceCount, 100)) or nil

        if not sourceCount or not resultCount then
            return false, "Please enter a positive whole amount within the safe limit" ..
                (multiple and " and divisible by 100." or ".")
        end

        if player:getItemCount(sourceId) < sourceCount then
            return false, "You don't have enough " .. sourceLabel .. "."
        end

        handler:addData(player, "currencySourceId", sourceId)
        handler:addData(player, "currencySourceCount", sourceCount)
        handler:addData(player, "currencyResultId", resultId)
        handler:addData(player, "currencyResultCount", resultCount)
        handler:addData(player, "currencySourceLabel", sourceLabel)
        handler:addData(player, "currencyResultLabel", resultLabel)
        return true, "You want to change " .. sourceCount .. " " .. sourceLabel ..
            " into " .. resultCount .. " " .. resultLabel .. "?"
    end

    local accept = answer:keyword({ "yes" })
    function accept:callback(npc, player, message, handler)
        local sourceId = handler:getData(player, "currencySourceId")
        local sourceCount = handler:getData(player, "currencySourceCount")
        local resultId = handler:getData(player, "currencyResultId")
        local resultCount = handler:getData(player, "currencyResultCount")
        local sourceLabel = handler:getData(player, "currencySourceLabel")
        local resultLabel = handler:getData(player, "currencyResultLabel")

        if not sourceId or not sourceCount or not resultId or not resultCount then
            handler:resetData(player)
            return false, "The exchange request is no longer valid."
        end

        local success, reason = CurrencyConversion.exchangePlayerItems(
            player, sourceId, sourceCount, resultId, resultCount)
        handler:resetData(player)
        if not success then
            if reason == "not_enough" or reason == "remove_failed" then
                return false, "You don't have enough " .. sourceLabel .. "."
            end
            return false, "The exchange could not be completed. Make sure you have enough inventory space."
        end

        return true, "You have changed " .. sourceCount .. " " .. sourceLabel ..
            " into " .. resultCount .. " " .. resultLabel .. "."
    end

    local decline = answer:keyword({ "no" })
    function decline:callback(npc, player, message, handler)
        handler:resetData(player)
        return true, "Ok then, not."
    end

    return exchange
end

registerCurrencyExchange(change, "gold", "How many gold coins would you like to change?",
    ITEM_GOLD_COIN, ITEM_PLATINUM_COIN, "gold coins", "platinum coins", "divide")

local platinum = change:keyword("platinum")
platinum:respond(
"Would you like to change your platinum coins into {gold} coins or into {crystal} coins?")
registerCurrencyExchange(platinum, "gold", "How many platinum coins would you like to change into gold?",
    ITEM_PLATINUM_COIN, ITEM_GOLD_COIN, "platinum coins", "gold coins", "multiply")
registerCurrencyExchange(platinum, "crystal", "How many platinum coins would you like to change into crystal?",
    ITEM_PLATINUM_COIN, ITEM_CRYSTAL_COIN, "platinum coins", "crystal coins", "divide")

local crystal = change:keyword("crystal")
crystal:respond(
"Would you like to change your crystal coins into {platinum} coins or into {spectral} gold nuggets?")
registerCurrencyExchange(crystal, "platinum", "How many crystal coins would you like to change into platinum?",
    ITEM_CRYSTAL_COIN, ITEM_PLATINUM_COIN, "crystal coins", "platinum coins", "multiply")

local spectralGoldNuggetId = CurrencyConversion.getItemIdByWorth(1000000)
if spectralGoldNuggetId then
    registerCurrencyExchange(crystal, { "spectral", "nugget" },
        "How many crystal coins would you like to change into spectral gold nuggets?",
        ITEM_CRYSTAL_COIN, spectralGoldNuggetId, "crystal coins", "spectral gold nuggets", "divide")
    registerCurrencyExchange(change, { "spectral", "nugget" },
        "How many spectral gold nuggets would you like to change into crystal coins?",
        spectralGoldNuggetId, ITEM_CRYSTAL_COIN, "spectral gold nuggets", "crystal coins", "multiply")
end

-- Fast transfer / deposit / withdraw
local fast = greet:onAnswer()
function fast:callback(npc, player, message, handler)
    local transfer = string.find(message, "transfer")
    if transfer then
        local msg = string.gsub(message, "transfer ", "")
        local data = string.split(msg, " ")
        local money = 0
        local playerName = ""
        for i = 1, #data do
            if tonumber(data[i]) then
                money = tonumber(data[i])
            else
                playerName = playerName ~= "" and playerName .. " " .. data[i] or data[i]
            end
        end
        local receiver = Player.getPlayerDatabaseInfo(playerName)
        if not receiver then
            return false, "There is no one named like '" .. playerName .. "'"
        end
        if receiver.name == player:getName() then
            return false, "You can't transfer money to yourself."
        end
        if receiver.vocation == VOCATION_NONE or player:getVocation() == VOCATION_NONE then
            return false, "You can't transfer money to or from a player without a vocation."
        end
        if not isValidMoney(money) then
            return false, "You can't transfer a negative amount of money or no money at all."
        end
        if player:getBankBalance() < money then
            return false, "You don't have enough money to transfer " .. money .. " gold coins to " .. playerName
        end
        handler:addData(player, "money", money)
        handler:addData(player, "playerName", playerName)
        handler:addData(player, "type", "transfer")
        return true, "You want to transfer money to " .. playerName .. " in the amount of " .. money .. " gold coins?"
    end

    local deposit = string.find(message, "deposit")
    if deposit then
        local sub = string.gsub(message, "deposit ", "")
        local money = 0
        if sub == "all" then
            money = player:getMoney()
        else
            money = tonumber(sub)
        end
        local valid = isValidMoney(money)
        if valid then
            if player:getMoney() < money then
                return false, "You don't have enough money to deposit " .. money .. " gold."
            end
            handler:addData(player, "money", money)
            handler:addData(player, "type", "deposit")
            return true, "You want to deposit " .. money .. " gold coins into your bank account?"
        end
        return false, "I'm sorry, but you can't deposit a negative amount of money or no money at all."
    end

    local withdraw = string.find(message, "withdraw")
    if withdraw then
        local sub = string.gsub(message, "withdraw ", "")
        local money = 0
        if sub == "all" then
            money = player:getBankBalance()
        else
            money = tonumber(sub)
        end
        local valid = isValidMoney(money)
        if valid then
            if player:getBankBalance() < money then
                return false, "You don't have enough money to withdraw " .. money .. " gold."
            end
            if not player:canCarryMoney(money) then
                return false, "You can't carry that much money."
            end
            handler:addData(player, "money", money)
            handler:addData(player, "type", "withdraw")
            return true, "You want to withdraw " .. money .. " gold coins from your bank account?"
        end
        return false, "I'm sorry, but you can't withdraw a negative amount of money or no money at all."
    end
    return false
end

fast.failureResponse = "I don't understand what you mean. Do you want to {deposit}, {withdraw}, or {transfer} money?"

local accept = fast:keyword({ "yes" })
function accept:callback(npc, player, message, handler)
    local money = handler:getData(player, "money")
    local playerName = handler:getData(player, "playerName")
    local receiver = Player.getPlayerDatabaseInfo(playerName)
    local type = handler:getData(player, "type")
    if type == "transfer" then
        if not player:transferMoneyTo(receiver, money) then
            return false, "You don't have enough money to transfer " .. money .. " gold coins to " .. playerName
        end
        handler:resetData(player)
        return true, "You have transferred " .. money .. " gold coins to " .. playerName
    elseif type == "deposit" then
        if player:getMoney() < money then
            return false, "You don't have enough money to deposit " .. money .. " gold."
        end
        player:depositMoney(money)
        handler:resetData(player)
        return true, "You have deposited " .. money .. " gold coins into your bank account."
    elseif type == "withdraw" then
        player:withdrawMoney(money)
        handler:resetData(player)
        return true, "You have withdrawn " .. money .. " gold coins from your bank account."
    end
    return false, "Something went wrong, please try again."
end

local decline = fast:keyword({ "no" })
decline:respond("Ok then, not.")

--[[
    npc tree structure:
    greet
        |- help
        |- bank account
        |   |- basic
        |   |- advanced
        |       |- rent
        |       |- market
        |- balance
        |- deposit
        |   |- answer
        |       |- yes
        |       |- no
        |- withdraw
        |   |- answer
        |       |- yes
        |       |- no
        |- transfer
        |   |- answer
        |       |- yes
        |       |- no
        |- change
        |   |- gold
        |   |   |- answer
        |   |       |- yes
        |   |       |- no
        |   |- platinum
        |   |   |- gold
        |   |   |   |- answer
        |   |   |       |- yes
        |   |   |       |- no
        |   |   |- crystal
        |   |       |- answer
        |   |           |- yes
        |   |           |- no
        |   |- crystal
        |       |- answer
        |           |- yes
        |           |- no
        |- fast (transfer, deposit, withdraw)
            |- yes
            |- no
]]
