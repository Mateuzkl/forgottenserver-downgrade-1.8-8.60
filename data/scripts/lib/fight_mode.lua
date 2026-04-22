-- FightMode constants & helpers
-- Centralises magic numbers for fight / chase / secure modes.

FIGHT_MODE = {
	FULL_ATTACK  = 0,
	BALANCED     = 1,
	FULL_DEFENSE = 2,
}

CHASE_MODE = {
	STAND_WHILE_FIGHTING = 0,
	CHASE_OPPONENT       = 1,
}

SECURE_MODE = {
	OFF = 0,
	ON  = 1,
}

-- Returns a human-readable string for the fight stance.
function getFightModeName(fightMode)
	local names = {
		[FIGHT_MODE.FULL_ATTACK]  = 'Full Attack',
		[FIGHT_MODE.BALANCED]     = 'Balanced',
		[FIGHT_MODE.FULL_DEFENSE] = 'Full Defense',
	}
	return names[fightMode] or 'Unknown'
end
