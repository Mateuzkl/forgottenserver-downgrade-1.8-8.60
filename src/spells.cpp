// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "spells.h"

#include "combat.h"
#include "configmanager.h"
#include "events.h"
#include "game.h"
#include "luavariant.h"
#include "monster.h"
#include "pugicast.h"
#include "scriptmanager.h"
#include "logger.h"
#include <fmt/format.h>

extern Game g_game;
extern Monsters g_monsters;
extern LuaEnvironment g_luaEnvironment;

namespace {
bool spellsIsMonkVocationId(uint16_t vocationId)
{
	return vocationId == 9 || vocationId == 10;
}

bool spellsIsDisabledMonkVocation(const Player* player)
{
	return player && !ConfigManager::getBoolean(ConfigManager::MONK_VOCATION_ENABLED) &&
	       spellsIsMonkVocationId(player->getVocationId());
}

bool spellsIsFamiliarSpell(std::string_view name)
{
	return name.find("Familiar") != std::string_view::npos || name.find("familiar") != std::string_view::npos;
}

int32_t saturatingAdd(int32_t value, int64_t increase)
{
	return static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(value) + increase,
	                                               std::numeric_limits<int32_t>::min(),
	                                               std::numeric_limits<int32_t>::max()));
}
} // namespace

Spells::Spells() { scriptInterface.initState(); }

Spells::~Spells() { clear(false); }

TalkActionResult Spells::playerSaySpell(Player* player, std::string& words)
{
	std::string str_words = words;

	// strip trailing spaces
	trimString(str_words);

	InstantSpell* instantSpell = getInstantSpell(str_words);

	std::string customSuffix;
	if (!instantSpell) {
		size_t quotePos = str_words.find(" \"");
		if (quotePos != std::string::npos) {
			std::string baseWords = str_words.substr(0, quotePos);
			InstantSpell* baseSpell = getInstantSpell(baseWords);

			if (baseSpell && !baseSpell->getHasParam()) {
				size_t start = quotePos + 2;
				size_t end = str_words.rfind('"');
				if (end > quotePos + 1) {
					customSuffix = str_words.substr(start, end - start);
				} else {
					customSuffix = str_words.substr(start);
				}
				str_words = baseWords;
				instantSpell = baseSpell;
			}
		}
	}

	if (!instantSpell) {
		return TalkActionResult::CONTINUE;
	}

	std::string param;

	if (instantSpell->getHasParam()) {
		size_t spellLen = instantSpell->getWords().length();
		size_t paramLen = str_words.length() - spellLen;
		std::string paramText = str_words.substr(spellLen, paramLen);
		if (!paramText.empty() && paramText.front() == ' ') {
			size_t loc1 = paramText.find('"', 1);
			if (loc1 != std::string::npos) {
				size_t loc2 = paramText.find('"', loc1 + 1);
				if (loc2 == std::string::npos) {
					loc2 = paramText.length();
				} else if (paramText.find_last_not_of(' ') != loc2) {
					return TalkActionResult::CONTINUE;
				}

				param = paramText.substr(loc1 + 1, loc2 - loc1 - 1);
			} else {
				trimString(paramText);
				loc1 = paramText.find(' ', 0);
				if (loc1 == std::string::npos) {
					param = paramText;
				} else {
					return TalkActionResult::CONTINUE;
				}
			}
		}
	}

	if (instantSpell->playerCastInstant(player, param)) {
		words = instantSpell->getWords();

		if (instantSpell->getHasParam() && !param.empty()) {
			words += " \"" + param + "\"";
		} else if (!customSuffix.empty()) {
			words += " \"" + customSuffix;
		}

		return TalkActionResult::BREAK;
	}

	return TalkActionResult::FAILED;
}

void Spells::clearMaps(bool fromLua)
{
	std::erase_if(instants, [this, fromLua](const auto& entry) {
		if (fromLua != entry.second.fromLua) {
			return false;
		}
		instantsByName.erase(std::string(entry.second.getName()));
		return true;
	});

	std::erase_if(runes, [this, fromLua](const auto& entry) {
		if (fromLua != entry.second.fromLua) {
			return false;
		}
		runesByName.erase(std::string(entry.second.getName()));
		return true;
	});
}

void Spells::clear(bool fromLua)
{
	clearMaps(fromLua);

	reInitState(fromLua);
}

LuaScriptInterface& Spells::getScriptInterface() { return scriptInterface; }

std::string_view Spells::getScriptBaseName() const { return "spells"; }

