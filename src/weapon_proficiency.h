// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_WEAPON_PROFICIENCY_H
#define FS_WEAPON_PROFICIENCY_H

#include "enums.h"
#include "tools.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>

struct CombatDamage;
class Player;
class Monster;

enum class WeaponProficiencyBonus_t : uint8_t {
	ATTACK_DAMAGE = 0,
	DEFENSE_BONUS = 1,
	WEAPON_SHIELD_MODIFIER = 2,
	SKILL_BONUS = 3,
	SPECIALIZED_MAGIC_LEVEL = 4,
	SPELL_AUGMENT = 5,
	WEAPON_PROFICIENCY_BESTIARY = 6,
	POWERFUL_FOE_BONUS = 7,
	CRITICAL_HIT_CHANCE = 8,
	ELEMENTAL_HIT_CHANCE = 9,
	RUNE_CRITICAL_HIT_CHANCE = 10,
	AUTO_ATTACK_CRITICAL_HIT_CHANCE = 11,
	CRITICAL_EXTRA_DAMAGE = 12,
	ELEMENTAL_CRITICAL_EXTRA_DAMAGE = 13,
	RUNE_CRITICAL_EXTRA_DAMAGE = 14,
	AUTO_ATTACK_CRITICAL_EXTRA_DAMAGE = 15,
	MANA_LEECH = 16,
	LIFE_LEECH = 17,
	MANA_GAIN_ON_HIT = 18,
	LIFE_GAIN_ON_HIT = 19,
	MANA_GAIN_ON_KILL = 20,
	LIFE_GAIN_ON_KILL = 21,
	PERFECT_SHOT_DAMAGE = 22,
	RANGED_HIT_CHANCE = 23,
	ATTACK_RANGE = 24,
	SKILL_PERCENTAGE_AUTO_ATTACK = 25,
	SKILL_PERCENTAGE_SPELL_DAMAGE = 26,
	SKILL_PERCENTAGE_SPELL_HEALING = 27,
	// 28-31 follow the ids used by data/items/proficiencies.json (Crystal Server's numbering).
	// They are parsed and stored but not consumed by combat yet.
	ALPHA_STRIKE_EXTRA_DAMAGE = 28,
	OMEGA_STRIKE_EXTRA_DAMAGE = 29,
	ARMOR_PENETRATION = 30,
	ELEMENTAL_PIERCE = 31,

	BONUS_COUNT = 32
};

enum class SkillPercentage_t : uint8_t {
	AutoAttack,
	SpellDamage,
	SpellHealing,
};

enum class WeaponProficiencyHealth_t : uint8_t {
	LIFE = 0,
	MANA = 1,
};

enum class WeaponProficiencyGain_t : uint8_t {
	HIT = 0,
	KILL = 1,
};

struct WeaponProficiencyCriticalBonus {
	double_t chance = 0;
	double_t damage = 0;
};

struct SkillPercentage {
	skills_t skill = SKILL_FIST;
	double_t spellHealing = 0;
	double_t autoAttack = 0;
	double_t spellDamage = 0;
};

struct WeaponProficiencyPerfectShotBonus {
	uint8_t range = 0;
	double_t damage = 0;
};

class WeaponProficiency {
public:
	explicit WeaponProficiency(Player& player);

	void resetStats();

	/**
	 * Apply a single perk's bonus to the player.
	 * @param perkType  The enum value of the perk type (0-27)
	 * @param value     The numeric value (percentage as decimal, e.g. 0.05 for 5%)
	 * @param spellId   For SPELL_AUGMENT type
	 * @param augmentType For SPELL_AUGMENT type (2=damage, 3=heal, 6=cooldown, 14=lifeleech, 15=manaleech, 16=critdmg, 17=critchance)
	 * @param skillId   For SKILL_BONUS and SKILL_PERCENTAGE types
	 * @param element   For SPECIALIZED_MAGIC_LEVEL and ELEMENTAL_CRITICAL types
	 * @param range     For PERFECT_SHOT_DAMAGE type
	 * @param bestiaryId For WEAPON_PROFICIENCY_BESTIARY type
	 */
	void applyPerk(uint8_t perkType, double_t value, uint16_t spellId = 0,
	               uint8_t augmentType = 0, skills_t skillId = SKILL_FIST,
	               CombatType_t element = COMBAT_NONE, uint8_t range = 0,
	               uint16_t bestiaryId = 0);

	double_t getStat(WeaponProficiencyBonus_t stat) const;
	uint32_t getSkillBonus(skills_t type) const;
	uint16_t getSpecializedMagic(CombatType_t type) const;

