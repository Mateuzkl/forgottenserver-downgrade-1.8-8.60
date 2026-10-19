// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "tile.h"

#include "bed.h"
#include "combat.h"
#include "configmanager.h"
#include "container.h"
#include "creature.h"
#include "game.h"
#include "housetile.h"
#include "instance_utils.h"
#include "mailbox.h"
#include "monster.h"
#include "movement.h"
#include "scriptmanager.h"
#include "teleport.h"
#include "trashholder.h"
#include "logger.h"

#include <algorithm>

extern Game g_game;

namespace {

std::shared_ptr<Creature> getSharedCreature(Creature* creature)
{
	return creature ? creature->weak_from_this().lock() : nullptr;
}

std::shared_ptr<Item> getSharedItem(Item* item)
{
	return item ? item->weak_from_this().lock() : nullptr;
}

void logSharedCreatureLockFailure(std::string_view context, const Creature* creature)
{
	LOG_WARN("[Warning - {}] Failed to lock creature shared ownership. creature={}, id={}, name={}", context,
	         static_cast<const void*>(creature), creature ? creature->getID() : 0,
	         creature ? std::string_view(creature->getName()) : std::string_view{});
}

void logSharedItemLockFailure(std::string_view context, const Item* item)
{
	LOG_WARN("[Warning - {}] Failed to lock item shared ownership. item={}, id={}", context,
	         static_cast<const void*>(item), item ? item->getID() : 0);
}

// Match the surface and blocker conditions in synchronizeAutoCarpet. A bed is
// a surface only while stationary; flat top-layer blockers also support it.
bool affectsAutoCarpetFurniture(const ItemType& type)
{
	const bool surface = !type.moveable && (type.isBed() || (type.blockSolid && type.alwaysOnTop && !type.hasHeight));
	return surface || type.blockSolid;
}

} // namespace

StaticTile real_nullptr_tile(0xFFFF, 0xFFFF, 0xFF);
Tile& Tile::nullptr_tile = real_nullptr_tile;

bool Tile::hasProperty(ITEMPROPERTY prop) const
{
	if (ground && ground->hasProperty(prop)) {
		return true;
	}

	if (const TileItemVector* items = getItemList()) {
		for (const auto& item : *items) {
			if (item->hasProperty(prop)) {
				return true;
			}
		}
	}
	return false;
}

bool Tile::hasProperty(const Item* exclude, ITEMPROPERTY prop) const
{
	assert(exclude);

	if (ground && exclude != ground.get() && ground->hasProperty(prop)) {
		return true;
	}

	if (const TileItemVector* items = getItemList()) {
		for (const auto& item : *items) {
			if (item.get() != exclude && item->hasProperty(prop)) {
				return true;
			}
		}
	}

	return false;
}

bool Tile::hasInstancedProperty(ITEMPROPERTY prop, uint32_t instanceID) const
{
	if (instanceID == 0) {
		return false;
	}

	if (const TileItemVector *items = getItemList()) {
		for (const auto& item : *items) {
			if (item->getInstanceID() == instanceID && item->hasProperty(prop)) {
				return true;
			}
		}
	}
	return false;
}

bool Tile::hasPropertyGlobal(const Item* exclude, ITEMPROPERTY prop) const
{
	assert(exclude);

	if (ground && exclude != ground.get() && ground->hasProperty(prop)) {
		return true;
	}

	if (const TileItemVector *items = getItemList()) {
		for (const auto& item : *items) {
			if (item.get() != exclude && item->getInstanceID() == 0 &&
					item->hasProperty(prop)) {
				return true;
			}
		}
	}
	return false;
}

bool Tile::hasHeight(uint32_t n) const
{
	uint32_t height = 0;

	if (ground) {
		if (ground->hasProperty(CONST_PROP_HASHEIGHT)) {
			++height;
		}

		if (n == height) {
			return true;
		}
	}

	if (const TileItemVector* items = getItemList()) {
		for (const auto& item : *items) {
			if (item->hasProperty(CONST_PROP_HASHEIGHT)) {
				++height;
			}

			if (n == height) {
				return true;
			}
		}
	}
	return false;
}

size_t Tile::getCreatureCount() const
{
	if (const CreatureVector* creatures = getCreatures()) {
		return creatures->size();
	}
	return 0;
}

size_t Tile::getItemCount() const
{
	if (const TileItemVector* items = getItemList()) {
		return items->size();
	}
	return 0;
}

uint32_t Tile::getTopItemCount() const
{
	if (const TileItemVector* items = getItemList()) {
		return items->getTopItemCount();
	}
	return 0;
}

uint32_t Tile::getDownItemCount() const
{
	if (const TileItemVector* items = getItemList()) {
		return items->getDownItemCount();
	}
	return 0;
}

Teleport* Tile::getTeleportItem() const
{
	if (!hasFlag(TILESTATE_TELEPORT)) {
		return nullptr;
	}

	if (const TileItemVector* items = getItemList()) {
		for (auto it = items->rbegin(), end = items->rend(); it != end; ++it) {
			if ((*it)->getTeleport()) {
				return (*it)->getTeleport();
			}
		}
	}
	return nullptr;
}

MagicField* Tile::getFieldItem() const
{
	if (!hasFlag(TILESTATE_MAGICFIELD)) {
		return nullptr;
	}

	if (ground && ground->getMagicField()) {
		return ground->getMagicField();
	}

	if (const TileItemVector* items = getItemList()) {
		for (auto it = items->rbegin(), end = items->rend(); it != end; ++it) {
			if ((*it)->getMagicField()) {
				return (*it)->getMagicField();
			}
		}
	}
	return nullptr;
}

MagicField* Tile::getFieldItem(uint32_t instanceID) const
{
	if (!hasFlag(TILESTATE_MAGICFIELD)) {
		return nullptr;
	}

	if (ground && ground->getMagicField()) {
		return ground->getMagicField();
	}

	if (const TileItemVector *items = getItemList()) {
		for (auto it = items->rbegin(), end = items->rend(); it != end; ++it) {
			if ((*it)->getMagicField()) {
				if ((*it)->getInstanceID() == instanceID) {
					return (*it)->getMagicField();
				}
			}
		}
	}
	return nullptr;
}

TrashHolder* Tile::getTrashHolder() const
{
	if (!hasFlag(TILESTATE_TRASHHOLDER)) {
		return nullptr;
	}

	if (ground && ground->getTrashHolder()) {
		return ground->getTrashHolder();
	}

	if (const TileItemVector* items = getItemList()) {
		for (auto it = items->rbegin(), end = items->rend(); it != end; ++it) {
			if ((*it)->getTrashHolder()) {
				return (*it)->getTrashHolder();
			}
		}
	}
	return nullptr;
}

Mailbox* Tile::getMailbox() const
{
	if (!hasFlag(TILESTATE_MAILBOX)) {
		return nullptr;
	}

	if (ground && ground->getMailbox()) {
		return ground->getMailbox();
	}

	if (const TileItemVector* items = getItemList()) {
		for (auto it = items->rbegin(), end = items->rend(); it != end; ++it) {
			if ((*it)->getMailbox()) {
				return (*it)->getMailbox();
			}
		}
	}
	return nullptr;
}

std::shared_ptr<BedItem> Tile::getBedItem() const
{
	if (!hasFlag(TILESTATE_BED)) {
		return nullptr;
	}

	if (ground) {
		if (auto bed = ground->getBed()) {
			return std::const_pointer_cast<BedItem>(bed);
		}
	}

	if (const TileItemVector* items = getItemList()) {
		for (auto it = items->rbegin(), end = items->rend(); it != end; ++it) {
			if (auto bed = (*it)->getBed()) {
				return std::const_pointer_cast<BedItem>(bed);
			}
		}
	}
	return nullptr;
}

Creature* Tile::getTopCreature() const
{
	if (const CreatureVector* creatures = getCreatures()) {
		if (!creatures->empty()) {
			return creatures->front().get();
		}
	}
	return nullptr;
}

const Creature* Tile::getBottomCreature() const
{
	if (const CreatureVector* creatures = getCreatures()) {
		if (!creatures->empty()) {
			return creatures->back().get();
		}
	}
	return nullptr;
}

Creature* Tile::getTopVisibleCreature(const Creature* creature) const
{
	if (const CreatureVector* creatures = getCreatures()) {
		if (creature) {
			for (const auto& tileCreature : *creatures) {
				if (creature->canSeeCreature(tileCreature.get())) {
					return tileCreature.get();
				}
			}
		} else {
			for (const auto& tileCreature : *creatures) {
				if (!tileCreature->isInvisible()) {
					const Player* player = tileCreature->getPlayer();
					if (!player || !player->isInGhostMode()) {
						return tileCreature.get();
					}
				}
			}
		}
	}
	return nullptr;
}