bool Spells::registerInstantLuaEvent(InstantSpell* event)
{
	InstantSpell_ptr instant{event};
	if (instant) {
		std::string words{instant->getWords()};
		auto result = instants.emplace(words, std::move(*instant));
		if (!result.second) {
			LOG_WARN(fmt::format("[Warning - Spells::registerInstantLuaEvent] Duplicate registered instant spell with words: {}", words));
		} else {
			instantsByName.emplace(std::string(result.first->second.getName()), &result.first->second);
		}
		return result.second;
	}

	return false;
}

bool Spells::registerRuneLuaEvent(RuneSpell* event)
{
	RuneSpell_ptr rune{event};
	if (rune) {
		uint16_t id = rune->getRuneItemId();
		auto result = runes.emplace(id, std::move(*rune));
		if (!result.second) {
			LOG_WARN(fmt::format("[Warning - Spells::registerRuneLuaEvent] Duplicate registered rune with id: {}", id));
		} else {
			runesByName.emplace(std::string(result.first->second.getName()), &result.first->second);
		}
		return result.second;
	}

	return false;
}

Spell* Spells::getSpellByName(std::string_view name)
{
	Spell* spell = getRuneSpellByName(name);
	if (!spell) {
		spell = getInstantSpellByName(name);
	}
	return spell;
}

RuneSpell* Spells::getRuneSpell(uint32_t id)
{
	auto it = runes.find(static_cast<uint16_t>(id));
	if (it == runes.end()) {
		return nullptr;
	}
	return &it->second;
}

RuneSpell* Spells::getRuneSpellByName(std::string_view name)
{
	auto it = runesByName.find(std::string(name));
	if (it != runesByName.end()) {
		return it->second;
	}
	return nullptr;
}

InstantSpell* Spells::getInstantSpell(std::string_view words)
{
	InstantSpell* result = nullptr;

	for (auto& it : instants) {
		auto instantSpellWords = it.second.getWords();
		size_t spellLen = instantSpellWords.length();
		if (caseInsensitiveStartsWith(words, instantSpellWords)) {
			if (!result || spellLen > result->getWords().size()) {
				result = &it.second;
				if (words.length() == spellLen) {
					break;
				}
			}
		}
	}

	if (result) {
		auto resultWords = result->getWords();
		if (words.length() > resultWords.length()) {
			if (!result->getHasParam()) {
				return nullptr;
			}

			size_t spellLen = resultWords.length();
			size_t paramLen = words.length() - spellLen;
			if (paramLen < 2 || words[spellLen] != ' ') {
				return nullptr;
			}
		}
		return result;
	}
	return nullptr;
}

InstantSpell* Spells::getInstantSpellByName(std::string_view name)
{
	auto it = instantsByName.find(std::string(name));
	if (it != instantsByName.end()) {
		return it->second;
	}
	return nullptr;
}

Position Spells::getCasterPosition(Creature* creature, Direction dir)
{
	return getNextPosition(dir, creature->getPosition());
}

CombatSpell::CombatSpell(Combat_ptr combat, bool needTarget, bool needDirection) :
    Event(&g_spells->getScriptInterface()), combat(combat), needDirection(needDirection), needTarget(needTarget)
{}

bool CombatSpell::loadScriptCombat()
{
	combat = g_luaEnvironment.getCombatObject(g_luaEnvironment.lastCombatId);
	return combat != nullptr;
}

bool CombatSpell::castSpell(Creature* creature)
{
	if (scripted) {
		LuaVariant var;

		if (needDirection) {
			var.setPosition(Spells::getCasterPosition(creature, creature->getDirection()));
		} else {
			var.setPosition(creature->getPosition());
		}

		return executeCastSpell(creature, var);
	}

	Position pos;
	if (needDirection) {
		pos = Spells::getCasterPosition(creature, creature->getDirection());
	} else {
		pos = creature->getPosition();
	}

	combat->doCombat(creature, pos);
	return true;
}

bool CombatSpell::castSpell(Creature* creature, Creature* target)
{
	if (scripted) {
		LuaVariant var;

		if (combat->hasArea()) {
			if (needTarget) {
				var.setPosition(target->getPosition());
			} else if (needDirection) {
				var.setPosition(Spells::getCasterPosition(creature, creature->getDirection()));
			} else {
				var.setPosition(creature->getPosition());
			}
		} else {
			var.setNumber(target->getID());
		}
		return executeCastSpell(creature, var);
	}

	if (combat->hasArea()) {
		if (needTarget) {
			combat->doCombat(creature, target->getPosition());
		} else {
			return castSpell(creature);
		}
	} else {
		combat->doCombat(creature, target);
	}
	return true;
}

