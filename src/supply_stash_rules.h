// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_SUPPLY_STASH_RULES_H
#define FS_SUPPLY_STASH_RULES_H

#include <cstdint>

class Item;
class Player;

namespace tfs::supply_stash {

// Why an item may not be stowed. Carried back so the caller can tell the player
// something better than a flat refusal, and so tests name the reason instead of
// just checking a bool.
enum class StowRejection : uint8_t
{
	None,
	NoItem,
	IsContainer,       // containers go through the container action, not this one
	NotPickupable,     // ground, doors, magic fields and anything else fixed in place
	NotMovable,        // pickupable is not enough; the Lua required both
	BlockedItemId,     // currency, market, inbox, store inbox, depot, the stash itself
	UnsupportedType,   // corpse, door, container, fluid container, magic field, ground
	Nameless,          // no name in items.xml, so nothing sensible to show in the stash
	StoreItem,
	HasImbuements,
	InstanceAttribute, // actionid, uniqueid, custom name, owner, custom armor…
	Decaying,          // a decay state or timestamp would be lost
	PartialDuration,   // used duration would silently reset to full
	PartialCharges,    // spent charges would silently come back
};

// The stash stores (itemId, tier, amount) and nothing else, so anything carrying
// per-instance state has to be refused: stowing it would quietly destroy that
// state and hand back a plain item on withdraw.
//
// These rules are ported from the Lua that this replaces, deliberately without
// relaxing any of them.
[[nodiscard]] StowRejection getStowRejection(const Item* item);

[[nodiscard]] inline bool canStowSupplyItem(const Item* item)
{
	return getStowRejection(item) == StowRejection::None;
}

// The stash is only reachable standing in a protection zone, on or next to a
// depot or the stash object itself. Checked server-side on every action: an open
// window on the client proves nothing, and the player may have walked away since.
//
// Ported from hasCurrentSupplyStashAccess in the Lua, same 3x3 area.
[[nodiscard]] bool hasSupplyStashAccess(const Player* player);

} // namespace tfs::supply_stash

#endif // FS_SUPPLY_STASH_RULES_H