const Creature* Tile::getBottomVisibleCreature(const Creature* creature) const
{
	if (const CreatureVector* creatures = getCreatures()) {
		if (creature) {
			for (auto it = creatures->rbegin(), end = creatures->rend(); it != end; ++it) {
				if (creature->canSeeCreature(it->get())) {
					return it->get();
				}
			}
		} else {
			for (auto it = creatures->rbegin(), end = creatures->rend(); it != end; ++it) {
				if (!(*it)->isInvisible()) {
					const Player* player = (*it)->getPlayer();
					if (!player || !player->isInGhostMode()) {
						return it->get();
					}
				}
			}
		}
	}
	return nullptr;
}

Item* Tile::getTopDownItem() const
{
	if (const TileItemVector* items = getItemList()) {
		return items->getTopDownItem();
	}
	return nullptr;
}

Item* Tile::getTopDownItem(uint32_t instanceId) const
{
	if (const TileItemVector* items = getItemList()) {
		for (auto it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end; ++it) {
			if (InstanceUtils::canSeeItemInInstance(instanceId, it->get())) {
				return it->get();
			}
		}
	}
	return nullptr;
}

Item* Tile::getTopTopItem() const
{
	if (const TileItemVector* items = getItemList()) {
		return items->getTopTopItem();
	}
	return nullptr;
}

Item* Tile::getItemByTopOrder(int32_t topOrder)
{
	// topOrder:
	// 1: borders
	// 2: ladders, signs, splashes
	// 3: doors etc
	// 4: creatures
	if (TileItemVector* items = getItemList()) {
		for (auto it = ItemVector::const_reverse_iterator(items->getEndTopItem()),
		          end = ItemVector::const_reverse_iterator(items->getBeginTopItem());
		     it != end; ++it) {
			if (Item::items[(*it)->getID()].alwaysOnTopOrder == topOrder) {
				return it->get();
			}
		}
	}
	return nullptr;
}

Thing* Tile::getTopVisibleThing(const Creature* creature)
{
	Thing* thing = getTopVisibleCreature(creature);
	if (thing) {
		return thing;
	}

	TileItemVector* items = getItemList();
	if (items) {
		for (ItemVector::const_iterator it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end;
		     ++it) {
			if (creature && !InstanceUtils::canSeeItemInInstance(creature->getInstanceID(), it->get())) {
				continue;
			}

			const ItemType& iit = Item::items[(*it)->getID()];
			if (!iit.lookThrough) {
				return it->get();
			}
		}

		for (auto it = ItemVector::const_reverse_iterator(items->getEndTopItem()),
		          end = ItemVector::const_reverse_iterator(items->getBeginTopItem());
		     it != end; ++it) {
			if (creature && !InstanceUtils::canSeeItemInInstance(creature->getInstanceID(), it->get())) {
				continue;
			}

			const ItemType& iit = Item::items[(*it)->getID()];
			if (!iit.lookThrough) {
				return it->get();
			}
		}
	}

	return ground.get();
}

void Tile::synchronizeAutoCarpets(uint32_t instanceId)
{
	const auto* originHouseTile = getHouseTile();
	const auto house = originHouseTile ? originHouseTile->getHouse() : nullptr;
	// Thing::instanceID is not serialized with house items.
	if (!house || instanceId != 0) {
		return;
	}
	const auto originRef = weak_from_this().lock();
	if (!originRef) {
		return;
	}
	const Position origin = getPosition();
	// Eight destinations, plus this tile for manual carpet priority and ground updates.
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			const int x = int(origin.x) + dx, y = int(origin.y) + dy;
			if (x <= 0 || y <= 0 || x >= 0xFFFF || y >= 0xFFFF) {
				continue;
			}
			Tile* target = g_game.map.getTile(uint16_t(x), uint16_t(y), origin.z);
			const auto* houseTile = target ? target->getHouseTile() : nullptr;
			if (houseTile && houseTile->getHouse() == house) {
				target->synchronizeAutoCarpet();
			}
		}
	}
}

void Tile::synchronizeAutoCarpet()
{
	if (autoCarpetSynchronizing) {
		// The outer reconciliation rechecks after every native mutation/callback.
		return;
	}
	const auto tileRef = weak_from_this().lock();
	const auto* houseTile = getHouseTile();
	const auto house = houseTile ? houseTile->getHouse() : nullptr;
	if (!tileRef || !house || g_game.map.getTile(getPosition()) != this) {
		return;
	}
	autoCarpetSynchronizing = true;
	struct ReconciliationGuard
	{
		bool& active;
		~ReconciliationGuard() { active = false; }
	} guard{autoCarpetSynchronizing};
	constexpr uint32_t instanceId = 0;
	constexpr auto marker = "__auto_carpet_source";
	const auto sameHouse = [house](const Tile* tile) {
		const auto* houseTile = tile ? tile->getHouseTile() : nullptr;
		return houseTile && houseTile->getHouse() == house;
	};
	const auto visible = [](const Item* item) {
		return item->getInstanceID() == 0 || item->getInstanceID() == instanceId;
	};
	const auto deployed = [](const Item* item) {
		const auto& type = Item::items[item->getID()];
		// Only a flat, stationary top-layer decoration can go below furniture.
		return type.isCarpet() && !type.moveable && !type.stackable && type.alwaysOnTop && !type.hasHeight &&
		       !type.blockSolid && !type.blockPathFind;
	};
	const auto nearby = [](const Position& position, int dx, int dy) -> Tile* {
		const int x = int(position.x) + dx, y = int(position.y) + dy;
		if (x <= 0 || y <= 0 || x >= 0xFFFF || y >= 0xFFFF) {
			return nullptr;
		}
		return g_game.map.getTile(uint16_t(x), uint16_t(y), position.z);
	};

	// Bound scripts which continually change the source in response to decorations.
	for (unsigned attempt = 0; attempt < 16; ++attempt) {
		autoCarpetPending = false;
		if (g_game.map.getTile(getPosition()) != this) {
			return;
		}
		const auto* items = getItemList();
		if (!items) {
			return;
		}
		ItemVector generated;
		bool manualCarpet = false;
		for (const auto& item : *items) {
			if (item->getInstanceID() == instanceId && item->getCustomAttribute(marker)) {
				generated.push_back(item); // retain ownership before any mutation
			} else if (visible(item.get()) && item->isCarpet() && !item->getCustomAttribute(marker)) {
				manualCarpet = true;
			}
		}

		std::shared_ptr<Item> winner;
		std::string owner;
		const auto* ground = getGround();
		if (!manualCarpet && ground && visible(ground) &&
		    !hasFlag(TILESTATE_FLOORCHANGE | TILESTATE_TELEPORT | TILESTATE_TRASHHOLDER | TILESTATE_MAILBOX)) {
			// y, x, item ID priority is independent of placement order; removal elects a fallback.
			for (int sy = -1; sy <= 1 && !winner; ++sy) {
				for (int sx = -1; sx <= 1 && !winner; ++sx) {
					if (sx == 0 && sy == 0) {
						continue;
					}
					const Tile* source = nearby(getPosition(), sx, sy);
					if (!sameHouse(source) || !source->getGround() || !visible(source->getGround()) ||
					    source->getGround()->getID() != ground->getID() || !source->getItemList()) {
						continue;
					}
					for (const auto& candidate : *source->getItemList()) {
						if (candidate->getInstanceID() != instanceId || !deployed(candidate.get()) ||
						    candidate->getCustomAttribute(marker)) {
							continue;
						}
						bool supported = false, blocked = false;
						const auto order = Item::items[candidate->getID()].alwaysOnTopOrder;
						for (const auto& furniture : *items) {
							if (!visible(furniture.get()) || furniture->getCustomAttribute(marker)) {
								continue;
							}
							const bool surface =
							    !furniture->isMoveable() && (furniture->getBed() || furniture->canReceiveAutoCarpet());
							if (surface) {
								supported = true;
								const auto& type = Item::items[furniture->getID()];
								blocked |= type.alwaysOnTop && type.alwaysOnTopOrder <= order;
							} else if (furniture->isBlocking()) {
								blocked = true;
							}
						}
						if (supported && !blocked && (!winner || candidate->getID() < winner->getID())) {
							winner = candidate;
						}
					}
					if (winner) {
						const auto& pos = source->getPosition();
						owner = std::to_string(house->getId()) + ":" + std::to_string(instanceId) + ":" +
						        std::to_string(pos.x) + ":" + std::to_string(pos.y) + ":" + std::to_string(pos.z);
					}
				}
			}
		}

		bool retained = false, removed = false, removalFailed = false;
		for (const auto& item : generated) {
			const auto* attr = item->getCustomAttribute(marker);
			const auto* savedOwner = attr ? std::get_if<std::string>(&attr->value) : nullptr;
			if (!retained && winner && item->getID() == winner->getID() && savedOwner && *savedOwner == owner) {
				retained = true;
				continue;
			}
			if (item->getParent() != this) {
				continue;
			}
			const auto result = g_game.internalRemoveItem(item.get(), -1, false, FLAG_IGNORENOTMOVEABLE);
			removalFailed |= result != RETURNVALUE_NOERROR;
			removed |= result == RETURNVALUE_NOERROR;
			if (g_game.map.getTile(getPosition()) != this) {
				return;
			}
		}
		// Removal notifications can change sources, ground, furniture or manual carpets.
		// Always elect again from the live lists before committing an insertion.
		if (removed) {
			continue;
		}
		if (removalFailed || !winner || retained) {
			return;
		}
		if (getThingCount() >= MAX_STACKPOS_THINGS) {
			autoCarpetPending = true;
			return;
		}
		if (winner) {
			// A fresh decoration deliberately inherits no UID, AID, wrap/quest/decay attributes.
			auto underlay = Item::CreateItem(winner->getID(), 1);
			if (underlay) {
				underlay->setInstanceID(instanceId);
				underlay->setCustomAttribute(marker, owner);
				addThing(underlay.get());
				if (underlay->getParent() == this) {
					postAddNotification(underlay.get(), nullptr, getThingIndex(underlay.get()));
					continue; // insertion callbacks also require a fresh eligibility check
				}
			}
		}
		return;
	}

	// A script cycle must not leave a stale decision or recurse indefinitely.
	// Fail closed and retry only on a later relevant event/capacity notification.
	ItemVector remaining;
	if (const auto* items = getItemList()) {
		for (const auto& item : *items) {
			if (item->getInstanceID() == instanceId && item->getCustomAttribute(marker)) {
				remaining.push_back(item);
			}
		}
	}
	for (const auto& item : remaining) {
		if (item->getParent() == this) {
			g_game.internalRemoveItem(item.get(), -1, false, FLAG_IGNORENOTMOVEABLE);
		}
	}
	autoCarpetPending = true;
}

