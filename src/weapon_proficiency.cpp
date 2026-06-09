// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "weapon_proficiency.h"

#include "configmanager.h"
#include "game.h"
#include "monster.h"
#include "monsters.h"
#include "player.h"

#include <cmath>

namespace {

int32_t saturatingAdd(int32_t value, int64_t increase)
{
	return static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(value) + increase,
	                                               std::numeric_limits<int32_t>::min(),
	                                               std::numeric_limits<int32_t>::max()));
}

bool isEnabled()
{
	return ConfigManager::getBoolean(ConfigManager::WEAPON_PROFICIENCY_SYSTEM_ENABLED);
}

} // namespace

WeaponProficiency::WeaponProficiency(Player& player) :
	m_player(player)
{}

void WeaponProficiency::resetStats()
{
	m_stats.fill(0);
	m_skills.fill(0);
	m_specializedMagic.fill(0);
	m_generalCritical = {};
	m_autoAttackCritical = {};
	m_runesCritical = {};
	m_elementCritical.fill({});
	m_powerfulFoeDamage = 0;
	m_perfectShot = {};
	m_bestiaryDamage.clear();
	m_skillPercentages.clear();

	if (m_lifeLeechAdded != 0) {
		m_player.setVarSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT, -m_lifeLeechAdded);
		m_lifeLeechAdded = 0;
	}
	if (m_manaLeechAdded != 0) {
		m_player.setVarSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT, -m_manaLeechAdded);
		m_manaLeechAdded = 0;
	}
}

void WeaponProficiency::applyPerk(uint8_t perkType, double_t value, uint16_t /*spellId*/,
                                  uint8_t /*augmentType*/, skills_t skillId,
                                  CombatType_t element, uint8_t range,
                                  uint16_t bestiaryId)
{
	if (!isEnabled()) {
		return;
	}

	using enum WeaponProficiencyBonus_t;

	switch (static_cast<WeaponProficiencyBonus_t>(perkType)) {
		case SPELL_AUGMENT:
			break;

		case SPECIALIZED_MAGIC_LEVEL:
			addSpecializedMagic(element, value);
			break;

		case AUTO_ATTACK_CRITICAL_EXTRA_DAMAGE:
		case AUTO_ATTACK_CRITICAL_HIT_CHANCE:
		case ELEMENTAL_HIT_CHANCE:
		case ELEMENTAL_CRITICAL_EXTRA_DAMAGE:
		case RUNE_CRITICAL_HIT_CHANCE:
		case RUNE_CRITICAL_EXTRA_DAMAGE:
		case CRITICAL_HIT_CHANCE:
		case CRITICAL_EXTRA_DAMAGE:
			applyCriticalBonus(perkType, element, value);
			break;

		case WEAPON_PROFICIENCY_BESTIARY:
			addBestiaryDamage(bestiaryId, value);
			break;

		case POWERFUL_FOE_BONUS:
			addPowerfulFoeDamage(value);
			break;

		case SKILL_BONUS:
			addSkillBonus(skillId, value);
			break;

		case LIFE_LEECH:
		case MANA_LEECH: {
			SpecialSkills_t specialSkill = (perkType == static_cast<uint8_t>(LIFE_LEECH))
			    ? SPECIALSKILL_LIFELEECHAMOUNT
			    : SPECIALSKILL_MANALEECHAMOUNT;
			int32_t amount = static_cast<int32_t>(std::llround(value * 10000.0));
			m_player.setVarSpecialSkill(specialSkill, amount);
			if (perkType == static_cast<uint8_t>(LIFE_LEECH)) {
				m_lifeLeechAdded += amount;
			} else {
				m_manaLeechAdded += amount;
			}
			break;
		}

		case PERFECT_SHOT_DAMAGE:
			if (m_perfectShot.range == 0 || m_perfectShot.range == range) {
				m_perfectShot.range = range;
				m_perfectShot.damage += value;
			} else if (value > m_perfectShot.damage) {
				m_perfectShot.range = range;
				m_perfectShot.damage = value;
			}
			break;

		case SKILL_PERCENTAGE_AUTO_ATTACK:
		case SKILL_PERCENTAGE_SPELL_DAMAGE:
		case SKILL_PERCENTAGE_SPELL_HEALING:
			applySkillPercentageBonus(perkType, skillId, value);
			break;

		default:
			addStat(static_cast<WeaponProficiencyBonus_t>(perkType), value);
			break;
	}
}

