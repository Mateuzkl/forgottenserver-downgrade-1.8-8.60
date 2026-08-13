-- Which spell each piece of equipment teaches, and to whom.
--
-- Keyed by server item id, then by BASE vocation id. Promoted vocations resolve
-- down to their base before the lookup, so listing "Knight" also covers "Elite
-- Knight" — do not add promoted ids here.
--
-- Each entry is a list, so one item may teach several spells at different
-- mastery levels.
--
--   spell       Spell NAME, exactly as the revscript declares it. Never a spell
--               id: only 183 of 826 spells define one, 14 ids are duplicated,
--               and there is no id -> spell lookup anywhere in the engine.
--   masterLevel Proficiency level (1-7) at which the spell becomes permanent.
--               Below it the spell is only castable while the item is worn.
--   modifiers   Optional. How the item empowers the spell while it is worn.
--               Each entry is { type = SpellModifier.X, perLevel = n, value = n }
--               where perLevel scales with the item's proficiency level and
--               value is flat. See data/lib/core/spell_modifiers.lua for the
--               available types and which ones the engine consumes today.
--
-- Every spell referenced here must declare spell:needLearn(true) and list the
-- matching vocations in spell:vocation(...) in its own script under
-- data/scripts/spells/. Marking a spell that is currently available by vocation
-- alone will take it away from players until they learn it.

-- Base vocation ids from data/XML/vocations.xml.
local V = {
	SORCERER = 1,
	DRUID = 2,
	PALADIN = 3,
	KNIGHT = 4,
	MONK = 9,
}

return {
	-- Fire Sword
	[2392] = {
		[V.KNIGHT] = {
			{
				spell = "Exori Flam",
				masterLevel = 3,
				modifiers = {
					{ type = SpellModifier.BaseDamage, perLevel = 2 },
				},
			},
		},
		[V.SORCERER] = {
			{ spell = "Exevo Flam Hur", masterLevel = 5 },
		},
	},
}
