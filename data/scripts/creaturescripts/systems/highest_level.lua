-- Tracks the highest level a character has ever reached.
--
-- Progression that is meant to survive dying (the skill tree point budget) is
-- derived from this watermark instead of the current level, so losing levels to
-- death never costs the player points. A character reset is different: it
-- deliberately restarts the axis and clears the watermark (see doPlayerReset).

local advance = CreatureEvent("BacklandsHighestLevel")

function advance.onAdvance(player, skill, oldLevel, newLevel)
	if skill == SKILL_LEVEL then
		player:updateHighestLevel()
	end
	return true
end

advance:register()

local login = CreatureEvent("BacklandsHighestLevelLogin")

function login.onLogin(player)
	player:registerEvent("BacklandsHighestLevel")
	-- Backfill: characters that existed before this system, and any level gained
	-- through a path that does not raise onAdvance.
	player:updateHighestLevel()
	return true
end

login:register()
