#include "../otpch.h"

#include "../configmanager.h"
#include "../player.h"
#include "../weapon_proficiency.h"

#include "test_support.h"

namespace {

using enum WeaponProficiencyBonus_t;

class ProficiencyEnabledGuard
{
public:
	ProficiencyEnabledGuard() : previous(ConfigManager::getBoolean(ConfigManager::WEAPON_PROFICIENCY_SYSTEM_ENABLED))
	{
		ConfigManager::setBoolean(ConfigManager::WEAPON_PROFICIENCY_SYSTEM_ENABLED, true);
	}

	~ProficiencyEnabledGuard()
	{
		ConfigManager::setBoolean(ConfigManager::WEAPON_PROFICIENCY_SYSTEM_ENABLED, previous);
	}

private:
	bool previous;
};

CombatDamage makeDamage(CombatOrigin origin, CombatType_t type)
{
	CombatDamage damage;
	damage.origin = origin;
	damage.primary.type = type;
	damage.primary.value = -1000;
	return damage;
}

CombatDamage makeInstantSpell(CombatType_t type)
{
	CombatDamage damage = makeDamage(ORIGIN_SPELL, type);
	damage.instantSpellName = "exori frigo";
	return damage;
}

CombatDamage makeRune(CombatType_t type, bool offensive)
{
	CombatDamage damage = makeDamage(ORIGIN_SPELL, type);
	damage.runeSpell = true;
	damage.offensiveRune = offensive;
	return damage;
}

// Mirrors the perk set that triggered the original report: an ice rod carrying
// "+2% critical hit chance for Ice spells and runes" and "+10% ice critical extra damage".
void applyIcePerks(WeaponProficiency& proficiency)
{
	proficiency.applyPerk(static_cast<uint8_t>(ELEMENTAL_HIT_CHANCE), 0.02, 0, 0, SKILL_FIST, COMBAT_ICEDAMAGE);
	proficiency.applyPerk(static_cast<uint8_t>(ELEMENTAL_CRITICAL_EXTRA_DAMAGE), 0.10, 0, 0, SKILL_FIST,
	                      COMBAT_ICEDAMAGE);
}

} // namespace

TEST_CASE(proficiency_perk_values_convert_to_basis_points)
{
	ProficiencyEnabledGuard guard;
	Player player(nullptr);
	WeaponProficiency proficiency(player);

	// 0.02 in the JSON means 2%, which combat consumes as 200 out of 10000. It must never
	// become 200% (20000) or 0.02% (2).
	proficiency.applyPerk(static_cast<uint8_t>(CRITICAL_HIT_CHANCE), 0.02);
	proficiency.applyPerk(static_cast<uint8_t>(CRITICAL_EXTRA_DAMAGE), 0.10);

	CombatDamage damage = makeDamage(ORIGIN_MELEE, COMBAT_PHYSICALDAMAGE);
	proficiency.applyCriticalPerks(damage);

	CHECK(damage.criticalChance == 200);
	CHECK(damage.criticalDamage == 1000);
}

TEST_CASE(elemental_crit_perks_skip_auto_attacks)
{
	ProficiencyEnabledGuard guard;
	Player player(nullptr);
	WeaponProficiency proficiency(player);
	applyIcePerks(proficiency);

	// A wand/rod swing is an auto attack, not a spell or rune: it must not pick up the
	// "for Ice spells and runes" perk. This was the source of the inflated wand criticals.
	for (const CombatOrigin origin : {ORIGIN_WAND, ORIGIN_MELEE, ORIGIN_RANGED}) {
		CombatDamage damage = makeDamage(origin, COMBAT_ICEDAMAGE);
		proficiency.applyCriticalPerks(damage);
		CHECK(damage.criticalChance == 0);
		CHECK(damage.criticalDamage == 0);
	}
}

TEST_CASE(elemental_crit_perks_apply_to_spells_and_runes_of_that_element)
{
	ProficiencyEnabledGuard guard;
	Player player(nullptr);
	WeaponProficiency proficiency(player);
	applyIcePerks(proficiency);

	CombatDamage spell = makeInstantSpell(COMBAT_ICEDAMAGE);
	proficiency.applyCriticalPerks(spell);
	CHECK(spell.criticalChance == 200);
	CHECK(spell.criticalDamage == 1000);

	CombatDamage rune = makeRune(COMBAT_ICEDAMAGE, true);
	proficiency.applyCriticalPerks(rune);
	CHECK(rune.criticalChance == 200);
	CHECK(rune.criticalDamage == 1000);

	// A non-attack rune is still "a rune" for the elemental perk, matching Crystal.
	CombatDamage supportRune = makeRune(COMBAT_ICEDAMAGE, false);
	proficiency.applyCriticalPerks(supportRune);
	CHECK(supportRune.criticalChance == 200);

	// Another element gets nothing.
	CombatDamage fireSpell = makeInstantSpell(COMBAT_FIREDAMAGE);
	proficiency.applyCriticalPerks(fireSpell);
	CHECK(fireSpell.criticalChance == 0);
	CHECK(fireSpell.criticalDamage == 0);
}

