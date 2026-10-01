--[[
    >> Modules <<

    Description:
        - This file contains the modules for the NPC system.
        - Modules are pre written NpcsHandler to make it easier to make certain types of NPCs.
        - You can write your own modules and add them to the system.

    Functions:
        - NpcsHandler:travelTo(params)
]]

---@alias travelTo fun(self: NpcsHandler, params: table<string, table>)

-- This Module enables fast and easy creation of Travel NPCs
---@class travelParams
---@field position Position
---@field money number
---@field level number
---@field premium boolean
---@field storage table<string, any>
---@field item table<string, number>
---@field removeItem table<string, any>
---@field isPzLocked boolean
---@field isInfight boolean
---@param params table<string, travelParams>
function NpcsHandler:travelTo(params)
    local greet = self:keyword(self.greetWords)
    greet:setGreetResponse("Hello |PLAYERNAME| I can {travel} you to wherever you want, just tell me your {destination}")
    local traveling = greet:keyword({"travel", "destination", "destinations"})
    local words = {}

    for name, dest in pairs(params) do
        local toDest = traveling:keyword(name)
        -- Cities are also valid immediately after greeting (and after listing
        -- destinations), not only after the player explicitly says "travel".
        if rawget(greet.keywords, name) == nil then
            greet.keywords[name] = toDest
        end
        toDest:respond(string.format("Do you want to travel to {%s} for {%d} gold?", name, dest.money and dest.money or 0))
        table.insert(words, "{" .. name .. "}")

        local accept = toDest:keyword("yes")
        accept:respond("I will take you there!")
        accept:teleport(dest.position)

        local require = accept:requirements()
        require:isPzLocked(dest.isPzLocked == true)
        require:isInfight(dest.isInfight == true)
        if dest.money then require:removeMoney(dest.money) end
        if dest.level then require:level(dest.level) end
        if dest.premium ~= nil then require:premium(dest.premium) end
        if dest.storage then require:storage(dest.storage.key, dest.storage.value, dest.storage.operator ~= nil and dest.storage.operator) end
        if dest.item then require:item(dest.item.item, dest.item.count, dest.item.subType) end
        if dest.removeItem then require:removeItem(dest.removeItem.item, dest.removeItem.count, dest.removeItem.subType, dest.removeItem.ignoreEquipped) end

        local decline = toDest:keyword("no")
        decline:respond("Ok, maybe next time.")
    end

    table.sort(words)
    traveling:respond("Here are the destinations: " .. table.concat(words, ", "))
    -- Listing cities must leave the player at the greeting menu, so a second
    -- list request or a directly selected city is accepted as well.
    traveling:resetTalkState()
end