void Tile::onAddTileItem(Item* item)
{
	if (isBrowseFieldVisibleItem(item)) {
		if (auto browseField = g_game.getBrowseFieldContainer(this)) {
			browseField->addItemBack(item);
			item->setParent(this);
		}
	}

	setTileFlags(item);

	const Position& cylinderMapPos = getPosition();

	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, cylinderMapPos, true);

	// send to client
	for (const auto& spectator : spectators.players()) {
		Player* tmpPlayer = static_cast<Player*>(spectator.get());
		if (InstanceUtils::canSeeItemInInstance(tmpPlayer->getInstanceID(), item)) {
			tmpPlayer->sendAddTileItem(this, cylinderMapPos, item);
		}
	}

	if ((!hasFlag(TILESTATE_PROTECTIONZONE) || getBoolean(ConfigManager::CLEAN_PROTECTION_ZONES)) &&
	    item->isCleanable()) {
		if (!getHouseTile()) {
			g_game.addTileToClean(this);
		}
	}
	if ((item->isCarpet() || Item::items[item->getID()].isGroundTile()) &&
	    !item->getCustomAttribute("__auto_carpet_source")) {
		synchronizeAutoCarpets(item->getInstanceID());
	} else if (affectsAutoCarpetFurniture(Item::items[item->getID()]) && item->getInstanceID() == 0 &&
	           !item->getCustomAttribute("__auto_carpet_source")) {
		synchronizeAutoCarpet();
	}
}

void Tile::onUpdateTileItem(Item* oldItem, const ItemType& oldType, Item* newItem, const ItemType& newType)
{
	// Reconciliation can remove this tile; retain it through the rest of the hook.
	const auto self = weak_from_this().lock();
	if (!self) {
		return;
	}

	if (auto browseField = g_game.getBrowseFieldContainer(this)) {
		if (isBrowseFieldVisibleItem(newItem)) {
			const int32_t index = browseField->getThingIndex(oldItem);
			if (index != -1) {
				browseField->replaceThing(static_cast<uint32_t>(index), newItem);
			} else {
				browseField->addItemBack(newItem);
			}
			newItem->setParent(this);
		} else if (isBrowseFieldVisibleItem(oldItem)) {
			Cylinder* oldParent = oldItem->getParent();
			browseField->removeThing(oldItem, oldItem->getItemCount());
			oldItem->setParent(oldParent);
		}
	}

	const Position& cylinderMapPos = getPosition();

	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, cylinderMapPos, true);

	// send to client
	for (const auto& spectator : spectators.players()) {
		Player* tmpPlayer = static_cast<Player*>(spectator.get());
		if (InstanceUtils::canSeeItemInInstance(tmpPlayer->getInstanceID(), newItem)) {
			tmpPlayer->sendUpdateTileItem(this, cylinderMapPos, newItem);
		}
	}

	// event methods
	for (const auto& spectator : spectators) {
		spectator->onUpdateTileItem(this, cylinderMapPos, oldItem, oldType, newItem, newType);
	}
	const bool sourceChange =
	    oldType.isCarpet() || newType.isCarpet() || oldType.isGroundTile() || newType.isGroundTile();
	if ((sourceChange || affectsAutoCarpetFurniture(oldType) || affectsAutoCarpetFurniture(newType)) &&
	    !oldItem->getCustomAttribute("__auto_carpet_source") && !newItem->getCustomAttribute("__auto_carpet_source")) {
		// Do not read raw item pointers after callbacks may have removed them.
		const auto oldInstance = oldItem->getInstanceID(), newInstance = newItem->getInstanceID();
		if (sourceChange) {
			synchronizeAutoCarpets(oldInstance);
			if (newInstance != oldInstance) {
				synchronizeAutoCarpets(newInstance);
			}
		} else if (oldInstance == 0 || newInstance == 0) {
			synchronizeAutoCarpet();
		}
	}
}

void Tile::onRemoveTileItem(const SpectatorVec& spectators, const std::vector<int32_t>& oldStackPosVector, Item* item)
{
	// Reconciliation can remove this tile; retain it through the rest of the hook.
	const auto self = weak_from_this().lock();
	if (!self) {
		// Standalone tiles still need native bookkeeping before map ownership.
		resetTileFlags(item);
		return;
	}

	if (isBrowseFieldVisibleItem(item)) {
		if (auto browseField = g_game.getBrowseFieldContainer(this)) {
			browseField->removeThing(item, item->getItemCount());
		}
	}

	resetTileFlags(item);

	const Position& cylinderMapPos = getPosition();
	const ItemType& iType = Item::items[item->getID()];

	// send to client
	size_t i = 0;
	for (const auto& spectator : spectators.players()) {
		Player* tmpPlayer = static_cast<Player*>(spectator.get());
		if (InstanceUtils::canSeeItemInInstance(tmpPlayer->getInstanceID(), item)) {
			tmpPlayer->sendRemoveTileThing(cylinderMapPos, oldStackPosVector[i]);
		}
		++i;
	}

	// event methods
	for (const auto& spectator : spectators) {
		spectator->onRemoveTileItem(this, cylinderMapPos, iType, item);
	}
	if ((iType.isCarpet() || iType.isGroundTile()) && !item->getCustomAttribute("__auto_carpet_source")) {
		synchronizeAutoCarpets(item->getInstanceID());
	} else if (affectsAutoCarpetFurniture(iType) && item->getInstanceID() == 0 &&
	           !item->getCustomAttribute("__auto_carpet_source")) {
		synchronizeAutoCarpet();
	}

	if (!hasFlag(TILESTATE_PROTECTIONZONE) || getBoolean(ConfigManager::CLEAN_PROTECTION_ZONES)) {
		auto items = getItemList();
		if (!items || items->empty()) {
			g_game.removeTileToClean(this);
			return;
		}

		bool ret = false;
		for (const auto& toCheck : *items) {
			if (toCheck->isCleanable()) {
				ret = true;
				break;
			}
		}

		if (!ret) {
			g_game.removeTileToClean(this);
		}
	}
}