bool CombatSpell::executeCastSpell(Creature* creature, const LuaVariant& var)
{
	// onCastSpell(creature, var)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CombatSpell::executeCastSpell] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);

	Lua::pushVariant(L, var);

	return scriptInterface->callFunction(2);
}

bool Spell::playerSpellCheck(Player* player) const
{
	if (player->hasFlag(PlayerFlag_CannotUseSpells)) {
		return false;
	}

	if (spellsIsDisabledMonkVocation(player)) {
		player->sendCancelMessage(RETURNVALUE_YOURVOCATIONCANNOTUSETHISSPELL);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if (!ConfigManager::getBoolean(ConfigManager::FAMILIAR_SYSTEM_ENABLED) && spellsIsFamiliarSpell(getName())) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if (player->hasFlag(PlayerFlag_IgnoreSpellCheck)) {
		return true;
	}

	if (!enabled) {
		return false;
	}

	if ((aggressive || pzLock) && (range < 1 || (range > 0 && !player->getAttackedCreatureShared())) &&
	    player->getSkull() == SKULL_BLACK) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return false;
	}

	if ((aggressive || pzLock) && player->hasCondition(CONDITION_PACIFIED)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if ((aggressive || pzLock) && !player->hasFlag(PlayerFlag_IgnoreProtectionZone) &&
	    player->getZone() == ZONE_PROTECTION) {
		player->sendCancelMessage(RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE);
		return false;
	}

	if (player->hasCondition(CONDITION_SPELLGROUPCOOLDOWN, group) ||
	    player->hasCondition(CONDITION_SPELLCOOLDOWN, spellId) ||
	    (secondaryGroup != SPELLGROUP_NONE && player->hasCondition(CONDITION_SPELLGROUPCOOLDOWN, secondaryGroup))) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);

		if (isInstant()) {
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		}

		return false;
	}

	if (player->getLevel() < level) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHLEVEL);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if (player->getMagicLevel() < magLevel) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHMAGICLEVEL);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if (player->getMana() < getManaCost(player) && !player->hasFlag(PlayerFlag_HasInfiniteMana)) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHMANA);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if (player->getSoul() < soul && !player->hasFlag(PlayerFlag_HasInfiniteSoul)) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHSOUL);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if (isInstant() && isLearnable()) {
		if (!player->hasLearnedInstantSpell(getName())) {
			player->sendCancelMessage(RETURNVALUE_YOUNEEDTOLEARNTHISSPELL);
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
			return false;
		}
	} else if (!hasVocationSpellMap(player->getVocationId())) {
		player->sendCancelMessage(RETURNVALUE_YOURVOCATIONCANNOTUSETHISSPELL);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if (needWeapon) {
		switch (player->getWeaponType()) {
			case WEAPON_SWORD:
			case WEAPON_CLUB:
			case WEAPON_AXE:
			case WEAPON_FIST:
				break;

			default: {
				player->sendCancelMessage(RETURNVALUE_YOUNEEDAWEAPONTOUSETHISSPELL);
				g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
				return false;
			}
		}
	}

	if (isPremium() && !player->isPremium()) {
		player->sendCancelMessage(RETURNVALUE_YOUNEEDPREMIUMACCOUNT);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	return true;
}

bool Spell::playerInstantSpellCheck(Player* player, const Position& toPos)
{
	if (toPos.x == 0xFFFF) {
		return true;
	}

	const Position& playerPos = player->getPosition();
	if (playerPos.z > toPos.z) {
		player->sendCancelMessage(RETURNVALUE_FIRSTGOUPSTAIRS);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	} else if (playerPos.z < toPos.z) {
		player->sendCancelMessage(RETURNVALUE_FIRSTGODOWNSTAIRS);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	Tile* tile = g_game.map.getTile(toPos);
	if (!tile) {
		auto newTile = std::make_unique<StaticTile>(toPos.x, toPos.y, toPos.z);
		tile = newTile.get();
		g_game.map.setTile(toPos, std::move(newTile));
	}

	if (isInstant()) {
		const InstantSpell* instantSpell = static_cast<const InstantSpell*>(this);
		if (instantSpell->getBlockWalls() && tile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
			player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
			return false;
		}
	}

	if (blockingCreature && tile->getBottomVisibleCreature(player) != nullptr) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if (blockingSolid && tile->hasFlag(TILESTATE_BLOCKSOLID)) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	return true;
}

bool Spell::playerRuneSpellCheck(Player* player, const Position& toPos)
{
	if (!playerSpellCheck(player)) {
		return false;
	}

	if (toPos.x == 0xFFFF) {
		return true;
	}

	const Position& playerPos = player->getPosition();
	if (playerPos.z > toPos.z) {
		player->sendCancelMessage(RETURNVALUE_FIRSTGOUPSTAIRS);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	} else if (playerPos.z < toPos.z) {
		player->sendCancelMessage(RETURNVALUE_FIRSTGODOWNSTAIRS);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	Tile* tile = g_game.map.getTile(toPos);
	if (!tile) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if (range != -1 && !g_game.canThrowObjectTo(playerPos, toPos, true, true, range, range)) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	ReturnValue ret = Combat::canDoCombat(player, tile, aggressive);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	const Creature* topVisibleCreature = tile->getBottomVisibleCreature(player);
	if (blockingCreature && topVisibleCreature) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	} else if (blockingSolid && tile->hasFlag(TILESTATE_BLOCKSOLID)) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if (needTarget && !topVisibleCreature) {
		player->sendCancelMessage(RETURNVALUE_CANONLYUSETHISRUNEONCREATURES);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		return false;
	}

	if (aggressive && needTarget && topVisibleCreature && player->isSecureModeEnabled()) {
		const Player* targetPlayer = topVisibleCreature->getPlayer();
		if (targetPlayer && targetPlayer != player && player->getSkullClient(targetPlayer) == SKULL_NONE &&
		    !Combat::isInPvpZone(player, targetPlayer)) {
			player->sendCancelMessage(RETURNVALUE_TURNSECUREMODETOATTACKUNMARKEDPLAYERS);
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
			return false;
		}
	}
	return true;
}

void Spell::getCombatDataAugment(const std::shared_ptr<Player>& player, CombatDamage& damage) const
{
	const bool augmentSystemEnabled = ConfigManager::getBoolean(ConfigManager::AUGMENT_SYSTEM_ENABLED);
	const bool wheelSystemEnabled = ConfigManager::getBoolean(ConfigManager::WHEEL_SYSTEM_ENABLED);
	if ((!augmentSystemEnabled && !wheelSystemEnabled) || !player) {
		return;
	}

	if (augmentSystemEnabled && !damage.instantSpellName.empty()) {
		for (const auto& item : player->getEquippedAugmentItems()) {
			for (const auto& augment : item->getAugmentsBySpellName(damage.instantSpellName)) {
				if (!augment || augment->value == 0) {
					continue;
				}

				switch (augment->type) {
					case Augment_t::BaseDamage:
					case Augment_t::BaseHealing:
					case Augment_t::IncreasedDamage:
					case Augment_t::PowerfulImpact:
					case Augment_t::StrongImpact: {
						const double percent = augment->value / 100.0;
						damage.primary.value = saturatingAdd(damage.primary.value, damage.primary.value * percent);
						damage.secondary.value = saturatingAdd(damage.secondary.value, damage.secondary.value * percent);
						break;
					}

					case Augment_t::LifeLeech:
						damage.lifeLeech = saturatingAdd(damage.lifeLeech, static_cast<int64_t>(augment->value) * 100);
						break;

					case Augment_t::ManaLeech:
						damage.manaLeech = saturatingAdd(damage.manaLeech, static_cast<int64_t>(augment->value) * 100);
						break;

					case Augment_t::CriticalExtraDamage:
						damage.criticalDamage =
						    saturatingAdd(damage.criticalDamage, static_cast<int64_t>(augment->value) * 100);
						break;

					case Augment_t::CriticalHitChance:
						damage.criticalChance =
						    saturatingAdd(damage.criticalChance, static_cast<int64_t>(augment->value) * 100);
						break;

					default:
						break;
				}
			}
		}
	}

	const auto applyBonus = [&damage](const ProficiencySpellAugmentBonus& bonus) {
		const int32_t percent = damage.primary.type == COMBAT_HEALING ? bonus.healingPercent : bonus.damagePercent;
		if (percent != 0) {
			const double multiplier = percent / 100.0;
			damage.primary.value = saturatingAdd(damage.primary.value, damage.primary.value * multiplier);
			damage.secondary.value = saturatingAdd(damage.secondary.value, damage.secondary.value * multiplier);
		}
		damage.lifeLeech = saturatingAdd(damage.lifeLeech, bonus.lifeLeech);
		damage.manaLeech = saturatingAdd(damage.manaLeech, bonus.manaLeech);
		damage.criticalDamage = saturatingAdd(damage.criticalDamage, bonus.criticalDamage);
		damage.criticalChance = saturatingAdd(damage.criticalChance, bonus.criticalChance);
	};

	if (augmentSystemEnabled) {
		applyBonus(player->getProficiencySpellAugmentBonus(getId()));
	}
	if (wheelSystemEnabled) {
		applyBonus(player->getWheelSpellAugmentBonus(getName()));
	}
}

int32_t Spell::calculateAugmentSpellCooldownReduction(const std::shared_ptr<Player>& player) const
{
	const bool augmentSystemEnabled = ConfigManager::getBoolean(ConfigManager::AUGMENT_SYSTEM_ENABLED);
	const bool wheelSystemEnabled = ConfigManager::getBoolean(ConfigManager::WHEEL_SYSTEM_ENABLED);
	if ((!augmentSystemEnabled && !wheelSystemEnabled) || !player) {
		return 0;
	}

	int32_t reduction = 0;
	if (augmentSystemEnabled) {
		for (const auto& item : player->getEquippedAugmentItemsByType(Augment_t::Cooldown)) {
			for (const auto& augment : item->getAugmentsBySpellNameAndType(getName(), Augment_t::Cooldown)) {
				if (augment && augment->value > 0) {
					reduction = saturatingAdd(reduction, augment->value);
				}
			}
		}
		reduction = saturatingAdd(reduction, player->getProficiencySpellAugmentBonus(getId()).cooldownReduction);
	}
	if (wheelSystemEnabled) {
		reduction = saturatingAdd(reduction, player->getWheelSpellAugmentBonus(getName()).cooldownReduction);
	}
	return reduction;
}

int32_t Spell::calculateAugmentSpellSecondaryGroupCooldownReduction(const std::shared_ptr<Player>& player) const
{
	const bool augmentSystemEnabled = ConfigManager::getBoolean(ConfigManager::AUGMENT_SYSTEM_ENABLED);
	const bool wheelSystemEnabled = ConfigManager::getBoolean(ConfigManager::WHEEL_SYSTEM_ENABLED);
	if ((!augmentSystemEnabled && !wheelSystemEnabled) || !player) {
		return 0;
	}

	int32_t reduction = 0;
	if (augmentSystemEnabled) {
		for (const auto& item : player->getEquippedAugmentItemsByType(Augment_t::SecondaryGroupCooldown)) {
			for (const auto& augment :
			     item->getAugmentsBySpellNameAndType(getName(), Augment_t::SecondaryGroupCooldown)) {
				if (augment && augment->value > 0) {
					reduction = saturatingAdd(reduction, augment->value);
				}
			}
		}
		reduction =
		    saturatingAdd(reduction, player->getProficiencySpellAugmentBonus(getId()).secondaryGroupCooldownReduction);
	}
	if (wheelSystemEnabled) {
		reduction =
		    saturatingAdd(reduction, player->getWheelSpellAugmentBonus(getName()).secondaryGroupCooldownReduction);
	}
	return reduction;
}

int32_t Spell::calculateAugmentSpellManaCostReduction(const Player* player) const
{
	const bool augmentSystemEnabled = ConfigManager::getBoolean(ConfigManager::AUGMENT_SYSTEM_ENABLED);
	const bool wheelSystemEnabled = ConfigManager::getBoolean(ConfigManager::WHEEL_SYSTEM_ENABLED);
	if ((!augmentSystemEnabled && !wheelSystemEnabled) || !player) {
		return 0;
	}

	int32_t reduction = 0;
	if (augmentSystemEnabled) {
		for (const auto& item : player->getEquippedAugmentItemsByType(Augment_t::ManaCost)) {
			for (const auto& augment : item->getAugmentsBySpellNameAndType(getName(), Augment_t::ManaCost)) {
				if (augment && augment->value > 0) {
					reduction = saturatingAdd(reduction, augment->value);
				}
			}
		}
		reduction = saturatingAdd(reduction, player->getProficiencySpellAugmentBonus(getId()).manaCostPercent);
	}
	if (wheelSystemEnabled) {
		reduction = saturatingAdd(reduction, player->getWheelSpellAugmentBonus(getName()).manaCostPercent);
	}
	return std::clamp(reduction, 0, 100);
}

void Spell::postCastSpell(Player* player, bool finishedCast /*= true*/, bool payCost /*= true*/) const
{
	if (finishedCast) {
        if (!player->hasFlag(PlayerFlag_HasNoExhaustion)) {
            int32_t momentumReduction = 0;
            int32_t augmentCooldownReduction = 0;
            int32_t augmentSecondaryGroupCooldownReduction = 0;

            if (const auto playerRef = std::dynamic_pointer_cast<Player>(player->weak_from_this().lock())) {
                augmentCooldownReduction = calculateAugmentSpellCooldownReduction(playerRef);
                augmentSecondaryGroupCooldownReduction =
                    calculateAugmentSpellSecondaryGroupCooldownReduction(playerRef);
            }

            Item* helmet = player->getInventoryItem(CONST_SLOT_HEAD);
            if (helmet && helmet->getTier() > 0) {
                double momentumChance = helmet->getMomentumChance();

                Item* boots = player->getInventoryItem(CONST_SLOT_FEET);
                if (boots && boots->getTier() > 0) {
                    double ampChance = boots->getMomentumChance() * 0.02;
                    momentumChance *= (1.0 + ampChance);
                }

                if (momentumChance > 0 && (normal_random(1, 10000) / 100.0) < momentumChance) {
                    momentumReduction = 2000;
                    g_game.addMagicEffect(player->getPosition(), CONST_ME_HOURGLASS, player->getInstanceID());
                }
            }

            if (cooldown > 0) {
                int32_t adjustedCooldown =
                    std::max<int32_t>(1000, static_cast<int32_t>(cooldown) - momentumReduction - augmentCooldownReduction);
                auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_SPELLCOOLDOWN,
                                                            adjustedCooldown, 0, false, spellId);
                player->addCondition(std::move(condition));
            }

            if (groupCooldown > 0) {
                int32_t adjustedGroupCooldown = std::max<int32_t>(1000, static_cast<int32_t>(groupCooldown) - momentumReduction);
                auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_SPELLGROUPCOOLDOWN,
                                                            adjustedGroupCooldown, 0, false, group);
                player->addCondition(std::move(condition));
            }

            if (secondaryGroupCooldown > 0) {
                int32_t adjustedSecondaryGroupCooldown =
                    std::max<int32_t>(1000, static_cast<int32_t>(secondaryGroupCooldown) - momentumReduction -
                                               augmentSecondaryGroupCooldownReduction);
                auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_SPELLGROUPCOOLDOWN,
                                                            adjustedSecondaryGroupCooldown, 0, false, secondaryGroup);
                player->addCondition(std::move(condition));
            }
        }

        if (aggressive) {
            player->addInFightTicks();
        }
    }

    if (payCost) { 
		Spell::postCastSpell(player, getManaCost(player), getSoulCost());
	}

	if (harmony) {
		player->setHarmony(0);
	}
}

