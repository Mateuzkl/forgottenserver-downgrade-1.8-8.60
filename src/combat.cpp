// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "combat.h"

#include "configmanager.h"
#include "events.h"
#include "game.h"
#include "instance_utils.h"
#include "matrixarea.h"
#include "monster.h"
#include "scriptmanager.h"
#include "scheduler.h"
#include "spells.h"
#include "weapons.h"

extern Game g_game;

namespace {

std::shared_ptr<Creature> lockCreature(Creature* creature)
{
	return creature ? creature->weak_from_this().lock() : nullptr;
}

bool isPlayerControlledCreature(const Creature* creature)
{
	if (!creature) {
		return false;
	}

	if (creature->getPlayer()) {
		return true;
	}

	auto master = creature->getMaster();
	return master && master->getPlayer();
}

} // namespace

static int32_t getEffectiveMagicLevel(const Player* player, CombatType_t combatType)
{
	if (!player) {
		return 0;
	}

	int32_t magicLevel = static_cast<int32_t>(player->getMagicLevel()) + static_cast<int32_t>(player->getSpecialMagicLevel(combatType));
	return std::max<int32_t>(0, magicLevel);
}

std::vector<Tile*> getList(const MatrixArea& area, const Position& targetPos, const Direction dir)
{
	auto casterPos = getNextPosition(dir, targetPos);

	std::vector<Tile*> vec;

	auto& center = area.getCenter();

	Position tmpPos(targetPos.x - center.first, targetPos.y - center.second, targetPos.z);
	for (uint32_t row = 0; row < area.getRows(); ++row, ++tmpPos.y) {
		for (uint32_t col = 0; col < area.getCols(); ++col, ++tmpPos.x) {
			if (area(row, col)) {
				if (g_game.isSightClear(casterPos, tmpPos, true)) {
					Tile* tile = g_game.map.getTile(tmpPos);
					if (!tile) {
						auto newTile = std::make_unique<StaticTile>(tmpPos.x, tmpPos.y, tmpPos.z);
						tile = newTile.get();
						g_game.map.setTile(tmpPos, std::move(newTile));
					}
					vec.push_back(tile);
				}
			}
		}
		tmpPos.x -= static_cast<uint16_t>(area.getCols());
	}
	return vec;
}

std::vector<Tile*> getCombatArea(const Position& centerPos, const Position& targetPos, const AreaCombat* area)
{
	if (targetPos.z >= MAP_MAX_LAYERS) {
		return {};
	}

	if (area) {
		return getList(area->getArea(centerPos, targetPos), targetPos, getDirectionTo(targetPos, centerPos));
	}

	Tile* tile = g_game.map.getTile(targetPos);
	if (!tile) {
		auto newTile = std::make_unique<StaticTile>(targetPos.x, targetPos.y, targetPos.z);
		tile = newTile.get();
		g_game.map.setTile(targetPos, std::move(newTile));
	}
	return {tile};
}

CombatDamage Combat::getCombatDamage(Creature* creature, Creature* target, std::string_view instantSpellName) const
{
	CombatDamage damage;
	damage.origin = params.origin;
	damage.primary.type = params.combatType;
	damage.instantSpellName = instantSpellName;
	if (formulaType == COMBAT_FORMULA_DAMAGE) {
		damage.primary.value = normal_random(static_cast<int32_t>(mina), static_cast<int32_t>(maxa));
	} else if (creature) {
		int32_t min, max;
		if (creature->getCombatValues(min, max)) {
			damage.primary.value = normal_random(min, max);
		} else if (Player* player = creature->getPlayer()) {
			if (params.valueCallback) {
				params.valueCallback->getMinMaxValues(player, damage);
			} else if (formulaType == COMBAT_FORMULA_LEVELMAGIC) {
				int32_t levelFormula = static_cast<int32_t>(player->getLevel()) * 2 + getEffectiveMagicLevel(player, damage.primary.type) * 3;
				damage.primary.value =
				    normal_random(std::fma(levelFormula, mina, minb), std::fma(levelFormula, maxa, maxb));
			} else if (formulaType == COMBAT_FORMULA_SKILL) {
				Item* tool = player->getWeapon();
				const Weapon* weapon = g_weapons->getWeapon(tool);
				if (weapon) {
					damage.primary.value =
					    normal_random(minb, std::fma(weapon->getWeaponDamage(player, target, tool, true), maxa, maxb));
					damage.secondary.type = weapon->getElementType();
					damage.secondary.value = weapon->getElementDamage(player, target, tool);
				} else {
					damage.primary.value = normal_random(minb, maxb);
				}
			}
		}
	}

	if (creature && g_spells && !damage.instantSpellName.empty()) {
		if (const auto player = std::dynamic_pointer_cast<Player>(creature->weak_from_this().lock())) {
			if (const auto spell = g_spells->getInstantSpellByName(damage.instantSpellName)) {
				spell->getCombatDataAugment(player, damage);
			}

			if (ConfigManager::getBoolean(ConfigManager::WEAPON_PROFICIENCY_SYSTEM_ENABLED)) {
				if (damage.primary.type == COMBAT_HEALING) {
					player->weaponProficiency().applySkillSpellPercentage(damage, true);
				} else {
					player->weaponProficiency().applySkillSpellPercentage(damage);
				}
			}
		}
	}
	return damage;
}

void Combat::setArea(std::unique_ptr<AreaCombat> area) { this->area = std::move(area); }

CombatType_t Combat::ConditionToDamageType(ConditionType_t type)
{
	switch (type) {
		case CONDITION_FIRE:
			return COMBAT_FIREDAMAGE;

		case CONDITION_ENERGY:
			return COMBAT_ENERGYDAMAGE;

		case CONDITION_BLEEDING:
			return COMBAT_PHYSICALDAMAGE;

		case CONDITION_DROWN:
			return COMBAT_DROWNDAMAGE;

		case CONDITION_POISON:
			return COMBAT_EARTHDAMAGE;

		case CONDITION_FREEZING:
			return COMBAT_ICEDAMAGE;

		case CONDITION_DAZZLED:
			return COMBAT_HOLYDAMAGE;

		case CONDITION_CURSED:
			return COMBAT_DEATHDAMAGE;

		case CONDITION_AGONY:
			return COMBAT_AGONYDAMAGE;

		default:
			break;
	}

	return COMBAT_NONE;
}

ConditionType_t Combat::DamageToConditionType(CombatType_t type)
{
	switch (type) {
		case COMBAT_FIREDAMAGE:
			return CONDITION_FIRE;

		case COMBAT_ENERGYDAMAGE:
			return CONDITION_ENERGY;

		case COMBAT_DROWNDAMAGE:
			return CONDITION_DROWN;

		case COMBAT_EARTHDAMAGE:
			return CONDITION_POISON;

		case COMBAT_ICEDAMAGE:
			return CONDITION_FREEZING;

		case COMBAT_HOLYDAMAGE:
			return CONDITION_DAZZLED;

		case COMBAT_DEATHDAMAGE:
			return CONDITION_CURSED;

		case COMBAT_AGONYDAMAGE:
			return CONDITION_AGONY;

		case COMBAT_PHYSICALDAMAGE:
			return CONDITION_BLEEDING;

		default:
			return CONDITION_NONE;
	}
}

bool Combat::isPlayerCombat(const Creature* target)
{
	if (target->getPlayer()) {
		return true;
	}

	auto master = target->getMaster();
	if (target->isSummon() && master && master->getPlayer()) {
		return true;
	}

	return false;
}

ReturnValue Combat::canTargetCreature(Player* attacker, Creature* target)
{
	if (attacker == target) {
		return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
	}

	if (!attacker->hasFlag(PlayerFlag_IgnoreProtectionZone)) {
		// pz-zone
		if (attacker->getZone() == ZONE_PROTECTION) {
			return RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE;
		}

		if (target->getZone() == ZONE_PROTECTION) {
			return RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE;
		}

		// nopvp-zone
		if (isPlayerCombat(target)) {
			if (attacker->getZone() == ZONE_NOPVP) {
				return RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE;
			}

			if (target->getZone() == ZONE_NOPVP) {
				return RETURNVALUE_YOUMAYNOTATTACKAPERSONINPROTECTIONZONE;
			}
		}
	}

	if (attacker->hasFlag(PlayerFlag_CannotUseCombat) || !target->isAttackable()) {
		if (target->getPlayer()) {
			return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
		}
		return RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE;
	}

	if (target->getPlayer()) {
		if (isProtected(attacker, target->getPlayer())) {
			return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
		}

		if (attacker->isSecureModeEnabled() && !Combat::isInPvpZone(attacker, target) &&
		    attacker->getSkullClient(target->getPlayer()) == SKULL_NONE) {
			return RETURNVALUE_TURNSECUREMODETOATTACKUNMARKEDPLAYERS;
		}
	}

	return Combat::canDoCombat(attacker, target);
}

ReturnValue Combat::canDoCombat(Creature* caster, Tile* tile, bool aggressive)
{
	if (tile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
		return RETURNVALUE_NOTENOUGHROOM;
	}

	if (tile->hasFlag(TILESTATE_FLOORCHANGE)) {
		return RETURNVALUE_NOTENOUGHROOM;
	}

	if (tile->getTeleportItem()) {
		return RETURNVALUE_NOTENOUGHROOM;
	}

	if (caster) {
		const Position& casterPosition = caster->getPosition();
		const Position& tilePosition = tile->getPosition();
		if (casterPosition.z < tilePosition.z) {
			return RETURNVALUE_FIRSTGODOWNSTAIRS;
		} else if (casterPosition.z > tilePosition.z) {
			return RETURNVALUE_FIRSTGOUPSTAIRS;
		}

		if (const Player* player = caster->getPlayer()) {
			if (player->hasFlag(PlayerFlag_IgnoreProtectionZone)) {
				return RETURNVALUE_NOERROR;
			}
		}
	}

	// pz-zone
	if (aggressive && tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
		return RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE;
	}

	return g_events->eventCreatureOnAreaCombat(caster, tile, aggressive);
}

