--[[
	Enhanced guild management system for TFS 1.4+
	Faster and better in-game experience for those who love old-school Tibia
	- Guild creation and management
	- Member ranking system
	- Invitation system with Accept/Decline
	- Administrative commands
	- Information display

Credits: Mateus Roberto (Mateuzkl)
]]


-- Global guild configuration
local GUILD_CONFIG = {
    MIN_NAME_LENGTH = 3,
    MAX_NAME_LENGTH = 30,
    DEFAULT_RANKS = {
        {name = "Leader", level = 3},
        {name = "Vice-Leader", level = 2},
        {name = "Member", level = 1}
    },
    INVITATION_EXPIRE_TIME = 86400, -- 24 hours in seconds
    MESSAGES = {
        PERMISSION_DENIED = "Access denied. Insufficient guild rank.",
        NOT_IN_GUILD = "You do not belong to any guild.",
        PLAYER_OFFLINE = "Player is offline or does not exist.",
        INVALID_SYNTAX = "Invalid syntax. Please use: ",
        SUCCESS = "Operation completed successfully.",
        ERROR = "An error occurred. Please try again."
    }
}

-- Utility functions
local GuildUtils = {}

function GuildUtils.isMultiWorld()
    return configManager and configKeys and configKeys.MULTI_WORLD_ENABLED
        and configManager.getBoolean(configKeys.MULTI_WORLD_ENABLED) or false
end

function GuildUtils.worldId()
    return GuildUtils.isMultiWorld() and math.max(1, tonumber(configManager.getNumber(configKeys.WORLD_ID)) or 1) or 1
end

function GuildUtils.worldWhere(alias)
    if not GuildUtils.isMultiWorld() then
        return ""
    end
    if alias and alias ~= "" then
        return " AND " .. alias .. ".world_id = " .. GuildUtils.worldId()
    end
    return " AND world_id = " .. GuildUtils.worldId()
end

function GuildUtils.sendMessage(player, message, messageType, usePopup)
    if usePopup then
        player:popupFYI(message)
    else
        messageType = messageType or MESSAGE_INFO_DESCR
        player:sendTextMessage(messageType, message)
    end
end

function GuildUtils.formatRank(rank)
    if rank == 3 then
        return "Leader"
    elseif rank == 2 then
        return "Vice-Leader"
    else
        return "Member"
    end
end

function GuildUtils.checkPermission(player, requiredLevel)
    local guild = player:getGuild()
    if not guild then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.NOT_IN_GUILD)
        return false
    end
    
    if player:getGuildLevel() < requiredLevel then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.PERMISSION_DENIED)
        return false
    end
    
    return true, guild
end

function GuildUtils.getTargetId(targetName, guildId)
    local query = db.storeQuery(string.format([[
        SELECT p.id 
        FROM players p 
        INNER JOIN guild_membership gm ON p.id = gm.player_id 
        WHERE p.name = %s AND gm.guild_id = %d%s
    ]], db.escapeString(targetName), guildId, GuildUtils.worldWhere("p")))
    
    if not query then
        return nil
    end
    
    local id = result.getNumber(query, "id")
    result.free(query)
    return id
end

function GuildUtils.getPlayerId(playerName)
    local query = db.storeQuery("SELECT id FROM players WHERE name = " .. db.escapeString(playerName) .. GuildUtils.worldWhere())
    if not query then
        return nil
    end
    
    local id = result.getNumber(query, "id")
    result.free(query)
    return id
end

function GuildUtils.removeExpiredInvitations()
    local currentTime = os.time()
    db.query("DELETE FROM guild_invitations WHERE expiration < " .. currentTime)
end

function GuildUtils.hasActiveInvitation(playerId)
    GuildUtils.removeExpiredInvitations()
    
    local query = db.storeQuery("SELECT * FROM guild_invitations WHERE player_id = " .. playerId)
    if query then
        result.free(query)
        return true
    end
    return false
end

function GuildUtils.getActiveInvitation(playerId)
    GuildUtils.removeExpiredInvitations()
    
    local query = db.storeQuery("SELECT * FROM guild_invitations WHERE player_id = " .. playerId)
    if not query then
        return nil
    end
    
    local invitation = {
        id = result.getNumber(query, "id"),
        guildId = result.getNumber(query, "guild_id"),
        expiration = result.getNumber(query, "expiration")
    }
    
    result.free(query)
    return invitation
end