void Spell::postCastSpell(Player* player, uint32_t manaCost, uint32_t soulCost)
{
	if (manaCost > 0) {
		player->addManaSpent(manaCost);
		player->changeMana(-static_cast<int32_t>(manaCost));
	}

	if (!player->hasFlag(PlayerFlag_HasInfiniteSoul)) {
		if (soulCost > 0) {
			player->changeSoul(-static_cast<int32_t>(soulCost));
		}
	}
}

uint32_t Spell::getManaCost(const Player* player) const
{
	uint32_t manaCost = 0;
	if (mana != 0) {
		manaCost = mana;
	} else if (manaPercent != 0) {
		uint32_t maxMana = player->getMaxMana();
		manaCost = (maxMana * manaPercent) / 100;
	}

	if (manaCost == 0 ||
	    (!ConfigManager::getBoolean(ConfigManager::AUGMENT_SYSTEM_ENABLED) &&
	     !ConfigManager::getBoolean(ConfigManager::WHEEL_SYSTEM_ENABLED))) {
		return manaCost;
	}

	const int32_t reduction = calculateAugmentSpellManaCostReduction(player);
	return static_cast<uint32_t>(std::lround(manaCost * ((100.0 - reduction) / 100.0)));
}

std::string_view InstantSpell::getScriptEventName() const { return "onCastSpell"; }

