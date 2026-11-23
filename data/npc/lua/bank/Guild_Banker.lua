-- Guild Banker – RevScript (NpcsHandler)
-- Converted from: Guild_Banker.xml + guildbank.lua
-- NOTE: dynamic amount input (deposit/withdraw/transfer) uses onAnswer
-- with a per-player state table. Test and adjust if your NpcsHandler
-- does not capture arbitrary text inside onAnswer callbacks.
local npcType = Game.createNpcType("Guild Banker")
npcType:outfit({lookType = 146, lookHead = 38, lookBody = 94, lookLegs = 52, lookFeet = 76, lookAddons = 0})
npcType:health(100)
npcType:maxHealth(100)
npcType:walkInterval(2000)
npcType:walkSpeed(100)
npcType:spawnRadius(3)
npcType:defaultBehavior()

------------------------------------------------------------------------
-- State tracking
------------------------------------------------------------------------
local TOPIC = {
    NONE                  = 0,
    DEPOSIT_GOLD          = 1,
    DEPOSIT_CONSENT       = 2,
    WITHDRAW_GOLD         = 3,
    WITHDRAW_CONSENT      = 4,
    TRANSFER_TYPE         = 5,
    TRANSFER_PLAYER_GOLD  = 6,
    TRANSFER_PLAYER_WHO   = 7,
    TRANSFER_PLAYER_CONSENT = 8,
    TRANSFER_GUILD_GOLD   = 9,
    TRANSFER_GUILD_WHO    = 10,
    TRANSFER_GUILD_CONSENT = 11,
    LEDGER_CONSENT        = 12,
}
local state  = {}
local amount = {}
local target = {}

npcType.onDisappear = function(npc, creature)
    if not creature then return end
    local success, pid = pcall(function() return creature:getGuid() end)
    if not success or not pid then return end
    if state[pid] then state[pid] = nil end
    if amount[pid] then amount[pid] = nil end
    if target[pid] then target[pid] = nil end
end

local function getGuildBalance(guildId)
    local q = db.storeQuery("SELECT `balance` FROM `guilds` WHERE `id` = " .. guildId)
    local bal = 0
    if q then bal = result.getNumber(q, "balance"); result.free(q) end
    return bal
end

local function getMoneyCount(msg)
    local n = tonumber(msg:match("%d+"))
    return n or 0
end

------------------------------------------------------------------------
-- Handler
------------------------------------------------------------------------
local handler = NpcsHandler("Guild Banker")
handler:setGreetKeywords({"hi", "hello"})
handler:setFarewellKeywords({"bye", "farewell"})
handler:setFarewellResponse({"Farewell. Keep your guild funds safe."})

local greet = handler:keyword(handler.greetWords)
greet:setGreetResponse({"Greetings, |PLAYERNAME|! Need help with your {guild account}?"})

local globalAnswer = greet:onAnswer()
function globalAnswer:callback(npc, player, message, handler)
    local pid = player:getGuid()
    local currentState = state[pid] or TOPIC.NONE
    local guild = player:getGuild()
    
    if currentState == TOPIC.DEPOSIT_GOLD then
        local money = getMoneyCount(message)
        if money < 1 then
            state[pid] = TOPIC.NONE
            return true, "Please tell me a valid amount."
        end
        if player:getBankBalance() < money then
            state[pid] = TOPIC.NONE
            return true, "Your personal bank account doesn't have enough gold."
        end
        amount[pid] = money
        state[pid] = TOPIC.DEPOSIT_CONSENT
        return true, "Do you want to deposit " .. money .. " gold to " .. guild:getName() .. "?"
    end
    
    if currentState == TOPIC.WITHDRAW_GOLD then
        local money = getMoneyCount(message)
        if money < 1 then
            state[pid] = TOPIC.NONE
            return true, "Please tell me a valid amount."
        end
        amount[pid] = money
        state[pid] = TOPIC.WITHDRAW_CONSENT
        return true, "Do you want to withdraw " .. money .. " gold from " .. guild:getName() .. "?"
    end
    
    return false
end

globalAnswer.failureResponse = "I don't understand. Please tell me what you'd like to do with your {guild account}."