TEST_CASE(rune_crit_perks_require_an_offensive_rune)
{
	ProficiencyEnabledGuard guard;
	Player player(nullptr);
	WeaponProficiency proficiency(player);
	proficiency.applyPerk(static_cast<uint8_t>(RUNE_CRITICAL_HIT_CHANCE), 0.02);
	proficiency.applyPerk(static_cast<uint8_t>(RUNE_CRITICAL_EXTRA_DAMAGE), 0.05);

	CombatDamage attackRune = makeRune(COMBAT_ICEDAMAGE, true);
	proficiency.applyCriticalPerks(attackRune);
	CHECK(attackRune.criticalChance == 200);
	CHECK(attackRune.criticalDamage == 500);

	// Instant spells must not collect the rune perk...
	CombatDamage spell = makeInstantSpell(COMBAT_ICEDAMAGE);
	proficiency.applyCriticalPerks(spell);
	CHECK(spell.criticalChance == 0);
	CHECK(spell.criticalDamage == 0);

	// ...nor runes outside the attack group, nor auto attacks.
	CombatDamage supportRune = makeRune(COMBAT_ICEDAMAGE, false);
	proficiency.applyCriticalPerks(supportRune);
	CHECK(supportRune.criticalChance == 0);

	CombatDamage wand = makeDamage(ORIGIN_WAND, COMBAT_ICEDAMAGE);
	proficiency.applyCriticalPerks(wand);
	CHECK(wand.criticalChance == 0);
}

TEST_CASE(auto_attack_crit_perks_cover_wands_but_not_spells)
{
	ProficiencyEnabledGuard guard;
	Player player(nullptr);
	WeaponProficiency proficiency(player);
	proficiency.applyPerk(static_cast<uint8_t>(AUTO_ATTACK_CRITICAL_HIT_CHANCE), 0.03);
	proficiency.applyPerk(static_cast<uint8_t>(AUTO_ATTACK_CRITICAL_EXTRA_DAMAGE), 0.20);

	for (const CombatOrigin origin : {ORIGIN_MELEE, ORIGIN_RANGED, ORIGIN_WAND}) {
		CombatDamage damage = makeDamage(origin, COMBAT_PHYSICALDAMAGE);
		proficiency.applyCriticalPerks(damage);
		CHECK(damage.criticalChance == 300);
		CHECK(damage.criticalDamage == 2000);
	}

	CombatDamage spell = makeInstantSpell(COMBAT_ICEDAMAGE);
	proficiency.applyCriticalPerks(spell);
	CHECK(spell.criticalChance == 0);

	CombatDamage rune = makeRune(COMBAT_ICEDAMAGE, true);
	proficiency.applyCriticalPerks(rune);
	CHECK(rune.criticalChance == 0);
}

TEST_CASE(general_crit_perks_apply_to_every_hit_exactly_once)
{
	ProficiencyEnabledGuard guard;
	Player player(nullptr);
	WeaponProficiency proficiency(player);
	proficiency.applyPerk(static_cast<uint8_t>(CRITICAL_HIT_CHANCE), 0.02);
	proficiency.applyPerk(static_cast<uint8_t>(CRITICAL_EXTRA_DAMAGE), 0.07);
	// An elemental perk on the same element must not be double counted on an auto attack.
	applyIcePerks(proficiency);

	CombatDamage wand = makeDamage(ORIGIN_WAND, COMBAT_ICEDAMAGE);
	proficiency.applyCriticalPerks(wand);
	CHECK(wand.criticalChance == 200);
	CHECK(wand.criticalDamage == 700);

	// On an ice spell the general and elemental perks stack, once each.
	CombatDamage spell = makeInstantSpell(COMBAT_ICEDAMAGE);
	proficiency.applyCriticalPerks(spell);
	CHECK(spell.criticalChance == 400);
	CHECK(spell.criticalDamage == 1700);
}

TEST_CASE(disabling_the_system_keeps_combat_untouched)
{
	Player player(nullptr);
	WeaponProficiency proficiency(player);

	{
		ProficiencyEnabledGuard guard;
		proficiency.applyPerk(static_cast<uint8_t>(CRITICAL_HIT_CHANCE), 0.05);
	}
	proficiency.resetStats();

	// With the system off applyPerk is a no-op, so nothing can leak into combat.
	ConfigManager::setBoolean(ConfigManager::WEAPON_PROFICIENCY_SYSTEM_ENABLED, false);
	proficiency.applyPerk(static_cast<uint8_t>(CRITICAL_HIT_CHANCE), 0.05);
	applyIcePerks(proficiency);

	CombatDamage spell = makeInstantSpell(COMBAT_ICEDAMAGE);
	proficiency.applyCriticalPerks(spell);
	CHECK(spell.criticalChance == 0);
	CHECK(spell.criticalDamage == 0);
}

TEST_CASE(resetting_stats_clears_perks_so_reequipping_does_not_accumulate)
{
	ProficiencyEnabledGuard guard;
	Player player(nullptr);
	WeaponProficiency proficiency(player);

	// Equip, unequip and equip again: the perk must count once, not three times.
	for (int i = 0; i < 3; ++i) {
		proficiency.resetStats();
		applyIcePerks(proficiency);
	}

	CombatDamage spell = makeInstantSpell(COMBAT_ICEDAMAGE);
	proficiency.applyCriticalPerks(spell);
	CHECK(spell.criticalChance == 200);
	CHECK(spell.criticalDamage == 1000);

	proficiency.resetStats();
	CombatDamage afterUnequip = makeInstantSpell(COMBAT_ICEDAMAGE);
	proficiency.applyCriticalPerks(afterUnequip);
	CHECK(afterUnequip.criticalChance == 0);
	CHECK(afterUnequip.criticalDamage == 0);
}

TEST_CASE(leech_perks_are_added_and_fully_reverted_on_reset)
{
	ProficiencyEnabledGuard guard;
	Player player(nullptr);
	WeaponProficiency proficiency(player);

	proficiency.applyPerk(static_cast<uint8_t>(LIFE_LEECH), 0.03);
	proficiency.applyPerk(static_cast<uint8_t>(MANA_LEECH), 0.02);
	CHECK(player.getSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT) == 300);
	CHECK(player.getSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT) == 200);

	proficiency.resetStats();
	CHECK(player.getSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT) == 0);
	CHECK(player.getSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT) == 0);
}

TFS_TEST_MAIN()
