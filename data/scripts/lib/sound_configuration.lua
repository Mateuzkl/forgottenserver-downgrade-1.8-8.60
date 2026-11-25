-- Sound APIs compatible with the CrystalServer sound configuration tables.
registerNpcType = registerNpcType or {}
registerNpcType.sounds = function(npcType, mask)
	local sounds = mask.sounds
	if type(sounds) ~= "table" then return end
	npcType:soundSpeedTicks(sounds.ticks or sounds.interval or 5000)
	npcType:soundChance(sounds.chance or 10)
	for _, id in ipairs(sounds.ids or {}) do npcType:addSound(id) end
end
function NpcType:sounds(config)
	registerNpcType.sounds(self, {sounds = config})
end
function Charm:sounds(config)
	if config.castSound ~= nil then self:castSound(config.castSound) end
	if config.impactSound ~= nil then self:impactSound(config.impactSound) end
end