local yesAnswer = globalAnswer:keyword("yes")
function yesAnswer:callback(npc, player, message, handler)
    local pid = player:getGuid()
    local currentState = state[pid] or TOPIC.NONE
    local guild = player:getGuild()
    
    if (currentState == TOPIC.DEPOSIT_CONSENT or currentState == TOPIC.WITHDRAW_CONSENT) and
        (not guild or guild:isBankBalanceBlocked()) then
        state[pid] = TOPIC.NONE
        return true, "The guild bank is temporarily unavailable."
    end
    if currentState == TOPIC.DEPOSIT_CONSENT then
        local dep = amount[pid] or 0
        if dep > 0 and player:getBankBalance() >= dep then
            player:setBankBalance(player:getBankBalance() - dep)
            player:save()
            db.query("UPDATE `guilds` SET `balance` = `balance` + " .. dep .. " WHERE `id` = " .. guild:getId())
            db.query(string.format(
                "INSERT INTO `guild_transactions` (`guild_id`,`player_associated`,`type`,`category`,`balance`,`time`) VALUES (%d,%d,'DEPOSIT','CONTRIBUTION',%d,%d)",
                guild:getId(), player:getGuid(), dep, os.time()
            ))
            state[pid] = TOPIC.NONE
            amount[pid] = nil
            return true, "We have added " .. dep .. " gold to the guild " .. guild:getName() .. "."
        end
        state[pid] = TOPIC.NONE
        amount[pid] = nil
        return true, "You do not have enough gold."
    end
    
    if currentState == TOPIC.WITHDRAW_CONSENT then
        local guild = player:getGuild()
        local wd = amount[pid] or 0
        local bal = getGuildBalance(guild:getId())
        if wd < 1 or wd > bal then
            state[pid] = TOPIC.NONE
            amount[pid] = nil
            return true, "There is not enough gold in the guild account."
        end
        if player:getGuid() ~= guild:getOwnerGUID() and player:getGuildLevel() ~= 2 then
            state[pid] = TOPIC.NONE
            amount[pid] = nil
            return true, "Sorry, only Leaders and Vice-leaders may withdraw funds."
        end
        db.query("UPDATE `guilds` SET `balance` = " .. (bal - wd) .. " WHERE `id` = " .. guild:getId())
        player:setBankBalance(player:getBankBalance() + wd)
        player:save()
        db.query(string.format(
            "INSERT INTO `guild_transactions` (`guild_id`,`player_associated`,`type`,`balance`,`time`) VALUES (%d,%d,'WITHDRAW',%d,%d)",
            guild:getId(), player:getGuid(), wd, os.time()
        ))
        state[pid] = TOPIC.NONE
        amount[pid] = nil
        return true, "We removed " .. wd .. " gold from " .. guild:getName() .. " and added it to your personal account."
    end
    
    return false
end

yesAnswer.failureResponse = "I don't understand. Please tell me if you want to proceed or not."

local noAnswer = globalAnswer:keyword("no")
function noAnswer:callback(npc, player, message, handler)
    local pid = player:getGuid()
    state[pid] = TOPIC.NONE
    amount[pid] = nil
    return true, "Alright then."
end

------------------------------------------------------------------------
-- Info keywords
------------------------------------------------------------------------
local guildAccount = greet:keyword({"help", "money", "guild account"})
guildAccount:respond(
    "You can check the {balance} of your guild account and {deposit} money to it. " ..
    "Guild Leaders and Vice-leaders can also {withdraw}. " ..
    "Guild Leaders can {transfer} money and check their guild {ledger}."
)

greet:keyword({"job", "functions", "basic"}):respond("I work in this bank. I can {help} you with your {guild account}.")
greet:keyword({"rent"}):respond("Once you acquire a guildhall the rent will be charged automatically from your {guild account} every month.")
greet:keyword({"personal"}):respond("Head over to my colleague the {Banker} to manage your personal account.")
greet:keyword({"banker"}):respond("Banker is my colleague. He will help you exchange money and manage your personal funds.")

------------------------------------------------------------------------
-- Balance (from guild account menu)
------------------------------------------------------------------------
local balanceFromInfo = guildAccount:keyword({"balance"})
function balanceFromInfo:callback(npc, player, message, handler)
    local guild = player:getGuild()
    if not guild then
        return true, "I'm busy serving guilds. My colleague the {Banker} can assist with personal accounts."
    end
    local bal = getGuildBalance(guild:getId())
    return true, "The {guild account} balance of " .. guild:getName() .. " is " .. bal .. " gold."
end