void WeaponProficiency::addStat(WeaponProficiencyBonus_t stat, double_t value)
{
	const auto index = static_cast<size_t>(stat);
	if (index < m_stats.size()) {
		m_stats[index] += value;
	}
}

double_t WeaponProficiency::getStat(WeaponProficiencyBonus_t stat) const
{
	const auto index = static_cast<size_t>(stat);
	if (index < m_stats.size()) {
		return m_stats[index];
	}
	return 0;
}

void WeaponProficiency::addSkillBonus(skills_t type, double_t value)
{
	const auto rounded = std::llround(value);
	if (rounded <= 0 || !std::isfinite(value)) {
		return;
	}
	const auto index = static_cast<size_t>(type);
	if (index < m_skills.size()) {
		m_skills[index] += static_cast<uint32_t>(rounded);
	}
}

uint32_t WeaponProficiency::getSkillBonus(skills_t type) const
{
	const auto index = static_cast<size_t>(type);
	if (index < m_skills.size()) {
		return m_skills[index];
	}
	return 0;
}

void WeaponProficiency::addSpecializedMagic(CombatType_t type, double_t value)
{
	if (!std::isfinite(value)) {
		return;
	}
	const size_t index = combatTypeToIndex(type);
	if (index < m_specializedMagic.size()) {
		const auto newVal = static_cast<double_t>(m_specializedMagic[index]) + value;
		m_specializedMagic[index] = static_cast<uint16_t>(
		    std::clamp<double_t>(newVal, 0, std::numeric_limits<uint16_t>::max()));
	}
}

uint16_t WeaponProficiency::getSpecializedMagic(CombatType_t type) const
{
	const size_t index = combatTypeToIndex(type);
	if (index < m_specializedMagic.size()) {
		return m_specializedMagic[index];
	}
	return 0;
}

void WeaponProficiency::addBestiaryDamage(uint16_t raceId, double_t value)
{
	m_bestiaryDamage[raceId] += value;
}

double_t WeaponProficiency::getBestiaryDamage(uint16_t raceId) const
{
	auto it = m_bestiaryDamage.find(raceId);
	if (it != m_bestiaryDamage.end()) {
		return it->second;
	}
	return 0;
}

void WeaponProficiency::addPowerfulFoeDamage(double_t value)
{
	m_powerfulFoeDamage += value;
}

double_t WeaponProficiency::getPowerfulFoeDamage() const
{
	return m_powerfulFoeDamage;
}

void WeaponProficiency::addGeneralCritical(const WeaponProficiencyCriticalBonus& bonus)
{
	m_generalCritical.chance += bonus.chance;
	m_generalCritical.damage += bonus.damage;
}

void WeaponProficiency::addAutoAttackCritical(const WeaponProficiencyCriticalBonus& bonus)
{
	m_autoAttackCritical.chance += bonus.chance;
	m_autoAttackCritical.damage += bonus.damage;
}

void WeaponProficiency::addRunesCritical(const WeaponProficiencyCriticalBonus& bonus)
{
	m_runesCritical.chance += bonus.chance;
	m_runesCritical.damage += bonus.damage;
}

void WeaponProficiency::addElementCritical(CombatType_t type, const WeaponProficiencyCriticalBonus& bonus)
{
	const size_t index = combatTypeToIndex(type);
	if (index < m_elementCritical.size()) {
		m_elementCritical[index].chance += bonus.chance;
		m_elementCritical[index].damage += bonus.damage;
	}
}

const WeaponProficiencyCriticalBonus& WeaponProficiency::getGeneralCritical() const
{
	return m_generalCritical;
}

const WeaponProficiencyCriticalBonus& WeaponProficiency::getAutoAttackCritical() const
{
	return m_autoAttackCritical;
}

const WeaponProficiencyCriticalBonus& WeaponProficiency::getRunesCritical() const
{
	return m_runesCritical;
}

WeaponProficiencyCriticalBonus WeaponProficiency::getElementCritical(CombatType_t type) const
{
	const size_t index = combatTypeToIndex(type);
	if (index < m_elementCritical.size()) {
		return m_elementCritical[index];
	}
	return {};
}

const WeaponProficiencyPerfectShotBonus& WeaponProficiency::getPerfectShotBonus() const
{
	return m_perfectShot;
}