bool Combat::isInPvpZone(const Creature* attacker, const Creature* target)
{
	return attacker->getZone() == ZONE_PVP && target->getZone() == ZONE_PVP;
}

bool Combat::isProtected(const Player* attacker, const Player* target)
{
	const int64_t protectionLevel = getInteger(ConfigManager::PROTECTION_LEVEL);
	if (target->getLevel() < protectionLevel || attacker->getLevel() < protectionLevel) {
		return true;
	}

	if (!attacker->getVocation()->allowsPvp() || !target->getVocation()->allowsPvp()) {
		return true;
	}

	if (attacker->getSkull() == SKULL_BLACK && attacker->getSkullClient(target) == SKULL_NONE) {
		return true;
	}

	return false;
}

ReturnValue Combat::canDoCombat(Creature* attacker, Creature* target)
{
	if (!attacker) {
		return g_events->eventCreatureOnTargetCombat(attacker, target);
	}

	if (!InstanceUtils::canInteract(attacker, target)) {
		return RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE;
	}

	// protection time
	if (const Player* attackerPlayer = attacker->getPlayer()) {
		if (attackerPlayer->getProtectionTime() > 0) {
			Player* player = attacker->getPlayer();
			player->setProtectionTime(0);
		}
	}

	if (const Player* targetPlayer = target->getPlayer()) {
		if (targetPlayer->hasFlag(PlayerFlag_CannotBeAttacked)) {
			return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
		}

		if (const Player* attackerPlayer = attacker->getPlayer()) {
			if (attackerPlayer->hasFlag(PlayerFlag_CannotAttackPlayer)) {
				return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
			}

			if (isProtected(attackerPlayer, targetPlayer)) {
				return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
			}

			// nopvp-zone
			const Tile* targetPlayerTile = targetPlayer->getTile();
			if (targetPlayerTile->hasFlag(TILESTATE_NOPVPZONE)) {
				return RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE;
			} else if (attackerPlayer->getTile()->hasFlag(TILESTATE_NOPVPZONE) &&
			           !targetPlayerTile->hasFlag(TILESTATE_NOPVPZONE | TILESTATE_PROTECTIONZONE)) {
				return RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE;
			}
		}

		if (attacker->isSummon()) {
			auto attackerMaster = attacker->getMaster();
			if (const Player* masterAttackerPlayer = attackerMaster ? attackerMaster->getPlayer() : nullptr) {
				if (masterAttackerPlayer->hasFlag(PlayerFlag_CannotAttackPlayer)) {
					return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
				}

				if (targetPlayer->getTile()->hasFlag(TILESTATE_NOPVPZONE)) {
					return RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE;
				}

				if (isProtected(masterAttackerPlayer, targetPlayer)) {
					return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
				}
			}
		}
	} else if (target->getMonster()) {
		if (const Player* attackerPlayer = attacker->getPlayer()) {
			if (attackerPlayer->hasFlag(PlayerFlag_CannotAttackMonster)) {
				return RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE;
			}

			auto targetMaster = target->getMaster();
			if (target->isSummon() && targetMaster && targetMaster->getPlayer() && target->getZone() == ZONE_NOPVP) {
				return RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE;
			}
		} else if (const Monster* attackerMonster = attacker->getMonster()) {
			if (attackerMonster->isOpponent(target)) {
				return g_events->eventCreatureOnTargetCombat(attacker, target);
			}

			auto targetMaster = target->getMaster();

			if (!targetMaster || !targetMaster->getPlayer()) {
				auto attackerMaster = attacker->getMaster();

				if (!attackerMaster || !attackerMaster->getPlayer()) {
					return RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE;
				}
			}
		}
	}

	if (g_game.getWorldType() == WORLD_TYPE_NO_PVP) {
		if (isPlayerControlledCreature(attacker)) {
			if (target->getPlayer()) {
				if (!isInPvpZone(attacker, target)) {
					return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
				}
			}

			auto targetMaster = target->getMaster();
			if (target->isSummon() && targetMaster && targetMaster->getPlayer()) {
				if (!isInPvpZone(attacker, target)) {
					return RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE;
				}
			}
		}
	}
	return g_events->eventCreatureOnTargetCombat(attacker, target);
}

void Combat::setPlayerCombatValues(formulaType_t formulaType, double mina, double minb, double maxa, double maxb)
{
	this->formulaType = formulaType;
	this->mina = mina;
	this->minb = minb;
	this->maxa = maxa;
	this->maxb = maxb;
}

bool Combat::setParam(CombatParam_t param, uint32_t value)
{
	switch (param) {
		case COMBAT_PARAM_TYPE: {
			params.combatType = static_cast<CombatType_t>(value);
			return true;
		}

		case COMBAT_PARAM_EFFECT: {
			params.impactEffect = static_cast<uint16_t>(value);
			return true;
		}

		case COMBAT_PARAM_DISTANCEEFFECT: {
			params.distanceEffect = static_cast<uint8_t>(value);
			return true;
		}

		case COMBAT_PARAM_BLOCKARMOR: {
			params.blockedByArmor = (value != 0);
			return true;
		}

		case COMBAT_PARAM_BLOCKSHIELD: {
			params.blockedByShield = (value != 0);
			return true;
		}

		case COMBAT_PARAM_TARGETCASTERORTOPMOST: {
			params.targetCasterOrTopMost = (value != 0);
			return true;
		}

		case COMBAT_PARAM_CREATEITEM: {
			params.itemId = static_cast<uint16_t>(value);
			return true;
		}

		case COMBAT_PARAM_AGGRESSIVE: {
			params.aggressive = (value != 0);
			return true;
		}

		case COMBAT_PARAM_DISPEL: {
			params.dispelType = static_cast<ConditionType_t>(value);
			return true;
		}

		case COMBAT_PARAM_USECHARGES: {
			params.useCharges = (value != 0);
			return true;
		}

		case COMBAT_PARAM_CHAIN_EFFECT: {
			params.chainEffect = static_cast<uint8_t>(value);
			return true;
		}

		case COMBAT_PARAM_RESET_DAMAGE_MULTIPLIER: {
			float f;
			std::memcpy(&f, &value, sizeof(float));
			params.resetDamageMultiplier = f;
			return true;
		}
	}
	return false;
}

int32_t Combat::getParam(CombatParam_t param) const
{
	switch (param) {
		case COMBAT_PARAM_TYPE:
			return static_cast<int32_t>(params.combatType);

		case COMBAT_PARAM_EFFECT:
			return static_cast<int32_t>(params.impactEffect);

		case COMBAT_PARAM_DISTANCEEFFECT:
			return static_cast<int32_t>(params.distanceEffect);

		case COMBAT_PARAM_BLOCKARMOR:
			return params.blockedByArmor ? 1 : 0;

		case COMBAT_PARAM_BLOCKSHIELD:
			return params.blockedByShield ? 1 : 0;

		case COMBAT_PARAM_TARGETCASTERORTOPMOST:
			return params.targetCasterOrTopMost ? 1 : 0;

		case COMBAT_PARAM_CREATEITEM:
			return params.itemId;

		case COMBAT_PARAM_AGGRESSIVE:
			return params.aggressive ? 1 : 0;

		case COMBAT_PARAM_DISPEL:
			return static_cast<int32_t>(params.dispelType);

		case COMBAT_PARAM_USECHARGES:
			return params.useCharges ? 1 : 0;

		default:
			return std::numeric_limits<int32_t>().max();
	}
}

bool Combat::setCallback(CallBackParam key)
{
	switch (key) {
		case CallBackParam::LEVELMAGICVALUE: {
			params.valueCallback = std::make_unique<ValueCallback>(COMBAT_FORMULA_LEVELMAGIC);
			return true;
		}

		case CallBackParam::SKILLVALUE: {
			params.valueCallback = std::make_unique<ValueCallback>(COMBAT_FORMULA_SKILL);
			return true;
		}

		case CallBackParam::TARGETTILE: {
			params.tileCallback = std::make_unique<TileCallback>();
			return true;
		}

		case CallBackParam::TARGETCREATURE: {
			params.targetCallback = std::make_unique<TargetCallback>();
			return true;
		}

		case CallBackParam::CHAINVALUE: {
			params.chainCallback = std::make_unique<ChainCallback>();
			params.chainCallback->setFromLua(true);
			return true;
		}

		case CallBackParam::CHAINPICKER: {
			params.chainPickerCallback = std::make_unique<ChainPickerCallback>();
			return true;
		}
	}
	return false;
}

CallBack* Combat::getCallback(CallBackParam key)
{
	switch (key) {
		case CallBackParam::LEVELMAGICVALUE:
		case CallBackParam::SKILLVALUE: {
			return params.valueCallback.get();
		}

		case CallBackParam::TARGETTILE: {
			return params.tileCallback.get();
		}

		case CallBackParam::TARGETCREATURE: {
			return params.targetCallback.get();
		}

		case CallBackParam::CHAINVALUE: {
			return params.chainCallback.get();
		}

		case CallBackParam::CHAINPICKER: {
			return params.chainPickerCallback.get();
		}
	}
	return nullptr;
}

bool Combat::loadCallBack(CallBackParam key, LuaScriptInterface* scriptInterface)
{
	if (!setCallback(key)) {
		return false;
	}

	if (auto callback = getCallback(key)) {
		return callback->loadCallBack(scriptInterface);
	}

	return false;
}