balanceFromInfo.failureResponse = "I don't understand. Please tell me what you'd like to know about your {guild account}."

------------------------------------------------------------------------
-- Deposit (from guild account menu)
------------------------------------------------------------------------
local depositFromInfo = guildAccount:keyword({"deposit"})
function depositFromInfo:callback(npc, player, message, handler)
    local guild = player:getGuild()
    if not guild then
        return true, "I'm busy serving guilds. My colleague the {Banker} can assist with personal accounts."
    end
    local pid = player:getGuid()
    if player:getBankBalance() < 1 then
        state[pid] = TOPIC.NONE
        return true, "Your personal bank account is empty. Deposit money there first."
    end
    state[pid] = TOPIC.DEPOSIT_GOLD
    return true, "Please tell me how much gold you would like to deposit to " .. guild:getName() .. "."
end

depositFromInfo.failureResponse = "I don't understand. Please tell me what you'd like to do with your {guild account}."

------------------------------------------------------------------------
-- Withdraw (from guild account menu)
------------------------------------------------------------------------
local withdrawFromInfo = guildAccount:keyword({"withdraw"})
function withdrawFromInfo:callback(npc, player, message, handler)
    local guild = player:getGuild()
    if not guild then
        return true, "I'm busy serving guilds. My colleague the {Banker} can assist with personal accounts."
    end
    local pid = player:getGuid()
    state[pid] = TOPIC.WITHDRAW_GOLD
    return true, "Please tell me how much gold you would like to withdraw from " .. guild:getName() .. "."
end

withdrawFromInfo.failureResponse = "I don't understand. Please tell me what you'd like to do with your {guild account}."

------------------------------------------------------------------------
-- Transfer (from guild account menu)
------------------------------------------------------------------------
local transferFromInfo = guildAccount:keyword({"transfer"})
function transferFromInfo:callback(npc, player, message, handler)
    local guild = player:getGuild()
    if not guild then return true, "I'm busy serving guilds." end
    local pid = player:getGuid()
    if player:getGuid() ~= guild:getOwnerGUID() and player:getGuildLevel() ~= 2 then
        state[pid] = TOPIC.NONE
        return true, "Sorry, only Leaders and Vice-leaders may transfer funds."
    end
    state[pid] = TOPIC.TRANSFER_TYPE
    return true, "Would you like to transfer money to a {guild} or a {player}?"
end

transferFromInfo.failureResponse = "I don't understand. Please tell me what you'd like to do with your {guild account}."

------------------------------------------------------------------------
-- Ledger (from guild account menu)
------------------------------------------------------------------------
local ledgerFromInfo = guildAccount:keyword({"ledger"})
function ledgerFromInfo:callback(npc, player, message, handler)
    local guild = player:getGuild()
    if not guild then return true, "I'm busy serving guilds." end
    if player:getGuid() ~= guild:getOwnerGUID() then
        return true, "Sorry, this is confidential between me and your Guild Leader!"
    end
    state[player:getGuid()] = TOPIC.LEDGER_CONSENT
    return true, "I have ledger records of all transactions for your {guild account}. Would you like a copy?"
end

ledgerFromInfo.failureResponse = "I don't understand. Please tell me what you'd like to do with your {guild account}."

------------------------------------------------------------------------
-- Balance (direct from greet)
------------------------------------------------------------------------
local balance = greet:keyword({"balance"})
function balance:callback(npc, player, message, handler)
    local guild = player:getGuild()
    if not guild then
        return true, "I'm busy serving guilds. My colleague the {Banker} can assist with personal accounts."
    end
    local bal = getGuildBalance(guild:getId())
    return true, "The {guild account} balance of " .. guild:getName() .. " is " .. bal .. " gold."
end

balance.failureResponse = "I don't understand. Please tell me what you'd like to do with your {guild account}."

------------------------------------------------------------------------
-- Deposit (direct from greet)
------------------------------------------------------------------------
local deposit = greet:keyword({"deposit"})
function deposit:callback(npc, player, message, handler)
    local guild = player:getGuild()
    if not guild then
        return true, "I'm busy serving guilds. My colleague the {Banker} can assist with personal accounts."
    end
    local pid = player:getGuid()
    if player:getBankBalance() < 1 then
        state[pid] = TOPIC.NONE
        return true, "Your personal bank account is empty. Deposit money there first."
    end
    state[pid] = TOPIC.DEPOSIT_GOLD
    return true, "Please tell me how much gold you would like to deposit to " .. guild:getName() .. "."