	const WeaponProficiencyCriticalBonus& getGeneralCritical() const;
	const WeaponProficiencyCriticalBonus& getAutoAttackCritical() const;
	const WeaponProficiencyCriticalBonus& getRunesCritical() const;
	WeaponProficiencyCriticalBonus getElementCritical(CombatType_t type) const;

	double_t getBestiaryDamage(uint16_t raceId) const;
	double_t getPowerfulFoeDamage() const;
	const WeaponProficiencyPerfectShotBonus& getPerfectShotBonus() const;
	const SkillPercentage& getSkillPercentage(skills_t skill) const;

	// Combat application — called from combat/game code when system is enabled

	/**
	 * Fold every critical perk that matches this hit into damage.criticalChance /
	 * damage.criticalDamage, which the combat roll then consumes once.
	 *
	 * Which perks match follows Crystal Server:
	 *  - general (8/12)          : every hit;
	 *  - auto attack (11/15)     : melee, distance and wand/rod swings only;
	 *  - offensive rune (10/14)  : runes whose spell group is "attack" only;
	 *  - elemental (9/13)        : instant spells and runes only, matched on primary damage type.
	 */
	void applyCriticalPerks(CombatDamage& damage) const;
	void applyBestiaryDamage(CombatDamage& damage, const std::shared_ptr<Monster>& monster) const;
	void applyPowerfulFoeDamage(CombatDamage& damage, const std::shared_ptr<Monster>& monster) const;
	void applySkillAutoAttackPercentage(CombatDamage& damage) const;
	void applySkillSpellPercentage(CombatDamage& damage, bool healing = false) const;

	/**
	 * Apply life/mana on hit/kill bonuses.
	 * @param healthType 0 = LIFE, 1 = MANA
	 * @param gainType   0 = HIT, 1 = KILL
	 */
	void applyOn(WeaponProficiencyHealth_t healthType, WeaponProficiencyGain_t gainType) const;

private:
	static constexpr size_t TRACKED_SKILL_COUNT = static_cast<size_t>(SKILL_MAGLEVEL) + 1; // covers SKILL_FIST(0)..SKILL_MAGLEVEL(7)
	static_assert(SKILL_MAGLEVEL > SKILL_FISHING, "TRACKED_SKILL_COUNT assumes SKILL_MAGLEVEL is the last combat skill");

	void addStat(WeaponProficiencyBonus_t stat, double_t value);
	void addSkillBonus(skills_t type, double_t value);
	void addSpecializedMagic(CombatType_t type, double_t value);
	void addBestiaryDamage(uint16_t raceId, double_t value);
	void addPowerfulFoeDamage(double_t value);
	void addGeneralCritical(const WeaponProficiencyCriticalBonus& bonus);
	void addAutoAttackCritical(const WeaponProficiencyCriticalBonus& bonus);
	void addRunesCritical(const WeaponProficiencyCriticalBonus& bonus);
	void addElementCritical(CombatType_t type, const WeaponProficiencyCriticalBonus& bonus);
	void addSkillPercentage(skills_t skill, SkillPercentage_t type, double_t value);
	void applyCriticalBonus(uint8_t perkType, CombatType_t element, double_t value);
	void applySkillPercentageBonus(uint8_t perkType, skills_t skillId, double_t value);
	void applyDamageMultiplier(CombatDamage& damage, double_t multiplier) const;

	// SKILL_MAGLEVEL sits outside Player::skills[], so it has to be read as the magic level.
	int32_t getProficiencyStatLevel(skills_t skill) const;
	// Adds a flat bonus in the direction the value already points: damage is negative, healing
	// positive (same convention as applyPerfectShotDamage in weapons.cpp).
	static void addExtraValue(CombatDamage& damage, int32_t bonus, bool includeSecondary);

	Player& m_player;

	std::array<double_t, static_cast<size_t>(WeaponProficiencyBonus_t::BONUS_COUNT)> m_stats = { 0 };

	std::array<uint32_t, TRACKED_SKILL_COUNT> m_skills = { 0 };
	std::array<uint16_t, COMBAT_COUNT> m_specializedMagic = { 0 };

	WeaponProficiencyCriticalBonus m_generalCritical;
	WeaponProficiencyCriticalBonus m_autoAttackCritical;
	WeaponProficiencyCriticalBonus m_runesCritical;
	std::array<WeaponProficiencyCriticalBonus, COMBAT_COUNT> m_elementCritical = {};

	double_t m_powerfulFoeDamage = 0;
	WeaponProficiencyPerfectShotBonus m_perfectShot;

	int32_t m_lifeLeechAdded = 0;
	int32_t m_manaLeechAdded = 0;

	std::unordered_map<uint16_t, double_t> m_bestiaryDamage;
	std::unordered_map<skills_t, SkillPercentage> m_skillPercentages;
};

#endif // FS_WEAPON_PROFICIENCY_H