void Combat::combatTileEffects(const SpectatorVec& spectators, Creature* caster, Tile* tile, const CombatParams& params)
{
	if (params.itemId != 0) {
		uint16_t itemId = params.itemId;
		switch (itemId) {
			case ITEM_FIREFIELD_PERSISTENT_FULL:
				itemId = ITEM_FIREFIELD_PVP_FULL;
				break;

			case ITEM_FIREFIELD_PERSISTENT_MEDIUM:
				itemId = ITEM_FIREFIELD_PVP_MEDIUM;
				break;

			case ITEM_FIREFIELD_PERSISTENT_SMALL:
				itemId = ITEM_FIREFIELD_PVP_SMALL;
				break;

			case ITEM_ENERGYFIELD_PERSISTENT:
				itemId = ITEM_ENERGYFIELD_PVP;
				break;

			case ITEM_POISONFIELD_PERSISTENT:
				itemId = ITEM_POISONFIELD_PVP;
				break;

			case ITEM_MAGICWALL_PERSISTENT:
				itemId = ITEM_MAGICWALL;
				break;

			case ITEM_WILDGROWTH_PERSISTENT:
				itemId = ITEM_WILDGROWTH;
				break;

			default:
				break;
		}

		if (caster) {
			Player* casterPlayer = caster->getPlayer();
			std::shared_ptr<Creature> casterMaster;
			if (!casterPlayer && caster->isSummon()) {
				casterMaster = caster->getMaster();
				casterPlayer = casterMaster ? casterMaster->getPlayer() : nullptr;
			}

			if (casterPlayer) {
				if (g_game.getWorldType() == WORLD_TYPE_NO_PVP || tile->hasFlag(TILESTATE_NOPVPZONE)) {
					if (itemId == ITEM_FIREFIELD_PVP_FULL) {
						itemId = ITEM_FIREFIELD_NOPVP;
					} else if (itemId == ITEM_POISONFIELD_PVP) {
						itemId = ITEM_POISONFIELD_NOPVP;
					} else if (itemId == ITEM_ENERGYFIELD_PVP) {
						itemId = ITEM_ENERGYFIELD_NOPVP;
					}
				} else if (itemId == ITEM_FIREFIELD_PVP_FULL || itemId == ITEM_POISONFIELD_PVP ||
				           itemId == ITEM_ENERGYFIELD_PVP) {
					casterPlayer->addInFightTicks();
				}
			}
		}

		auto itemPtr = Item::CreateItem(itemId);
		if (itemPtr) {
			if (caster) {
				itemPtr->setOwner(caster->getID());
				itemPtr->setInstanceID(caster->getInstanceID());
			}

			ReturnValue ret = g_game.internalAddItem(tile, itemPtr.get());
			if (ret == RETURNVALUE_NOERROR && !itemPtr->isRemoved()) {
				g_game.startDecay(itemPtr.get());

				auto field = std::dynamic_pointer_cast<MagicField>(itemPtr);
				if (field) {
					if (CreatureVector* creatures = tile->getCreatures()) {
						const CreatureVector creatureRefs = *creatures;
						for (const auto& creature : creatureRefs) {
							if (creature && !creature->isRemoved() && creature->getTile() == tile &&
							    creature->getInstanceID() == itemPtr->getInstanceID()) {
								field->onStepInField(creature);
							}
						}
					}
				}
			}
		}
	}

	if (params.tileCallback) {
		params.tileCallback->onTileCombat(caster, tile);
	}

	if (params.impactEffect != CONST_ME_NONE) {
		if (caster) {
			SpectatorVec filtered;
			for (const auto& s : spectators.players()) {
				Player *p = static_cast<Player*>(s.get());
				if (p->compareInstance(caster->getInstanceID())) {
					filtered.emplace_back(s);
				}
			}
			Game::addMagicEffect(filtered, tile->getPosition(), params.impactEffect);
		} else {
			Game::addMagicEffect(spectators, tile->getPosition(), params.impactEffect);
        }
    }
}

void Combat::postCombatEffects(Creature* caster, const Position& pos, const CombatParams& params)
{
	if (caster && params.distanceEffect != CONST_ANI_NONE) {
		addDistanceEffect(caster, caster->getPosition(), pos, params.distanceEffect);
	}
}

void Combat::addDistanceEffect(Creature* caster, const Position& fromPos, const Position& toPos, uint16_t effect)
{
	if (effect == CONST_ANI_WEAPONTYPE) {
		if (!caster) {
			return;
		}

		Player* player = caster->getPlayer();
		if (!player) {
			return;
		}

		switch (player->getWeaponType()) {
			case WEAPON_AXE:
				effect = CONST_ANI_WHIRLWINDAXE;
				break;
			case WEAPON_SWORD:
				effect = CONST_ANI_WHIRLWINDSWORD;
				break;
			case WEAPON_CLUB:
				effect = CONST_ANI_WHIRLWINDCLUB;
				break;
			case WEAPON_FIST:
				effect = CONST_ANI_LARGEROCK;
				break;
			default:
				effect = CONST_ANI_NONE;
				break;
		}
	}

	if (effect != CONST_ANI_NONE) {
		if (caster) {
			SpectatorVec spectators, toPosSpectators;
			g_game.map.getSpectators(spectators, fromPos, true, true);
			g_game.map.getSpectators(toPosSpectators, toPos, true, true);
			spectators.addSpectators(toPosSpectators);
			spectators.partitionByType();
			SpectatorVec filtered;
			for (const auto& s : spectators.players()) {
				Player *p = static_cast<Player*>(s.get());
				if (p->compareInstance(caster->getInstanceID())) {
					filtered.emplace_back(s);
				}
			}
			g_game.addDistanceEffect(filtered, fromPos, toPos, effect);
		} else {
			g_game.addDistanceEffect(fromPos, toPos, effect);
        }
    }
}

void Combat::doCombat(Creature* caster, Creature* target, std::string_view instantSpellName) const
{
	if (params.chainCallback) {
		if (doCombatChain(caster, target, params.aggressive, std::string(instantSpellName))) {
			return;
		}
	}

	// target combat callback function
	if (params.combatType != COMBAT_NONE) {
		CombatDamage damage = getCombatDamage(caster, target, instantSpellName);

		bool canCombat =
		    !params.aggressive || (caster != target && Combat::canDoCombat(caster, target) == RETURNVALUE_NOERROR);
		if ((caster == target || canCombat) && params.impactEffect != CONST_ME_NONE) {
			SpectatorVec spectators;
			g_game.map.getSpectators(spectators, target->getPosition(), true, true);
			SpectatorVec filtered;
			for (const auto& s : spectators.players()) {
				Player *p = static_cast<Player*>(s.get());
				if (p->compareInstance(target->getInstanceID())) {
					filtered.emplace_back(s);
				}
			}
			Game::addMagicEffect(filtered, target->getPosition(), params.impactEffect);
        }

		if (canCombat) {
			doTargetCombat(caster, target, damage, params);
		}
	} else {
		if (!params.aggressive || (caster != target && Combat::canDoCombat(caster, target) == RETURNVALUE_NOERROR)) {
			SpectatorVec spectators;
			g_game.map.getSpectators(spectators, target->getPosition(), true, true);

			if (params.origin != ORIGIN_MELEE) {
				for (const auto& condition : params.conditionList) {
					if (caster == target || !target->isImmune(condition->getType())) {
						auto conditionCopy = condition->clone();
						conditionCopy->setParam(CONDITION_PARAM_OWNER, caster->getID());
						conditionCopy->setPositionParam(CONDITION_PARAM_CASTER_POSITION, caster->getPosition());
						target->addCombatCondition(std::move(conditionCopy));
					}
				}
			}

			if (params.dispelType == CONDITION_PARALYZE) {
				target->removeCondition(CONDITION_PARALYZE);
			} else {
				target->removeCombatCondition(params.dispelType);
			}

			combatTileEffects(spectators, caster, target->getTile(), params);

			if (params.targetCallback) {
				params.targetCallback->onTargetCombat(caster, target);
			}

			/*
			if (params.impactEffect != CONST_ME_NONE) {
			    g_game.addMagicEffect(target->getPosition(), params.impactEffect);
			}
			*/

			if (caster && params.distanceEffect != CONST_ANI_NONE) {
				addDistanceEffect(caster, caster->getPosition(), target->getPosition(), params.distanceEffect);
			}
		}
	}
}

void Combat::doCombat(Creature* caster, const Position& position, std::string_view instantSpellName) const
{
	if (params.chainCallback) {
		if (doCombatChain(caster, nullptr, params.aggressive, std::string(instantSpellName))) {
			return;
		}
	}

	// area combat callback function
	if (params.combatType != COMBAT_NONE) {
		CombatDamage damage = getCombatDamage(caster, nullptr, instantSpellName);
		doAreaCombat(caster, position, area.get(), damage, params);
	} else {
		auto tiles = caster ? getCombatArea(caster->getPosition(), position, area.get())
		                    : getCombatArea(position, position, area.get());

		SpectatorVec spectators;
		int32_t maxX = 0;
		int32_t maxY = 0;

		// calculate the max viewable range
		for (Tile* tile : tiles) {
			const Position& tilePos = tile->getPosition();

			maxX = std::max(maxX, tilePos.getDistanceX(position));
			maxY = std::max(maxY, tilePos.getDistanceY(position));
		}

		const int32_t rangeX = maxX + Map::maxViewportX;
		const int32_t rangeY = maxY + Map::maxViewportY;
		g_game.map.getSpectators(spectators, position, true, true, rangeX, rangeX, rangeY, rangeY);

		postCombatEffects(caster, position, params);

		for (Tile* tile : tiles) {
			if (canDoCombat(caster, tile, params.aggressive) != RETURNVALUE_NOERROR) {
				continue;
			}

			combatTileEffects(spectators, caster, tile, params);

			if (CreatureVector* creatures = tile->getCreatures()) {
				const Creature* topCreature = tile->getTopCreature();
				for (const auto& creature : *creatures) {
					if (params.targetCasterOrTopMost) {
						if (caster && caster->getTile() == tile) {
							if (creature.get() != caster) {
								continue;
							}
						} else if (creature.get() != topCreature) {
							continue;
						}
					}

					if (!params.aggressive ||
					    (caster != creature.get() && Combat::canDoCombat(caster, creature.get()) == RETURNVALUE_NOERROR)) {
						for (const auto& condition : params.conditionList) {
							if (caster == creature.get() || !creature->isImmune(condition->getType())) {
								auto conditionCopy = condition->clone();
								if (caster) {
									conditionCopy->setParam(CONDITION_PARAM_OWNER, caster->getID());
									conditionCopy->setPositionParam(CONDITION_PARAM_CASTER_POSITION, caster->getPosition());
								}

								// TODO: infight condition until all aggressive conditions has ended
								creature->addCombatCondition(std::move(conditionCopy));
							}
						}
					}

					if (params.dispelType == CONDITION_PARALYZE) {
						creature->removeCondition(CONDITION_PARALYZE);
					} else {
						creature->removeCombatCondition(params.dispelType);
					}

					if (params.targetCallback) {
						params.targetCallback->onTargetCombat(caster, creature.get());
					}

					if (params.targetCasterOrTopMost) {
						break;
					}
				}
			}
		}
	}
}

