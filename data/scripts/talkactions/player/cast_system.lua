local Storage = PlayerStorageKeys

function dump(o)
   	if type(o) == 'table' then
      	local s = '{ '
      	for k,v in pairs(o) do
        	if type(k) ~= 'number' then k = '"'..k..'"' end
         	s = s .. '['..k..'] = ' .. dump(v) .. ','
      	end
      	return s .. '} '
   	else
     	return tostring(o)
   	end
end

function sendHelp(player) 
    player:popupFYI("Commands of Casting:\n!cast on - enable the gameplay \n!cast off - disable the gameplay\n!cast password <password> - sets a password on the stream\n!cast password off - disable the password\n!cast list - displays the amount and nicknames of current spectators\n!cast mute <name> - mutes selected spectator from chat\n!cast unmute <name> - removes muted\n!cast ban - shows banished spectators list\n!cast unban <name> - removes banishment lock\n!cast kick <name> - kick a spectator from your stream")
end

function findIP(spectators, name)
	for k, v in pairs(spectators) do
		if k:lower() == name:lower() then
			return v
		end
	end
	return nil
end

function removeKey(t, k_to_remove)
  	local new = {}
  	for k, v in pairs(t) do
		if k:lower() ~= k_to_remove:lower() then
			new[k] = v
		end
  	end
  	return new
end

local talkAction = TalkAction("!cast")

function talkAction.onSay(player, words, param)

    local data = player:getSpectators()
	if param == "" or param == nil then
		sendHelp(player)
		return false
	end
	
	local split = param:split(" ")
	local action = split[1]:lower()
	table.remove(split, 1)
	local target = table.concat(split, " ")
	
	
	if action == "on" and player:getStorageValue(Storage.isCasting) == 1 then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "You already have the cast activated.")
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "You already have the cast activated.")
		return false
	end
	

	if action == "on" then
		data['broadcast'] = true
		--data['password'] = target
		player:setSpectators(data)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "You have started casting your gameplay, now you have 10% Exp Bonus.")
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "You have started casting your gameplay, now you have 10% Exp Bonus.")
		player:setStorageValue(Storage.isCasting, 1)
		return false
	elseif action == "password" then
		if player:getStorageValue(Storage.isCasting) == 1 then
			data['password'] = target
			player:setSpectators(data)
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "You have set new password for your stream, but instead you lost 10% exp bonus.")
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You have set new password for your stream, but instead you lost 10% exp bonus.")
			player:setStorageValue(Storage.isCastingPassword, 1)
		else
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "You do not have the cast activated.")
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have the cast activated.")
		end
		return false
	elseif action == "passwordoff" and player:getStorageValue(Storage.isCastingPassword) == 1 then
			data['password'] = target
			player:setSpectators(data)
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "You have removed password for your stream.")
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You have removed password for your stream.")
			player:setStorageValue(Storage.isCastingPassword, -1)
		return false
	elseif action == "off" then
		if player:getStorageValue(Storage.isCasting) == 1 then
			data['broadcast'] = false
			player:setSpectators(data)
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "You have stopped casting your gameplay.")
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You have stopped casting your gameplay.")
			player:setStorageValue(Storage.isCasting, -1)
		else
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "You do not have the cast activated.")
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have the cast activated.")
		end
		return false
	elseif action == "list" or action == "show" then
		local spectators_list = ""
		if player:getStorageValue(Storage.isCasting) == 1 then
			for k, v in pairs(data['spectators']) do
				spectators_list = spectators_list .. k .. ", "
			end
			player:sendCastChannelMessage('', 'Spectators: ' .. spectators_list, 8)
		else
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "You do not have the cast activated.")
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You do not have the cast activated.")
		end
		return false
	elseif action == "kick" then
		local ip = findIP(data['spectators'], target)
		if ip == nil then
			player:sendCastChannelMessage('', 'There is no spectator: ' .. target, 8)		
		else
			player:sendCastChannelMessage('', 'Spectator ' .. target .. ' has been kicked', 8)		
			data['kicks'][target] = ip
		end
		player:setSpectators(data)
		return false
	elseif action == "mute" then
		local ip = findIP(data['spectators'], target)
		if ip == nil then
			player:sendCastChannelMessage('', 'There is no spectator: ' .. target, 8)		
		else
			player:sendCastChannelMessage('', 'Spectator ' .. target .. ' has been muted', 8)		
			data['mutes'][target] = ip
		end
		player:setSpectators(data)
		return false
	elseif action == "ban" then
		local ip = findIP(data['spectators'], target)
		if ip == nil then
			player:sendCastChannelMessage('', 'There is no spectator: ' .. target, 8)		
		else
			player:sendCastChannelMessage('', 'Spectator ' .. target .. ' has been banned', 8)		
			data['bans'][target] = ip
		end
		player:setSpectators(data)
		return false
	elseif action == "unmute" then
		local ip = findIP(data['mutes'], target)
		if ip == nil then
			player:sendCastChannelMessage('', 'There is no spectator: ' .. target, 8)		
		else
			player:sendCastChannelMessage('', 'Spectator ' .. target .. ' has been unmuted', 8)		
			data['mutes'] = removeKey(data['mutes'], target)
		end
		player:setSpectators(data)
		return false
	elseif action == "unban" then
		local ip = findIP(data['bans'], target)
		if ip == nil then
			player:sendCastChannelMessage('', 'There is no spectator: ' .. target, 8)		
		else
			player:sendCastChannelMessage('', 'Spectator ' .. target .. ' has been unbanned', 8)		
			data['bans'] = removeKey(data['bans'], target)
		end
		player:setSpectators(data)
		return false
	end
	sendHelp(player)
end

talkAction:separator(" ")
talkAction:register()

local cast_login = CreatureEvent("cast_login")
function cast_login.onLogin(player)
	player:setStorageValue(Storage.isCasting, -1)
	player:setStorageValue(Storage.isCastingPassword, -1)
	return true
end

cast_login:register()

local event = Event()
event.onGainExperience = function(self, source, exp, rawExp)

	local cast = 0
	if self:getStorageValue(Storage.isCasting) == 1 and self:getStorageValue(Storage.isCastingPassword) == -1 then
		cast = exp * 0.10 -- 10% Exp
	end
	
	return exp + cast
end

event:register()