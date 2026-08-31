-- NpcType Wrapper
-- CrystalServer uses these options for its NPC dialog-buttons packet, which
-- this 8.60 fork does not send. Accept the optional UI metadata so imported
-- scripts reach register(), where outfits, callbacks and shops are configured.
-- Text keywords such as "trade" and "bye" remain handled by the NPC system.
if not NpcType.addDialogOptions then
	function NpcType:addDialogOptions(...)
		return true
	end
end

if not _G.OriginalNpcTypeRegister then
	_G.OriginalNpcTypeRegister = NpcType.register
end

function NpcType:register(npcConfig)
	_G.OriginalNpcTypeRegister(self, npcConfig)
	
	local handler = _G.LastCrystalNpcHandler
	if handler then
		_G.LastCrystalNpcHandler = nil
		return
	end
end