void Combat::doTargetCombat(Creature* caster, Creature* target, CombatDamage& damage, const CombatParams& params)
{
	if (caster && target && params.distanceEffect != CONST_ANI_NONE) {
		addDistanceEffect(caster, caster->getPosition(), target->getPosition(), params.distanceEffect);
	}

	Player* casterPlayer = caster ? caster->getPlayer() : nullptr;
	const bool wpEnabled = casterPlayer && ConfigManager::getBoolean(ConfigManager::WEAPON_PROFICIENCY_SYSTEM_ENABLED);

	bool success = false;
	if (damage.primary.type != COMBAT_MANADRAIN) {
		if (g_game.combatBlockHit(damage, caster, target, params.blockedByShield, params.blockedByArmor,
		                          params.itemId != 0, params.ignoreResistances)) {
			return;
		}

		if (target && target->getPlayer() &&
				damage.primary.type != COMBAT_HEALING &&
				damage.origin != ORIGIN_CONDITION) {
			Player *targetPlayer = target->getPlayer();
			Item *armor = targetPlayer->getInventoryItem(CONST_SLOT_ARMOR);
			if (armor && armor->getTier() > 0) {
				double dodgeChance = armor->getDodgeChance();
				Item *boots = targetPlayer->getInventoryItem(CONST_SLOT_FEET);
				if (boots && boots->getTier() > 0) {
					double ampChance = boots->getMomentumChance()* 0.02;
					dodgeChance *= (1.0 + ampChance);
				}
				if (dodgeChance > 0 && (normal_random(1, 10000) / 100.0) < dodgeChance) {
					damage.primary.value = 0;
					damage.secondary.value = 0;
					damage.blockType = BLOCK_DODGE;
					damage.dodge = true;
					SpectatorVec dodgeSpectators;
					g_game.map.getSpectators(dodgeSpectators, target->getPosition(), true, true);
					InstanceUtils::sendMagicEffectToInstance(dodgeSpectators,
						target->getPosition(), CONST_ME_DODGE, target->getInstanceID());
					return;
				}
			}
		}

		if (casterPlayer) {
			Player* targetPlayer = target ? target->getPlayer() : nullptr;

			if (wpEnabled) {
				casterPlayer->weaponProficiency().applySkillAutoAttackPercentage(damage);
			}

			if (targetPlayer && casterPlayer != targetPlayer && targetPlayer->getSkull() != SKULL_BLACK &&
			    damage.primary.type != COMBAT_HEALING) {
				damage.primary.value /= 2;
				damage.secondary.value /= 2;
			}

			if (!damage.critical && damage.primary.type != COMBAT_HEALING && damage.origin != ORIGIN_CONDITION) {
				if (wpEnabled) {
					casterPlayer->weaponProficiency().applyAutoAttackCritical(damage);
					casterPlayer->weaponProficiency().applyGeneralCritical(damage);
					casterPlayer->weaponProficiency().applyRunesCritical(damage, params.aggressive);
					casterPlayer->weaponProficiency().applyElementCritical(damage);
				}

				int32_t chance = std::clamp<int32_t>(
				    static_cast<int32_t>(casterPlayer->getSpecialSkill(SPECIALSKILL_CRITICALHITCHANCE)) +
				        damage.criticalChance,
				    0, 10000);
				int32_t skill = std::max<int32_t>(
				    0, static_cast<int32_t>(casterPlayer->getSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT)) +
				           damage.criticalDamage);
				if (skill == 0 && chance > 0) {
					skill = 5000;
				}
				if (chance > 0 && skill > 0 && uniform_random(1, 10000) <= chance) {
					damage.primary.value += std::round(damage.primary.value * (skill / 10000.));
					damage.secondary.value += std::round(damage.secondary.value * (skill / 10000.));
					damage.critical = true;
				}
			}

			if (!damage.fatal && damage.primary.type != COMBAT_HEALING && damage.origin != ORIGIN_CONDITION) {
				Item* weapon = casterPlayer->getWeapon();
				if (weapon && weapon->getTier() > 0) {
					double fatalChance = weapon->getFatalChance();
					Item* boots = casterPlayer->getInventoryItem(CONST_SLOT_FEET);
					if (boots && boots->getTier() > 0) {
						double ampChance = boots->getMomentumChance() * 0.02;
						fatalChance *= (1.0 + ampChance);
					}
					if (fatalChance > 0 && (normal_random(1, 10000) / 100.0) < fatalChance) {
						int32_t fatalPrimary = std::round(damage.primary.value * 0.5);
						int32_t fatalSecondary = std::round(damage.secondary.value * 0.5);
						damage.primary.value += fatalPrimary;
						damage.secondary.value += fatalSecondary;
						damage.fatal = true;
					}
				}
			}
		}

		if (params.resetDamageMultiplier >= 0.0f) {
			damage.spellResetMultiplier = params.resetDamageMultiplier;
		}

		if (casterPlayer && wpEnabled) {
			if (Monster* targetMonster = target ? target->getMonster() : nullptr) {
				auto monsterRef = targetMonster->weak_from_this().lock();
				if (monsterRef) {
					casterPlayer->weaponProficiency().applyBestiaryDamage(damage, std::static_pointer_cast<Monster>(monsterRef));
					casterPlayer->weaponProficiency().applyPowerfulFoeDamage(damage, std::static_pointer_cast<Monster>(monsterRef));
				}
			}
		}

		success = g_game.combatChangeHealth(caster, target, damage);
	} else {
		success = g_game.combatChangeMana(caster, target, damage);
	}

	if (success) {
		if (casterPlayer && wpEnabled && damage.primary.type != COMBAT_HEALING && damage.primary.type != COMBAT_MANADRAIN) {
			casterPlayer->weaponProficiency().applyOn(WeaponProficiencyHealth_t::LIFE, WeaponProficiencyGain_t::HIT);
			casterPlayer->weaponProficiency().applyOn(WeaponProficiencyHealth_t::MANA, WeaponProficiencyGain_t::HIT);
		}

		if (casterPlayer && wpEnabled && target && target->getHealth() <= 0) {
			casterPlayer->weaponProficiency().applyOn(WeaponProficiencyHealth_t::LIFE, WeaponProficiencyGain_t::KILL);
			casterPlayer->weaponProficiency().applyOn(WeaponProficiencyHealth_t::MANA, WeaponProficiencyGain_t::KILL);
		}

		if (damage.blockType == BLOCK_NONE || damage.blockType == BLOCK_ARMOR) {
			for (const auto& condition : params.conditionList) {
				if (caster == target || (target && !target->isImmune(condition->getType()))) {
					auto conditionCopy = condition->clone();
					if (caster) {
						conditionCopy->setParam(CONDITION_PARAM_OWNER, caster->getID());
						conditionCopy->setPositionParam(CONDITION_PARAM_CASTER_POSITION, caster->getPosition());
					}

					// TODO: infight condition until all aggressive conditions has ended
					if (target) {
						target->addCombatCondition(std::move(conditionCopy));
					}
				}
			}
		}

		if (damage.critical && target) {
			SpectatorVec critSpectators;
			g_game.map.getSpectators(critSpectators, target->getPosition(), true, true);
			InstanceUtils::sendMagicEffectToInstance(
				critSpectators, target->getPosition(), CONST_ME_CRITICAL_DAMAGE, target->getInstanceID());
		}

		if (damage.fatal && target) {
			SpectatorVec fatalSpectators;
			g_game.map.getSpectators(fatalSpectators, target->getPosition(), true, true);
			InstanceUtils::sendMagicEffectToInstance(fatalSpectators, target->getPosition(), CONST_ME_FATAL, target->getInstanceID());
		}

		if (target && target->getPlayer() &&
				damage.primary.type != COMBAT_HEALING &&
				damage.origin != ORIGIN_CONDITION) {
			Player *targetPlayer = target->getPlayer();
			if (!targetPlayer->isAvatarActive()) {
				Item *legs = targetPlayer->getInventoryItem(CONST_SLOT_LEGS);
				if (legs && legs->getTier() > 0) {
					double transChance = legs->getTranscendenceChance();
					Item *boots = targetPlayer->getInventoryItem(CONST_SLOT_FEET);
					if (boots && boots->getTier() > 0) {
						double ampChance = boots->getMomentumChance()* 0.02;
						transChance *= (1.0 + ampChance);
					}
					if (transChance > 0 &&
							(normal_random(1, 10000) / 100.0) < transChance) {
						uint16_t avatarLookType = 0;
						int32_t avatarDuration = 15000;
						if (targetPlayer->isKnight()) {
							avatarLookType = AVATAR_LOOKTYPE_STEEL;
						} else if (targetPlayer->isPaladin()) {
							avatarLookType = AVATAR_LOOKTYPE_LIGHT;
						} else if (targetPlayer->isSorcerer()) {
							avatarLookType = AVATAR_LOOKTYPE_STORM;
						} else if (targetPlayer->isDruid()) {
							avatarLookType = AVATAR_LOOKTYPE_NATURE;
						} else if (targetPlayer->isMonk()) {
							avatarLookType = AVATAR_LOOKTYPE_BALANCE;
							avatarDuration = 30000;
						}

						if (avatarLookType != 0) {
							auto outfitCondPtr = Condition::createCondition(CONDITIONID_COMBAT,
								CONDITION_OUTFIT, avatarDuration);
							Outfit_t avatarOutfit;
							avatarOutfit.lookType = avatarLookType;
							static_cast<ConditionOutfit*>(outfitCondPtr.get())->setOutfit(avatarOutfit);
							targetPlayer->addCondition(std::move(outfitCondPtr));

							auto attrCond = Condition::createCondition(
									CONDITIONID_COMBAT, CONDITION_ATTRIBUTES, avatarDuration, 0,
									false, 9999);
							attrCond->setParam(
									CONDITION_PARAM_SPECIALSKILL_CRITICALHITCHANCE, 1000);
							attrCond->setParam(
									CONDITION_PARAM_SPECIALSKILL_CRITICALHITAMOUNT, 5000);
							targetPlayer->addCondition(std::move(attrCond));

							targetPlayer->setStorageValue(
									AVATAR_TIMER_STORAGE,
									static_cast<int64_t>(OTSYS_TIME()) + avatarDuration);

							SpectatorVec avatarSpecs;
							g_game.map.getSpectators(avatarSpecs, target->getPosition(),
																			 true, true);
							InstanceUtils::sendMagicEffectToInstance(
									avatarSpecs, target->getPosition(), CONST_ME_AVATAR_APPEAR,
									target->getInstanceID());
						}
					}
				}
			}
		}

		if (!damage.leeched && damage.primary.type != COMBAT_HEALING && casterPlayer && target != caster &&
		    damage.origin != ORIGIN_CONDITION) {
			CombatDamage leechCombat;
			leechCombat.origin = ORIGIN_NONE;
			leechCombat.leeched = true;

			int32_t totalDamage = std::abs(damage.primary.value + damage.secondary.value);

			if (casterPlayer->getHealth() < casterPlayer->getMaxHealth()) {
				uint16_t chance = casterPlayer->getSpecialSkill(SPECIALSKILL_LIFELEECHCHANCE);
				int32_t skill = std::max<int32_t>(
				    0, static_cast<int32_t>(casterPlayer->getSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT)) +
				           damage.lifeLeech);
				leechCombat.primary.type = COMBAT_HEALING;

				if (skill > 0 && chance == 0) { chance = 10000; }
				if (chance > 0 && skill > 0 && uniform_random(1, 10000) <= chance) {
					leechCombat.primary.value = std::round(totalDamage * (skill / 10000.));
					g_game.combatChangeHealth(nullptr, casterPlayer, leechCombat);
					casterPlayer->sendMagicEffect(casterPlayer->getPosition(), CONST_ME_MAGIC_RED);
				}
			}

			if (casterPlayer->getMana() < casterPlayer->getMaxMana()) {
				uint16_t chance = casterPlayer->getSpecialSkill(SPECIALSKILL_MANALEECHCHANCE);
				int32_t skill = std::max<int32_t>(
				    0, static_cast<int32_t>(casterPlayer->getSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT)) +
				           damage.manaLeech);

				if (skill > 0 && chance == 0) { chance = 10000; }
				if (chance > 0 && skill > 0 && uniform_random(1, 10000) <= chance) {
					leechCombat.primary.value = std::round(totalDamage * (skill / 10000.));
					g_game.combatChangeMana(nullptr, casterPlayer, leechCombat);
					casterPlayer->sendMagicEffect(casterPlayer->getPosition(), CONST_ME_MAGIC_BLUE);
				}
			}
		}

		if (params.dispelType == CONDITION_PARALYZE) {
			target->removeCondition(CONDITION_PARALYZE);
		} else {
			target->removeCombatCondition(params.dispelType);
		}
	}

	if (params.targetCallback) {
		params.targetCallback->onTargetCombat(caster, target);
	}
}

