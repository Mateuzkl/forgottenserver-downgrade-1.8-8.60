// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "housetile.h"

#include "configmanager.h"
#include "game.h"
#include "house.h"
#include "logger.h"
#include <fmt/format.h>

extern Game g_game;

namespace {

std::shared_ptr<House> getSharedHouse(House* house)
{
	return house ? house->weak_from_this().lock() : nullptr;
}

} // namespace

HouseTile::HouseTile(uint16_t x, uint16_t y, uint8_t z, House* house) : DynamicTile(x, y, z), house(getSharedHouse(house)) {}

void HouseTile::addThing(int32_t index, Thing* thing)
{
	Tile::addThing(index, thing);

	if (!thing->getParent()) {
		return;
	}

	if (Item* item = thing->getItem()) {
		updateHouse(item);
	}
}

void HouseTile::internalAddThing(uint32_t index, Thing* thing)
{
	Tile::internalAddThing(index, thing);

	if (!thing->getParent()) {
		return;
	}

	if (Item* item = thing->getItem()) {
		updateHouse(item);
	}
}

void HouseTile::updateHouse(Item* item)
{
	if (item->getParent() != this) {
		return;
	}

	Door* door = item->getDoor();
	if (door) {
		if (door->getDoorId() != 0) {
			getHouse()->addDoor(door);
		}
	} else {
		BedItem* bed = item->getBed();
		if (bed) {
			getHouse()->addBed(bed);
		}
	}
}

ReturnValue HouseTile::queryAdd(int32_t index, const Thing& thing, uint32_t count, uint32_t flags,
                                Creature* actor /* = nullptr*/) const
{
	if (hasBitSet(FLAG_NOLIMIT, flags)) {
		return RETURNVALUE_NOERROR;
	}

	if (const Creature* creature = thing.getCreature()) {
		if (const Player* player = creature->getPlayer()) {
			if (!getHouse()->isInvited(player)) {
				if (getHouse()->getType() == HOUSE_TYPE_GUILDHALL) {
					return RETURNVALUE_ONLYGUILDMEMBERSMAYENTER;
				}
				return RETURNVALUE_PLAYERISNOTINVITED;
			}
		} else {
			return RETURNVALUE_NOTPOSSIBLE;
		}
	} else if (const Item* item = thing.getItem()) {
		if (item->isStoreItem() && !item->hasAttribute(ITEM_ATTRIBUTE_WRAPID)) {
			return RETURNVALUE_ITEMCANNOTBEMOVEDTHERE;
		}

		if (actor) {
			if (getHouse()->getProtected() && !getHouse()->canModifyItems(actor->getPlayer())) {
				return RETURNVALUE_CANNOTMOVEITEMISPROTECTED;
			}
			Player* actorPlayer = actor->getPlayer();
			if (!getHouse()->isInvited(actorPlayer)) {
				return RETURNVALUE_CANNOTTHROW;
			}
		}
	}
	return Tile::queryAdd(index, thing, count, flags, actor);
}

Tile* HouseTile::queryDestination(int32_t& index, const Thing& thing, Item** destItem, uint32_t& flags)
{
	if (const Creature* creature = thing.getCreature()) {
		if (const Player* player = creature->getPlayer()) {
			if (!getHouse()->isInvited(player)) {
				const Position& entryPos = getHouse()->getEntryPosition();
				Tile* destTile = g_game.map.getTile(entryPos);
				if (!destTile) {
					LOG_ERROR(fmt::format("[HouseTile::queryDestination] House entry not correct - Name: {} - House id: {} - Tile not found: {}", getHouse()->getName(), getHouse()->getId(), entryPos));

					destTile = g_game.map.getTile(player->getTemplePosition());
					if (!destTile) {
						destTile = &(Tile::nullptr_tile);
					}
				}

				index = -1;
				*destItem = nullptr;
				return destTile;
			}
		}
	}

	return Tile::queryDestination(index, thing, destItem, flags);
}

ReturnValue HouseTile::queryRemove(const Thing& thing, uint32_t count, uint32_t flags,
                                   Creature* actor /*= nullptr*/) const
{
	const Item* item = thing.getItem();
	if (!item) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (actor && getHouse()->getProtected() && !getHouse()->canModifyItems(actor->getPlayer())) {
		return RETURNVALUE_CANNOTMOVEITEMISPROTECTED;
	}

	if (actor && getBoolean(ConfigManager::ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS)) {
		if (!getHouse()->isInvited(actor->getPlayer())) {
			return RETURNVALUE_PLAYERISNOTINVITED;
		}
	}

	return Tile::queryRemove(thing, count, flags);
}
