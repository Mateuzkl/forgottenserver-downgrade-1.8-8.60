-- Mirrors Augment_t in src/items.h. The enum is not registered with the
-- scripting engine, so systems that feed per-spell modifiers (equipment
-- proficiency, the wheel, the skill tree) share these names instead of passing
-- raw numbers around.
--
-- Values marked "declared only" are accepted by the engine but have no consumer
-- yet: they parse and store, but nothing reads them back at cast time. They are
-- the reserved extension points for future dimensions of spell power.
SpellModifier = {
	ManaCost = 1,
	BaseDamage = 2,
	BaseHealing = 3,
	DurationIncreased = 4, -- declared only
	AdditionalTargets = 5, -- declared only
	Cooldown = 6,
	SecondaryGroupCooldown = 7,
	AffectedAreaEnlarged = 8, -- declared only
	IncreasedDamageReduction = 9, -- declared only
	EnhancedEffect = 12, -- declared only
	IncreasedSkill = 13, -- declared only
	LifeLeech = 14,
	ManaLeech = 15,
	CriticalExtraDamage = 16,
	CriticalHitChance = 17,
	PowerfulImpact = 100,
	StrongImpact = 101,
	IncreasedDamage = 102,
}