void Combat::doAreaCombat(Creature* caster, const Position& position, const AreaCombat* area, CombatDamage& damage,
                          const CombatParams& params)
{
	auto tiles =
	    caster ? getCombatArea(caster->getPosition(), position, area) : getCombatArea(position, position, area);

	Player* casterPlayer = caster ? caster->getPlayer() : nullptr;
	const bool wpEnabled = casterPlayer && ConfigManager::getBoolean(ConfigManager::WEAPON_PROFICIENCY_SYSTEM_ENABLED);

	if (wpEnabled) {
		casterPlayer->weaponProficiency().applySkillAutoAttackPercentage(damage);
		if (!damage.instantSpellName.empty()) {
			if (damage.primary.type == COMBAT_HEALING) {
				casterPlayer->weaponProficiency().applySkillSpellPercentage(damage, true);
			} else {
				casterPlayer->weaponProficiency().applySkillSpellPercentage(damage);
			}
		}
	}

	int32_t criticalPrimary = 0;
	int32_t criticalSecondary = 0;
	if (!damage.critical && damage.primary.type != COMBAT_HEALING && casterPlayer &&
	    damage.origin != ORIGIN_CONDITION) {
		if (wpEnabled) {
			casterPlayer->weaponProficiency().applyAutoAttackCritical(damage);
			casterPlayer->weaponProficiency().applyGeneralCritical(damage);
			casterPlayer->weaponProficiency().applyRunesCritical(damage, params.aggressive);
			casterPlayer->weaponProficiency().applyElementCritical(damage);
		}

		int32_t chance = std::clamp<int32_t>(
		    static_cast<int32_t>(casterPlayer->getSpecialSkill(SPECIALSKILL_CRITICALHITCHANCE)) +
		        damage.criticalChance,
		    0, 10000);
		int32_t skill = std::max<int32_t>(
		    0, static_cast<int32_t>(casterPlayer->getSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT)) +
		           damage.criticalDamage);

		if (skill == 0 && chance > 0) { skill = 5000; }
		if (chance > 0 && skill > 0 && uniform_random(1, 10000) <= chance) {
			criticalPrimary = std::round(damage.primary.value * (skill / 10000.));
			criticalSecondary = std::round(damage.secondary.value * (skill / 10000.));
			damage.critical = true;
		}
	}

	int32_t maxX = 0;
	int32_t maxY = 0;

	// calculate the max viewable range
	for (Tile* tile : tiles) {
		const Position& tilePos = tile->getPosition();

		maxX = std::max(maxX, tilePos.getDistanceX(position));
		maxY = std::max(maxY, tilePos.getDistanceY(position));
	}

	const int32_t rangeX = maxX + Map::maxViewportX;
	const int32_t rangeY = maxY + Map::maxViewportY;

	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, position, true, true, rangeX, rangeX, rangeY, rangeY);

	postCombatEffects(caster, position, params);

	std::vector<std::shared_ptr<Creature>> toDamageCreatures;
	toDamageCreatures.reserve(100);

	for (Tile* tile : tiles) {
		if (canDoCombat(caster, tile, params.aggressive) != RETURNVALUE_NOERROR) {
			continue;
		}

		combatTileEffects(spectators, caster, tile, params);

		if (CreatureVector* creatures = tile->getCreatures()) {
			const Creature* topCreature = tile->getTopCreature();
			for (const auto& creature : *creatures) {
				if (params.targetCasterOrTopMost) {
					if (caster && caster->getTile() == tile) {
						if (creature.get() != caster) {
							continue;
						}
					} else if (creature.get() != topCreature) {
						continue;
					}
				}

				if (!params.aggressive ||
				    (caster != creature.get() && Combat::canDoCombat(caster, creature.get()) == RETURNVALUE_NOERROR)) {
					toDamageCreatures.push_back(creature);

					if (params.targetCasterOrTopMost) {
						break;
					}
				}
			}
		}
	}

	CombatDamage leechCombat;
	leechCombat.origin = ORIGIN_NONE;
	leechCombat.leeched = true;

	for (const auto& creature : toDamageCreatures) {
		CombatDamage damageCopy = damage; // we cannot avoid copying here, because we don't know if it's player combat
		                                  // or not, so we can't modify the initial damage.
		bool playerCombatReduced = false;
		if ((damageCopy.primary.value < 0 || damageCopy.secondary.value < 0) && caster) {
			Player* targetPlayer = creature->getPlayer();
			if (casterPlayer && targetPlayer && casterPlayer != targetPlayer &&
			    targetPlayer->getSkull() != SKULL_BLACK) {
				damageCopy.primary.value /= 2;
				damageCopy.secondary.value /= 2;
				playerCombatReduced = true;
			}
		}

		if (damageCopy.critical) {
			damageCopy.primary.value += playerCombatReduced ? criticalPrimary / 2 : criticalPrimary;
			damageCopy.secondary.value += playerCombatReduced ? criticalSecondary / 2 : criticalSecondary;
			SpectatorVec critSpectators;
			g_game.map.getSpectators(critSpectators, creature->getPosition(), true, true);
			InstanceUtils::sendMagicEffectToInstance(
				critSpectators, creature->getPosition(), CONST_ME_CRITICAL_DAMAGE, creature->getInstanceID());
			  }

		bool success = false;
		if (damageCopy.primary.type != COMBAT_MANADRAIN) {
			if (g_game.combatBlockHit(damageCopy, caster, creature.get(), params.blockedByShield, params.blockedByArmor,
			                          params.itemId != 0, params.ignoreResistances)) {
				continue;
			}
			if (params.resetDamageMultiplier >= 0.0f) {
				damageCopy.spellResetMultiplier = params.resetDamageMultiplier;
			}

			if (casterPlayer && wpEnabled) {
				if (Monster* targetMonster = creature->getMonster()) {
					auto monsterRef = targetMonster->weak_from_this().lock();
					if (monsterRef) {
						casterPlayer->weaponProficiency().applyBestiaryDamage(damageCopy, std::static_pointer_cast<Monster>(monsterRef));
						casterPlayer->weaponProficiency().applyPowerfulFoeDamage(damageCopy, std::static_pointer_cast<Monster>(monsterRef));
					}
				}
			}

			success = g_game.combatChangeHealth(caster, creature.get(), damageCopy);
		} else {
			success = g_game.combatChangeMana(caster, creature.get(), damageCopy);
		}

		if (success) {
			if (casterPlayer && wpEnabled && damageCopy.primary.type != COMBAT_HEALING && damageCopy.primary.type != COMBAT_MANADRAIN) {
				casterPlayer->weaponProficiency().applyOn(WeaponProficiencyHealth_t::LIFE, WeaponProficiencyGain_t::HIT);
				casterPlayer->weaponProficiency().applyOn(WeaponProficiencyHealth_t::MANA, WeaponProficiencyGain_t::HIT);
			}

			if (casterPlayer && wpEnabled && creature && creature->getHealth() <= 0) {
				casterPlayer->weaponProficiency().applyOn(WeaponProficiencyHealth_t::LIFE, WeaponProficiencyGain_t::KILL);
				casterPlayer->weaponProficiency().applyOn(WeaponProficiencyHealth_t::MANA, WeaponProficiencyGain_t::KILL);
			}

			if (damage.blockType == BLOCK_NONE || damage.blockType == BLOCK_ARMOR) {
				for (const auto& condition : params.conditionList) {
					if (caster == creature.get() || !creature->isImmune(condition->getType())) {
						auto conditionCopy = condition->clone();
						if (caster) {
							conditionCopy->setParam(CONDITION_PARAM_OWNER, caster->getID());
							conditionCopy->setPositionParam(CONDITION_PARAM_CASTER_POSITION, caster->getPosition());
						}

						// TODO: infight condition until all aggressive conditions has ended
						creature->addCombatCondition(std::move(conditionCopy));
					}
				}
			}

			int32_t totalDamage = std::abs(damageCopy.primary.value + damageCopy.secondary.value);

			if (casterPlayer && !damage.leeched && damage.primary.type != COMBAT_HEALING &&
			    damage.origin != ORIGIN_CONDITION) {
				int32_t targetsCount = toDamageCreatures.size();

				if (casterPlayer->getHealth() < casterPlayer->getMaxHealth()) {
					uint16_t chance = casterPlayer->getSpecialSkill(SPECIALSKILL_LIFELEECHCHANCE);
					int32_t skill = std::max<int32_t>(
					    0, static_cast<int32_t>(casterPlayer->getSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT)) +
					           damage.lifeLeech);

					if (skill > 0 && chance == 0) { chance = 10000; }
					if (chance > 0 && skill > 0 && uniform_random(1, 10000) <= chance) {
						leechCombat.primary.value = std::ceil(
						    totalDamage * ((skill / 10000.) + ((targetsCount - 1) * ((skill / 10000.) / 10.))) /
						    targetsCount);
						g_game.combatChangeHealth(nullptr, casterPlayer, leechCombat);
						casterPlayer->sendMagicEffect(casterPlayer->getPosition(), CONST_ME_MAGIC_RED);
					}
				}

				if (casterPlayer->getMana() < casterPlayer->getMaxMana()) {
					uint16_t chance = casterPlayer->getSpecialSkill(SPECIALSKILL_MANALEECHCHANCE);
					int32_t skill = std::max<int32_t>(
					    0, static_cast<int32_t>(casterPlayer->getSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT)) +
					           damage.manaLeech);

					if (skill > 0 && chance == 0) { chance = 10000; }
					if (chance > 0 && skill > 0 && uniform_random(1, 10000) <= chance) {
						leechCombat.primary.value = std::ceil(
						    totalDamage * ((skill / 10000.) + ((targetsCount - 1) * ((skill / 10000.) / 10.))) /
						    targetsCount);
						g_game.combatChangeMana(nullptr, casterPlayer, leechCombat);
						casterPlayer->sendMagicEffect(casterPlayer->getPosition(), CONST_ME_MAGIC_BLUE);
					}
				}
			}

			if (params.dispelType == CONDITION_PARALYZE) {
				creature->removeCondition(CONDITION_PARALYZE);
			} else {
				creature->removeCombatCondition(params.dispelType);
			}
		}

		if (params.targetCallback) {
			params.targetCallback->onTargetCombat(caster, creature.get());
		}
	}
}