end

deposit.failureResponse = "I don't understand. Please tell me what you'd like to do with your {guild account}."

local depositYes = deposit:keyword("yes")
function depositYes:callback(npc, player, message, handler)
    local pid = player:getGuid()
    if state[pid] ~= TOPIC.DEPOSIT_CONSENT then return false end
    local guild = player:getGuild()
    if not guild or guild:isBankBalanceBlocked() then
        state[pid] = TOPIC.NONE
        return true, "The guild bank is temporarily unavailable."
    end
    local dep = amount[pid] or 0
    if dep > 0 and player:getBankBalance() >= dep then
        player:setBankBalance(player:getBankBalance() - dep)
        player:save()
        db.query("UPDATE `guilds` SET `balance` = `balance` + " .. dep .. " WHERE `id` = " .. guild:getId())
        db.query(string.format(
            "INSERT INTO `guild_transactions` (`guild_id`,`player_associated`,`type`,`category`,`balance`,`time`) VALUES (%d,%d,'DEPOSIT','CONTRIBUTION',%d,%d)",
            guild:getId(), player:getGuid(), dep, os.time()
        ))
        state[pid] = TOPIC.NONE
        return true, "We have added " .. dep .. " gold to the guild " .. guild:getName() .. "."
    end
    state[pid] = TOPIC.NONE
    return true, "You do not have enough gold."
end

depositYes.failureResponse = "I don't understand. Please tell me if you want to {deposit} or not."

local depositNo = deposit:keyword("no")
function depositNo:callback(npc, player, message, handler)
    state[player:getGuid()] = TOPIC.NONE
    return true, "As you wish. Is there something else I can do for you?"
end

------------------------------------------------------------------------
-- Withdraw (direct from greet)
------------------------------------------------------------------------
local withdraw = greet:keyword({"withdraw"})
function withdraw:callback(npc, player, message, handler)
    local guild = player:getGuild()
    if not guild then
        return true, "I'm busy serving guilds. My colleague the {Banker} can assist with personal accounts."
    end
    local pid = player:getGuid()
    state[pid] = TOPIC.WITHDRAW_GOLD
    return true, "Please tell me how much gold you would like to withdraw from " .. guild:getName() .. "."
end

withdraw.failureResponse = "I don't understand. Please tell me what you'd like to do with your {guild account}."

local withdrawYes = withdraw:keyword("yes")
function withdrawYes:callback(npc, player, message, handler)
    local pid = player:getGuid()
    if state[pid] ~= TOPIC.WITHDRAW_CONSENT then return false end
    local guild = player:getGuild()
    if not guild or guild:isBankBalanceBlocked() then
        state[pid] = TOPIC.NONE
        return true, "The guild bank is temporarily unavailable."
    end
    local wd = amount[pid] or 0
    local bal = getGuildBalance(guild:getId())
    if wd < 1 or wd > bal then
        state[pid] = TOPIC.NONE
        return true, "There is not enough gold in the guild account."
    end
    if player:getGuid() ~= guild:getOwnerGUID() and player:getGuildLevel() ~= 2 then
        state[pid] = TOPIC.NONE
        return true, "Sorry, only Leaders and Vice-leaders may withdraw funds."
    end
    db.query("UPDATE `guilds` SET `balance` = " .. (bal - wd) .. " WHERE `id` = " .. guild:getId())
    player:setBankBalance(player:getBankBalance() + wd)
    player:save()
    db.query(string.format(
        "INSERT INTO `guild_transactions` (`guild_id`,`player_associated`,`type`,`balance`,`time`) VALUES (%d,%d,'WITHDRAW',%d,%d)",
        guild:getId(), player:getGuid(), wd, os.time()
    ))
    state[pid] = TOPIC.NONE
    return true, "We removed " .. wd .. " gold from " .. guild:getName() .. " and added it to your personal account."
end

withdrawYes.failureResponse = "I don't understand. Please tell me if you want to {withdraw} or not."

local withdrawNo = withdraw:keyword("no")
function withdrawNo:callback(npc, player, message, handler)
    state[player:getGuid()] = TOPIC.NONE
    return true, "Come back anytime you wish to {withdraw}."
end