-- Make sure guild_invitations table exists
db.query([[
CREATE TABLE IF NOT EXISTS `guild_invitations` (
    `id` int(11) NOT NULL AUTO_INCREMENT,
    `player_id` int(11) NOT NULL,
    `guild_id` int(11) NOT NULL,
    `expiration` int(11) NOT NULL,
    PRIMARY KEY (`id`),
    KEY `player_id` (`player_id`),
    KEY `guild_id` (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
]])

-- Create Guild Command
local createGuild = TalkAction("/createguild")

function createGuild.onSay(player, words, param, type)
    if not param or param == "" then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "/createguild \"Guild Name\"")
        return false
    end
    
    if player:getGuild() then
        GuildUtils.sendMessage(player, "You are already a member of a guild.")
        return false
    end
    
    local guildName = param:match('"([^"]+)"') or param
    guildName = guildName:gsub('"', '')
    
    if guildName:len() < GUILD_CONFIG.MIN_NAME_LENGTH or guildName:len() > GUILD_CONFIG.MAX_NAME_LENGTH then
        GuildUtils.sendMessage(player, string.format("Guild name must be between %d and %d characters.", 
            GUILD_CONFIG.MIN_NAME_LENGTH, GUILD_CONFIG.MAX_NAME_LENGTH))
        return false
    end
    
    local query = db.storeQuery("SELECT id FROM guilds WHERE name = " .. db.escapeString(guildName) .. GuildUtils.worldWhere())
    if query then
        result.free(query)
        GuildUtils.sendMessage(player, "A guild with this name already exists.")
        return false
    end
    
    local playerId = player:getGuid()
    
    -- Verify player exists in database
    query = db.storeQuery("SELECT id FROM players WHERE id = " .. playerId .. GuildUtils.worldWhere())
    if not query then
        GuildUtils.sendMessage(player, "Error verifying your character in the database.")
        return false
    end
    result.free(query)
    
    -- Create guild in database
    if GuildUtils.isMultiWorld() then
        db.query(string.format("INSERT INTO guilds (name, ownerid, creationdata, world_id) VALUES (%s, %d, %d, %d)",
            db.escapeString(guildName), playerId, os.time(), GuildUtils.worldId()))
    else
        db.query(string.format("INSERT INTO guilds (name, ownerid, creationdata) VALUES (%s, %d, %d)",
            db.escapeString(guildName), playerId, os.time()))
    end
    
    -- Get newly created guild ID
    local guildId = 0
    query = db.storeQuery("SELECT id FROM guilds WHERE name = " .. db.escapeString(guildName) .. GuildUtils.worldWhere())
    if query then
        guildId = result.getNumber(query, "id")
        result.free(query)
    else
        GuildUtils.sendMessage(player, "Error creating guild. Please try again.")
        return false
    end
    
    -- Create default ranks
    for _, rank in ipairs(GUILD_CONFIG.DEFAULT_RANKS) do
        db.query(string.format("INSERT INTO guild_ranks (guild_id, name, level) VALUES (%d, %s, %d)",
            guildId, db.escapeString(rank.name), rank.level))
    end
    
    -- Get leader rank ID
    local leaderRankId = 0
    query = db.storeQuery("SELECT id FROM guild_ranks WHERE guild_id = " .. guildId .. " AND level = 3")
    if query then
        leaderRankId = result.getNumber(query, "id")
        result.free(query)
    else
        GuildUtils.sendMessage(player, "Error creating guild ranks. Please try again.")
        return false
    end
    
    -- Add player as guild leader
    db.query(string.format("INSERT INTO guild_membership (player_id, guild_id, rank_id) VALUES (%d, %d, %d)",
        playerId, guildId, leaderRankId))
    
    GuildUtils.sendMessage(player, string.format("Guild '%s' successfully created. You are now the Leader.", guildName))
    return false
end

createGuild:separator(" ")
createGuild:register()

-- Disband Guild Command
local disbandGuild = TalkAction("/disbandguild")

function disbandGuild.onSay(player, words, param, type)
    local hasPermission, guild = GuildUtils.checkPermission(player, 3) -- Leader only
    if not hasPermission then
        return false
    end
    
    local guildId = guild:getId()
    local guildName = guild:getName()
    
    -- Delete guild invitations
    db.query("DELETE FROM guild_invitations WHERE guild_id = " .. guildId)
    
    -- Delete guild memberships
    db.query("DELETE FROM guild_membership WHERE guild_id = " .. guildId)
    
    -- Delete guild ranks
    db.query("DELETE FROM guild_ranks WHERE guild_id = " .. guildId)
    
    -- Delete guild
    db.query("DELETE FROM guilds WHERE id = " .. guildId)
    
    GuildUtils.sendMessage(player, string.format("Guild '%s' has been disbanded.", guildName))
    return false
end

disbandGuild:separator(" ")
disbandGuild:register()

-- Invite Member Command - Guild Manager Side
local inviteMember = TalkAction("/guildinvite")

function inviteMember.onSay(player, words, param, type)
    if not param or param == "" then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "/guildinvite <player name>")
        return false
    end
    
    local hasPermission, guild = GuildUtils.checkPermission(player, 2) -- Vice-Leader or higher
    if not hasPermission then
        return false
    end
    
    local targetPlayer = Player(param)
    if not targetPlayer then
        GuildUtils.sendMessage(player, string.format("Player '%s' is not online.", param))
        return false
    end
    
    if targetPlayer:getGuild() then
        GuildUtils.sendMessage(player, "This player already belongs to a guild.")
        return false
    end
    
    local targetId = targetPlayer:getGuid()
    
    -- Check if player already has an invitation
    if GuildUtils.hasActiveInvitation(targetId) then
        GuildUtils.sendMessage(player, "This player already has a pending guild invitation.")
        return false
    end
    
    -- Create invitation
    local expirationTime = os.time() + GUILD_CONFIG.INVITATION_EXPIRE_TIME
    db.query(string.format("INSERT INTO guild_invitations (player_id, guild_id, expiration) VALUES (%d, %d, %d)",
        targetId, guild:getId(), expirationTime))
    
    GuildUtils.sendMessage(player, string.format("Invitation sent to '%s'.", param))
    GuildUtils.sendMessage(targetPlayer, string.format("You have been invited to join guild '%s'. Use !guild accept to join or !guild decline to refuse.", guild:getName()))
    return false
end

inviteMember:separator(" ")
inviteMember:register()

-- Guild Command for Players to Accept/Decline Invitations
local guildCommand = TalkAction("!guild")