//**********************************************************//

void ValueCallback::getMinMaxValues(Player* player, CombatDamage& damage) const
{
	// onGetPlayerMinMaxValues(...)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - ValueCallback::getMinMaxValues] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	if (!env->setCallbackId(scriptId, scriptInterface)) {
		scriptInterface->resetScriptEnv();
		return;
	}

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	int parameters = 1;
	switch (type) {
		case COMBAT_FORMULA_LEVELMAGIC: {
			// onGetPlayerMinMaxValues(player, level, maglevel)
			lua_pushinteger(L, player->getLevel());
			lua_pushinteger(L, getEffectiveMagicLevel(player, damage.primary.type));
			parameters += 2;
			break;
		}

		case COMBAT_FORMULA_SKILL: {
			// onGetPlayerMinMaxValues(player, attackSkill, attackValue, attackFactor)
			Item* tool = player->getWeapon();
			const Weapon* weapon = g_weapons->getWeapon(tool);
			Item* item = nullptr;

			int32_t attackValue = 7;
			if (weapon) {
				attackValue = tool->getAttack();
				if (tool->getWeaponType() == WEAPON_AMMO) {
					item = player->getWeapon(true);
					if (item) {
						attackValue += item->getAttack();
					}
				}

				damage.secondary.type = weapon->getElementType();
				damage.secondary.value = weapon->getElementDamage(player, nullptr, tool);
			}

			lua_pushinteger(L, player->getWeaponSkill(item ? item : tool));
			lua_pushinteger(L, attackValue);
			lua_pushnumber(L, player->getAttackFactor());
			parameters += 3;
			break;
		}

		default: {
			LOG_ERROR("ValueCallback::getMinMaxValues - unknown callback type");
			scriptInterface->resetScriptEnv();
			return;
		}
	}

	int size0 = lua_gettop(L);
	if (lua_pcall(L, parameters, 2, 0) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		damage.primary.value = normal_random(static_cast<int32_t>(Lua::getNumber<double>(L, -2)),
		                                     static_cast<int32_t>(Lua::getNumber<double>(L, -1)));
		lua_pop(L, 2);
	}

	if ((lua_gettop(L) + parameters + 1) != size0) {
		LuaScriptInterface::reportError(nullptr, "Stack size changed!");
	}

	scriptInterface->resetScriptEnv();
}

//**********************************************************//

void TileCallback::onTileCombat(Creature* creature, Tile* tile) const
{
	// onTileCombat(creature, pos)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - TileCallback::onTileCombat] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	if (!env->setCallbackId(scriptId, scriptInterface)) {
		scriptInterface->resetScriptEnv();
		return;
	}

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	if (creature) {
		Lua::pushUserdata<Creature>(L, creature);
		Lua::setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}
	Lua::pushPosition(L, tile->getPosition());

	scriptInterface->callFunction(2);
}

//**********************************************************//

void TargetCallback::onTargetCombat(Creature* creature, Creature* target) const
{
	// onTargetCombat(creature, target)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - TargetCallback::onTargetCombat] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	if (!env->setCallbackId(scriptId, scriptInterface)) {
		scriptInterface->resetScriptEnv();
		return;
	}

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	if (creature) {
		Lua::pushUserdata<Creature>(L, creature);
		Lua::setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}

	if (target) {
		Lua::pushUserdata<Creature>(L, target);
		Lua::setCreatureMetatable(L, -1, target);
	} else {
		lua_pushnil(L);
	}

	int size0 = lua_gettop(L);

	if (lua_pcall(L, 2, 0 /*nReturnValues*/, 0) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	}

	if ((lua_gettop(L) + 2 /*nParams*/ + 1) != size0) {
		LuaScriptInterface::reportError(nullptr, "Stack size changed!");
	}

	scriptInterface->resetScriptEnv();
}

const MatrixArea& AreaCombat::getArea(const Position& centerPos, const Position& targetPos) const
{
	int32_t dx = targetPos.getOffsetX(centerPos);
	int32_t dy = targetPos.getOffsetY(centerPos);

	Direction dir;
	if (dx < 0) {
		dir = DIRECTION_WEST;
	} else if (dx > 0) {
		dir = DIRECTION_EAST;
	} else if (dy < 0) {
		dir = DIRECTION_NORTH;
	} else {
		dir = DIRECTION_SOUTH;
	}

	if (hasExtArea) {
		if (dx < 0 && dy < 0) {
			dir = DIRECTION_NORTHWEST;
		} else if (dx > 0 && dy < 0) {
			dir = DIRECTION_NORTHEAST;
		} else if (dx < 0 && dy > 0) {
			dir = DIRECTION_SOUTHWEST;
		} else if (dx > 0 && dy > 0) {
			dir = DIRECTION_SOUTHEAST;
		}
	}

	if (dir >= areas.size()) {
		// this should not happen. it means we forgot to call setupArea.
		static MatrixArea empty;
		return empty;
	}
	return areas[dir];
}

void AreaCombat::setupArea(const std::vector<uint32_t>& vec, uint32_t rows)
{
	auto area = createArea(vec, rows);
	if (areas.size() == 0) {
		areas.resize(4);
	}

	areas[DIRECTION_EAST] = area.rotate90();
	areas[DIRECTION_SOUTH] = area.rotate180();
	areas[DIRECTION_WEST] = area.rotate270();
	areas[DIRECTION_NORTH] = std::move(area);
}

