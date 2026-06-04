PacketHandlers = {}
NetworkGuard = NetworkGuard or {}

local guardCooldowns = NetworkGuard.cooldowns or {}
NetworkGuard.cooldowns = guardCooldowns

function NetworkGuard.remaining(msg)
	if not msg then
		return 0
	end
	return math.max(0, (tonumber(msg:len()) or 0) - (tonumber(msg:tell()) or 0))
end

function NetworkGuard.canRead(msg, bytes)
	bytes = tonumber(bytes) or 0
	return bytes >= 0 and NetworkGuard.remaining(msg) >= bytes
end

function NetworkGuard.readByte(msg)
	if not NetworkGuard.canRead(msg, 1) then
		return nil
	end
	return msg:getByte()
end

function NetworkGuard.readU16(msg)
	if not NetworkGuard.canRead(msg, 2) then
		return nil
	end
	return msg:getU16()
end

function NetworkGuard.readU32(msg)
	if not NetworkGuard.canRead(msg, 4) then
		return nil
	end
	return msg:getU32()
end

function NetworkGuard.readPosition(msg)
	if not NetworkGuard.canRead(msg, 5) then
		return nil
	end
	return msg:getPosition()
end

function NetworkGuard.readString(msg, maxLength)
	if not NetworkGuard.canRead(msg, 2) then
		return nil
	end

	maxLength = tonumber(maxLength) or 8192
	local start = msg:tell()
	local length = msg:getU16()
	msg:seek(start)
	if length > maxLength or not NetworkGuard.canRead(msg, 2 + length) then
		return nil
	end

	local value = msg:getString()
	if not value or #value > maxLength then
		return nil
	end
	return value
end

function NetworkGuard.cooldown(player, key, milliseconds)
	if not player then
		return false
	end

	milliseconds = tonumber(milliseconds) or 0
	if milliseconds <= 0 then
		return true
	end

	local playerId = player:getId()
	local now = os.mtime and os.mtime() or (os.time() * 1000)
	guardCooldowns[playerId] = guardCooldowns[playerId] or {}
	local last = guardCooldowns[playerId][key]
	if last and now - last < milliseconds then
		return false
	end

	guardCooldowns[playerId][key] = now
	return true
end

function NetworkGuard.clearPlayer(player)
	if player then
		guardCooldowns[player:getId()] = nil
	end
end

local function register(self)
	if isScriptsInterface() then
		if not self.onReceive then
			debugPrint(
				"[Warning - PacketHandler::register] need to setup a callback before you can register.")
			return
		end

		if type(self.onReceive) ~= "function" then
			debugPrint(string.format(
				           "[Warning - PacketHandler::onReceive] a function is expected."))
			return
		end

		PacketHandlers[self.packetType] = self.onReceive
	end
end

local function clear(self) PacketHandlers[self.packetType] = nil end

function PacketHandler(packetType)
	return {clear = clear, packetType = packetType, register = register}
end