function guildCommand.onSay(player, words, param, type)
    if not param or param == "" then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "!guild accept/decline")
        return false
    end
    
    local action = param:lower()
    local playerId = player:getGuid()
    
    if player:getGuild() then
        GuildUtils.sendMessage(player, "You are already a member of a guild.")
        return false
    end
    
    -- Check if player has an invitation
    local invitation = GuildUtils.getActiveInvitation(playerId)
    if not invitation then
        GuildUtils.sendMessage(player, "You don't have any pending guild invitations.")
        return false
    end
    
    if action == "accept" then
        -- Get guild name
        local guildName = "Unknown"
        local query = db.storeQuery("SELECT name FROM guilds WHERE id = " .. invitation.guildId)
        if query then
            guildName = result.getString(query, "name")
            result.free(query)
        end
        
        -- Get default member rank
        local rankId = 0
        query = db.storeQuery("SELECT id FROM guild_ranks WHERE guild_id = " .. invitation.guildId .. " AND level = 1")
        if query then
            rankId = result.getNumber(query, "id")
            result.free(query)
        else
            GuildUtils.sendMessage(player, "Error obtaining default rank. Please try again.")
            return false
        end
        
        -- Add player to guild
        db.query(string.format("INSERT INTO guild_membership (player_id, guild_id, rank_id) VALUES (%d, %d, %d)",
            playerId, invitation.guildId, rankId))
        
        -- Remove invitation
        db.query("DELETE FROM guild_invitations WHERE id = " .. invitation.id)
        
        GuildUtils.sendMessage(player, string.format("You have joined guild '%s'.", guildName))
        
        -- Notify online guild members
        local guild = player:getGuild()
        if guild then
            -- Get all online guild members
            local query = db.storeQuery(string.format([[
                SELECT p.id 
                FROM players p 
                INNER JOIN guild_membership gm ON p.id = gm.player_id 
                WHERE gm.guild_id = %d AND p.id != %d
            ]], guild:getId(), playerId))
            
            if query then
                repeat
                    local memberId = result.getNumber(query, "id")
                    local memberPlayer = Player(memberId)
                    if memberPlayer then
                        GuildUtils.sendMessage(memberPlayer, string.format("%s has joined the guild.", player:getName()))
                    end
                until not result.next(query)
                result.free(query)
            end
        end
        
    elseif action == "decline" then
        -- Get guild name
        local guildName = "Unknown"
        local query = db.storeQuery("SELECT name FROM guilds WHERE id = " .. invitation.guildId)
        if query then
            guildName = result.getString(query, "name")
            result.free(query)
        end
        
        -- Remove invitation
        db.query("DELETE FROM guild_invitations WHERE id = " .. invitation.id)
        
        GuildUtils.sendMessage(player, string.format("You have declined the invitation to join guild '%s'.", guildName))
    else
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "!guild accept/decline")
    end
    
    return false
end

guildCommand:separator(" ")
guildCommand:register()

-- Leave Guild Command
local leaveGuild = TalkAction("/guildleave")

function leaveGuild.onSay(player, words, param, type)
    local guild = player:getGuild()
    if not guild then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.NOT_IN_GUILD)
        return false
    end
    
    -- Check if player is guild leader
    if player:getGuildLevel() == 3 then
        GuildUtils.sendMessage(player, "Guild leaders cannot leave their guild. Use /disbandguild or /transferleadership instead.")
        return false
    end
    
    local guildName = guild:getName()
    local playerName = player:getName()
    
    -- Remove player from guild
    db.query("DELETE FROM guild_membership WHERE player_id = " .. player:getGuid())
    
    GuildUtils.sendMessage(player, string.format("You have left guild '%s'.", guildName))
    
    -- Notify online guild members
    local query = db.storeQuery(string.format([[
        SELECT p.id 
        FROM players p 
        INNER JOIN guild_membership gm ON p.id = gm.player_id 
        WHERE gm.guild_id = %d
    ]], guild:getId()))
    
    if query then
        repeat
            local memberId = result.getNumber(query, "id")
            local memberPlayer = Player(memberId)
            if memberPlayer then
                GuildUtils.sendMessage(memberPlayer, string.format("%s has left the guild.", playerName))
            end
        until not result.next(query)
        result.free(query)
    end
    
    return false
end

leaveGuild:separator(" ")
leaveGuild:register()

-- Remove Member Command
local removeMember = TalkAction("/guildkick")

function removeMember.onSay(player, words, param, type)
    if not param or param == "" then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "/guildkick <player name>")
        return false
    end
    
    local hasPermission, guild = GuildUtils.checkPermission(player, 2) -- Vice-Leader or higher
    if not hasPermission then
        return false
    end
    
    local targetName = param
    
    -- Check if target player belongs to the same guild
    local query = db.storeQuery(string.format([[
        SELECT p.id, gr.level 
        FROM players p 
        INNER JOIN guild_membership gm ON p.id = gm.player_id 
        INNER JOIN guild_ranks gr ON gm.rank_id = gr.id 
        WHERE p.name = %s AND gm.guild_id = %d
    ]], db.escapeString(targetName), guild:getId()))
    
    if not query then
        GuildUtils.sendMessage(player, string.format("Player '%s' is not a member of your guild.", targetName))
        return false
    end
    
    local targetId = result.getNumber(query, "id")
    local targetLevel = result.getNumber(query, "level")
    result.free(query)
    
    -- Check hierarchy
    if targetLevel >= player:getGuildLevel() then
        GuildUtils.sendMessage(player, "You cannot remove a member with equal or higher rank than yours.")
        return false
    end
    
    -- Remove player from guild
    db.query("DELETE FROM guild_membership WHERE player_id = " .. targetId)
    
    GuildUtils.sendMessage(player, string.format("Player '%s' has been removed from the guild.", targetName))
    
    local targetPlayer = Player(targetName)
    if targetPlayer then
        GuildUtils.sendMessage(targetPlayer, string.format("You have been removed from guild '%s'.", guild:getName()))
    end
    
    return false