bool InstantSpell::playerCastInstant(Player* player, std::string& param)
{
	if (!playerSpellCheck(player)) {
		return false;
	}

	LuaVariant var;
	var.instantName = getName();

	if (selfTarget) {
		var.setNumber(player->getID());
	} else if (needTarget || casterTargetOrDirection) {
		std::shared_ptr<Creature> targetHolder;
		Creature* target = nullptr;
		bool useDirection = false;

		if (hasParam) {
			std::shared_ptr<Player> playerTarget;
			ReturnValue ret = g_game.getPlayerByNameWildcard(param, playerTarget);

			if (playerTarget && playerTarget->isAccessPlayer() && !player->isAccessPlayer()) {
				playerTarget.reset();
			}

			target = playerTarget.get();
			if (!target || target->isRemoved() || target->isDead()) {
				if (!casterTargetOrDirection) {
					if (cooldown > 0) {
						auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_SPELLCOOLDOWN,
						                                                  cooldown, 0, false, spellId);
						player->addCondition(std::move(condition));
					}

					if (groupCooldown > 0) {
						auto condition = Condition::createCondition(
						    CONDITIONID_DEFAULT, CONDITION_SPELLGROUPCOOLDOWN, groupCooldown, 0, false, group);
						player->addCondition(std::move(condition));
					}

					if (secondaryGroupCooldown > 0) {
						auto condition =
						    Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_SPELLGROUPCOOLDOWN,
						                               secondaryGroupCooldown, 0, false, secondaryGroup);
						player->addCondition(std::move(condition));
					}

					player->sendCancelMessage(ret);
					g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
					return false;
				}

				useDirection = true;
			}

			if (playerTarget) {
				param = playerTarget->getName();
			}
		} else {
			targetHolder = player->getAttackedCreatureShared();
			target = targetHolder.get();
			if (!target || target->isRemoved() || target->isDead()) {
				if (!casterTargetOrDirection) {
					player->sendCancelMessage(RETURNVALUE_YOUCANONLYUSEITONCREATURES);
					g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
					return false;
				}

				useDirection = true;
			}
		}

		if (!useDirection) {
			if (!canThrowSpell(player, target)) {
				player->sendCancelMessage(RETURNVALUE_CREATUREISNOTREACHABLE);
				g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
				return false;
			}

			var.setNumber(target->getID());
		} else {
			var.setPosition(Spells::getCasterPosition(player, player->getDirection()));

			if (!playerInstantSpellCheck(player, var.getPosition())) {
				return false;
			}
		}
	} else if (hasParam) {
		if (getHasPlayerNameParam()) {
			std::shared_ptr<Player> playerTarget;
			ReturnValue ret = g_game.getPlayerByNameWildcard(param, playerTarget);

			if (ret != RETURNVALUE_NOERROR) {
				if (cooldown > 0) {
					auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_SPELLCOOLDOWN,
					                                                  cooldown, 0, false, spellId);
					player->addCondition(std::move(condition));
				}

				if (groupCooldown > 0) {
					auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_SPELLGROUPCOOLDOWN,
					                                                  groupCooldown, 0, false, group);
					player->addCondition(std::move(condition));
				}

				if (secondaryGroupCooldown > 0) {
					auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_SPELLGROUPCOOLDOWN,
					                                                  secondaryGroupCooldown, 0, false, secondaryGroup);
					player->addCondition(std::move(condition));
				}

				player->sendCancelMessage(ret);
				g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
				return false;
			}

			if (playerTarget && (!playerTarget->isAccessPlayer() || player->isAccessPlayer())) {
				param = playerTarget->getName();
			}
		}

		var.setString(param);
	} else {
		if (needDirection) {
			var.setPosition(Spells::getCasterPosition(player, player->getDirection()));
		} else {
			var.setPosition(player->getPosition());
		}

		if (!playerInstantSpellCheck(player, var.getPosition())) {
			return false;
		}
	}

	bool result = internalCastSpell(player, var);
	if (result) {
		postCastSpell(player);
	}

	return result;
}

