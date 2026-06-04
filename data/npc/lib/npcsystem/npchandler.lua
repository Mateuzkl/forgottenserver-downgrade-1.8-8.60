-- Advanced NPC System by Jiddo
if NpcHandler == nil then
    -- Constant talkdelay behaviors.
    TALKDELAY_NONE = 0 -- No talkdelay. Npc will reply immedeatly.
    TALKDELAY_ONTHINK = 1 -- Talkdelay handled through the onThink callback function. (Default)
    TALKDELAY_EVENT = 2 -- Not yet implemented

    -- Currently applied talkdelay behavior. TALKDELAY_ONTHINK is default.
    NPCHANDLER_TALKDELAY = TALKDELAY_ONTHINK

    -- Constant indexes for defining default messages.
    MESSAGE_GREET = 1 -- When the player greets the npc.
    MESSAGE_FAREWELL = 2 -- When the player unGreets the npc.
    MESSAGE_BUY = 3 -- When the npc asks the player if he wants to buy something.
    MESSAGE_ONBUY = 4 -- When the player successfully buys something via talk.
    MESSAGE_BOUGHT = 5 -- When the player bought something through the shop window.
    MESSAGE_SELL = 6 -- When the npc asks the player if he wants to sell something.
    MESSAGE_ONSELL = 7 -- When the player successfully sells something via talk.
    MESSAGE_SOLD = 8 -- When the player sold something through the shop window.
    MESSAGE_MISSINGMONEY = 9 -- When the player does not have enough money.
    MESSAGE_NEEDMONEY = 10 -- Same as above, used for shop window.
    MESSAGE_MISSINGITEM = 11 -- When the player is trying to sell an item he does not have.
    MESSAGE_NEEDITEM = 12 -- Same as above, used for shop window.
    MESSAGE_NEEDSPACE = 13 -- When the player don't have any space to buy an item
    MESSAGE_NEEDMORESPACE = 14 -- When the player has some space to buy an item, but not enough space
    MESSAGE_IDLETIMEOUT = 15 -- When the player has been idle for longer then idleTime allows.
    MESSAGE_WALKAWAY = 16 -- When the player walks out of the talkRadius of the npc.
    MESSAGE_DECLINE = 17 -- When the player says no to something.
    MESSAGE_SENDTRADE = 18 -- When the npc sends the trade window to the player
    MESSAGE_NOSHOP = 19 -- When the npc's shop is requested but he doesn't have any
    MESSAGE_ONCLOSESHOP = 20 -- When the player closes the npc's shop window
    MESSAGE_ALREADYFOCUSED = 21 -- When the player already has the focus of this npc.
    MESSAGE_WALKAWAY_MALE = 22 -- When a male player walks out of the talkRadius of the npc.
    MESSAGE_WALKAWAY_FEMALE = 23 -- When a female player walks out of the talkRadius of the npc.

    -- Constant indexes for callback functions. These are also used for module callback ids.
    CALLBACK_CREATURE_APPEAR = 1
    CALLBACK_CREATURE_DISAPPEAR = 2
    CALLBACK_CREATURE_SAY = 3
    CALLBACK_ONTHINK = 4
    CALLBACK_GREET = 5
    CALLBACK_FAREWELL = 6
    CALLBACK_MESSAGE_DEFAULT = 7
    CALLBACK_PLAYER_ENDTRADE = 8
    CALLBACK_PLAYER_CLOSECHANNEL = 9
    CALLBACK_ONBUY = 10
    CALLBACK_ONSELL = 11
    CALLBACK_ONADDFOCUS = 18
    CALLBACK_ONRELEASEFOCUS = 19
    CALLBACK_ONTRADEREQUEST = 20

    -- Addidional module callback ids
    CALLBACK_MODULE_INIT = 12
    CALLBACK_MODULE_RESET = 13

    -- Constant strings defining the keywords to replace in the default messages.
    TAG_PLAYERNAME = "|PLAYERNAME|"
    TAG_ITEMCOUNT = "|ITEMCOUNT|"
    TAG_TOTALCOST = "|TOTALCOST|"
    TAG_ITEMNAME = "|ITEMNAME|"

    NpcHandler = {
        keywordHandler = nil,
        focuses = nil,
        talkStart = nil,
        idleTime = 120,
        talkRadius = 3,
        talkDelayTime = 1, -- Seconds to delay outgoing messages.
        talkDelay = nil,
        callbackFunctions = nil,
        modules = nil,
        shopItems = nil, -- They must be here since ShopModule uses 'static' functions
        eventSay = nil,
        eventDelayedSay = nil,
        topic = nil,
        messages = {
            -- These are the default replies of all npcs. They can/should be changed individually for each npc.
            [MESSAGE_GREET] = "Greetings, |PLAYERNAME|.",
            [MESSAGE_FAREWELL] = "Good bye, |PLAYERNAME|.",
            [MESSAGE_BUY] = "Do you want to buy |ITEMCOUNT| |ITEMNAME| for |TOTALCOST| gold coins?",
            [MESSAGE_ONBUY] = "Here you are.",
            [MESSAGE_BOUGHT] = "Bought |ITEMCOUNT|x |ITEMNAME| for |TOTALCOST| gold.",
            [MESSAGE_SELL] = "Do you want to sell |ITEMCOUNT| |ITEMNAME| for |TOTALCOST| gold coins?",
            [MESSAGE_ONSELL] = "Here you are, |TOTALCOST| gold.",
            [MESSAGE_SOLD] = "Sold |ITEMCOUNT|x |ITEMNAME| for |TOTALCOST| gold.",
            [MESSAGE_MISSINGMONEY] = "You don't have enough money.",
            [MESSAGE_NEEDMONEY] = "You don't have enough money.",
            [MESSAGE_MISSINGITEM] = "You don't have so many.",
            [MESSAGE_NEEDITEM] = "You do not have this object.",
            [MESSAGE_NEEDSPACE] = "You do not have enough capacity.",
            [MESSAGE_NEEDMORESPACE] = "You do not have enough capacity for all items.",
            [MESSAGE_IDLETIMEOUT] = "Good bye.",
            [MESSAGE_WALKAWAY] = "Good bye.",
            [MESSAGE_DECLINE] = "Then not.",
            [MESSAGE_SENDTRADE] = "Of course, just browse through my wares.",
            [MESSAGE_NOSHOP] = "Sorry, I'm not offering anything.",
            [MESSAGE_ONCLOSESHOP] = "Thank you, come back whenever you're in need of something else.",
            [MESSAGE_ALREADYFOCUSED] = "|PLAYERNAME|, I am already talking to you.",
            [MESSAGE_WALKAWAY_MALE] = "Good bye.",
            [MESSAGE_WALKAWAY_FEMALE] = "Good bye."
        }
    }

    -- Helper to retrieve isolated state for the currently executing NPC instance.
    function NpcHandler:getNpcState()
        local npcId = (getNpcCid and getNpcCid()) or 0
        if not self.npcStates then
            self.npcStates = {}
        end
        if not self.npcStates[npcId] then
            self.npcStates[npcId] = {
                focuses = {},
                talkStart = {},
                topic = {},
                talkDelay = {},
                eventSay = {},
                eventDelayedSay = {}
            }
        end
        return self.npcStates[npcId]
    end

    -- Creates a new NpcHandler with an empty callbackFunction stack.
    function NpcHandler:new(keywordHandler)
        local obj = {}
        obj.callbackFunctions = {}
        obj.modules = {}

        -- Setup redirect metatables to support per-NPC-instance states
        obj.eventSay = setmetatable({}, {
            __index = function(t, k) return obj:getNpcState().eventSay[k] end,
            __newindex = function(t, k, v) obj:getNpcState().eventSay[k] = v end
        })
        obj.eventDelayedSay = setmetatable({}, {
            __index = function(t, k) return obj:getNpcState().eventDelayedSay[k] end,
            __newindex = function(t, k, v) obj:getNpcState().eventDelayedSay[k] = v end
        })
        obj.topic = setmetatable({}, {
            __index = function(t, k) return obj:getNpcState().topic[k] end,
            __newindex = function(t, k, v) obj:getNpcState().topic[k] = v end
        })
        obj.focuses = setmetatable({}, {
            __index = function(t, k) return obj:getNpcState().focuses[k] end,
            __newindex = function(t, k, v) obj:getNpcState().focuses[k] = v end
        })
        obj.talkStart = setmetatable({}, {
            __index = function(t, k) return obj:getNpcState().talkStart[k] end,
            __newindex = function(t, k, v) obj:getNpcState().talkStart[k] = v end
        })
        obj.talkDelay = setmetatable({}, {
            __index = function(t, k) return obj:getNpcState().talkDelay[k] end,
            __newindex = function(t, k, v) obj:getNpcState().talkDelay[k] = v end
        })

        obj.keywordHandler = keywordHandler
        obj.messages = {}
        obj.shopItems = {}

        setmetatable(obj.messages, self.messages)
        self.messages.__index = self.messages

        setmetatable(obj, self)
        self.__index = self
        return obj
    end

    -- Re-defines the maximum idle time allowed for a player when talking to this npc.
    function NpcHandler:setMaxIdleTime(newTime) self.idleTime = newTime end

    -- Attaches a new keyword handler to this npchandler
    function NpcHandler:setKeywordHandler(newHandler)
        self.keywordHandler = newHandler
    end

    function NpcHandler:isConversationMessage(msgtype)
        return msgtype == TALKTYPE_SAY or msgtype == TALKTYPE_WHISPER or
                   msgtype == TALKTYPE_YELL or msgtype == TALKTYPE_PRIVATE_PN
    end

    function NpcHandler:getFocus()
        return self:getNpcState().focuses[1]
    end

    -- Function used to change the focus of this npc.
    function NpcHandler:addFocus(newFocus)
        if self:isFocused(newFocus) then return end

        local focuses = self:getNpcState().focuses
        focuses[#focuses + 1] = newFocus
        self:getNpcState().topic[newFocus] = 0
        local callback = self:getCallback(CALLBACK_ONADDFOCUS)
        if callback == nil or callback(newFocus) then
            self:processModuleCallback(CALLBACK_ONADDFOCUS, newFocus)
        end
        self:updateFocus()
    end

    -- Function used to verify if npc is focused to certain player
    function NpcHandler:isFocused(focus)
        for k, v in pairs(self:getNpcState().focuses) do
            if v == focus then return true end
        end
        return false
    end

    -- This function should be called on each onThink and makes sure the npc faces the player it is talking to.
    --	Should also be called whenever a new player is focused.
    function NpcHandler:updateFocus()
        local function setNativeFocus(focus)
            doNpcSetCreatureFocus(focus or 0)

            if Npc and getNpcCid then
                local npc = Npc(getNpcCid())
                if npc then
                    npc:setFocus(focus and Player(focus) or 0)
                end
            end
        end

        for pos, focus in pairs(self:getNpcState().focuses) do
            if focus then
                setNativeFocus(focus)
                return
            end
        end
        setNativeFocus(nil)
    end

    -- Used when the npc should un-focus the player.
    function NpcHandler:releaseFocus(focus)
        if shop_cost[focus] then
            shop_amount[focus] = nil
            shop_cost[focus] = nil
            shop_rlname[focus] = nil
            shop_itemid[focus] = nil
            shop_container[focus] = nil
            shop_npcuid[focus] = nil
            shop_eventtype[focus] = nil
            shop_subtype[focus] = nil
            shop_destination[focus] = nil
            shop_premium[focus] = nil
        end

        if self:getNpcState().eventDelayedSay[focus] then
            self:cancelNPCTalk(self:getNpcState().eventDelayedSay[focus])
        end

        if not self:isFocused(focus) then return end

        local pos = nil
        local focuses = self:getNpcState().focuses
        for k, v in pairs(focuses) do if v == focus then pos = k end end
        focuses[pos] = nil

        self:getNpcState().eventSay[focus] = nil
        self:getNpcState().eventDelayedSay[focus] = nil
        self:getNpcState().talkStart[focus] = nil
        self:getNpcState().topic[focus] = nil

        local callback = self:getCallback(CALLBACK_ONRELEASEFOCUS)
        if callback == nil or callback(focus) then
            self:processModuleCallback(CALLBACK_ONRELEASEFOCUS, focus)
        end

        if Player(focus) then
            closeShopWindow(focus) -- Even if it can not exist, we need to prevent it.
        end
        self:updateFocus()
    end

    -- Returns the callback function with the specified id or nil if no such callback function exists.
    function NpcHandler:getCallback(id)
        local ret = nil
        if self.callbackFunctions then ret = self.callbackFunctions[id] end
        return ret
    end

    -- Changes the callback function for the given id to callback.
    function NpcHandler:setCallback(id, callback)
        if self.callbackFunctions then
            self.callbackFunctions[id] = callback
        end
    end

    -- Adds a module to this npchandler and inits it.
    function NpcHandler:addModule(module)
        if self.modules then
            self.modules[#self.modules + 1] = module
            module:init(self)
        end
    end

    -- Calls the callback function represented by id for all modules added to this npchandler with the given arguments.
    function NpcHandler:processModuleCallback(id, ...)
        local ret = true
        for i, module in pairs(self.modules) do
            local tmpRet = true
            if id == CALLBACK_CREATURE_APPEAR and
                module.callbackOnCreatureAppear then
                tmpRet = module:callbackOnCreatureAppear(...)
            elseif id == CALLBACK_CREATURE_DISAPPEAR and
                module.callbackOnCreatureDisappear then
                tmpRet = module:callbackOnCreatureDisappear(...)
            elseif id == CALLBACK_CREATURE_SAY and module.callbackOnCreatureSay then
                tmpRet = module:callbackOnCreatureSay(...)
            elseif id == CALLBACK_PLAYER_ENDTRADE and
                module.callbackOnPlayerEndTrade then
                tmpRet = module:callbackOnPlayerEndTrade(...)
            elseif id == CALLBACK_PLAYER_CLOSECHANNEL and
                module.callbackOnPlayerCloseChannel then
                tmpRet = module:callbackOnPlayerCloseChannel(...)
            elseif id == CALLBACK_ONBUY and module.callbackOnBuy then
                tmpRet = module:callbackOnBuy(...)
            elseif id == CALLBACK_ONSELL and module.callbackOnSell then
                tmpRet = module:callbackOnSell(...)
            elseif id == CALLBACK_ONTRADEREQUEST and
                module.callbackOnTradeRequest then
                tmpRet = module:callbackOnTradeRequest(...)
            elseif id == CALLBACK_ONADDFOCUS and module.callbackOnAddFocus then
                tmpRet = module:callbackOnAddFocus(...)
            elseif id == CALLBACK_ONRELEASEFOCUS and
                module.callbackOnReleaseFocus then
                tmpRet = module:callbackOnReleaseFocus(...)
            elseif id == CALLBACK_ONTHINK and module.callbackOnThink then
                tmpRet = module:callbackOnThink(...)
            elseif id == CALLBACK_GREET and module.callbackOnGreet then
                tmpRet = module:callbackOnGreet(...)
            elseif id == CALLBACK_FAREWELL and module.callbackOnFarewell then
                tmpRet = module:callbackOnFarewell(...)
            elseif id == CALLBACK_MESSAGE_DEFAULT and
                module.callbackOnMessageDefault then
                tmpRet = module:callbackOnMessageDefault(...)
            elseif id == CALLBACK_MODULE_RESET and module.callbackOnModuleReset then
                tmpRet = module:callbackOnModuleReset(...)
            end
            if not tmpRet then
                ret = false
                break
            end
        end
        return ret
    end

    -- Returns the message represented by id.
    function NpcHandler:getMessage(id)
        local ret = nil
        if self.messages then ret = self.messages[id] end
        return ret
    end

    -- Changes the default response message with the specified id to newMessage.
    function NpcHandler:setMessage(id, newMessage)
        if self.messages then self.messages[id] = newMessage end
    end

    -- Translates all message tags found in msg using parseInfo
    function NpcHandler:parseMessage(msg, parseInfo)
        local ret = msg
        for search, replace in pairs(parseInfo) do
            ret = string.gsub(ret, search, replace)
        end
        return ret
    end

    -- Makes sure the npc un-focuses the currently focused player
    function NpcHandler:unGreet(cid)
        if not self:isFocused(cid) then return end

        local callback = self:getCallback(CALLBACK_FAREWELL)
        if callback == nil or callback(cid) then
            if self:processModuleCallback(CALLBACK_FAREWELL) then
                local msg = self:getMessage(MESSAGE_FAREWELL)
                local player = Player(cid)
                local playerName = player and player:getName() or -1
                local parseInfo = {[TAG_PLAYERNAME] = playerName}
                self:resetNpc(cid)
                msg = self:parseMessage(msg, parseInfo)
                self:say(msg, cid, true)
                self:releaseFocus(cid)
            end
        end
    end

    -- Greets a new player.
    function NpcHandler:greet(cid)
        if cid ~= 0 then
            local callback = self:getCallback(CALLBACK_GREET)
            if callback == nil or callback(cid) then
                if self:processModuleCallback(CALLBACK_GREET, cid) then
                    local msg = self:getMessage(MESSAGE_GREET)
                    local player = Player(cid)
                    local playerName = player and player:getName() or -1
                    local parseInfo = {[TAG_PLAYERNAME] = playerName}
                    msg = self:parseMessage(msg, parseInfo)
                    self:say(msg, cid, true)
                else
                    return
                end
            else
                return
            end
        end
        self:addFocus(cid)
    end

    -- Handles onCreatureAppear events. If you with to handle this yourself, please use the CALLBACK_CREATURE_APPEAR callback.
    function NpcHandler:onCreatureAppear(creature)
        local cid = creature:getId()
        local callback = self:getCallback(CALLBACK_CREATURE_APPEAR)
        if callback == nil or callback(cid) then
            if self:processModuleCallback(CALLBACK_CREATURE_APPEAR, cid) then
                --
            end
        end
    end

    -- Handles onCreatureDisappear events. If you with to handle this yourself, please use the CALLBACK_CREATURE_DISAPPEAR callback.
    function NpcHandler:onCreatureDisappear(creature)
        local cid = creature:getId()
        if getNpcCid() == cid then return end

        local callback = self:getCallback(CALLBACK_CREATURE_DISAPPEAR)
        if callback == nil or callback(cid) then
            if self:processModuleCallback(CALLBACK_CREATURE_DISAPPEAR, cid) then
                if self:isFocused(cid) then
                    local player = Player(cid)
                    if player then
                        self:unGreet(cid)
                    else
                        self:resetNpc(cid)
                        self:releaseFocus(cid)
                    end
                end
            end
        end
    end

    -- Handles onCreatureSay events. If you with to handle this yourself, please use the CALLBACK_CREATURE_SAY callback.
    function NpcHandler:onCreatureSay(creature, msgtype, msg)
        local cid = creature:getId()
        if not self:isConversationMessage(msgtype) then return false end

        local isFocused = self:isFocused(cid)
        local currentFocus = self:getFocus()
        if currentFocus and not isFocused then return false end

        local callback = self:getCallback(CALLBACK_CREATURE_SAY)
        if callback == nil or callback(cid, msgtype, msg) then
            if self:processModuleCallback(CALLBACK_CREATURE_SAY, cid, msgtype,
                                          msg) then
                if not self:isInRange(cid) then return end

                if self.keywordHandler then
                    if isFocused or not currentFocus then
                        local ret = self.keywordHandler:processMessage(cid, msg)
                        if not ret then
                            local callback = self:getCallback(
                                                 CALLBACK_MESSAGE_DEFAULT)
                            if callback and callback(cid, msgtype, msg) then
                                self.talkStart[cid] = os.time()
                            end
                        else
                            self.talkStart[cid] = os.time()
                        end
                    end
                end
            end
        end
    end

    -- Handles onPlayerEndTrade events. If you wish to handle this yourself, use the CALLBACK_PLAYER_ENDTRADE callback.
    function NpcHandler:onPlayerEndTrade(creature)
        local cid = creature:getId()
        local callback = self:getCallback(CALLBACK_PLAYER_ENDTRADE)
        if callback == nil or callback(cid) then
            if self:processModuleCallback(CALLBACK_PLAYER_ENDTRADE, cid) then
                if self:isFocused(cid) then
                    local player = Player(cid)
                    local playerName = player and player:getName() or -1
                    local parseInfo = {[TAG_PLAYERNAME] = playerName}
                    local msg = self:parseMessage(self:getMessage(
                                                      MESSAGE_ONCLOSESHOP),
                                                  parseInfo)
                    self:say(msg, cid)
                end
            end
        end
    end

    -- Handles onPlayerCloseChannel events. If you wish to handle this yourself, use the CALLBACK_PLAYER_CLOSECHANNEL callback.
    function NpcHandler:onPlayerCloseChannel(creature)
        local cid = creature:getId()
        local callback = self:getCallback(CALLBACK_PLAYER_CLOSECHANNEL)
        if callback == nil or callback(cid) then
            if self:processModuleCallback(CALLBACK_PLAYER_CLOSECHANNEL, cid) then
                if self:isFocused(cid) then self:unGreet(cid) end
            end
        end
    end

    -- Handles onBuy events. If you wish to handle this yourself, use the CALLBACK_ONBUY callback.
    function NpcHandler:onBuy(creature, itemid, subType, amount, ignoreCap,
                              inBackpacks)
        local cid = creature:getId()
        local callback = self:getCallback(CALLBACK_ONBUY)
        if callback == nil or
            callback(cid, itemid, subType, amount, ignoreCap, inBackpacks) then
            if self:processModuleCallback(CALLBACK_ONBUY, cid, itemid, subType,
                                          amount, ignoreCap, inBackpacks) then
                --
            end
        end
    end

    -- Handles onSell events. If you wish to handle this yourself, use the CALLBACK_ONSELL callback.
    function NpcHandler:onSell(creature, itemid, subType, amount, ignoreCap,
                               inBackpacks)
        local cid = creature:getId()
        local callback = self:getCallback(CALLBACK_ONSELL)
        if callback == nil or
            callback(cid, itemid, subType, amount, ignoreCap, inBackpacks) then
            if self:processModuleCallback(CALLBACK_ONSELL, cid, itemid, subType,
                                          amount, ignoreCap, inBackpacks) then
                --
            end
        end
    end

    -- Handles onTradeRequest events. If you wish to handle this yourself, use the CALLBACK_ONTRADEREQUEST callback.
    function NpcHandler:onTradeRequest(cid)
        local callback = self:getCallback(CALLBACK_ONTRADEREQUEST)
        if callback == nil or callback(cid) then
            if self:processModuleCallback(CALLBACK_ONTRADEREQUEST, cid) then
                return true
            end
        end
        return false
    end

    -- Handles onThink events. If you wish to handle this yourself, please use the CALLBACK_ONTHINK callback.
    function NpcHandler:onThink()
        local callback = self:getCallback(CALLBACK_ONTHINK)
        if callback == nil or callback() then
            if NPCHANDLER_TALKDELAY == TALKDELAY_ONTHINK then
                local talkDelayTable = self:getNpcState().talkDelay
                for cid, talkDelay in pairs(talkDelayTable) do
                    if talkDelay.time and talkDelay.message and os.time() >=
                        talkDelay.time then
                        selfSay(talkDelay.message, cid,
                                talkDelay.publicize and true or false)
                        talkDelayTable[cid] = nil
                    end
                end
            end

            if self:processModuleCallback(CALLBACK_ONTHINK) then
                for pos, focus in pairs(self:getNpcState().focuses) do
                    if focus then
                        local player = Player(focus)
                        if not player then
                            self:resetNpc(focus)
                            self:releaseFocus(focus)
                        elseif not self:isInRange(focus) then
                            self:onWalkAway(focus)
                        elseif self:getNpcState().talkStart[focus] and
                            (os.time() - self:getNpcState().talkStart[focus]) > self.idleTime then
                            self:unGreet(focus)
                        else
                            self:updateFocus()
                        end
                    end
                end
            end
        end
    end

    -- Tries to greet the player with the given cid.
    function NpcHandler:onGreet(cid)
        if self:isInRange(cid) then
            if not self:isFocused(cid) then
                self:greet(cid)
                return
            end
        end
    end

    -- Simply calls the underlying unGreet function.
    function NpcHandler:onFarewell(cid) self:unGreet(cid) end

    -- Should be called on this npc's focus if the distance to focus is greater then talkRadius.
    function NpcHandler:onWalkAway(cid)
        if self:isFocused(cid) then
            local callback = self:getCallback(CALLBACK_CREATURE_DISAPPEAR)
            if callback == nil or callback(cid) then
                if self:processModuleCallback(CALLBACK_CREATURE_DISAPPEAR, cid) then
                    local player = Player(cid)
                    if player then
                        local msg = self:getMessage(MESSAGE_WALKAWAY)
                        local playerName = player:getName()
                        local playerSex = player:getSex()

                        local parseInfo = {[TAG_PLAYERNAME] = playerName}
                        local message = self:parseMessage(msg, parseInfo)

                        local msg_male = self:getMessage(MESSAGE_WALKAWAY_MALE)
                        local message_male = self:parseMessage(msg_male, parseInfo)
                        local msg_female = self:getMessage(MESSAGE_WALKAWAY_FEMALE)
                        local message_female =
                            self:parseMessage(msg_female, parseInfo)
                        if message_female ~= message_male then
                            if playerSex == PLAYERSEX_FEMALE then
                                selfSay(message_female)
                            else
                                selfSay(message_male)
                            end
                        elseif message ~= "" then
                            selfSay(message)
                        end
                    end
                    self:resetNpc(cid)
                    self:releaseFocus(cid)
                end
            end
        end
    end

    -- Returns true if cid is within the talkRadius of this npc.
    function NpcHandler:isInRange(cid)
        local distance = Player(cid) and getDistanceTo(cid) or -1
        if distance == -1 then return false end

        return distance <= self.talkRadius
    end

    -- Resets the npc into its initial state (in regard of the keywordhandler).
    --	All modules are also receiving a reset call through their callbackOnModuleReset function.
    function NpcHandler:resetNpc(cid)
        if self:processModuleCallback(CALLBACK_MODULE_RESET) then
            self.keywordHandler:reset(cid)
        end
    end

    -- Helper to stop scheduled speech events.
    function NpcHandler:cancelNPCTalk(events)
        for aux = 1, #events do stopEvent(events[aux].event) end
        events = nil
    end

    function NpcHandler:doNPCTalkALot(msgs, interval, pcid)
        if self:getNpcState().eventDelayedSay[pcid] then
            self:cancelNPCTalk(self:getNpcState().eventDelayedSay[pcid])
        end

        self:getNpcState().eventDelayedSay[pcid] = {}
        local ret = {}
        for aux = 1, #msgs do
            self:getNpcState().eventDelayedSay[pcid][aux] = {}
            doCreatureSayWithDelay(getNpcCid(), msgs[aux], TALKTYPE_PRIVATE_NP,
                                   ((aux - 1) * (interval or 4000)) + 700,
                                   self:getNpcState().eventDelayedSay[pcid][aux], pcid)
            ret[#ret + 1] = self:getNpcState().eventDelayedSay[pcid][aux]
        end
        return (ret)
    end

    -- Makes the npc represented by this instance of NpcHandler say something.
    --	This implements the currently set type of talkdelay.
    --	shallDelay is a boolean value. If it is false, the message is not delayed. Default value is true.
    function NpcHandler:say(message, focus, publicize, shallDelay, delay)
        if type(message) == "table" then
            return self:doNPCTalkALot(message, delay or 6000, focus)
        end

        if self:getNpcState().eventDelayedSay[focus] then
            self:cancelNPCTalk(self:getNpcState().eventDelayedSay[focus])
        end

        local shallDelay = not shallDelay and true or shallDelay
        if NPCHANDLER_TALKDELAY == TALKDELAY_NONE or shallDelay == false then
            selfSay(message, focus, publicize and true or false)
            return
        end

        stopEvent(self:getNpcState().eventSay[focus])
        self:getNpcState().eventSay[focus] = addEvent(function(npcId, message, focusId)
            local npc = Npc(npcId)
            if npc == nil then return end
            local player = Player(focusId)
            if player then
                npc:say(message, TALKTYPE_PRIVATE_NP, false, player,
                        npc:getPosition())
            end
        end, self.talkDelayTime * 1000, Npc():getId(), message, focus)
    end
end