end

removeMember:separator(" ")
removeMember:register()

-- Demote Member Command
local demoteMember = TalkAction("/guilddemote")

function demoteMember.onSay(player, words, param, type)
    if not param or param == "" then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "/guilddemote <player name>")
        return false
    end
    
    local hasPermission, guild = GuildUtils.checkPermission(player, 3) -- Leader only
    if not hasPermission then
        return false
    end
    
    local targetName = param
    
    -- Check if target player belongs to the same guild
    local query = db.storeQuery(string.format([[
        SELECT p.id, gm.rank_id, gr.level 
        FROM players p 
        INNER JOIN guild_membership gm ON p.id = gm.player_id 
        INNER JOIN guild_ranks gr ON gm.rank_id = gr.id 
        WHERE p.name = %s AND gm.guild_id = %d
    ]], db.escapeString(targetName), guild:getId()))
    
    if not query then
        GuildUtils.sendMessage(player, string.format("Player '%s' is not a member of your guild.", targetName))
        return false
    end
    
    local targetId = result.getNumber(query, "id")
    local currentLevel = result.getNumber(query, "level")
    result.free(query)
    
    if currentLevel == 1 then
        GuildUtils.sendMessage(player, "This player already has the lowest rank in the guild.")
        return false
    end
    
    -- Get new rank
    local newLevel = currentLevel - 1
    local newRankId = 0
    
    query = db.storeQuery("SELECT id FROM guild_ranks WHERE guild_id = " .. guild:getId() .. " AND level = " .. newLevel)
    if query then
        newRankId = result.getNumber(query, "id")
        result.free(query)
    else
        GuildUtils.sendMessage(player, "Error obtaining lower rank. Please try again.")
        return false
    end
    
    -- Update player's rank
    db.query("UPDATE guild_membership SET rank_id = " .. newRankId .. " WHERE player_id = " .. targetId)
    
    GuildUtils.sendMessage(player, string.format("Player '%s' has been demoted to %s.", 
        targetName, GuildUtils.formatRank(newLevel)))
    
    local targetPlayer = Player(targetName)
    if targetPlayer then
        GuildUtils.sendMessage(targetPlayer, string.format("You have been demoted to %s in guild '%s'.", 
            GuildUtils.formatRank(newLevel), guild:getName()))
    end
    
    return false
end

demoteMember:separator(" ")
demoteMember:register()

-- Promote Member Command
local promoteMember = TalkAction("/guildpromote")

function promoteMember.onSay(player, words, param, type)
    if not param or param == "" then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "/guildpromote <player name>")
        return false
    end
    
    local hasPermission, guild = GuildUtils.checkPermission(player, 3) -- Leader only
    if not hasPermission then
        return false
    end
    
    local targetName = param
    
    -- Check if target player belongs to the same guild
    local query = db.storeQuery(string.format([[
        SELECT p.id, gm.rank_id, gr.level 
        FROM players p 
        INNER JOIN guild_membership gm ON p.id = gm.player_id 
        INNER JOIN guild_ranks gr ON gm.rank_id = gr.id 
        WHERE p.name = %s AND gm.guild_id = %d
    ]], db.escapeString(targetName), guild:getId()))
    
    if not query then
        GuildUtils.sendMessage(player, string.format("Player '%s' is not a member of your guild.", targetName))
        return false
    end
    
    local targetId = result.getNumber(query, "id")
    local currentLevel = result.getNumber(query, "level")
    result.free(query)
    
    if currentLevel == 3 then
        GuildUtils.sendMessage(player, "This player already has the highest rank in the guild.")
        return false
    end
    
    if currentLevel == 2 then
        GuildUtils.sendMessage(player, "Use /transferleadership to make this member the leader.")
        return false
    end
    
    -- Get new rank
    local newLevel = currentLevel + 1
    local newRankId = 0
    
    query = db.storeQuery("SELECT id FROM guild_ranks WHERE guild_id = " .. guild:getId() .. " AND level = " .. newLevel)
    if query then
        newRankId = result.getNumber(query, "id")
        result.free(query)
    else
        GuildUtils.sendMessage(player, "Error obtaining higher rank. Please try again.")
        return false
    end
    
    -- Update player's rank
    db.query("UPDATE guild_membership SET rank_id = " .. newRankId .. " WHERE player_id = " .. targetId)
    
    GuildUtils.sendMessage(player, string.format("Player '%s' has been promoted to %s.", 
        targetName, GuildUtils.formatRank(newLevel)))
    
    local targetPlayer = Player(targetName)
    if targetPlayer then
        GuildUtils.sendMessage(targetPlayer, string.format("You have been promoted to %s in guild '%s'.", 
            GuildUtils.formatRank(newLevel), guild:getName()))
    end
    
    return false
end

promoteMember:separator(" ")
promoteMember:register()

-- Transfer Leadership Command
local transferLeadership = TalkAction("/transferleadership")

