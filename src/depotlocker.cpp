// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "depotlocker.h"

#include "tools.h"

DepotLocker::DepotLocker(uint16_t type) : Container(type, 4) {}

ReturnValue DepotLocker::queryAdd(int32_t, const Thing& thing, uint32_t, uint32_t flags,
                                  Creature* actor) const
{
	// The locker is only the shell that holds the depot chest, inbox, market and supply stash.
	// Those are placed through internalAddThing, which does not go through queryAdd, so anything
	// arriving here is an attempt to store an item in the locker itself. Items belong in the
	// depot boxes inside the chest.
	if (actor && !hasBitSet(FLAG_NOLIMIT, flags)) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!hasBitSet(FLAG_NOLIMIT, flags)) {
		return RETURNVALUE_CONTAINERNOTENOUGHROOM;
	}

	const Item* item = thing.getItem();
	if (!item) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (item == this) {
		return RETURNVALUE_THISISIMPOSSIBLE;
	}

	// No isPickupable() check here, unlike Inbox: what legitimately goes into the locker is the
	// depot chest, inbox, market and supply stash, and those are fixed structures rather than
	// pickupable items.
	return RETURNVALUE_NOERROR;
}

Attr_ReadValue DepotLocker::readAttr(AttrTypes_t attr, PropStream& propStream)
{
	if (attr == ATTR_DEPOT_ID) {
		if (!propStream.read<uint16_t>(depotId)) {
			return ATTR_READ_ERROR;
		}
		return ATTR_READ_CONTINUE;
	}
	return Item::readAttr(attr, propStream);
}

void DepotLocker::postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index, cylinderlink_t)
{
	if (parent != nullptr) {
		parent->postAddNotification(thing, oldParent, index, LINK_PARENT);
	}

	save = true;
}

void DepotLocker::postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index, cylinderlink_t)
{
	if (parent != nullptr) {
		parent->postRemoveNotification(thing, newParent, index, LINK_PARENT);
	}

	save = true;
}

void DepotLocker::removeInbox(Inbox* inbox)
{
	auto cit = std::find_if(itemlist.begin(), itemlist.end(), [inbox](const auto& item) { return item.get() == inbox; });
	if (cit == itemlist.end()) {
		return;
	}
	itemlist.erase(cit);
}