bool InstantSpell::canThrowSpell(const Creature* creature, const Creature* target) const
{
	if (!creature || !target) {
		return false;
	}

	const Position& fromPos = creature->getPosition();
	const Position& toPos = target->getPosition();
	if (fromPos.z != toPos.z ||
	    (range == -1 && !g_game.canThrowObjectTo(fromPos, toPos, checkLineOfSight, true, Map::maxClientViewportX - 1,
	                                             Map::maxClientViewportY - 1)) ||
	    (range != -1 && !g_game.canThrowObjectTo(fromPos, toPos, checkLineOfSight, true, range, range))) {
		return false;
	}
	return true;
}

bool InstantSpell::castSpell(Creature* creature)
{
	LuaVariant var;
	var.instantName = getName();

	if (casterTargetOrDirection) {
		auto target = creature->getAttackedCreatureShared();
		if (target && !target->isDead()) {
			if (!canThrowSpell(creature, target.get())) {
				return false;
			}

			var.setNumber(target->getID());
			return internalCastSpell(creature, var);
		}

		return false;
	} else if (needDirection) {
		var.setPosition(Spells::getCasterPosition(creature, creature->getDirection()));
	} else {
		var.setPosition(creature->getPosition());
	}

	return internalCastSpell(creature, var);
}