function transferLeadership.onSay(player, words, param, type)
    if not param or param == "" then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "/transferleadership <player name>")
        return false
    end
    
    local hasPermission, guild = GuildUtils.checkPermission(player, 3) -- Leader only
    if not hasPermission then
        return false
    end
    
    local targetName = param
    
    -- Check if target player belongs to the same guild
    local query = db.storeQuery(string.format([[
        SELECT p.id, gm.rank_id 
        FROM players p 
        INNER JOIN guild_membership gm ON p.id = gm.player_id 
        WHERE p.name = %s AND gm.guild_id = %d
    ]], db.escapeString(targetName), guild:getId()))
    
    if not query then
        GuildUtils.sendMessage(player, string.format("Player '%s' is not a member of your guild.", targetName))
        return false
    end
    
    local targetId = result.getNumber(query, "id")
    result.free(query)
    
    -- Get rank IDs
    local leaderRankId = 0
    local viceLeaderRankId = 0
    
    query = db.storeQuery("SELECT id FROM guild_ranks WHERE guild_id = " .. guild:getId() .. " AND level = 3")
    if query then
        leaderRankId = result.getNumber(query, "id")
        result.free(query)
    else
        GuildUtils.sendMessage(player, "Error obtaining leader rank. Please try again.")
        return false
    end
    
    query = db.storeQuery("SELECT id FROM guild_ranks WHERE guild_id = " .. guild:getId() .. " AND level = 2")
    if query then
        viceLeaderRankId = result.getNumber(query, "id")
        result.free(query)
    else
        GuildUtils.sendMessage(player, "Error obtaining vice-leader rank. Please try again.")
        return false
    end
    
    -- Update leadership
    db.query("UPDATE guild_membership SET rank_id = " .. viceLeaderRankId .. " WHERE player_id = " .. player:getGuid())
    db.query("UPDATE guild_membership SET rank_id = " .. leaderRankId .. " WHERE player_id = " .. targetId)
    db.query("UPDATE guilds SET ownerid = " .. targetId .. " WHERE id = " .. guild:getId())
    
    GuildUtils.sendMessage(player, string.format("Guild leadership transferred to '%s'.", targetName))
    
    local targetPlayer = Player(targetName)
    if targetPlayer then
        GuildUtils.sendMessage(targetPlayer, string.format("You are now the leader of guild '%s'.", guild:getName()))
    end
    
    return false
end

transferLeadership:separator(" ")
transferLeadership:register()

-- Set Nickname Command
local setNickname = TalkAction("/guildnick")

function setNickname.onSay(player, words, param, type)
    local params = param:split(",")
    if #params ~= 2 then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "/guildnick <player name>,<nickname>")
        return false
    end
    
    local hasPermission, guild = GuildUtils.checkPermission(player, 2) -- Vice-Leader or higher
    if not hasPermission then
        return false
    end
    
    local targetName = params[1]:trim()
    local nickname = params[2]:trim()
    
    -- Check if target player belongs to the same guild
    local targetId = GuildUtils.getTargetId(targetName, guild:getId())
    if not targetId then
        GuildUtils.sendMessage(player, string.format("Player '%s' is not a member of your guild.", targetName))
        return false
    end
    
    -- Update nickname
    db.query("UPDATE guild_membership SET nick = " .. db.escapeString(nickname) .. " WHERE player_id = " .. targetId)
    
    GuildUtils.sendMessage(player, string.format("Nickname for '%s' set to '%s'.", targetName, nickname))
    
    local targetPlayer = Player(targetName)
    if targetPlayer then
        GuildUtils.sendMessage(targetPlayer, string.format("Your guild nickname has been set to '%s'.", nickname))
    end
    
    return false
end

setNickname:separator(" ")
setNickname:register()

-- Rename Rank Command
local renameRank = TalkAction("/renamerank")

function renameRank.onSay(player, words, param, type)
    local params = param:split(",")
    if #params ~= 2 then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "/renamerank <old name>,<new name>")
        return false
    end
    
    local hasPermission, guild = GuildUtils.checkPermission(player, 3) -- Leader only
    if not hasPermission then
        return false
    end
    
    local oldName = params[1]:trim()
    local newName = params[2]:trim()
    
    -- Check if rank exists
    local query = db.storeQuery("SELECT id FROM guild_ranks WHERE guild_id = " .. guild:getId() .. 
        " AND name = " .. db.escapeString(oldName))
    
    if not query then
        GuildUtils.sendMessage(player, string.format("Rank '%s' does not exist in your guild.", oldName))
        return false
    end
    
    local rankId = result.getNumber(query, "id")
    result.free(query)
    
    -- Update rank name
    db.query("UPDATE guild_ranks SET name = " .. db.escapeString(newName) .. " WHERE id = " .. rankId)
    
    GuildUtils.sendMessage(player, string.format("Rank '%s' renamed to '%s'.", oldName, newName))
    return false
end

renameRank:separator(" ")
renameRank:register()

-- Set Message of the Day Command
local setMotd = TalkAction("/guildmotd")

function setMotd.onSay(player, words, param, type)
    if not param or param == "" then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "/guildmotd <message>")
        return false
    end
    
    local hasPermission, guild = GuildUtils.checkPermission(player, 2) -- Vice-Leader or higher
    if not hasPermission then
        return false
    end
    
    -- Update message of the day
    db.query("UPDATE guilds SET motd = " .. db.escapeString(param) .. " WHERE id = " .. guild:getId())
    
    GuildUtils.sendMessage(player, "Guild message of the day updated.")
    return false
end

setMotd:separator(" ")
setMotd:register()

-- Clear Message of the Day Command
local clearMotd = TalkAction("/clearmotd")

function clearMotd.onSay(player, words, param, type)
    local hasPermission, guild = GuildUtils.checkPermission(player, 2) -- Vice-Leader or higher
    if not hasPermission then
        return false
    end
    
    -- Clear message of the day
    db.query("UPDATE guilds SET motd = '' WHERE id = " .. guild:getId())
    
    GuildUtils.sendMessage(player, "Guild message of the day cleared.")
    return false
