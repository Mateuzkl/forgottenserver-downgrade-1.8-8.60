// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "supply_stash_rules.h"

#include "item.h"

#include "game.h"
#include "player.h"
#include "tile.h"

#include <algorithm>
#include <limits>

namespace tfs::supply_stash {

namespace {

// Every attribute here is per-instance state the stash cannot carry. Stowing an
// item that has one would drop it silently, and the withdraw would hand back a
// plain item — same list the Lua refuses on.
constexpr itemAttrTypes RESTRICTED_ATTRIBUTES[] = {
    ITEM_ATTRIBUTE_ACTIONID,     ITEM_ATTRIBUTE_UNIQUEID,   ITEM_ATTRIBUTE_DESCRIPTION,
    ITEM_ATTRIBUTE_TEXT,         ITEM_ATTRIBUTE_DATE,       ITEM_ATTRIBUTE_WRITER,
    ITEM_ATTRIBUTE_NAME,         ITEM_ATTRIBUTE_ARTICLE,    ITEM_ATTRIBUTE_PLURALNAME,
    ITEM_ATTRIBUTE_WEIGHT,       ITEM_ATTRIBUTE_ATTACK,     ITEM_ATTRIBUTE_DEFENSE,
    ITEM_ATTRIBUTE_EXTRADEFENSE, ITEM_ATTRIBUTE_ARMOR,      ITEM_ATTRIBUTE_HITCHANCE,
    ITEM_ATTRIBUTE_SHOOTRANGE,   ITEM_ATTRIBUTE_OWNER,      ITEM_ATTRIBUTE_CORPSEOWNER,
    ITEM_ATTRIBUTE_FLUIDTYPE,    ITEM_ATTRIBUTE_DOORID,     ITEM_ATTRIBUTE_WRAPID,
    ITEM_ATTRIBUTE_STOREITEM,    ITEM_ATTRIBUTE_ATTACK_SPEED, ITEM_ATTRIBUTE_REWARDID,
};

} // namespace

StowRejection getStowRejection(const Item* item)
{
	if (!item) {
		return StowRejection::NoItem;
	}

	// A container is stowed through the container action, which walks its contents.
	// Storing the container itself would lose everything inside it.
	if (item->getContainer()) {
		return StowRejection::IsContainer;
	}

	// Covers ground, doors, magic fields and anything else that is not a loose item.
	if (!item->isPickupable()) {
		return StowRejection::NotPickupable;
	}

	if (item->isStoreItem() || item->hasAttribute(ITEM_ATTRIBUTE_STOREITEM)) {
		return StowRejection::StoreItem;
	}

	if (item->hasImbuements()) {
		return StowRejection::HasImbuements;
	}

	for (const itemAttrTypes attribute : RESTRICTED_ATTRIBUTES) {
		if (item->hasAttribute(attribute)) {
			return StowRejection::InstanceAttribute;
		}
	}

	// A decaying item carries a deadline. Stowing it would stop the clock and hand
	// back a fresh one later.
	if (item->hasAttribute(ITEM_ATTRIBUTE_DECAYSTATE) || item->hasAttribute(ITEM_ATTRIBUTE_DURATION_TIMESTAMP)) {
		return StowRejection::Decaying;
	}

	// A partly used item may only be stowed if it is still at full value, because
	// only the count survives. Anything less would be refunded to full on withdraw,
	// which is a quiet duplication of value.
	if (item->hasAttribute(ITEM_ATTRIBUTE_DURATION)) {
		const uint32_t duration = static_cast<uint32_t>(item->getDuration());
		const uint32_t maxDuration = std::max(item->getDefaultDurationMin(), item->getDefaultDurationMax());
		if (maxDuration == 0 || duration < maxDuration) {
			return StowRejection::PartialDuration;
		}
	}

	if (item->hasAttribute(ITEM_ATTRIBUTE_CHARGES)) {
		const uint32_t charges = item->getCharges();
		const uint32_t maxCharges = Item::items[item->getID()].charges;
		if (maxCharges == 0 || charges < maxCharges) {
			return StowRejection::PartialCharges;
		}
	}

	return StowRejection::None;
}

namespace {

// A tile grants access if it holds a depot or the stash object itself.
bool tileGrantsStashAccess(const Tile* tile)
{
	if (!tile) {
		return false;
	}

	const TileItemVector* items = tile->getItemList();
	if (!items) {
		return false;
	}

	for (const auto& item : *items) {
		if (!item) {
			continue;
		}
		const ItemType& type = Item::items[item->getID()];
		if (type.isDepot() || item->getID() == ITEM_SUPPLY_STASH) {
			return true;
		}
	}
	return false;
}

} // namespace

bool hasSupplyStashAccess(const Player* player)
{
	if (!player) {
		return false;
	}

	const Position& position = player->getPosition();
	const Tile* playerTile = g_game.map.getTile(position.x, position.y, position.z);

	// Protection zone first: the stash is a town service, and requiring it keeps
	// the whole thing out of reach mid-fight.
	if (!playerTile || !playerTile->hasFlag(TILESTATE_PROTECTIONZONE)) {
		return false;
	}

	if (tileGrantsStashAccess(playerTile)) {
		return true;
	}

	for (int32_t dx = -1; dx <= 1; ++dx) {
		for (int32_t dy = -1; dy <= 1; ++dy) {
			if (dx == 0 && dy == 0) {
				continue;
			}

			// Kept signed and bounds-checked before the cast. At x or y of 0 the
			// neighbour is -1, and casting that to uint16_t gives 65535 — a depot at
			// the opposite edge of the map would satisfy an adjacency check.
			const int32_t neighbourX = static_cast<int32_t>(position.x) + dx;
			const int32_t neighbourY = static_cast<int32_t>(position.y) + dy;
			if (neighbourX < 0 || neighbourY < 0 || neighbourX > std::numeric_limits<uint16_t>::max() ||
			    neighbourY > std::numeric_limits<uint16_t>::max()) {
				continue;
			}

			if (tileGrantsStashAccess(g_game.map.getTile(static_cast<uint16_t>(neighbourX),
			                                             static_cast<uint16_t>(neighbourY), position.z))) {
				return true;
			}
		}
	}
	return false;
}

} // namespace tfs::supply_stash