void AreaCombat::setupArea(int32_t length, int32_t spread)
{
	uint32_t rows = length;
	int32_t cols = 1;

	if (spread != 0) {
		cols = ((length - (length % spread)) / spread) * 2 + 1;
	}

	int32_t colSpread = cols;

	std::vector<uint32_t> vec;
	vec.reserve(rows * cols);
	for (uint32_t y = 1; y <= rows; ++y) {
		int32_t mincol = cols - colSpread + 1;
		int32_t maxcol = cols - (cols - colSpread);

		for (int32_t x = 1; x <= cols; ++x) {
			if (y == rows && x == ((cols - (cols % 2)) / 2) + 1) {
				vec.push_back(3);
			} else if (x >= mincol && x <= maxcol) {
				vec.push_back(1);
			} else {
				vec.push_back(0);
			}
		}

		if (spread > 0 && y % spread == 0) {
			--colSpread;
		}
	}

	setupArea(vec, rows);
}

void AreaCombat::setupArea(int32_t radius)
{
	int32_t area[13][13] = {{0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 8, 8, 7, 8, 8, 0, 0, 0, 0},
	                        {0, 0, 0, 8, 7, 6, 6, 6, 7, 8, 0, 0, 0}, {0, 0, 8, 7, 6, 5, 5, 5, 6, 7, 8, 0, 0},
	                        {0, 8, 7, 6, 5, 4, 4, 4, 5, 6, 7, 8, 0}, {0, 8, 6, 5, 4, 3, 2, 3, 4, 5, 6, 8, 0},
	                        {8, 7, 6, 5, 4, 2, 1, 2, 4, 5, 6, 7, 8}, {0, 8, 6, 5, 4, 3, 2, 3, 4, 5, 6, 8, 0},
	                        {0, 8, 7, 6, 5, 4, 4, 4, 5, 6, 7, 8, 0}, {0, 0, 8, 7, 6, 5, 5, 5, 6, 7, 8, 0, 0},
	                        {0, 0, 0, 8, 7, 6, 6, 6, 7, 8, 0, 0, 0}, {0, 0, 0, 0, 8, 8, 7, 8, 8, 0, 0, 0, 0},
	                        {0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0}};

	std::vector<uint32_t> vec;
	vec.reserve(13 * 13);
	for (auto& row : area) {
		for (int cell : row) {
			if (cell == 1) {
				vec.push_back(3);
			} else if (cell > 0 && cell <= radius) {
				vec.push_back(1);
			} else {
				vec.push_back(0);
			}
		}
	}

	setupArea(vec, 13);
}

void AreaCombat::setupAreaRing(int32_t ring)
{
	int32_t area[13][13] = {{0, 0, 0, 0, 0, 7, 7, 7, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 7, 6, 6, 6, 7, 0, 0, 0, 0},
	                        {0, 0, 0, 7, 6, 5, 5, 5, 6, 7, 0, 0, 0}, {0, 0, 7, 6, 5, 4, 4, 4, 5, 6, 7, 0, 0},
	                        {0, 7, 6, 5, 4, 3, 3, 3, 4, 5, 6, 7, 0}, {7, 6, 5, 4, 3, 2, 0, 2, 3, 4, 5, 6, 7},
	                        {7, 6, 5, 4, 3, 0, 1, 0, 3, 4, 5, 6, 7}, {7, 6, 5, 4, 3, 2, 0, 2, 3, 4, 5, 6, 7},
	                        {0, 7, 6, 5, 4, 3, 3, 3, 4, 5, 6, 7, 0}, {0, 0, 7, 6, 5, 4, 4, 4, 5, 6, 7, 0, 0},
	                        {0, 0, 0, 7, 6, 5, 5, 5, 6, 7, 0, 0, 0}, {0, 0, 0, 0, 7, 6, 6, 6, 7, 0, 0, 0, 0},
	                        {0, 0, 0, 0, 0, 7, 7, 7, 0, 0, 0, 0, 0}};

	std::vector<uint32_t> vec;
	vec.reserve(13 * 13);
	for (auto& row : area) {
		for (int cell : row) {
			if (cell == 1) {
				vec.push_back(3);
			} else if (cell > 0 && cell == ring) {
				vec.push_back(1);
			} else {
				vec.push_back(0);
			}
		}
	}

	setupArea(vec, 13);
}

void AreaCombat::setupExtArea(const std::vector<uint32_t>& vec, uint32_t rows)
{
	if (vec.empty()) {
		return;
	}

	hasExtArea = true;
	auto area = createArea(vec, rows);
	areas.resize(8);
	areas[DIRECTION_NORTHEAST] = area.mirror();
	areas[DIRECTION_SOUTHWEST] = area.flip();
	areas[DIRECTION_SOUTHEAST] = area.rotate180();
	areas[DIRECTION_NORTHWEST] = std::move(area);
}

//**********************************************************//
// Chain System
//**********************************************************//

ChainCallback::ChainCallback(uint8_t chainTargets, uint8_t chainDistance, bool backtracking)
    : m_chainDistance(chainDistance), m_chainTargets(chainTargets), m_backtracking(backtracking) {}

void ChainCallback::setFromLua(bool fromLua) { m_fromLua = fromLua; }

void ChainCallback::getChainValues(Creature* creature, uint8_t& maxTargets, uint8_t& chainDistance, bool& backtracking)
{
	if (m_fromLua) {
		onChainCombat(creature, maxTargets, chainDistance, backtracking);
		return;
	}

	if (m_chainTargets && m_chainDistance) {
		maxTargets = m_chainTargets;
		chainDistance = m_chainDistance;
		backtracking = m_backtracking;
	}
}

void ChainCallback::onChainCombat(Creature* creature, uint8_t& chainTargets, uint8_t& chainDistance, bool& backtracking) const
{
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[ChainCallback::onChainCombat] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	if (!env->setCallbackId(scriptId, scriptInterface)) {
		scriptInterface->resetScriptEnv();
		return;
	}

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(scriptId);

	if (creature) {
		Lua::pushUserdata<Creature>(L, creature);
		Lua::setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}

	int size0 = lua_gettop(L);
	if (lua_pcall(L, 1, 3, 0) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		chainTargets = std::max<uint8_t>(1, Lua::getNumber<uint8_t>(L, -3));
		chainDistance = std::max<uint8_t>(1, Lua::getNumber<uint8_t>(L, -2));
		backtracking = Lua::getBoolean(L, -1);
		lua_pop(L, 3);
	}

	if ((lua_gettop(L) + 1 + 1) != size0) {
		LuaScriptInterface::reportError(nullptr, "Stack size changed!");
	}

	scriptInterface->resetScriptEnv();
}

//**********************************************************//