end

clearMotd:separator(" ")
clearMotd:register()

-- Guild Information Command with popup
local guildInfo = TalkAction("/guildinfo")

function guildInfo.onSay(player, words, param, type)
    local playerId = player:getGuid()
    
    -- Check if player belongs to a guild
    local query = db.storeQuery(string.format([[
        SELECT 
            g.id AS guild_id, 
            g.name AS guild_name, 
            gr.name AS rank_name, 
            gr.level AS rank_level, 
            p2.name AS leader_name, 
            gm.nick AS nick, 
            g.motd AS motd, 
            (SELECT COUNT(*) FROM guild_membership WHERE guild_id = g.id) AS total_members 
        FROM players p 
        JOIN guild_membership gm ON p.id = gm.player_id 
        JOIN guilds g ON gm.guild_id = g.id 
        JOIN guild_ranks gr ON gm.rank_id = gr.id 
        JOIN players p2 ON g.ownerid = p2.id 
        WHERE p.id = %d
    ]], playerId))
    
    if not query then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.NOT_IN_GUILD)
        return false
    end
    
    -- Retrieve guild information
    local guildId = result.getNumber(query, "guild_id")
    local guildName = result.getString(query, "guild_name")
    local rankName = result.getString(query, "rank_name")
    local rankLevel = result.getNumber(query, "rank_level")
    local leaderName = result.getString(query, "leader_name")
    local nick = result.getString(query, "nick") or ""
    local motd = result.getString(query, "motd") or ""
    local totalMembers = result.getNumber(query, "total_members")
    
    result.free(query)
    
    -- Display information to player in a popup
    local infoText = "=== Guild Information ===\n\n"
    infoText = infoText .. "Name: " .. guildName .. "\n"
    infoText = infoText .. "Your Rank: " .. rankName .. "\n"
    infoText = infoText .. "Your Nickname: " .. (nick ~= "" and nick or "None") .. "\n"
    infoText = infoText .. "Leader: " .. leaderName .. "\n"
    infoText = infoText .. "Total Members: " .. totalMembers .. "\n"
    
    if motd ~= "" then
        infoText = infoText .. "\nMessage of the Day: " .. motd .. "\n"
    end
    
    -- Add available commands based on rank
    infoText = infoText .. "\n=== Available Commands ===\n"
    
    if rankLevel >= 2 then -- Vice-Leader or higher
        if rankLevel == 3 then -- Leader only
            infoText = infoText .. "/disbandguild - Dissolve the guild\n"
            infoText = infoText .. "/transferleadership <name> - Transfer leadership\n"
            infoText = infoText .. "/guildpromote <name> - Promote a member\n"
            infoText = infoText .. "/guilddemote <name> - Demote a member\n"
            infoText = infoText .. "/renamerank <old>,<new> - Rename rank\n"
        end
        
        -- Commands for Vice-Leader and Leader
        infoText = infoText .. "/guildinvite <name> - Invite a player\n"
        infoText = infoText .. "/guildkick <name> - Remove a member\n"
        infoText = infoText .. "/guildnick <name>,<nick> - Set nickname\n"
        infoText = infoText .. "/guildmotd <message> - Set message of the day\n"
        infoText = infoText .. "/clearmotd - Clear message of the day\n"
    else
        -- Commands for regular Member
        infoText = infoText .. "/guildleave - Leave the guild\n"
    end
    
    -- Command to list all members
    infoText = infoText .. "/guildmembers - List all guild members"
    
    GuildUtils.sendMessage(player, infoText, nil, true) -- Use popup
    return false
end

guildInfo:separator(" ")
guildInfo:register()

-- List Guild Members Command with popup
local listMembers = TalkAction("/guildmembers")

function listMembers.onSay(player, words, param, type)
    local playerId = player:getGuid()
    
    -- Check if player belongs to a guild
    local query = db.storeQuery("SELECT guild_id FROM guild_membership WHERE player_id = " .. playerId)
    if not query then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.NOT_IN_GUILD)
        return false
    end
    
    local guildId = result.getNumber(query, "guild_id")
    result.free(query)
    
    -- Fetch all guild members
    query = db.storeQuery(string.format([[
        SELECT 
            p.name, 
            gr.name AS rank, 
            gr.level, 
            gm.nick 
        FROM guild_membership gm 
        JOIN players p ON gm.player_id = p.id 
        JOIN guild_ranks gr ON gm.rank_id = gr.id 
        WHERE gm.guild_id = %d 
        ORDER BY gr.level DESC, p.name ASC
    ]], guildId))
    
    if not query then
        GuildUtils.sendMessage(player, "Error fetching guild members.")
        return false
    end
    
    -- Organize members by rank
    local membersByRank = {
        leaders = {},
        viceLeaders = {},
        members = {}
    }
    
    repeat
        local memberName = result.getString(query, "name")
        local rankName = result.getString(query, "rank")
        local rankLevel = result.getNumber(query, "level")
        local nick = result.getString(query, "nick")
        
        local displayName = memberName
        if nick and nick ~= "" then
            displayName = displayName .. " (" .. nick .. ")"
        end
        
        if rankLevel == 3 then
            table.insert(membersByRank.leaders, {name = displayName, rank = rankName})
        elseif rankLevel == 2 then
            table.insert(membersByRank.viceLeaders, {name = displayName, rank = rankName})
        else
            table.insert(membersByRank.members, {name = displayName, rank = rankName})
        end
    until not result.next(query)
    
    result.free(query)
    
    -- Build display text for popup
    local membersText = "=== Guild Members ===\n\n"
    
    if #membersByRank.leaders > 0 then
        membersText = membersText .. "Leaders:\n"
        for _, member in ipairs(membersByRank.leaders) do
            membersText = membersText .. "  - " .. member.name .. "\n"
        end
        membersText = membersText .. "\n"
    end
    
    if #membersByRank.viceLeaders > 0 then
        membersText = membersText .. "Vice-Leaders:\n"
        for _, member in ipairs(membersByRank.viceLeaders) do
            membersText = membersText .. "  - " .. member.name .. "\n"
        end
        membersText = membersText .. "\n"
    end
    
    if #membersByRank.members > 0 then
        membersText = membersText .. "Members:\n"
        for _, member in ipairs(membersByRank.members) do
            membersText = membersText .. "  - " .. member.name .. "\n"
        end
    end
    
    -- Display the list in a popup
    GuildUtils.sendMessage(player, membersText, nil, true) -- Use popup
    return false