bool InstantSpell::castSpell(Creature* creature, Creature* target)
{
	if (needTarget) {
		LuaVariant var;
		var.instantName = getName();
		var.setNumber(target->getID());
		return internalCastSpell(creature, var);
	}
	return castSpell(creature);
}

bool InstantSpell::internalCastSpell(Creature* creature, const LuaVariant& var)
{
	return executeCastSpell(creature, var);
}

bool InstantSpell::executeCastSpell(Creature* creature, const LuaVariant& var)
{
	// onCastSpell(creature, var)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - InstantSpell::executeCastSpell] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);

	Lua::pushVariant(L, var);

	return scriptInterface->callFunction(2);
}

bool InstantSpell::canCast(const Player* player) const
{
	if (player->hasFlag(PlayerFlag_CannotUseSpells)) {
		return false;
	}

	if (spellsIsDisabledMonkVocation(player)) {
		return false;
	}

	if (!ConfigManager::getBoolean(ConfigManager::FAMILIAR_SYSTEM_ENABLED) && spellsIsFamiliarSpell(getName())) {
		return false;
	}

	if (player->hasFlag(PlayerFlag_IgnoreSpellCheck)) {
		return true;
	}

	if (isLearnable()) {
		if (player->hasLearnedInstantSpell(getName())) {
			return true;
		}
	} else if (hasVocationSpellMap(player->getVocationId())) {
		return true;
	}

	return false;
}