ReturnValue Tile::queryAdd(int32_t, const Thing& thing, uint32_t, uint32_t flags, Creature* actor) const
{
	const uint32_t actorInstanceID = actor ? actor->getInstanceID() : 0;

	if (const Creature* creature = thing.getCreature()) {
		if (hasBitSet(FLAG_NOLIMIT, flags)) {
			return RETURNVALUE_NOERROR;
		}

		if (hasBitSet(FLAG_PATHFINDING, flags) && hasFlag(TILESTATE_FLOORCHANGE | TILESTATE_TELEPORT)) {
			return RETURNVALUE_NOTPOSSIBLE;
		}

		if (ground == nullptr) {
			return RETURNVALUE_NOTPOSSIBLE;
		}

		if (const Monster* monster = creature->getMonster()) {
			const bool canEnterProtectionZone = monster->isFamiliar() &&
			                                    getBoolean(ConfigManager::FAMILIAR_ENTER_PZ);
			if ((!canEnterProtectionZone && hasFlag(TILESTATE_PROTECTIONZONE)) ||
			    hasFlag(TILESTATE_FLOORCHANGE | TILESTATE_TELEPORT)) {
				return RETURNVALUE_NOTPOSSIBLE;
			}

			const CreatureVector* creatures = getCreatures();
			if (monster->canPushCreatures() && !monster->isSummon()) {
				if (creatures) {
					for (const auto& tileCreature : *creatures) {
						if (tileCreature->getPlayer() && tileCreature->getPlayer()->isInGhostMode()) {
							continue;
						}

						if (!monster->compareInstance(tileCreature->getInstanceID())) {
							continue;
						}

						const Monster* creatureMonster = tileCreature->getMonster();
						auto creatureMaster = tileCreature->getMaster();
						if (!creatureMonster || !tileCreature->isPushable() ||
						    (creatureMonster->isSummon() && creatureMaster && creatureMaster->getPlayer())) {
							return RETURNVALUE_NOTPOSSIBLE;
						}
					}
				}
			} else if (creatures && !creatures->empty()) {
				for (const auto& tileCreature : *creatures) {
					if (!tileCreature->isInGhostMode() && monster->compareInstance(tileCreature->getInstanceID())) {
						return RETURNVALUE_NOTENOUGHROOM;
					}
				}
			}

			if (hasFlag(TILESTATE_IMMOVABLEBLOCKSOLID) || hasInstancedProperty(CONST_PROP_IMMOVABLEBLOCKSOLID, actorInstanceID)) {
				return RETURNVALUE_NOTPOSSIBLE;
			}

			if (hasBitSet(FLAG_PATHFINDING, flags) && (hasFlag(TILESTATE_IMMOVABLENOFIELDBLOCKPATH) ||
													   hasInstancedProperty(CONST_PROP_IMMOVABLENOFIELDBLOCKPATH, actorInstanceID))) {
				return RETURNVALUE_NOTPOSSIBLE;
			}

			if ((hasFlag(TILESTATE_BLOCKSOLID) || hasInstancedProperty(CONST_PROP_BLOCKSOLID, actorInstanceID)) ||
			    (hasBitSet(FLAG_PATHFINDING, flags) && (hasFlag(TILESTATE_NOFIELDBLOCKPATH) ||
														hasInstancedProperty(CONST_PROP_NOFIELDBLOCKPATH, actorInstanceID)))) {
				if (!(monster->canPushItems() || hasBitSet(FLAG_IGNOREBLOCKITEM, flags))) {
					return RETURNVALUE_NOTPOSSIBLE;
				}
			}

			MagicField* field = getFieldItem(actorInstanceID);
			if (!field || field->isBlocking() || field->getDamage() == 0) {
				return RETURNVALUE_NOERROR;
			}

			CombatType_t combatType = field->getCombatType();

			// There are 3 options for a monster to enter a magic field:
			// 1) Monster is immune
			// 2) Monster is explicitly allowed to walk over that field type
			// 3) Monster is in combat with the player that owns this field
			if (!monster->isImmune(combatType)) {
				if (hasBitSet(FLAG_IGNOREFIELDDAMAGE, flags)) {
					const uint32_t fieldOwner = field->getOwner();
					const bool canForceField = fieldOwner != 0 && monster->isIgnoringFieldDamage() &&
					                           const_cast<Monster*>(monster)->hasBeenAttacked(fieldOwner);
					if (!monster->canWalkOnFieldType(combatType) && !canForceField) {
						return RETURNVALUE_NOTPOSSIBLE;
					}
				} else {
					return RETURNVALUE_NOTPOSSIBLE;
				}
			}

			return RETURNVALUE_NOERROR;
		}

		const CreatureVector* creatures = getCreatures();
		if (const Player* player = creature->getPlayer()) {
			if (creatures && !creatures->empty() && !hasBitSet(FLAG_IGNOREBLOCKCREATURE, flags) &&
			    !player->isAccessPlayer()) {
				for (const auto& tileCreature : *creatures) {
					if (!player->canWalkthrough(tileCreature.get())) {
						return RETURNVALUE_NOTPOSSIBLE;
					}
				}
			}

			if (MagicField* field = getFieldItem(actorInstanceID)) {
				if (field->getDamage() != 0 && hasBitSet(FLAG_PATHFINDING, flags) &&
				    !hasBitSet(FLAG_IGNOREFIELDDAMAGE, flags)) {
					return RETURNVALUE_NOTPOSSIBLE;
				}
			}

			if (!player->getParent() && hasFlag(TILESTATE_NOLOGOUT)) {
				// player is trying to login to a "no logout" tile
				return RETURNVALUE_NOTPOSSIBLE;
			}

			const Tile* playerTile = player->getTile();
			if (playerTile && player->isPzLocked()) {
				if (!playerTile->hasFlag(TILESTATE_PVPZONE)) {
					// player is trying to enter a pvp zone while being pz-locked
					if (hasFlag(TILESTATE_PVPZONE)) {
						return RETURNVALUE_PLAYERISPZLOCKEDENTERPVPZONE;
					}
				} else if (!hasFlag(TILESTATE_PVPZONE)) {
					// player is trying to leave a pvp zone while being pz-locked
					return RETURNVALUE_PLAYERISPZLOCKEDLEAVEPVPZONE;
				}

				if ((!playerTile->hasFlag(TILESTATE_NOPVPZONE) && hasFlag(TILESTATE_NOPVPZONE)) ||
				    (!playerTile->hasFlag(TILESTATE_PROTECTIONZONE) && hasFlag(TILESTATE_PROTECTIONZONE))) {
					// player is trying to enter a non-pvp/protection zone while being pz-locked
					return RETURNVALUE_PLAYERISPZLOCKED;
				}
			}
		} else if (creatures && !creatures->empty() && !hasBitSet(FLAG_IGNOREBLOCKCREATURE, flags)) {
			for (const auto& tileCreature : *creatures) {
				if (!tileCreature->isInGhostMode() &&
        			creature->compareInstance(tileCreature->getInstanceID())) {
					return RETURNVALUE_NOTENOUGHROOM;
				}
			}
		}

		if (!hasBitSet(FLAG_IGNOREBLOCKITEM, flags)) {
			// If the FLAG_IGNOREBLOCKITEM bit isn't set we dont have to iterate every single item
			if (hasFlag(TILESTATE_BLOCKSOLID) || hasInstancedProperty(CONST_PROP_BLOCKSOLID, actorInstanceID)) {
				return RETURNVALUE_NOTENOUGHROOM;
			}
		} else {
			// FLAG_IGNOREBLOCKITEM is set
			if (ground) {
				const ItemType& iiType = Item::items[ground->getID()];
				if (iiType.blockSolid && (!iiType.moveable || ground->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID))) {
					return RETURNVALUE_NOTPOSSIBLE;
				}
			}

			if (const auto items = getItemList()) {
				for (const auto& item : *items) {
					if (item->getInstanceID() != 0 &&
							item->getInstanceID() != actorInstanceID) {
						continue;
					}
					const ItemType& iiType = Item::items[item->getID()];
					if (iiType.blockSolid && (!iiType.moveable || item->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID))) {
						return RETURNVALUE_NOTPOSSIBLE;
					}
				}
			}
		}
	} else if (const Item* item = thing.getItem()) {
		const TileItemVector* items = getItemList();
		if (items && items->size() >= 0xFFFF) {
			return RETURNVALUE_NOTPOSSIBLE;
		}

		if (hasBitSet(FLAG_NOLIMIT, flags)) {
			return RETURNVALUE_NOERROR;
		}

		if (items && items->size() >= TILE_MAX_ITEMS) {
			return RETURNVALUE_CANNOTADDMOREITEMSONTILE;
		}

		if (item->isStoreItem()) {
			return RETURNVALUE_ITEMCANNOTBEMOVEDTHERE;
		}

		if (hasFlag(TILESTATE_TRASHHOLDER) && item->hasProperty(CONST_PROP_MOVEABLE)) {
			// Trash holders intentionally accept movable items before floor and blocking checks.
			return RETURNVALUE_NOERROR;
		}

		bool itemIsHangable = item->isHangable();
		if (ground == nullptr && !itemIsHangable) {
			return RETURNVALUE_NOTPOSSIBLE;
		}

		const CreatureVector* creatures = getCreatures();
		if (creatures && !creatures->empty() && item->isBlocking() && !hasBitSet(FLAG_IGNOREBLOCKCREATURE, flags)) {
			for (const auto& tileCreature : *creatures) {
				if (!tileCreature->isInGhostMode()) {
					return RETURNVALUE_NOTENOUGHROOM;
				}
			}
		}

		if (itemIsHangable && hasFlag(TILESTATE_SUPPORTS_HANGABLE)) {
			if (items) {
				for (const auto& tileItem : *items) {
					if (tileItem->isHangable()) {
						return RETURNVALUE_NEEDEXCHANGE;
					}
				}
			}
		} else {
			if (ground) {
				const ItemType& iiType = Item::items[ground->getID()];
				if (iiType.blockSolid) {
					if (!iiType.ignoreBlocking || item->isMagicField() || item->isBlocking()) {
						if (!item->isPickupable()) {
							return RETURNVALUE_NOTENOUGHROOM;
						}

						if (!iiType.hasHeight || iiType.pickupable || iiType.isBed()) {
							return RETURNVALUE_NOTENOUGHROOM;
						}
					}
				}
			}

			if (items) {
				for (const auto& tileItem : *items) {
					if (tileItem->getInstanceID() != 0 &&
							tileItem->getInstanceID() != actorInstanceID) {
						continue;
					}
					const ItemType& iiType = Item::items[tileItem->getID()];
					if (!iiType.blockSolid) {
						continue;
					}

					if (iiType.ignoreBlocking && !item->isMagicField() && !item->isBlocking()) {
						continue;
					}

					if (!item->isPickupable()) {
						return RETURNVALUE_NOTENOUGHROOM;
					}

					if (!iiType.hasHeight || iiType.pickupable || iiType.isBed()) {
						return RETURNVALUE_NOTENOUGHROOM;
					}
				}
			}
		}
	}
	return RETURNVALUE_NOERROR;
}