end

listMembers:separator(" ")
listMembers:register()

-- Guild Status Command (simplified version of /guildinfo)
local guildStatus = TalkAction("/guildstatus")

function guildStatus.onSay(player, words, param, type)
    local guild = player:getGuild()
    if not guild then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.NOT_IN_GUILD)
        return false
    end
    
    local guildName = guild:getName()
    local rankName = "Unknown"
    local rankLevel = player:getGuildLevel()
    
    -- Get rank name
    local query = db.storeQuery(string.format([[
        SELECT gr.name 
        FROM guild_ranks gr 
        JOIN guild_membership gm ON gr.id = gm.rank_id 
        WHERE gm.player_id = %d
    ]], player:getGuid()))
    
    if query then
        rankName = result.getString(query, "name")
        result.free(query)
    end
    
    -- Display compact guild information
    GuildUtils.sendMessage(player, string.format("Guild: %s", guildName))
    GuildUtils.sendMessage(player, string.format("Rank: %s", rankName))
    GuildUtils.sendMessage(player, string.format("Use /guildinfo for more details"))
    
    return false
end

guildStatus:separator(" ")
guildStatus:register()

-- Cancel Invitation Command
local cancelInvitation = TalkAction("/guildcancel")

function cancelInvitation.onSay(player, words, param, type)
    if not param or param == "" then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.INVALID_SYNTAX .. "/guildcancel <player name>")
        return false
    end
    
    local hasPermission, guild = GuildUtils.checkPermission(player, 2) -- Vice-Leader or higher
    if not hasPermission then
        return false
    end
    
    local targetName = param
    local targetId = GuildUtils.getPlayerId(targetName)
    
    if not targetId then
        GuildUtils.sendMessage(player, string.format("Player '%s' does not exist.", targetName))
        return false
    end
    
    -- Check if there's an invitation for this player
    local query = db.storeQuery(string.format([[
        SELECT id 
        FROM guild_invitations 
        WHERE player_id = %d AND guild_id = %d
    ]], targetId, guild:getId()))
    
    if not query then
        GuildUtils.sendMessage(player, string.format("There is no pending invitation for player '%s'.", targetName))
        return false
    end
    
    local invitationId = result.getNumber(query, "id")
    result.free(query)
    
    -- Delete invitation
    db.query("DELETE FROM guild_invitations WHERE id = " .. invitationId)
    
    GuildUtils.sendMessage(player, string.format("Guild invitation for '%s' has been canceled.", targetName))
    
    local targetPlayer = Player(targetName)
    if targetPlayer then
        GuildUtils.sendMessage(targetPlayer, string.format("Your invitation to guild '%s' has been canceled.", guild:getName()))
    end
    
    return false
end

cancelInvitation:separator(" ")
cancelInvitation:register()

-- Check Invitations Command
local checkInvitations = TalkAction("/guildinvites")

function checkInvitations.onSay(player, words, param, type)
    local hasPermission, guild = GuildUtils.checkPermission(player, 2) -- Vice-Leader or higher
    if not hasPermission then
        return false
    end
    
    GuildUtils.removeExpiredInvitations()
    
    local query = db.storeQuery(string.format([[
        SELECT 
            gi.id, 
            p.name, 
            gi.expiration 
        FROM guild_invitations gi 
        JOIN players p ON gi.player_id = p.id 
        WHERE gi.guild_id = %d
    ]], guild:getId()))
    
    if not query then
        GuildUtils.sendMessage(player, "There are no pending guild invitations.")
        return false
    end
    
    local invitations = {}
    
    repeat
        local playerName = result.getString(query, "name")
        local expirationTime = result.getNumber(query, "expiration")
        local timeLeft = expirationTime - os.time()
        
        if timeLeft > 0 then
            local hoursLeft = math.floor(timeLeft / 3600)
            local minutesLeft = math.floor((timeLeft % 3600) / 60)
            
            table.insert(invitations, {
                name = playerName,
                timeLeft = string.format("%d hours, %d minutes", hoursLeft, minutesLeft)
            })
        end
    until not result.next(query)
    
    result.free(query)
    
    if #invitations == 0 then
        GuildUtils.sendMessage(player, "There are no pending guild invitations.")
        return false
    end
    
    local inviteText = "=== Pending Guild Invitations ===\n\n"
    
    for _, invitation in ipairs(invitations) do
        inviteText = inviteText .. invitation.name .. " - Expires in: " .. invitation.timeLeft .. "\n"
    end
    
    inviteText = inviteText .. "\nUse /guildcancel <name> to cancel an invitation."
    
    GuildUtils.sendMessage(player, inviteText, nil, true) -- Use popup
    return false
