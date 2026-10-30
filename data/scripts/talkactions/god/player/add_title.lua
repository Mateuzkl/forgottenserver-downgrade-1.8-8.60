local talkaction = TalkAction("/addtitle")

function talkaction.onSay(player, words, param)
    local parts = param:splitTrimmed(",")
    local target = parts[1] and Player(parts[1])
    local id = parts[2] and tonumber(parts[2])
    if not target or not id or not CharacterTitles or not CharacterTitles.grant(target, id) then
        player:sendCancelMessage("Usage: /addtitle online player name, title ID (1-95)")
        return false
    end
    player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Granted title " .. id .. " to " .. target:getName() .. ".")
    return false
end

talkaction:separator(" ")
talkaction:accountType(6)
talkaction:access(true)
talkaction:register()
