local mt = rawgetmetatable("Position")

local function mergeInstanceId(lhs, rhs)
	if lhs.instanceId and lhs.instanceId ~= 0 then
		return lhs.instanceId
	end
	if rhs.instanceId and rhs.instanceId ~= 0 then
		return rhs.instanceId
	end
	return 0
end

---@param lhs Position
---@param rhs Position
function mt.__add(lhs, rhs)
	local stackpos = lhs.stackpos or rhs.stackpos
	return Position(lhs.x + (rhs.x or 0), lhs.y + (rhs.y or 0), lhs.z + (rhs.z or 0),
	                stackpos, mergeInstanceId(lhs, rhs))
end

---@param lhs Position
---@param rhs Position
function mt.__sub(lhs, rhs)
	local stackpos = lhs.stackpos or rhs.stackpos
	return Position(lhs.x - (rhs.x or 0), lhs.y - (rhs.y or 0), lhs.z - (rhs.z or 0),
	                stackpos, mergeInstanceId(lhs, rhs))
end

---@param lhs Position
---@param rhs Position
function mt.__concat(lhs, rhs) return tostring(lhs) .. tostring(rhs) end

---@param lhs Position
---@param rhs Position
function mt.__eq(lhs, rhs) return lhs.x == rhs.x and lhs.y == rhs.y and lhs.z == rhs.z end

---@param self Position
function mt.__tostring(self)
	return string.format("Position(%d, %d, %d)", self.x, self.y, self.z)
end

Position.directionOffset = {
	[DIRECTION_NORTH] = {x = 0, y = -1},
	[DIRECTION_EAST] = {x = 1, y = 0},
	[DIRECTION_SOUTH] = {x = 0, y = 1},
	[DIRECTION_WEST] = {x = -1, y = 0},
	[DIRECTION_SOUTHWEST] = {x = -1, y = 1},
	[DIRECTION_SOUTHEAST] = {x = 1, y = 1},
	[DIRECTION_NORTHWEST] = {x = -1, y = -1},
	[DIRECTION_NORTHEAST] = {x = 1, y = -1}
}

local abs, max = math.abs, math.max
function Position:getDistance(positionEx)
	local dx = abs(self.x - positionEx.x)
	local dy = abs(self.y - positionEx.y)
	local dz = abs(self.z - positionEx.z)
	return max(dx, dy, dz)
end

function Position:getTile() return Tile(self) end

--- This method modifies the position and returns self
function Position:getNextPosition(direction, steps)
	local offset = Position.directionOffset[direction]
	if offset then
		steps = steps or 1
		self.x = self.x + offset.x * steps
		self.y = self.y + offset.y * steps
	end
	return self
end

do
	local swap = function(lhs, rhs)
		lhs.x, rhs.x = rhs.x, lhs.x
		lhs.y, rhs.y = rhs.y, lhs.y
		lhs.z, rhs.z = rhs.z, lhs.z
	end

	function Position:moveUpstairs()
		self.z = self.z - 1

		local defaultPosition = self + Position.directionOffset[DIRECTION_SOUTH]
		local toTile = Tile(defaultPosition)
		if not toTile or not toTile:isWalkable() then
			local currentDirection
			for direction = DIRECTION_NORTH, DIRECTION_NORTHEAST do
				currentDirection = direction
				if currentDirection == DIRECTION_SOUTH then
					currentDirection = DIRECTION_WEST
				end

				local position = self + Position.directionOffset[currentDirection]
				toTile = Tile(position)
				if toTile and toTile:isWalkable() then
					swap(self, position)
					return self
				end
			end
		end
		swap(self, defaultPosition)
		return self
	end
end

function Position:isInRange(from, to)
	-- No matter what corner from and to is, we want to make
	-- life easier by calculating north-west and south-east
	local zone = {
		nW = {
			x = (from.x < to.x and from.x or to.x),
			y = (from.y < to.y and from.y or to.y),
			z = (from.z < to.z and from.z or to.z)
		},
		sE = {
			x = (to.x > from.x and to.x or from.x),
			y = (to.y > from.y and to.y or from.y),
			z = (to.z > from.z and to.z or from.z)
		}
	}

	if self.x >= zone.nW.x and self.x <= zone.sE.x and self.y >= zone.nW.y and self.y <=
		zone.sE.y and self.z >= zone.nW.z and self.z <= zone.sE.z then return true end
	return false
end

function Position:notifySummonAppear(summon)
	local spectators = Game.getSpectators(self)
	for _, spectator in ipairs(spectators) do
		local monster = spectator:getMonster()
		if monster and monster ~= summon and monster:canSeeCreature(summon) then monster:addTarget(summon) end
	end
end