end

checkInvitations:separator(" ")
checkInvitations:register()

-- Online Members Command with popup
local onlineMembers = TalkAction("/guildonline")

function onlineMembers.onSay(player, words, param, type)
    local guild = player:getGuild()
    if not guild then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.NOT_IN_GUILD)
        return false
    end
    
    local guildId = guild:getId()
    
    local onlineCount = 0
    local onlineMembersList = {}
    
    local query = db.storeQuery(string.format([[
        SELECT 
            p.name, 
            gr.name AS rank, 
            gm.nick 
        FROM guild_membership gm 
        JOIN players p ON gm.player_id = p.id 
        JOIN guild_ranks gr ON gm.rank_id = gr.id 
        WHERE gm.guild_id = %d
    ]], guildId))
    
    if query then
        repeat
            local memberName = result.getString(query, "name")
            local rankName = result.getString(query, "rank")
            local nick = result.getString(query, "nick")
            
            local targetPlayer = Player(memberName)
            if targetPlayer then
                onlineCount = onlineCount + 1
                
                local displayName = memberName
                if nick and nick ~= "" then
                    displayName = displayName .. " (" .. nick .. ")"
                end
                
                table.insert(onlineMembersList, {
                    name = displayName,
                    rank = rankName
                })
            end
        until not result.next(query)
        
        result.free(query)
    end
    
    -- Display online guild members in a popup
    local onlineText = string.format("=== Online Guild Members: %d ===\n\n", onlineCount)
    
    if onlineCount > 0 then
        for _, member in ipairs(onlineMembersList) do
            onlineText = onlineText .. member.rank .. ": " .. member.name .. "\n"
        end
    else
        onlineText = onlineText .. "No guild members are currently online."
    end
    
    GuildUtils.sendMessage(player, onlineText, nil, true)
    return false
end

onlineMembers:separator(" ")
onlineMembers:register()

-- Command to check guild statistics with popup
local guildStats = TalkAction("/guildstats")

function guildStats.onSay(player, words, param, type)
    local guild = player:getGuild()
    if not guild then
        GuildUtils.sendMessage(player, GUILD_CONFIG.MESSAGES.NOT_IN_GUILD)
        return false
    end
    
    local guildId = guild:getId()
    local guildName = guild:getName()
    
    local creationDate = "Unknown"
    local query = db.storeQuery("SELECT creationdata FROM guilds WHERE id = " .. guildId)
    if query then
        local timestamp = result.getNumber(query, "creationdata")
        if timestamp and timestamp > 0 then
            creationDate = os.date("%Y-%m-%d %H:%M", timestamp)
        end
        result.free(query)
    end
    
    local memberCount = 0
    query = db.storeQuery("SELECT COUNT(*) as count FROM guild_membership WHERE guild_id = " .. guildId)
    if query then
        memberCount = result.getNumber(query, "count")
        result.free(query)
    end
    
    local statsText = "=== Guild Statistics ===\n\n"
    statsText = statsText .. "Name: " .. guildName .. "\n"
    statsText = statsText .. "Founded: " .. creationDate .. "\n"
    statsText = statsText .. "Total Members: " .. memberCount .. "\n"
    
    statsText = statsText .. "\nUse /guildmembers to see all members"
    
    GuildUtils.sendMessage(player, statsText, nil, true) -- Use popup
    return false
end

guildStats:separator(" ")
guildStats:register()

-- Help command for guild system with popup
local guildHelp = TalkAction("/guildhelp")

function guildHelp.onSay(player, words, param, type)
    local helpText = "=== Guild System Commands ===\n\n"
    
    helpText = helpText .. "General Commands:\n"
    helpText = helpText .. "/createguild \"name\" - Create a new guild\n"
    helpText = helpText .. "!guild accept - Accept a guild invitation\n"
    helpText = helpText .. "!guild decline - Decline a guild invitation\n"
    helpText = helpText .. "/guildinfo - View detailed guild information\n"
    helpText = helpText .. "/guildstatus - View brief guild status\n"
    helpText = helpText .. "/guildmembers - List all guild members\n"
    helpText = helpText .. "/guildonline - View online guild members\n"
    helpText = helpText .. "/guildstats - View guild statistics\n"
    helpText = helpText .. "/guildleave - Leave your current guild\n\n"
    
    helpText = helpText .. "Management Commands (Vice-Leader+):\n"
    helpText = helpText .. "/guildinvite <name> - Invite a player to the guild\n"
    helpText = helpText .. "/guildcancel <name> - Cancel a guild invitation\n"
    helpText = helpText .. "/guildinvites - View pending invitations\n"
    helpText = helpText .. "/guildkick <name> - Remove a member from the guild\n"
    helpText = helpText .. "/guildnick <name>,<nick> - Set a member's nickname\n"
    helpText = helpText .. "/guildmotd <message> - Set message of the day\n"
    helpText = helpText .. "/clearmotd - Clear message of the day\n\n"
    
    helpText = helpText .. "Leadership Commands (Leader only):\n"
    helpText = helpText .. "/guildpromote <name> - Promote a member\n"
    helpText = helpText .. "/guilddemote <name> - Demote a member\n"
    helpText = helpText .. "/renamerank <old>,<new> - Rename a guild rank\n"
    helpText = helpText .. "/transferleadership <name> - Transfer guild leadership\n"
    helpText = helpText .. "/disbandguild - Dissolve the guild"
    
    GuildUtils.sendMessage(player, helpText, nil, true) -- Use popup
    return false
end

guildHelp:separator(" ")
guildHelp:register()