void WeaponProficiency::addSkillPercentage(skills_t skill, SkillPercentage_t type, double_t value)
{
	auto& sp = m_skillPercentages[skill];
	sp.skill = skill;

	switch (type) {
		case SkillPercentage_t::AutoAttack:
			sp.autoAttack += value;
			break;
		case SkillPercentage_t::SpellDamage:
			sp.spellDamage += value;
			break;
		case SkillPercentage_t::SpellHealing:
			sp.spellHealing += value;
			break;
	}
}

const SkillPercentage& WeaponProficiency::getSkillPercentage(skills_t skill) const
{
	static const SkillPercentage empty;
	auto it = m_skillPercentages.find(skill);
	if (it != m_skillPercentages.end()) {
		return it->second;
	}
	return empty;
}

void WeaponProficiency::applyCriticalBonus(uint8_t perkType, CombatType_t element, double_t value)
{
	using enum WeaponProficiencyBonus_t;
	WeaponProficiencyCriticalBonus criticalBonus;
	const auto type = static_cast<WeaponProficiencyBonus_t>(perkType);

	const bool isChance = (type == AUTO_ATTACK_CRITICAL_HIT_CHANCE ||
	                       type == ELEMENTAL_HIT_CHANCE ||
	                       type == RUNE_CRITICAL_HIT_CHANCE ||
	                       type == CRITICAL_HIT_CHANCE);
	const bool isDamage = (type == AUTO_ATTACK_CRITICAL_EXTRA_DAMAGE ||
	                       type == ELEMENTAL_CRITICAL_EXTRA_DAMAGE ||
	                       type == RUNE_CRITICAL_EXTRA_DAMAGE ||
	                       type == CRITICAL_EXTRA_DAMAGE);

	if (isChance) {
		criticalBonus.chance = value;
	} else if (isDamage) {
		criticalBonus.damage = value;
	}

	switch (type) {
		case AUTO_ATTACK_CRITICAL_EXTRA_DAMAGE:
		case AUTO_ATTACK_CRITICAL_HIT_CHANCE:
			addAutoAttackCritical(criticalBonus);
			break;
		case ELEMENTAL_HIT_CHANCE:
		case ELEMENTAL_CRITICAL_EXTRA_DAMAGE:
			if (element != COMBAT_NONE) {
				addElementCritical(element, criticalBonus);
			}
			break;
		case RUNE_CRITICAL_HIT_CHANCE:
		case RUNE_CRITICAL_EXTRA_DAMAGE:
			addRunesCritical(criticalBonus);
			break;
		case CRITICAL_HIT_CHANCE:
		case CRITICAL_EXTRA_DAMAGE:
			addGeneralCritical(criticalBonus);
			break;
		default:
			break;
	}
}

void WeaponProficiency::applySkillPercentageBonus(uint8_t perkType, skills_t skillId, double_t value)
{
	using enum WeaponProficiencyBonus_t;
	using enum SkillPercentage_t;

	SkillPercentage_t spType;
	switch (static_cast<WeaponProficiencyBonus_t>(perkType)) {
		case SKILL_PERCENTAGE_AUTO_ATTACK:
			spType = AutoAttack;
			break;
		case SKILL_PERCENTAGE_SPELL_DAMAGE:
			spType = SpellDamage;
			break;
		case SKILL_PERCENTAGE_SPELL_HEALING:
			spType = SpellHealing;
			break;
		default:
			return;
	}

	addSkillPercentage(skillId, spType, value);
}

// ---- Combat application methods ---- //

void WeaponProficiency::applyAutoAttackCritical(CombatDamage& damage) const
{
	if (damage.origin == ORIGIN_WAND) {
		return;
	}

	if (damage.origin == ORIGIN_MELEE || damage.origin == ORIGIN_RANGED) {
		damage.criticalChance = saturatingAdd(damage.criticalChance,
		                                      static_cast<int64_t>(std::llround(m_autoAttackCritical.chance * 10000.0)));
		damage.criticalDamage = saturatingAdd(damage.criticalDamage,
		                                      static_cast<int64_t>(std::llround(m_autoAttackCritical.damage * 10000.0)));
	}
}

void WeaponProficiency::applyGeneralCritical(CombatDamage& damage) const
{
	if (m_generalCritical.chance > 0 || m_generalCritical.damage > 0) {
		damage.criticalChance = saturatingAdd(damage.criticalChance,
		                                      static_cast<int64_t>(std::llround(m_generalCritical.chance * 10000.0)));
		damage.criticalDamage = saturatingAdd(damage.criticalDamage,
		                                      static_cast<int64_t>(std::llround(m_generalCritical.damage * 10000.0)));
	}
}