ReturnValue Tile::queryMaxCount(int32_t, const Thing&, uint32_t count, uint32_t& maxQueryCount, uint32_t) const
{
	maxQueryCount = std::max<uint32_t>(1, count);
	return RETURNVALUE_NOERROR;
}

ReturnValue Tile::queryRemove(const Thing& thing, uint32_t count, uint32_t flags, Creature* /*= nullptr */) const
{
	int32_t index = getThingIndex(&thing);
	if (index == -1) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	const Item* item = thing.getItem();
	if (item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (count == 0 || (item->isStackable() && count > item->getItemCount())) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!item->isMoveable() && !hasBitSet(FLAG_IGNORENOTMOVEABLE, flags)) {
		return RETURNVALUE_NOTMOVEABLE;
	}

	return RETURNVALUE_NOERROR;
}

Tile* Tile::queryDestination(int32_t&, const Thing& thing, Item** destItem, uint32_t& flags,
                             uint32_t destinationInstanceId)
{
	Tile* destTile = nullptr;
	*destItem = nullptr;

	if (hasFlag(TILESTATE_FLOORCHANGE_DOWN)) {
		uint16_t dx = tilePos.x;
		uint16_t dy = tilePos.y;
		uint8_t dz = tilePos.z + 1;

		Tile* southDownTile = g_game.map.getTile(dx, dy - 1, dz);
		if (southDownTile && southDownTile->hasFlag(TILESTATE_FLOORCHANGE_SOUTH_ALT)) {
			dy -= 2;
			destTile = g_game.map.getTile(dx, dy, dz);
		} else {
			Tile* eastDownTile = g_game.map.getTile(dx - 1, dy, dz);
			if (eastDownTile && eastDownTile->hasFlag(TILESTATE_FLOORCHANGE_EAST_ALT)) {
				dx -= 2;
				destTile = g_game.map.getTile(dx, dy, dz);
			} else {
				Tile* downTile = g_game.map.getTile(dx, dy, dz);
				if (downTile) {
					if (downTile->hasFlag(TILESTATE_FLOORCHANGE_NORTH)) {
						++dy;
					}

					if (downTile->hasFlag(TILESTATE_FLOORCHANGE_SOUTH)) {
						--dy;
					}

					if (downTile->hasFlag(TILESTATE_FLOORCHANGE_SOUTH_ALT)) {
						dy -= 2;
					}

					if (downTile->hasFlag(TILESTATE_FLOORCHANGE_EAST)) {
						--dx;
					}

					if (downTile->hasFlag(TILESTATE_FLOORCHANGE_EAST_ALT)) {
						dx -= 2;
					}

					if (downTile->hasFlag(TILESTATE_FLOORCHANGE_WEST)) {
						++dx;
					}

					destTile = g_game.map.getTile(dx, dy, dz);
				}
			}
		}
	} else if (hasFlag(TILESTATE_FLOORCHANGE)) {
		uint16_t dx = tilePos.x;
		uint16_t dy = tilePos.y;
		uint8_t dz = tilePos.z - 1;

		if (hasFlag(TILESTATE_FLOORCHANGE_NORTH)) {
			--dy;
		}

		if (hasFlag(TILESTATE_FLOORCHANGE_SOUTH)) {
			++dy;
		}

		if (hasFlag(TILESTATE_FLOORCHANGE_EAST)) {
			++dx;
		}

		if (hasFlag(TILESTATE_FLOORCHANGE_WEST)) {
			--dx;
		}

		if (hasFlag(TILESTATE_FLOORCHANGE_SOUTH_ALT)) {
			dy += 2;
		}

		if (hasFlag(TILESTATE_FLOORCHANGE_EAST_ALT)) {
			dx += 2;
		}

		destTile = g_game.map.getTile(dx, dy, dz);
	}

	if (destTile == nullptr) {
		destTile = this;
	} else {
		flags |= FLAG_NOLIMIT; // Will ignore that there is blocking items/creatures
	}

	if (destTile) {
		const Item* movingItem = thing.getItem();
		if (movingItem && movingItem->isStackable() && !hasBitSet(FLAG_IGNOREAUTOSTACK, flags)) {
			if (const TileItemVector* items = destTile->getItemList()) {
				for (auto it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end; ++it) {
					Item* candidate = it->get();
					if (candidate != movingItem && candidate->getInstanceID() == destinationInstanceId &&
					    candidate->equalsIgnoringInstance(movingItem) &&
					    candidate->getItemCount() < candidate->getStackSize()) {
						*destItem = candidate;
						break;
					}
				}
			}
		} else if (const TileItemVector* items = destTile->getItemList()) {
			for (auto it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end; ++it) {
				if ((*it)->getInstanceID() == destinationInstanceId) {
					*destItem = it->get();
					break;
				}
			}
		}
	}
	return destTile;
}

void Tile::addThing(Thing* thing) { addThing(0, thing); }