------------------------------------------------------------------------
-- Transfer (direct from greet)
------------------------------------------------------------------------
local transfer = greet:keyword({"transfer"})
function transfer:callback(npc, player, message, handler)
    local guild = player:getGuild()
    if not guild then return true, "I'm busy serving guilds." end
    local pid = player:getGuid()
    if player:getGuid() ~= guild:getOwnerGUID() and player:getGuildLevel() ~= 2 then
        state[pid] = TOPIC.NONE
        return true, "Sorry, only Leaders and Vice-leaders may transfer funds."
    end
    state[pid] = TOPIC.TRANSFER_TYPE
    return true, "Would you like to transfer money to a {guild} or a {player}?"
end

transfer.failureResponse = "I don't understand. Please tell me what you'd like to do with your {guild account}."

local transferGuild = transfer:keyword({"guild"})
function transferGuild:callback(npc, player, message, handler)
    local pid = player:getGuid()
    local guild = player:getGuild()
    if player:getGuid() ~= guild:getOwnerGUID() then
        state[pid] = TOPIC.NONE
        return true, "Only Guild Leaders may transfer funds between guilds."
    end
    state[pid] = TOPIC.TRANSFER_GUILD_GOLD
    return true, "Please tell me the amount of gold you would like to transfer to another guild."
end

transferGuild.failureResponse = "I don't understand. Please tell me if you want to transfer to a {guild} or {player}."

local transferPlayer = transfer:keyword({"player"})
function transferPlayer:callback(npc, player, message, handler)
    local pid = player:getGuid()
    state[pid] = TOPIC.TRANSFER_PLAYER_GOLD
    return true, "Please tell me the amount of gold you would like to transfer."
end

transferPlayer.failureResponse = "I don't understand. Please tell me if you want to transfer to a {guild} or {player}."

local transferYes = transfer:keyword("yes")
function transferYes:callback(npc, player, message, handler)
    local pid = player:getGuid()
    local guild = player:getGuild()
    local t = target[pid]
    local amt = amount[pid] or 0
    if (state[pid] == TOPIC.TRANSFER_GUILD_CONSENT or state[pid] == TOPIC.TRANSFER_PLAYER_CONSENT) and
        (not guild or guild:isBankBalanceBlocked()) then
        state[pid] = TOPIC.NONE
        return true, "The guild bank is temporarily unavailable."
    end

    if state[pid] == TOPIC.TRANSFER_GUILD_CONSENT then
        if t and Guild.isBankBalanceBlocked(t.id) then
            state[pid] = TOPIC.NONE
            return true, "The recipient's guild bank is temporarily unavailable."
        end
        if not t or amt < 1 or amt > guild:getBankBalance() then
            state[pid] = TOPIC.NONE
            return true, "Your guild account cannot afford this transfer."
        end
        if not guild:setBankBalance(guild:getBankBalance() - amt) then
            state[pid] = TOPIC.NONE
            return true, "The guild bank is temporarily unavailable."
        end
        local tGuild = Guild(t.name)
        if tGuild then
            tGuild:setBankBalance(tGuild:getBankBalance() + amt)
        else
            db.query("UPDATE `guilds` SET `balance` = (`balance`+" .. amt .. ") WHERE `id`=" .. t.id)
        end
        local now = os.time()
        db.query(string.format("INSERT INTO `guild_transactions` (`guild_id`,`guild_associated`,`player_associated`,`type`,`balance`,`time`) VALUES (%d,%d,%d,'WITHDRAW',%d,%d)", guild:getId(), t.id, player:getGuid(), amt, now))
        db.query(string.format("INSERT INTO `guild_transactions` (`guild_id`,`guild_associated`,`player_associated`,`type`,`balance`,`time`) VALUES (%d,%d,%d,'DEPOSIT',%d,%d)", t.id, guild:getId(), player:getGuid(), amt, now))
        target[pid] = nil; state[pid] = TOPIC.NONE
        return true, "You have transferred " .. amt .. " gold to " .. t.name .. "."

    elseif state[pid] == TOPIC.TRANSFER_PLAYER_CONSENT then
        if not t or amt < 1 or amt > guild:getBankBalance() then
            state[pid] = TOPIC.NONE
            return true, "Your guild account cannot afford this transfer."
        end
        if not guild:setBankBalance(guild:getBankBalance() - amt) then
            state[pid] = TOPIC.NONE
            return true, "The guild bank is temporarily unavailable."
        end
        if not player:transferMoneyTo(t, amt) then
            guild:setBankBalance(guild:getBankBalance() + amt)
            state[pid] = TOPIC.NONE
            return true, "You cannot transfer money to this account."
        end
        db.query(string.format("INSERT INTO `guild_transactions` (`guild_id`,`player_associated`,`type`,`balance`,`time`) VALUES (%d,%d,'WITHDRAW',%d,%d)", guild:getId(), player:getGuid(), amt, os.time()))
        target[pid] = nil; state[pid] = TOPIC.NONE
        return true, "You have transferred " .. amt .. " gold to " .. t.name .. "."
    end
    return false