void WeaponProficiency::applyRunesCritical(CombatDamage& damage, bool aggressive) const
{
	if (!aggressive) {
		return;
	}

	if (!damage.instantSpellName.empty() && damage.origin == ORIGIN_SPELL) {
		damage.criticalChance = saturatingAdd(damage.criticalChance,
		                                      static_cast<int64_t>(std::llround(m_runesCritical.chance * 10000.0)));
		damage.criticalDamage = saturatingAdd(damage.criticalDamage,
		                                      static_cast<int64_t>(std::llround(m_runesCritical.damage * 10000.0)));
	}
}

void WeaponProficiency::applyElementCritical(CombatDamage& damage) const
{
	const size_t index = combatTypeToIndex(damage.primary.type);
	if (index < m_elementCritical.size()) {
		const auto& ec = m_elementCritical[index];
		damage.criticalChance = saturatingAdd(damage.criticalChance,
		                                      static_cast<int64_t>(std::llround(ec.chance * 10000.0)));
		damage.criticalDamage = saturatingAdd(damage.criticalDamage,
		                                      static_cast<int64_t>(std::llround(ec.damage * 10000.0)));
	}
}

void WeaponProficiency::applyBestiaryDamage(CombatDamage& damage, const std::shared_ptr<Monster>& monster) const
{
	if (!monster) {
		return;
	}

	const auto* mType = monster->getMonsterType();
	if (!mType) {
		return;
	}

	auto it = m_bestiaryDamage.find(static_cast<uint16_t>(mType->raceId));
	if (it != m_bestiaryDamage.end() && it->second > 0) {
		applyDamageMultiplier(damage, it->second);
	}
}

void WeaponProficiency::applyPowerfulFoeDamage(CombatDamage& damage, const std::shared_ptr<Monster>& monster) const
{
	if (!monster || m_powerfulFoeDamage <= 0) {
		return;
	}

	const bool isBossTarget = monster->isBoss() || monster->isInfluenced() || monster->isFiendish();
	if (isBossTarget) {
		applyDamageMultiplier(damage, m_powerfulFoeDamage);
	}
}

void WeaponProficiency::applyDamageMultiplier(CombatDamage& damage, double_t multiplier) const
{
	const double_t mult = 1.0 + multiplier;
	damage.primary.value = static_cast<int32_t>(std::llround(damage.primary.value * mult));
	damage.secondary.value = static_cast<int32_t>(std::llround(damage.secondary.value * mult));
}

void WeaponProficiency::applySkillAutoAttackPercentage(CombatDamage& damage) const
{
	if (damage.origin != ORIGIN_MELEE && damage.origin != ORIGIN_RANGED) {
		return;
	}

	for (const auto& kv : m_skillPercentages) {
		const auto& sp = kv.second;
		if (sp.autoAttack > 0) {
			const int32_t extra = static_cast<int32_t>(
			    std::ceil(m_player.getSkillLevel(sp.skill) * sp.autoAttack));
			damage.primary.value += extra;
		}
	}
}

void WeaponProficiency::applySkillSpellPercentage(CombatDamage& damage, bool healing) const
{
	if (damage.instantSpellName.empty()) {
		return;
	}

	for (const auto& kv : m_skillPercentages) {
		const auto& sp = kv.second;
		double_t value = healing ? sp.spellHealing : sp.spellDamage;

		if (value > 0) {
			const int32_t extra = static_cast<int32_t>(
			    std::ceil(m_player.getSkillLevel(sp.skill) * value));
			damage.primary.value += extra;
		}
	}
}

void WeaponProficiency::applyOn(WeaponProficiencyHealth_t healthType, WeaponProficiencyGain_t gainType) const
{
	using enum WeaponProficiencyBonus_t;
	using enum WeaponProficiencyHealth_t;
	using enum WeaponProficiencyGain_t;

	double_t value = 0;

	if (healthType == LIFE) {
		value = getStat(gainType == HIT ? LIFE_GAIN_ON_HIT : LIFE_GAIN_ON_KILL);
		if (value > 0) {
			m_player.gainHealth(nullptr, static_cast<int32_t>(std::llround(value)));
		}
	} else if (healthType == MANA) {
		value = getStat(gainType == HIT ? MANA_GAIN_ON_HIT : MANA_GAIN_ON_KILL);
		if (value > 0) {
			m_player.changeMana(static_cast<int32_t>(std::llround(value)));
		}
	}
}