std::string_view RuneSpell::getScriptEventName() const { return "onCastSpell"; }

ReturnValue RuneSpell::canExecuteAction(const Player* player, const Position& toPos)
{
	if (player->hasFlag(PlayerFlag_CannotUseSpells)) {
		return RETURNVALUE_CANNOTUSETHISOBJECT;
	}

	ReturnValue ret = Action::canExecuteAction(player, toPos);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	if (toPos.x == 0xFFFF) {
		if (needTarget) {
			return RETURNVALUE_CANONLYUSETHISRUNEONCREATURES;
		} else if (!selfTarget) {
			return RETURNVALUE_NOTENOUGHROOM;
		}
	}

	return RETURNVALUE_NOERROR;
}

bool RuneSpell::executeUse(Player* player, const std::shared_ptr<Item>& item, const Position&, Thing* target,
                           const Position& toPosition, bool isHotkey)
{
	if (!playerRuneSpellCheck(player, toPosition)) {
		return false;
	}

	if (!scripted) {
		return false;
	}

	LuaVariant var;

	if (needTarget) {
		if (target == nullptr) {
			Tile* toTile = g_game.map.getTile(toPosition);
			if (toTile) {
				const Creature* visibleCreature = toTile->getBottomVisibleCreature(player);
				if (visibleCreature) {
					var.setNumber(visibleCreature->getID());
				}
			}
		} else {
			var.setNumber(target->getCreature()->getID());
		}
	} else {
		var.setPosition(toPosition);
	}

	if (!internalCastSpell(player, var, isHotkey)) {
		return false;
	}

	postCastSpell(player);

	if (var.isNumber()) {
		target = g_game.getCreatureByID(var.getNumber());
		if (getPzLock() && target) {
			if (Creature* targetCreature = target->getCreature()) {
				player->onAttackedCreature(targetCreature->shared_from_this());
			}
		}
	}

	if (hasCharges && item && getBoolean(ConfigManager::REMOVE_RUNE_CHARGES)) {
		int32_t newCount = std::max<int32_t>(0, item->getItemCount() - 1);
		g_game.transformItem(item.get(), item->getID(), newCount);
	}
	return true;
}

bool RuneSpell::castSpell(Creature* creature)
{
	LuaVariant var;
	var.setNumber(creature->getID());
	return internalCastSpell(creature, var, false);
}

bool RuneSpell::castSpell(Creature* creature, Creature* target)
{
	LuaVariant var;
	var.setNumber(target->getID());
	return internalCastSpell(creature, var, false);
}

bool RuneSpell::internalCastSpell(Creature* creature, const LuaVariant& var, bool isHotkey)
{
	bool result;
	if (scripted) {
		result = executeCastSpell(creature, var, isHotkey);
	} else {
		result = false;
	}
	return result;
}

bool RuneSpell::executeCastSpell(Creature* creature, const LuaVariant& var, bool isHotkey)
{
	// onCastSpell(creature, var, isHotkey)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - RuneSpell::executeCastSpell] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);

	Lua::pushVariant(L, var);

	Lua::pushBoolean(L, isHotkey);

	return scriptInterface->callFunction(3);
}