void Tile::addThing(int32_t, Thing* thing)
{
	Creature* creature = thing->getCreature();
	if (creature) {
		auto creatureRef = getSharedCreature(creature);
		if (!creatureRef) {
			logSharedCreatureLockFailure("Tile::addThing", creature);
			return;
		}

		creature->setParent(this);
		CreatureVector* creatures = makeCreatures();
		creatures->insert(creatures->begin(), std::move(creatureRef));
	} else {
		Item* item = thing->getItem();
		if (item == nullptr) {
			return /*RETURNVALUE_NOTPOSSIBLE*/;
		}

		auto itemRef = getSharedItem(item);
		if (!itemRef) {
			logSharedItemLockFailure("Tile::addThing", item);
			return /*RETURNVALUE_NOTPOSSIBLE*/;
		}

		TileItemVector* items = getItemList();
		if (items && items->size() >= 0xFFFF) {
			return /*RETURNVALUE_NOTPOSSIBLE*/;
		}

		item->setParent(this);

		const ItemType& itemType = Item::items[item->getID()];
		if (itemType.isGroundTile()) {
			if (ground == nullptr) {
				ground = itemRef;
				onAddTileItem(item);
			} else {
				// The update callback can detach this tile before postRemoveNotification.
				const auto self = weak_from_this().lock();
				const ItemType& oldType = Item::items[ground->getID()];

				auto oldGroundSp = std::move(ground);
				Item* oldGround = oldGroundSp.get();
				oldGround->setParent(nullptr);
				ground = itemRef;
				resetTileFlags(oldGround);
				setTileFlags(item);
				onUpdateTileItem(oldGround, oldType, item, itemType);
				postRemoveNotification(oldGround, nullptr, 0);
			}
		} else if (itemType.alwaysOnTop) {
			if (itemType.isSplash() && items) {
				// remove old splash if exists
				for (ItemVector::const_iterator it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end;
				     ++it) {
					if (!Item::items[(*it)->getID()].isSplash()) {
						continue;
					}

					auto oldSplashSp = *it;
					Item* oldSplash = oldSplashSp.get();
					removeThing(oldSplash, 1);
					oldSplash->setParent(nullptr);
					postRemoveNotification(oldSplash, nullptr, 0);
					break;
				}
			}

			bool isInserted = false;

			if (items) {
				for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
					// Note: this is different from internalAddThing
					if (itemType.alwaysOnTopOrder <= Item::items[(*it)->getID()].alwaysOnTopOrder) {
						items->insert(it, itemRef);
						isInserted = true;
						break;
					}
				}
			} else {
				items = makeItemList();
			}

			if (!isInserted) {
				items->push_back(itemRef);
			}

			onAddTileItem(item);
		} else {
			if (itemType.isMagicField()) {
				// remove old field item if exists
				if (items) {
					for (ItemVector::const_iterator it = items->getBeginDownItem(), end = items->getEndDownItem();
					     it != end; ++it) {
						MagicField* oldField = (*it)->getMagicField();
						if (oldField) {
							if (oldField->isReplaceable()) {
								auto fieldItemSp = *it;
								removeThing(oldField, 1);

								oldField->setParent(nullptr);
								postRemoveNotification(oldField, nullptr, 0);
								break;
							} else {
								// This magic field cannot be replaced.
								item->setParent(nullptr);
								return;
							}
						}
					}
				}
			}

			items = makeItemList();
			items->insert(items->getBeginDownItem(), itemRef);
			items->addDownItemCount(1);
			onAddTileItem(item);
		}
	}
}

void Tile::updateThing(Thing* thing, uint16_t itemId, uint32_t count)
{
	int32_t index = getThingIndex(thing);
	if (index == -1) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (item == nullptr) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	const ItemType& oldType = Item::items[item->getID()];
	const ItemType& newType = Item::items[itemId];
	resetTileFlags(item);
	item->setID(itemId);
	item->setSubType(static_cast<uint16_t>(count));
	setTileFlags(item);
	onUpdateTileItem(item, oldType, item, newType);
}

void Tile::refreshThing(Thing* thing)
{
	Item* item = thing ? thing->getItem() : nullptr;
	if (!item) {
		return;
	}

	if (getThingIndex(thing) == -1) {
		return;
	}

	const ItemType& itemType = Item::items[item->getID()];
	onUpdateTileItem(item, itemType, item, itemType);
}

void Tile::replaceThing(uint32_t index, Thing* thing)
{
	int32_t pos = index;

	Item* item = thing->getItem();
	if (item == nullptr) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	auto itemRef = getSharedItem(item);
	if (!itemRef) {
		logSharedItemLockFailure("Tile::replaceThing", item);
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* oldItem = nullptr;
	std::shared_ptr<Item> oldItemSp;
	bool isInserted = false;

	if (ground) {
		if (pos == 0) {
			oldItem = ground.get();
			oldItemSp = ground;
			ground = itemRef;
			isInserted = true;
		}

		--pos;
	}

	TileItemVector* items = getItemList();
	if (items && !isInserted) {
		int32_t topItemSize = getTopItemCount();
		if (pos < topItemSize) {
			auto it = items->getBeginTopItem();
			it += pos;

			oldItem = it->get();
			oldItemSp = *it;
			it = items->erase(it);
			items->insert(it, itemRef);
			isInserted = true;
		}

		pos -= topItemSize;
	}

	CreatureVector* creatures = getCreatures();
	if (creatures) {
		if (!isInserted && pos < static_cast<int32_t>(creatures->size())) {
			return /*RETURNVALUE_NOTPOSSIBLE*/;
		}

		pos -= static_cast<uint32_t>(creatures->size());
	}

	if (items && !isInserted) {
		int32_t downItemSize = getDownItemCount();
		if (pos < downItemSize) {
			auto it = items->getBeginDownItem() + pos;
			oldItem = it->get();
			oldItemSp = *it;
			it = items->erase(it);
			items->insert(it, itemRef);
			isInserted = true;
		}
	}

	if (isInserted) {
		item->setParent(this);

		resetTileFlags(oldItem);
		setTileFlags(item);
		const ItemType& oldType = Item::items[oldItem->getID()];
		const ItemType& newType = Item::items[item->getID()];
		onUpdateTileItem(oldItem, oldType, item, newType);

		oldItem->setParent(nullptr);
		return /*RETURNVALUE_NOERROR*/;
	}
}

void Tile::removeThing(Thing* thing, uint32_t count)
{
	Creature* creature = thing->getCreature();
	if (creature) {
		CreatureVector* creatures = getCreatures();
		if (creatures) {
			auto it = std::find_if(creatures->begin(), creatures->end(),
				[creature](const auto& sp) { return sp.get() == creature; });
			if (it != creatures->end()) {
				creatures->erase(it);
			}
		}
		return;
	}

	Item* item = thing->getItem();
	if (!item) {
		return;
	}

	int32_t index = getThingIndex(item);
	if (index == -1) {
		return;
	}

	if (item == ground.get()) {
		auto groundSp = std::move(ground);
		groundSp->setParent(nullptr);

		SpectatorVec spectators;
		g_game.map.getSpectators(spectators, getPosition(), true);
		onRemoveTileItem(spectators, std::vector<int32_t>(spectators.size(), 0), item);
		return;
	}

	TileItemVector* items = getItemList();
	if (!items) {
		return;
	}

	const ItemType& itemType = Item::items[item->getID()];
	if (itemType.alwaysOnTop) {
		auto it = std::find_if(items->getBeginTopItem(), items->getEndTopItem(), [item](const auto& sp){ return sp.get() == item; });
		if (it == items->getEndTopItem()) {
			return;
		}

		SpectatorVec spectators;
		g_game.map.getSpectators(spectators, getPosition(), true);

		std::vector<int32_t> oldStackPosVector;
		oldStackPosVector.reserve(spectators.size());
		for (const auto& spectator : spectators.players()) {
			Player* tmpPlayer = static_cast<Player*>(spectator.get());
			oldStackPosVector.push_back(getStackposOfItem(tmpPlayer, item));
		}

		auto itemSp = *it;
		item->setParent(nullptr);
		items->erase(it);
		onRemoveTileItem(spectators, oldStackPosVector, item);
	} else {
		auto it = std::find_if(items->getBeginDownItem(), items->getEndDownItem(), [item](const auto& sp){ return sp.get() == item; });
		if (it == items->getEndDownItem()) {
			return;
		}

		if (itemType.stackable && count != item->getItemCount()) {
			uint8_t newCount =
			    static_cast<uint8_t>(std::max<int32_t>(0, static_cast<int32_t>(item->getItemCount() - count)));
			item->setItemCount(newCount);
			onUpdateTileItem(item, itemType, item, itemType);
		} else {
			SpectatorVec spectators;
			g_game.map.getSpectators(spectators, getPosition(), true);

			std::vector<int32_t> oldStackPosVector;
			oldStackPosVector.reserve(spectators.size());
			for (const auto& spectator : spectators.players()) {
				Player* tmpPlayer = static_cast<Player*>(spectator.get());
				oldStackPosVector.push_back(getStackposOfItem(tmpPlayer, item));
			}

			auto itemSp = *it;
			item->setParent(nullptr);
			items->erase(it);
			items->addDownItemCount(-1);
			onRemoveTileItem(spectators, oldStackPosVector, item);
		}
	}
}

bool Tile::hasCreature(Creature* creature) const
{
	if (const CreatureVector* creatures = getCreatures()) {
		return std::find_if(creatures->begin(), creatures->end(),
			[creature](const auto& sp) { return sp.get() == creature; }) != creatures->end();
	}
	return false;
}

void Tile::removeCreature(Creature* creature)
{
	g_game.map.getQTNode(tilePos.x, tilePos.y)->removeCreature(creature);
	removeThing(creature, 0);
}

int32_t Tile::getThingIndex(const Thing* thing) const
{
	int32_t n = -1;
	if (ground) {
		if (ground.get() == thing) {
			return 0;
		}
		++n;
	}

	const TileItemVector* items = getItemList();
	if (items) {
		const Item* item = thing->getItem();
		if (item && item->isAlwaysOnTop()) {
			for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
				++n;
				if (it->get() == item) {
					return n;
				}
			}
		} else {
			n += items->getTopItemCount();
		}
	}

	if (const CreatureVector* creatures = getCreatures()) {
		if (thing->getCreature()) {
			for (const auto& creature : *creatures) {
				++n;
				if (creature.get() == thing) {
					return n;
				}
			}
		} else {
			n += creatures->size();
		}
	}

	if (items) {
		const Item* item = thing->getItem();
		if (item && !item->isAlwaysOnTop()) {
			for (auto it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end; ++it) {
				++n;
				if (it->get() == item) {
					return n;
				}
			}
		}
	}
	return -1;
}