end

transferYes.failureResponse = "I don't understand. Please tell me if you want to {transfer} or not."

local transferNo = transfer:keyword("no")
function transferNo:callback(npc, player, message, handler)
    state[player:getGuid()] = TOPIC.NONE
    return true, "Alright, is there something else I can do for you?"
end

------------------------------------------------------------------------
-- Ledger (direct from greet)
------------------------------------------------------------------------
local ledger = greet:keyword({"ledger"})
function ledger:callback(npc, player, message, handler)
    local guild = player:getGuild()
    if not guild then return true, "I'm busy serving guilds." end
    if player:getGuid() ~= guild:getOwnerGUID() then
        return true, "Sorry, this is confidential between me and your Guild Leader!"
    end
    state[player:getGuid()] = TOPIC.LEDGER_CONSENT
    return true, "I have ledger records of all transactions for your {guild account}. Would you like a copy?"
end

ledger.failureResponse = "I don't understand. Please tell me what you'd like to do with your {guild account}."

local ledgerYes = ledger:keyword("yes")
function ledgerYes:callback(npc, player, message, handler)
    local pid = player:getGuid()
    if state[pid] ~= TOPIC.LEDGER_CONSENT then return false end
    local guild = player:getGuild()
    local dbTx = db.storeQuery([[
        SELECT g.name as guild_name, g2.name as guild_associated_name,
               p.name as player_name, t.type, t.balance, t.time
        FROM guild_transactions as t
        JOIN guilds as g ON t.guild_id = g.id
        LEFT JOIN guilds as g2 ON t.guild_associated = g2.id
        LEFT JOIN players as p ON t.player_associated = p.id
        WHERE guild_id = ]] .. guild:getId() .. [[ ORDER BY t.time DESC
    ]])
    if not dbTx then
        state[pid] = TOPIC.NONE
        return true, "Your ledger is empty. Start using your {guild account}!"
    end
    local text = "Ledger Date: " .. os.date("%d. %b %Y - %H:%M:%S", os.time()) ..
                 "\nOfficial ledger for Guild: " .. guild:getName() ..
                 "\nGuild balance: " .. guild:getBankBalance() .. "\n\n"
    local records = {}
    repeat
        local gAssoc = result.getString(dbTx, "guild_associated_name")
        gAssoc = gAssoc ~= "" and ("\nReceiving Guild: The " .. gAssoc) or ""
        local tp = result.getString(dbTx, "type") == "WITHDRAW" and "Withdraw" or "Deposit"
        table.insert(records,
            "Date: " .. os.date("%d. %b %Y - %H:%M:%S", result.getNumber(dbTx, "time")) ..
            "\nType: Guild " .. tp ..
            "\nGold Amount: " .. result.getNumber(dbTx, "balance") ..
            "\nReceipt Owner: " .. result.getString(dbTx, "player_name") ..
            "\nReceipt Guild: The " .. result.getString(dbTx, "guild_name") .. gAssoc
        )
    until not result.next(dbTx)
    result.free(dbTx)
    local doc = Game.createItem(ITEM_DOCUMENT_RO, 1)
    doc:setAttribute(ITEM_ATTRIBUTE_TEXT, text .. table.concat(records, "\n\n"))
    player:addItemEx(doc)
    state[pid] = TOPIC.NONE
    return true, "Here is your ledger, " .. player:getName() .. ". Come back anytime for an updated copy."
end

ledgerYes.failureResponse = "I don't understand. Please tell me if you want a {ledger} or not."

local ledgerNo = ledger:keyword("no")
function ledgerNo:callback(npc, player, message, handler)
    state[player:getGuid()] = TOPIC.NONE
    return true, "No worries, I will keep it updated for a later date."
end
