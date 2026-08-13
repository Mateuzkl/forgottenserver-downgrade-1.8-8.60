-- Skill tree nodes.
--
-- A directed graph: each node names the nodes it depends on and the level those
-- need to reach. Points come from the highest level the character ever reached,
-- so dying never costs a node.
--
-- Cost doubles every level: baseCost, 2x, 4x, 8x... A node at level N has cost
-- baseCost * (2^N - 1) in total. Keep maxLevel small — at baseCost 1 and
-- maxLevel 5 a full node already costs 31 points.
--
--   name        Display name.
--   maxLevel    Level ceiling for this node.
--   baseCost    Cost of the first level.
--   requires    { { node = "id", level = n }, ... } — all must be satisfied.
--   teaches     Optional spell NAME, learned permanently at level 1. The spell
--               must declare spell:needLearn(true) in its own script.
--   modifiers   Optional. { { spell = "Name", type = SpellModifier.X,
--                             perLevel = n, value = n }, ... }
--   attributes  Optional per-level stat bonuses. Supported keys: health, mana,
--               capacity, magic, melee, distance, fist, shielding.

return {
	fire_affinity = {
		name = "Fire Affinity",
		maxLevel = 5,
		baseCost = 1,
		requires = {},
		attributes = { magic = 1 },
	},

	flame_strike = {
		name = "Flame Strike",
		maxLevel = 5,
		baseCost = 1,
		requires = { { node = "fire_affinity", level = 2 } },
		modifiers = {
			{ spell = "Exori Flam", type = SpellModifier.BaseDamage, perLevel = 3 },
		},
	},

	conflagration = {
		name = "Conflagration",
		maxLevel = 3,
		baseCost = 2,
		requires = { { node = "flame_strike", level = 3 } },
		teaches = "Exevo Flam Hur",
		modifiers = {
			{ spell = "Exevo Flam Hur", type = SpellModifier.Cooldown, perLevel = 200 },
		},
	},

	toughness = {
		name = "Toughness",
		maxLevel = 5,
		baseCost = 1,
		requires = {},
		attributes = { health = 20, capacity = 40 },
	},

	shield_training = {
		name = "Shield Training",
		maxLevel = 4,
		baseCost = 1,
		requires = { { node = "toughness", level = 2 } },
		attributes = { shielding = 1 },
	},
}