int32_t Tile::getClientIndexOfCreature(const Player* player, const Creature* creature) const
{
	int32_t n;
	if (ground) {
		n = 1;
	} else {
		n = 0;
	}

	const uint32_t viewerInstanceId = player->getInstanceID();
	const TileItemVector* items = getItemList();
	if (items) {
		for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
			if (InstanceUtils::canSeeItemInInstance(viewerInstanceId, it->get())) {
				++n;
			}
		}
	}

	if (const CreatureVector* creatures = getCreatures()) {
		for (auto it = creatures->rbegin(), end = creatures->rend(); it != end; ++it) {
			const Creature* c = it->get();
			if (c == creature) {
				return n;
			} else if (player->canSeeCreature(c)) {
				++n;
			}
		}
	}
	return -1;
}

int32_t Tile::getStackposOfItem(const Player* player, const Item* item) const
{
	int32_t n = 0;
	if (ground) {
		if (ground.get() == item) {
			return n;
		}
		++n;
	}

	const uint32_t viewerInstanceId = player->getInstanceID();
	const TileItemVector* items = getItemList();
	if (items) {
		if (item->isAlwaysOnTop()) {
			for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
				if (it->get() == item) {
					return n;
				}
				if (InstanceUtils::canSeeItemInInstance(viewerInstanceId, it->get())) {
					++n;
				}
			}
		} else {
			for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
				if (InstanceUtils::canSeeItemInInstance(viewerInstanceId, it->get())) {
					++n;
				}
			}
		}
	}

	if (const CreatureVector* creatures = getCreatures()) {
		for (const auto& creature : *creatures) {
			if (player->canSeeCreature(creature.get())) {
				++n;
			}
		}
	}

	if (items && !item->isAlwaysOnTop()) {
		for (auto it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end; ++it) {
			if (it->get() == item) {
				return n;
			}
			if (InstanceUtils::canSeeItemInInstance(viewerInstanceId, it->get())) {
				++n;
			}
		}
	}
	return -1;
}

size_t Tile::getFirstIndex() const { return 0; }

size_t Tile::getLastIndex() const { return getThingCount(); }

uint32_t Tile::getItemTypeCount(uint16_t itemId, int32_t subType /*= -1*/, bool /*ignoreEquipped = false*/) const
{
	uint32_t count = 0;
	if (ground && ground->getID() == itemId) {
		count += Item::countByType(ground.get(), subType);
	}

	const TileItemVector* items = getItemList();
	if (items) {
		for (const auto& item : *items) {
			if (item->getID() == itemId) {
				count += Item::countByType(item.get(), subType);
			}
		}
	}
	return count;
}

Thing* Tile::getThing(size_t index) const
{
	if (ground) {
		if (index == 0) {
			return ground.get();
		}

		--index;
	}

	const TileItemVector* items = getItemList();
	if (items) {
		uint32_t topItemSize = items->getTopItemCount();
		if (index < topItemSize) {
			return items->at(items->getDownItemCount() + index).get();
		}
		index -= topItemSize;
	}

	if (const CreatureVector* creatures = getCreatures()) {
		if (index < creatures->size()) {
			return (*creatures)[index].get();
		}
		index -= creatures->size();
	}

	if (items && index < items->getDownItemCount()) {
		return items->at(index).get();
	}
	return nullptr;
}

Thing* Tile::getThing(const Player* player, size_t index) const
{
	if (!player) {
		return getThing(index);
	}

	if (ground) {
		if (index == 0) {
			return ground.get();
		}
		--index;
	}

	const uint32_t instanceId = player->getInstanceID();
	const TileItemVector* items = getItemList();
	if (items) {
		for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
			if (!InstanceUtils::canSeeItemInInstance(instanceId, it->get())) {
				continue;
			}
			if (index-- == 0) {
				return it->get();
			}
		}
	}

	if (const CreatureVector* creatures = getCreatures()) {
		for (auto it = creatures->rbegin(), end = creatures->rend(); it != end; ++it) {
			if (!player->canSeeCreature(it->get())) {
				continue;
			}
			if (index-- == 0) {
				return it->get();
			}
		}
	}

	if (items) {
		for (auto it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end; ++it) {
			if (!InstanceUtils::canSeeItemInInstance(instanceId, it->get())) {
				continue;
			}
			if (index-- == 0) {
				return it->get();
			}
		}
	}
	return nullptr;
}

void Tile::postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index,
                               cylinderlink_t link /*= LINK_OWNER*/)
{
	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, getPosition(), true, true);
	for (const auto& spectator : spectators.players()) {
		Player* player = static_cast<Player*>(spectator.get());
		if (!InstanceUtils::isPlayerInSameInstance(player, thing->getInstanceID())) {
			continue;
		}

		player->postAddNotification(thing, oldParent, index, LINK_NEAR);
	}

	// add a reference to this item, it may be deleted after being added (mailbox for example)
	Creature* creature = thing->getCreature();
	Item* item;
	if (creature) {
		item = nullptr;
	} else {
		item = thing->getItem();
	}

	if (link == LINK_OWNER) {
		if (hasFlag(TILESTATE_TELEPORT)) {
			Teleport* teleport = getTeleportItem();
			if (teleport) {
				teleport->addThing(thing);
			}
		} else if (hasFlag(TILESTATE_TRASHHOLDER)) {
			TrashHolder* trashholder = getTrashHolder();
			if (trashholder) {
				trashholder->addThing(thing);
			}
		} else if (hasFlag(TILESTATE_MAILBOX)) {
			Mailbox* mailbox = getMailbox();
			if (mailbox) {
				mailbox->addThing(thing);
			}
		}

		// calling movement scripts
		if (g_moveEvents) {
			if (creature) {
				g_moveEvents->onCreatureMove(creature, this, MOVE_EVENT_STEP_IN);
			} else if (item) {
				g_moveEvents->onItemMove(item, this, true);
			}
		}
	}

	// release the reference to this item onces we are finished
	if (creature) {
		g_game.ReleaseCreature(creature);
	}
}

void Tile::postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index, cylinderlink_t)
{
	const auto thingCount = getThingCount();
	// Movement callbacks may delete this tile. Only pending destinations need a hold.
	const auto pendingCarpetTile = autoCarpetPending ? weak_from_this().lock() : nullptr;

	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, tilePos, true, true);

	for (const auto& spectator : spectators.players()) {
		Player* player = static_cast<Player*>(spectator.get());
		if (!InstanceUtils::isPlayerInSameInstance(player, thing->getInstanceID())) {
			continue;
		}

		if (thingCount > TILE_UPDATE_THRESHOLD) {
			player->sendUpdateTile(this, tilePos);
		}

		player->postRemoveNotification(thing, newParent, index, LINK_NEAR);
	}

	if (g_moveEvents) {
		if (Creature* creature = thing->getCreature()) {
			g_moveEvents->onCreatureMove(creature, this, MOVE_EVENT_STEP_OUT);
		} else if (Item* item = thing->getItem()) {
			g_moveEvents->onItemMove(item, this, false);
		}
	}
	// Ordinary removals only check this flag; no neighborhood lookup or timer.
	if (pendingCarpetTile && pendingCarpetTile->autoCarpetPending &&
	    pendingCarpetTile->getThingCount() < MAX_STACKPOS_THINGS) {
		pendingCarpetTile->synchronizeAutoCarpet();
	}
}