bool ChainPickerCallback::onChainCombat(Creature* creature, Creature* target) const
{
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[ChainPickerCallback::onChainCombat] Call stack overflow");
		return true;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	if (!env->setCallbackId(scriptId, scriptInterface)) {
		scriptInterface->resetScriptEnv();
		return true;
	}

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(scriptId);

	if (creature) {
		Lua::pushUserdata<Creature>(L, creature);
		Lua::setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}

	if (target) {
		Lua::pushUserdata<Creature>(L, target);
		Lua::setCreatureMetatable(L, -1, target);
	} else {
		lua_pushnil(L);
	}

	int size0 = lua_gettop(L);
	bool result = true;

	if (lua_pcall(L, 2, 1, 0) != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else {
		result = Lua::getBoolean(L, -1);
		lua_pop(L, 1);
	}

	if ((lua_gettop(L) + 2 + 1) != size0) {
		LuaScriptInterface::reportError(nullptr, "Stack size changed!");
	}

	scriptInterface->resetScriptEnv();
	return result;
}

//**********************************************************//

void Combat::setChainCallback(uint8_t chainTargets, uint8_t chainDistance, bool backtracking)
{
	params.chainCallback = std::make_unique<ChainCallback>(chainTargets, chainDistance, backtracking);
}

void Combat::doChainEffect(const Position& origin, const Position& pos, uint8_t effect)
{
	if (effect == CONST_ME_NONE) {
		return;
	}

	g_game.addMagicEffect(origin, effect);
	g_game.addMagicEffect(pos, effect);
}

bool Combat::isValidChainTarget(Creature* caster, Creature* currentTarget, Creature* potentialTarget,
                                const CombatParams& params, bool /*aggressive*/)
{
	if (Combat::canDoCombat(caster, potentialTarget) != RETURNVALUE_NOERROR) {
		return false;
	}

	if (params.chainPickerCallback && !params.chainPickerCallback->onChainCombat(caster, potentialTarget)) {
		return false;
	}

	if (!g_game.isSightClear(currentTarget->getPosition(), potentialTarget->getPosition(), true)) {
		return false;
	}

	return true;
}

std::vector<std::pair<Position, std::vector<uint32_t>>> Combat::pickChainTargets(
    Creature* caster, const CombatParams& params, uint8_t chainDistance, uint8_t maxTargets,
    bool aggressive, bool backtracking, Creature* initialTarget)
{
	std::vector<std::pair<Position, std::vector<uint32_t>>> resultMap;
	std::vector<Creature*> targets;
	std::unordered_set<uint32_t> visited;

	if (initialTarget && initialTarget != caster) {
		targets.push_back(initialTarget);
		visited.insert(initialTarget->getID());
		resultMap.emplace_back(caster->getPosition(), std::vector<uint32_t>{initialTarget->getID()});
	} else {
		targets.push_back(caster);
		maxTargets++;
	}

	int backtrackingAttempts = 10;
	while (!targets.empty() && targets.size() < static_cast<size_t>(maxTargets) && backtrackingAttempts > 0) {
		Creature* currentTarget = targets.back();

		SpectatorVec spectators;
		g_game.map.getSpectators(spectators, currentTarget->getPosition(), false, false,
		                         chainDistance, chainDistance, chainDistance, chainDistance);

		double closestDist = std::numeric_limits<double>::max();
		Creature* closestSpectator = nullptr;

		for (const auto& spectator : spectators) {
			if (!spectator || visited.contains(spectator->getID())) {
				continue;
			}

			if (spectator->getNpc()) {
				visited.insert(spectator->getID());
				continue;
			}

			if (spectator.get() == caster) {
				visited.insert(spectator->getID());
				continue;
			}

			Tile* tile = spectator->getTile();
			if (tile && tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
				visited.insert(spectator->getID());
				continue;
			}

			Player* casterPlayer = caster ? caster->getPlayer() : nullptr;
			Monster* casterMonster = caster ? caster->getMonster() : nullptr;
			Player* spectatorPlayer = spectator->getPlayer();
			bool spectatorSummon = spectator->isSummon();

			if (casterPlayer) {
				if (casterPlayer->hasSecureMode()) {
					if (spectatorPlayer) {
						visited.insert(spectator->getID());
						continue;
					}
					auto spectatorMaster = spectator->getMaster();
					if (spectatorSummon && spectatorMaster && spectatorMaster->getPlayer()) {
						visited.insert(spectator->getID());
						continue;
					}
				}
			} else if (casterMonster) {
				if (spectatorSummon) {
					auto master = spectator->getMaster();
					if (!master || !master->getPlayer()) {
						visited.insert(spectator->getID());
						continue;
					}
				} else if (!spectatorPlayer) {
					visited.insert(spectator->getID());
					continue;
				}
			}

			if (!isValidChainTarget(caster, currentTarget, spectator.get(), params, aggressive)) {
				visited.insert(spectator->getID());
				continue;
			}

			double dist = currentTarget->getPosition().getDistanceX(spectator->getPosition()) +
			              currentTarget->getPosition().getDistanceY(spectator->getPosition());

			if (dist < closestDist) {
				closestDist = dist;
				closestSpectator = spectator.get();
			}
		}

		if (closestSpectator) {
			bool found = false;
			for (auto& [pos, vec] : resultMap) {
				if (pos == currentTarget->getPosition()) {
					vec.push_back(closestSpectator->getID());
					found = true;
					break;
				}
			}
			if (!found) {
				resultMap.emplace_back(currentTarget->getPosition(),
				                       std::vector<uint32_t>{closestSpectator->getID()});
			}

			targets.push_back(closestSpectator);
			visited.insert(closestSpectator->getID());
			continue;
		}

		if (backtracking) {
			targets.pop_back();
			backtrackingAttempts--;
			continue;
		}
		break;
	}

	return resultMap;
}

bool Combat::doCombatChain(Creature* caster, Creature* target, bool aggressive, std::string instantSpellName) const
{
	if (!params.chainCallback) {
		return false;
	}

	uint8_t maxTargets = 0;
	uint8_t chainDistance = 0;
	bool backtracking = false;
	params.chainCallback->getChainValues(caster, maxTargets, chainDistance, backtracking);

	auto targets = pickChainTargets(caster, params, chainDistance, maxTargets, aggressive, backtracking, target);
	if (targets.empty() || (targets.size() == 1 && targets.begin()->second.empty())) {
		return false;
	}

	auto self = shared_from_this();
	const uint8_t capturedChainEffect = params.chainEffect;
	int i = 0;
	for (const auto& [from, toVector] : targets) {
		auto delay = i * std::max<int32_t>(SCHEDULER_MINTICKS, ConfigManager::getInteger(ConfigManager::COMBAT_CHAIN_DELAY));
		++i;
		for (const auto& to : toVector) {
			g_scheduler.addEvent(delay, [self, casterId = caster ? caster->getID() : 0, to, from, capturedChainEffect,
			                             instantSpellName]() {
				Creature* resolvedCaster = g_game.getCreatureByID(casterId);
				Creature* nextTarget = g_game.getCreatureByID(to);
				if (!nextTarget) {
					return;
				}
				Combat::doChainEffect(from, nextTarget->getPosition(), capturedChainEffect);
				if (resolvedCaster) {
					CombatDamage damage = self->getCombatDamage(resolvedCaster, nextTarget, instantSpellName);
					bool canCombat = !self->params.aggressive ||
					                 (resolvedCaster != nextTarget &&
					                  Combat::canDoCombat(resolvedCaster, nextTarget) == RETURNVALUE_NOERROR);
					if (canCombat) {
						doTargetCombat(resolvedCaster, nextTarget, damage, self->params);
					}
				}
			});
		}
	}

	return true;
}

void Combat::setupChain(const Weapon* weapon)
{
	if (!weapon) {
		return;
	}

	WeaponType_t weaponType = weapon->weaponType;
	if (weaponType == WEAPON_NONE || weaponType == WEAPON_SHIELD) {
		return;
	}

	if (weapon->isChainDisabled()) {
		return;
	}

	params.combatType = weapon->params.combatType;
	if (weaponType != WEAPON_WAND) {
		params.blockedByArmor = true;
	}

	setChainCallback(static_cast<uint8_t>(ConfigManager::getInteger(ConfigManager::COMBAT_CHAIN_TARGETS)), 1, true);

	double formula = 1.0;
	uint16_t effect = CONST_ME_HITAREA;

	switch (weaponType) {
		case WEAPON_FIST:
			formula = ConfigManager::getFloat(ConfigManager::COMBAT_CHAIN_SKILL_FORMULA_FIST);
			effect = CONST_ME_HITAREA;
			break;
		case WEAPON_SWORD:
			formula = ConfigManager::getFloat(ConfigManager::COMBAT_CHAIN_SKILL_FORMULA_SWORD);
			effect = CONST_ME_SLASH;
			break;
		case WEAPON_CLUB:
			formula = ConfigManager::getFloat(ConfigManager::COMBAT_CHAIN_SKILL_FORMULA_CLUB);
			effect = CONST_ME_BLACK_BLOOD;
			break;
		case WEAPON_AXE:
			formula = ConfigManager::getFloat(ConfigManager::COMBAT_CHAIN_SKILL_FORMULA_AXE);
			effect = CONST_ME_HITAREA;
			break;
		case WEAPON_DISTANCE:
		case WEAPON_AMMO:
			formula = ConfigManager::getFloat(ConfigManager::COMBAT_CHAIN_SKILL_FORMULA_DISTANCE);
			effect = CONST_ME_HOLYDAMAGE;
			break;
		default:
			break;
	}

	double weaponSkillFormula = weapon->getChainSkillValue();
	double useFormula = weaponSkillFormula > 0 ? weaponSkillFormula : formula;
	setPlayerCombatValues(COMBAT_FORMULA_SKILL, 0, 0, useFormula, 0);
	params.impactEffect = effect;

	if (weaponType == WEAPON_WAND) {
		static const std::map<CombatType_t, std::pair<uint16_t, uint16_t>> elementEffects = {
		    {COMBAT_DEATHDAMAGE, {CONST_ME_MORTAREA, CONST_ME_BLACK_BLOOD}},
		    {COMBAT_ENERGYDAMAGE, {CONST_ME_ENERGYAREA, CONST_ME_ENERGYHIT}},
		    {COMBAT_FIREDAMAGE, {CONST_ME_FIREATTACK, CONST_ME_FIREATTACK}},
		    {COMBAT_ICEDAMAGE, {CONST_ME_ICEATTACK, CONST_ME_ICEATTACK}},
		    {COMBAT_EARTHDAMAGE, {CONST_ME_STONES, CONST_ME_POISONAREA}},
		};

		auto it = elementEffects.find(weapon->getElementType());
		if (it != elementEffects.end()) {
			setPlayerCombatValues(COMBAT_FORMULA_LEVELMAGIC, 0, 0,
			                      -ConfigManager::getFloat(ConfigManager::COMBAT_CHAIN_SKILL_FORMULA_WANDS_AND_RODS), 0);
			params.impactEffect = it->second.first;
			params.chainEffect = static_cast<uint8_t>(it->second.second);
		}
	}
}

//**********************************************************//

void MagicField::onStepInField(const std::shared_ptr<Creature>& creature)
{
	if (!creature || creature->isRemoved()) {
		return;
	}

	const ItemType& it = items[getID()];
	if (it.conditionDamage) {
		Tile* fieldTile = getTile();
		if (!fieldTile) {
			return;
		}

		auto conditionCopy = it.conditionDamage->clone();
		uint32_t ownerId = getOwner();
		if (ownerId) {
			bool harmfulField = true;

			if (g_game.getWorldType() == WORLD_TYPE_NO_PVP || fieldTile->hasFlag(TILESTATE_NOPVPZONE)) {
				auto owner = lockCreature(g_game.getCreatureByID(ownerId));
				if (owner) {
					auto ownerMaster = owner->getMaster();
					if (owner->getPlayer() || (owner->isSummon() && ownerMaster && ownerMaster->getPlayer())) {
						harmfulField = false;
					}
				}
			}

			Player* targetPlayer = creature->getPlayer();
			if (targetPlayer) {
				auto attackerPlayer = g_game.getPlayerByID(ownerId);
				if (attackerPlayer) {
					if (Combat::isProtected(attackerPlayer.get(), targetPlayer)) {
						harmfulField = false;
					}
				}
			}

			if (!harmfulField || (OTSYS_TIME() - createTime <= 5000) || creature->hasBeenAttacked(ownerId)) {
				conditionCopy->setParam(CONDITION_PARAM_OWNER, ownerId);
			}
		}

		if (conditionCopy->getType() == CONDITION_AGONY) {
			creature->addCombatCondition(std::move(conditionCopy));
		} else {
			creature->addCondition(std::move(conditionCopy));
		}
	}
}