void Tile::internalAddThing(Thing* thing) { internalAddThing(0, thing); }

void Tile::internalAddThing(uint32_t, Thing* thing)
{
	if (!thing) {
		return;
	}

	Creature* creature = thing->getCreature();
	if (creature) {
		auto creatureRef = getSharedCreature(creature);
		if (!creatureRef) {
			logSharedCreatureLockFailure("Tile::internalAddThing", creature);
			return;
		}

		creature->setParent(this);
		CreatureVector* creatures = makeCreatures();
		creatures->insert(creatures->begin(), std::move(creatureRef));
	} else {
		Item* item = thing->getItem();
		if (item == nullptr) {
			return;
		}

		auto itemRef = getSharedItem(item);
		if (!itemRef) {
			logSharedItemLockFailure("Tile::internalAddThing", item);
			return;
		}

		item->setParent(this);
		const ItemType& itemType = Item::items[item->getID()];
		if (itemType.isGroundTile()) {
			if (ground == nullptr) {
				ground = itemRef;
				setTileFlags(item);
			}
			return;
		}

		TileItemVector* items = makeItemList();
		if (items->size() >= 0xFFFF) {
			return /*RETURNVALUE_NOTPOSSIBLE*/;
		}

		if (itemType.alwaysOnTop) {
			bool isInserted = false;
			for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
				if (Item::items[(*it)->getID()].alwaysOnTopOrder > itemType.alwaysOnTopOrder) {
					items->insert(it, itemRef);
					isInserted = true;
					break;
				}
			}

			if (!isInserted) {
				items->push_back(itemRef);
			}
		} else {
			items->insert(items->getBeginDownItem(), itemRef);
			items->addDownItemCount(1);
		}

		setTileFlags(item);
	}
}

void Tile::setTileFlags(const Item* item)
{
	if (!hasFlag(TILESTATE_FLOORCHANGE)) {
		const ItemType& it = Item::items[item->getID()];
		if (it.floorChange != 0) {
			setFlag(it.floorChange);
		}
	}

	if (item->hasProperty(CONST_PROP_IMMOVABLEBLOCKSOLID)) {
		setFlag(TILESTATE_IMMOVABLEBLOCKSOLID);
	}

	if (item->hasProperty(CONST_PROP_BLOCKPATH)) {
		setFlag(TILESTATE_BLOCKPATH);
	}

	if (item->hasProperty(CONST_PROP_NOFIELDBLOCKPATH)) {
		setFlag(TILESTATE_NOFIELDBLOCKPATH);
	}

	if (item->hasProperty(CONST_PROP_IMMOVABLENOFIELDBLOCKPATH)) {
		setFlag(TILESTATE_IMMOVABLENOFIELDBLOCKPATH);
	}

	if (item->getTeleport()) {
		setFlag(TILESTATE_TELEPORT);
	}

	if (item->getMagicField()) {
		setFlag(TILESTATE_MAGICFIELD);
	}

	if (item->getMailbox()) {
		setFlag(TILESTATE_MAILBOX);
	}

	if (item->getTrashHolder()) {
		setFlag(TILESTATE_TRASHHOLDER);
	}

	if (item->hasProperty(CONST_PROP_BLOCKSOLID)) {
		setFlag(TILESTATE_BLOCKSOLID);
	}

	if (item->getBed()) {
		setFlag(TILESTATE_BED);
	}

	const Container* container = item->getContainer();
	if (container && container->getDepotLocker()) {
		setFlag(TILESTATE_DEPOT);
	}

	if (item->hasProperty(CONST_PROP_SUPPORTHANGABLE)) {
		setFlag(TILESTATE_SUPPORTS_HANGABLE);
	}
}

void Tile::resetTileFlags(const Item* item)
{
	const ItemType& it = Item::items[item->getID()];
	if (it.floorChange != 0) {
		resetFlag(TILESTATE_FLOORCHANGE);
	}

	if (item->hasProperty(CONST_PROP_BLOCKSOLID) && !hasProperty(item, CONST_PROP_BLOCKSOLID)) {
		resetFlag(TILESTATE_BLOCKSOLID);
	}

	if (item->hasProperty(CONST_PROP_IMMOVABLEBLOCKSOLID) && !hasProperty(item, CONST_PROP_IMMOVABLEBLOCKSOLID)) {
		resetFlag(TILESTATE_IMMOVABLEBLOCKSOLID);
	}

	if (item->hasProperty(CONST_PROP_BLOCKPATH) && !hasProperty(item, CONST_PROP_BLOCKPATH)) {
		resetFlag(TILESTATE_BLOCKPATH);
	}

	if (item->hasProperty(CONST_PROP_NOFIELDBLOCKPATH) && !hasProperty(item, CONST_PROP_NOFIELDBLOCKPATH)) {
		resetFlag(TILESTATE_NOFIELDBLOCKPATH);
	}

	if (item->hasProperty(CONST_PROP_IMMOVABLEBLOCKPATH) && !hasProperty(item, CONST_PROP_IMMOVABLEBLOCKPATH)) {
		resetFlag(TILESTATE_IMMOVABLEBLOCKPATH);
	}

	if (item->hasProperty(CONST_PROP_IMMOVABLENOFIELDBLOCKPATH) &&
	    !hasProperty(item, CONST_PROP_IMMOVABLENOFIELDBLOCKPATH)) {
		resetFlag(TILESTATE_IMMOVABLENOFIELDBLOCKPATH);
	}

	if (item->getTeleport()) {
		resetFlag(TILESTATE_TELEPORT);
	}

	if (item->getMagicField()) {
		resetFlag(TILESTATE_MAGICFIELD);
	}

	if (item->getMailbox()) {
		resetFlag(TILESTATE_MAILBOX);
	}

	if (item->getTrashHolder()) {
		resetFlag(TILESTATE_TRASHHOLDER);
	}

	if (item->getBed()) {
		resetFlag(TILESTATE_BED);
	}

	const Container* container = item->getContainer();
	if (container && container->getDepotLocker()) {
		resetFlag(TILESTATE_DEPOT);
	}

	if (item->hasProperty(CONST_PROP_SUPPORTHANGABLE)) {
		resetFlag(TILESTATE_SUPPORTS_HANGABLE);
	}
}

bool Tile::isMoveableBlocking() const { return !ground || hasFlag(TILESTATE_BLOCKSOLID); }

Item* Tile::getUseItem(int32_t index, const Player* player) const
{
	const TileItemVector* items = getItemList();

	// no items, get ground
	if (!items || items->size() == 0) {
		return ground.get();
	}

	// try getting thing by index
	if (Thing* thing = player ? getThing(player, index) : getThing(index)) {
		Item* thingItem = thing->getItem();
		if (thingItem) {
			return thingItem;
		}
	}

	// try getting top movable item
	Item* topDownItem = player ? getTopDownItem(player->getInstanceID()) : getTopDownItem();
	if (topDownItem) {
		return topDownItem;
	}

	// try getting door
	for (auto it = items->rbegin(), end = items->rend(); it != end; ++it) {
		if (player && !InstanceUtils::canSeeItemInInstance(player->getInstanceID(), it->get())) {
			continue;
		}
		if ((*it)->getDoor()) {
			return (*it)->getItem();
		}
	}

	for (const auto& item : *items) {
		if (!player || InstanceUtils::canSeeItemInInstance(player->getInstanceID(), item.get())) {
			return item.get();
		}
	}
	return ground.get();
}

void Tile::setZoneIds(std::vector<ZoneId> newZoneIds)
{
	newZoneIds.erase(std::remove(newZoneIds.begin(), newZoneIds.end(), ZoneId{}), newZoneIds.end());
	std::sort(newZoneIds.begin(), newZoneIds.end());
	newZoneIds.erase(std::unique(newZoneIds.begin(), newZoneIds.end()), newZoneIds.end());
	zoneIds = std::move(newZoneIds);
}

void Tile::addZoneId(ZoneId zoneId)
{
	if (zoneId == 0) {
		return;
	}

	const auto it = std::lower_bound(zoneIds.begin(), zoneIds.end(), zoneId);
	if (it == zoneIds.end() || *it != zoneId) {
		zoneIds.insert(it, zoneId);
	}
}

bool Tile::hasZoneId(ZoneId zoneId) const
{
	return std::binary_search(zoneIds.begin(), zoneIds.end(), zoneId);
}
