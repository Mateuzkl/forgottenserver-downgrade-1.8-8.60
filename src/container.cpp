// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "container.h"
#include "depotchest.h"
#include "depotlocker.h"
#include "inbox.h"
#include "storeinbox.h"

#include "game.h"
#include "iomap.h"
#include "logger.h"

#include <queue>

extern Game g_game;

namespace {

std::shared_ptr<Item> getSharedItem(Item* item)
{
	return item ? item->weak_from_this().lock() : nullptr;
}

void logSharedItemLockFailure(std::string_view context, const Item* item)
{
	LOG_WARN("[Warning - {}] Failed to lock item shared ownership. item={}, id={}", context,
	         static_cast<const void*>(item), item ? item->getID() : 0);
}

std::shared_ptr<Container> getSharedContainer(Container* container)
{
	return std::dynamic_pointer_cast<Container>(getSharedItem(container));
}

} // namespace

Container::Container(uint16_t type) : Container(type, items[type].maxItems) {}

Container::Container(uint16_t type, uint16_t size) : Item(type), maxSize(size)
{
	if (getID() == ITEM_GOLD_POUCH) {
		pagination = true;
	}
}

Container::~Container()
{
	for (const auto& item : itemlist) {
		if (!item) {
			continue;
		}
		item->setParent(nullptr);
	}
}

void Container::updateAmmoCount(const Item* item, int32_t diff)
{
	if (!item || item->getWeaponType() != WEAPON_AMMO || diff == 0) {
		return;
	}

	if (diff > 0) {
		ammoCount += static_cast<uint32_t>(diff);
		return;
	}

	ammoCount -= std::min<uint32_t>(ammoCount, static_cast<uint32_t>(-diff));
}

void Container::sendQuiverInventoryUpdate() const
{
	if (getWeaponType() != WEAPON_QUIVER) {
		return;
	}

	Cylinder* parent = getParent();
	Creature* creature = parent ? parent->getCreature() : nullptr;
	Player* player = creature ? creature->getPlayer() : nullptr;
	if (!player || player->getInventoryItem(CONST_SLOT_RIGHT) != this) {
		return;
	}

	player->sendQuiverUpdate();
}

std::shared_ptr<Item> Container::clone() const
{
	auto clone = std::static_pointer_cast<Container>(Item::clone());
	if (!clone) {
		return nullptr;
	}
	for (const auto& item : itemlist) {
		auto clonedItem = item->clone();
		clone->addItem(clonedItem);
	}
	clone->totalWeight = totalWeight;
	return clone;
}

std::string Container::getName(bool addArticle /* = false*/) const
{
	const ItemType& it = items[id];
	return getNameDescription(it, this, -1, addArticle);
}

bool Container::addItem(const std::shared_ptr<Item>& item)
{
	if (!item) {
		return false;
	}
	itemlist.push_back(item);
	item->setParent(this);
	updateAmmoCount(item.get(), item->getItemCount());
	return true;
}

bool Container::addItem(Item* item)
{
	auto itemRef = getSharedItem(item);
	if (!itemRef) {
		logSharedItemLockFailure("Container::addItem", item);
		return false;
	}
	return addItem(itemRef);
}

Attr_ReadValue Container::readAttr(AttrTypes_t attr, PropStream& propStream)
{
	if (attr == ATTR_CONTAINER_ITEMS) {
		if (!propStream.read<uint32_t>(serializationCount)) {
			return ATTR_READ_ERROR;
		}
		return ATTR_READ_END;
	}
	return Item::readAttr(attr, propStream);
}

bool Container::unserializeItemNode(OTB::Loader& loader, const OTB::Node& node, PropStream& propStream)
{
	bool ret = Item::unserializeItemNode(loader, node, propStream);
	if (!ret) {
		return false;
	}

	for (auto& itemNode : node.children) {
		// load container items
		if (static_cast<OTBM_NodeTypes_t>(itemNode.type) != OTBM_NodeTypes_t::ITEM) {
			// unknown type
			return false;
		}

		PropStream itemPropStream;
		if (!loader.getProps(itemNode, itemPropStream)) {
			return false;
		}

		auto item = Item::CreateItem(itemPropStream);
		if (!item) {
			return false;
		}

		if (!item->unserializeItemNode(loader, itemNode, itemPropStream)) {
			return false;
		}

		if (!addItem(item)) {
			return false;
		}
		updateItemWeight(item->getWeight());
	}
	return true;
}

void Container::updateItemWeight(int32_t diff)
{
	totalWeight += diff;

	if (const auto parent = getParent()) {
		if (const auto item = parent->getItem()) {
			if (const auto parentContainer = item->getContainer()) {
				parentContainer->updateItemWeight(diff);
			}
		}
	}
}

uint32_t Container::getWeight() const { return Item::getWeight() + totalWeight; }

bool Container::hasCapacityLimit() const
{
	return getID() != ITEM_GOLD_POUCH && !hasPagination() && !getDepotLocker() && !getRewardChest() &&
	       !getStoreInbox() && !dynamic_cast<const DepotChest*>(this) &&
	       !dynamic_cast<const Inbox*>(this);
}

uint32_t Container::getFreeSlotsFor(const Item* item, uint32_t count) const
{
	const size_t currentSize = size();
	uint32_t freeSlots = currentSize < capacity() ? capacity() - static_cast<uint32_t>(currentSize) : 0;
	if (item && item->getParent() == this && (!item->isStackable() || count >= item->getItemCount())) {
		++freeSlots;
	}
	return freeSlots;
}

bool Container::canMergeIntoExistingStack(const Item* item, int32_t index) const
{
	if (!item || !item->isStackable()) {
		return false;
	}

	auto canStackWith = [item](const Item* stackItem) {
		return stackItem && stackItem != item && stackItem->equals(item) &&
		       stackItem->getItemCount() < stackItem->getStackSize();
	};

	if (index == INDEX_WHEREEVER) {
		for (const auto& containerItem : itemlist) {
			if (canStackWith(containerItem.get())) {
				return true;
			}
		}
		return false;
	}

	return canStackWith(getItemByIndex(index));
}

bool Container::hasRoomForItem(const Item* item, int32_t index, uint32_t count) const
{
	return !hasCapacityLimit() || getFreeSlotsFor(item, count) > 0 || canMergeIntoExistingStack(item, index);
}

uint64_t Container::getWeightReductionContentWeight() const
{
	constexpr uint32_t equipmentSlotBits = SLOTP_HEAD | SLOTP_NECKLACE | SLOTP_ARMOR | SLOTP_LEGS |
	                                       SLOTP_FEET | SLOTP_RING | SLOTP_AMMO | SLOTP_TWO_HAND;

	uint64_t weight = 0;
	for (const std::shared_ptr<Item>& item : itemlist) {
		if (!item) {
			continue;
		}

		const std::shared_ptr<const Container> container = std::dynamic_pointer_cast<const Container>(item);
		const bool isEquipment = (item->getSlotPosition() & equipmentSlotBits) != 0 ||
		                         item->getWeaponType() != WEAPON_NONE || item->getAttack() != 0 ||
		                         item->getDefense() != 0 || item->getExtraDefense() != 0 || item->getArmor() != 0;
		if (!isEquipment) {
			weight += container ? container->getBaseWeight() : item->getWeight();
		}

		if (container) {
			weight += container->getWeightReductionContentWeight();
		}
	}
	return weight;
}

Item* Container::getItemByIndex(size_t index) const
{
	return getItemByIndexRef(index).get();
}

std::shared_ptr<Item> Container::getItemByIndexRef(size_t index) const
{
	if (index >= size()) {
		return nullptr;
	}
	return itemlist[index];
}

uint32_t Container::getItemHoldingCount() const
{
	uint32_t counter = 0;
	for (ContainerIterator it = iterator(); it.hasNext(); it.advance()) {
		++counter;
	}
	return counter;
}

bool Container::isHoldingItem(const Item* item) const
{
	for (ContainerIterator it = iterator(); it.hasNext(); it.advance()) {
		if (*it == item) {
			return true;
		}
	}
	return false;
}

std::shared_ptr<Player> Container::getHoldingPlayerForNotification() const
{
	Cylinder* cylinder = getParent();
	Cylinder* topParent = nullptr;
	while (cylinder && cylinder->getParent() != nullptr) {
		topParent = cylinder;
		cylinder = cylinder->getParent();
	}

	if (!topParent) {
		topParent = cylinder;
	}

	if (!topParent) {
		return nullptr;
	}

	if (Creature* creature = topParent->getCreature()) {
		Player* player = creature->getPlayer();
		if (!player) {
			return nullptr;
		}

		return std::static_pointer_cast<Player>(player->weak_from_this().lock());
	}
	return nullptr;
}

void Container::onAddContainerItem(Item* item) const
{
	if (auto player = getHoldingPlayerForNotification()) {
		player->sendAddContainerItem(this, item);
		player->onAddContainerItem(item);
		return;
	}

	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, getPosition(), false, true, 1, 1, 1, 1);
	spectators.partitionByType();

	// send to client
	for (const auto& spectator : spectators.players()) {
		static_cast<Player*>(spectator.get())->sendAddContainerItem(this, item);
	}

	// event methods
	for (const auto& spectator : spectators.players()) {
		static_cast<Player*>(spectator.get())->onAddContainerItem(item);
	}
}

void Container::onUpdateContainerItem(uint32_t index, Item* oldItem, Item* newItem) const
{
	if (auto player = getHoldingPlayerForNotification()) {
		player->sendUpdateContainerItem(this, static_cast<uint16_t>(index), newItem);
		player->onUpdateContainerItem(this, oldItem, newItem);
		return;
	}

	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, getPosition(), false, true, 1, 1, 1, 1);
	spectators.partitionByType();

	// send to client
	for (const auto& spectator : spectators.players()) {
		static_cast<Player*>(spectator.get())->sendUpdateContainerItem(this, static_cast<uint16_t>(index), newItem);
	}

	// event methods
	for (const auto& spectator : spectators.players()) {
		static_cast<Player*>(spectator.get())->onUpdateContainerItem(this, oldItem, newItem);
	}
}

void Container::onRemoveContainerItem(uint32_t index, Item* item) const
{
	if (auto player = getHoldingPlayerForNotification()) {
		player->sendRemoveContainerItem(this, static_cast<uint16_t>(index));
		player->onRemoveContainerItem(this, item);
		return;
	}

	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, getPosition(), false, true, 1, 1, 1, 1);
	spectators.partitionByType();

	// send change to client
	for (const auto& spectator : spectators.players()) {
		static_cast<Player*>(spectator.get())->sendRemoveContainerItem(this, static_cast<uint16_t>(index));
	}

	// event methods
	for (const auto& spectator : spectators.players()) {
		static_cast<Player*>(spectator.get())->onRemoveContainerItem(this, item);
	}
}

ReturnValue Container::queryAdd(int32_t index, const Thing& thing, uint32_t count, uint32_t flags,
                                Creature* actor /* = nullptr*/) const
{
	bool childIsOwner = hasBitSet(FLAG_CHILDISOWNER, flags);
	if (childIsOwner) {
		// a child container is querying, since we are the top container (not carried by a player)
		// just return with no error.
		return RETURNVALUE_NOERROR;
	}

	const Item* item = thing.getItem();
	if (item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!item->isPickupable()) {
		return RETURNVALUE_CANNOTPICKUP;
	}

	if (item == this) {
		return RETURNVALUE_THISISIMPOSSIBLE;
	}

	// Store items can only be moved into depot chests or the Store inbox.
	if (item->isStoreItem() && !dynamic_cast<const DepotChest*>(this) && !dynamic_cast<const StoreInbox*>(this)) {
		return RETURNVALUE_ITEMCANNOTBEMOVEDTHERE;
	}

	uint32_t corpseOwner = getCorpseOwner();
	if (corpseOwner != 0 && corpseOwner != static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) && 
		getID() != ITEM_MALE_CORPSE && getID() != ITEM_FEMALE_CORPSE && actor && actor->getPlayer()) {
		return RETURNVALUE_CANNOTPLACEITEMINMONSTERCORPSE;
	}

	if (getWeaponType() == WEAPON_QUIVER && item->getWeaponType() != WEAPON_AMMO) {
		return RETURNVALUE_QUIVERAMMOONLY;
	}

	const Cylinder* cylinder = getParent();

	// Do not allow inserting items into a Store item container while it is inside the Store inbox.
	if (isStoreItem() && dynamic_cast<const StoreInbox*>(cylinder)) {
		return item->isStoreItem() ? RETURNVALUE_ITEMCANNOTBEMOVEDTHERE : RETURNVALUE_CANNOTMOVEITEMISNOTSTOREITEM;
	}

	while (cylinder) {
		if (cylinder == &thing) {
			return RETURNVALUE_THISISIMPOSSIBLE;
		}

		cylinder = cylinder->getParent();
	}

	if (!hasRoomForItem(item, index, count)) {
		return RETURNVALUE_CONTAINERNOTENOUGHROOM;
	}

	const Cylinder* const topParent = getTopParent();
	if (const auto tile = topParent->getTile()) {
		if (const auto houseTile = tile->getHouseTile()) {
			const auto house = houseTile->getHouse();
			if (house && house->getProtected() && actor && !topParent->getCreature() && !house->canModifyItems(actor->getPlayer())) {
				return RETURNVALUE_CANNOTMOVEITEMISPROTECTED;
		}
		if (actor && getBoolean(ConfigManager::ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS)) {
			if (!topParent->getCreature() && !house->isInvited(actor->getPlayer())) {
				return RETURNVALUE_PLAYERISNOTINVITED;
				}
			}
		}
	}

	if (topParent != this) {
		return topParent->queryAdd(INDEX_WHEREEVER, *item, count, flags | FLAG_CHILDISOWNER, actor);
	}
	return RETURNVALUE_NOERROR;
}

ReturnValue Container::queryMaxCount(int32_t index, const Thing& thing, uint32_t count, uint32_t& maxQueryCount,
                                     uint32_t flags) const
{
	const Item* item = thing.getItem();
	if (item == nullptr) {
		maxQueryCount = 0;
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!hasCapacityLimit()) {
		maxQueryCount = std::max<uint32_t>(1, count);
		return RETURNVALUE_NOERROR;
	}

	uint32_t freeSlots = getFreeSlotsFor(item, count);

	if (item->isStackable()) {
		uint32_t n = 0;

		if (index == INDEX_WHEREEVER) {
			// Iterate through every item and check how much free stackable slots there is.
			uint32_t slotIndex = 0;
			for (const auto& containerItem : itemlist) {
				if (containerItem.get() != item && containerItem->equals(item) &&
				    containerItem->getItemCount() < containerItem->getStackSize()) {
					if (queryAdd(slotIndex, *item, count, flags) == RETURNVALUE_NOERROR) {
						n += containerItem->getStackSize() - containerItem->getItemCount();
					}
				}
				++slotIndex;
			}
		} else {
			const auto destItemRef = getItemByIndexRef(index);
			const Item* destItem = destItemRef.get();
			if (item->equals(destItem) && destItem->getItemCount() < destItem->getStackSize()) {
				if (queryAdd(index, *item, count, flags) == RETURNVALUE_NOERROR) {
					n = destItem->getStackSize() - destItem->getItemCount();
				}
			}
		}

		maxQueryCount = freeSlots * item->getStackSize() + n;
		if (maxQueryCount < count) {
			return RETURNVALUE_CONTAINERNOTENOUGHROOM;
		}
	} else {
		maxQueryCount = freeSlots;
		if (maxQueryCount == 0) {
			return RETURNVALUE_CONTAINERNOTENOUGHROOM;
		}
	}
	return RETURNVALUE_NOERROR;
}

ReturnValue Container::queryRemove(const Thing& thing, uint32_t count, uint32_t flags,
                                   Creature* actor /*= nullptr */) const
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

	const Cylinder* const topParent = getTopParent();
	if (const auto tile = topParent->getTile()) {
		if (const auto houseTile = tile->getHouseTile()) {
			const auto house = houseTile->getHouse();
			if (house && house->getProtected() && actor && !topParent->getCreature() && !house->canModifyItems(actor->getPlayer())) {
				return RETURNVALUE_CANNOTMOVEITEMISPROTECTED;
		}
		if (actor && getBoolean(ConfigManager::ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS)) {
			if (!topParent->getCreature() && !house->isInvited(actor->getPlayer())) {
				return RETURNVALUE_PLAYERISNOTINVITED;
				}
			}
		}
	}

	return RETURNVALUE_NOERROR;
}

Cylinder* Container::queryDestination(int32_t& index, const Thing& thing, Item** destItem, uint32_t& flags)
{
	if (index == 254 /*move up*/) {
		index = INDEX_WHEREEVER;
		*destItem = nullptr;

		Container* parentContainer = dynamic_cast<Container*>(getParent());
		if (parentContainer) {
			return parentContainer;
		}
		return this;
	}

	if (index == 255 /*add wherever*/) {
		index = INDEX_WHEREEVER;
		*destItem = nullptr;
	} else if (index >= static_cast<int32_t>(capacity())) {
		/*
		if you have a container, maximize it to show all 20 slots
		then you open a bag that is inside the container you will have a bag with 8 slots
		and a "grey" area where the other 12 slots where from the container
		if you drop the item on that grey area
		the client calculates the slot position as if the bag has 20 slots
		*/
		index = INDEX_WHEREEVER;
		*destItem = nullptr;
	}

	const Item* item = thing.getItem();
	if (!item) {
		return this;
	}

	if (index != INDEX_WHEREEVER) {
		auto itemFromIndexRef = getItemByIndexRef(index);
		Item* itemFromIndex = itemFromIndexRef.get();
		if (itemFromIndex) {
			*destItem = itemFromIndex;
		}

		Cylinder* subCylinder = dynamic_cast<Cylinder*>(*destItem);
		if (subCylinder) {
			index = INDEX_WHEREEVER;
			*destItem = nullptr;
			return subCylinder;
		}
	}

	bool autoStack = !hasBitSet(FLAG_IGNOREAUTOSTACK, flags);
	if (autoStack && item->isStackable() && item->getParent() != this) {
		if (auto tmpItem = *destItem) {
			if (tmpItem->equals(item) && tmpItem->getItemCount() < tmpItem->getStackSize()) {
				return this;
			}
		}

		// try find a suitable item to stack with
		uint32_t n = 0;
		for (const auto& listItem : itemlist) {
			if (listItem.get() != item && listItem->equals(item) && listItem->getItemCount() < listItem->getStackSize()) {
				*destItem = listItem.get();
				index = n;
				return this;
			}
			++n;
		}
	}
	return this;
}

void Container::addThing(Thing* thing) { return addThing(0, thing); }

void Container::addThing(int32_t index, Thing* thing)
{
	if (index >= static_cast<int32_t>(capacity())) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (item == nullptr) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	auto itemRef = getSharedItem(item);
	if (!itemRef) {
		logSharedItemLockFailure("Container::addThing", item);
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	item->setParent(this);
	itemlist.push_front(std::move(itemRef));
	updateAmmoCount(item, item->getItemCount());
	updateItemWeight(item->getWeight());

	// send change to client
	if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
		onAddContainerItem(item);
		sendQuiverInventoryUpdate();
	}
}

void Container::addItemBack(Item* item)
{
	if (!addItem(item)) {
		return;
	}
	updateItemWeight(item->getWeight());

	// send change to client
	if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
		onAddContainerItem(item);
		sendQuiverInventoryUpdate();
	}
}

void Container::updateThing(Thing* thing, uint16_t itemId, uint32_t count)
{
	int32_t index = getThingIndex(thing);
	if (index == -1) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (item == nullptr) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	const int32_t oldWeight = item->getWeight();
	updateAmmoCount(item, -static_cast<int32_t>(item->getItemCount()));
	item->setID(itemId);
	item->setSubType(static_cast<uint16_t>(count));
	updateAmmoCount(item, static_cast<int32_t>(item->getItemCount()));
	updateItemWeight(-oldWeight + item->getWeight());

	// send change to client
	if (getParent()) {
		onUpdateContainerItem(index, item, item);
		sendQuiverInventoryUpdate();
	}
}

void Container::replaceThing(uint32_t index, Thing* thing)
{
	if (!thing) {
		return;
	}

	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	auto replacedItemRef = getItemByIndexRef(index);
	Item* replacedItem = replacedItemRef.get();
	if (!replacedItem) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	auto itemRef = getSharedItem(item);
	if (!itemRef) {
		logSharedItemLockFailure("Container::replaceThing", item);
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	itemlist[index] = std::move(itemRef);
	item->setParent(this);
	updateItemWeight(-static_cast<int32_t>(replacedItem->getWeight()) + item->getWeight());
	updateAmmoCount(replacedItem, -static_cast<int32_t>(replacedItem->getItemCount()));
	updateAmmoCount(item, item->getItemCount());

	// send change to client
	if (getParent()) {
		onUpdateContainerItem(index, replacedItem, item);
		sendQuiverInventoryUpdate();
	}

	replacedItem->setParent(nullptr);
}

void Container::removeThing(Thing* thing, uint32_t count)
{
	if (!thing) {
		return;
	}

	Item* item = thing->getItem();
	if (item == nullptr) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	int32_t index = getThingIndex(thing);
	if (index == -1) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	if (item->isStackable() && count != item->getItemCount()) {
		uint8_t newCount = static_cast<uint8_t>(std::max<int32_t>(0, item->getItemCount() - count));
		const int32_t oldWeight = item->getWeight();
		updateAmmoCount(item, -static_cast<int32_t>(item->getItemCount() - newCount));
		item->setItemCount(newCount);
		updateItemWeight(-oldWeight + item->getWeight());

		// send change to client
		if (getParent()) {
			onUpdateContainerItem(index, item, item);
			sendQuiverInventoryUpdate();
		}
	} else {
		updateItemWeight(-static_cast<int32_t>(item->getWeight()));
		updateAmmoCount(item, -static_cast<int32_t>(item->getItemCount()));

		// send change to client
		if (getParent()) {
			onRemoveContainerItem(index, item);
			sendQuiverInventoryUpdate();
		}

		auto itemSp = *(itemlist.begin() + index); // prevent destruction during erase
		item->setParent(nullptr);
		itemlist.erase(itemlist.begin() + index);
	}
}

int32_t Container::getThingIndex(const Thing* thing) const
{
	int32_t index = 0;
	for (const auto& item : itemlist) {
		if (item.get() == thing) {
			return index;
		}
		++index;
	}
	return -1;
}

size_t Container::getFirstIndex() const { return 0; }

size_t Container::getLastIndex() const { return size(); }

uint32_t Container::getItemTypeCount(uint16_t itemId, int32_t subType /* = -1*/, bool /*ignoreEquipped = false*/) const
{
	uint32_t count = 0;
	for (const auto& item : itemlist) {
		if (item->getID() == itemId) {
			count += countByType(item.get(), subType);
		}
	}
	return count;
}

std::unordered_map<uint32_t, uint32_t>& Container::getAllItemTypeCount(std::unordered_map<uint32_t, uint32_t>& countMap) const
{
	for (const auto& item : itemlist) {
		countMap[item->getID()] += item->getItemCount();
	}
	return countMap;
}

ItemVector Container::getItems(bool recursive /*= false*/) const
{
	if (recursive) {
		ContainerIterator it = iterator();
		return std::move(it.items);
	}

	return {itemlist.begin(), itemlist.end()};
}

Thing* Container::getThing(size_t index) const { return getItemByIndex(index); }

void Container::postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index, cylinderlink_t)
{
	Cylinder* topParent = getTopParent();
	if (topParent->getCreature() || dynamic_cast<DepotLocker*>(topParent)) {
		topParent->postAddNotification(thing, oldParent, index, LINK_TOPPARENT);
	} else if (topParent == this) {
		// let the tile class notify surrounding players
		if (topParent->getParent()) {
			topParent->getParent()->postAddNotification(thing, oldParent, index, LINK_NEAR);
		}
	} else {
		topParent->postAddNotification(thing, oldParent, index, LINK_PARENT);
	}
}

void Container::postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index, cylinderlink_t)
{
	Cylinder* topParent = getTopParent();
	if (topParent->getCreature() || dynamic_cast<DepotLocker*>(topParent)) {
		topParent->postRemoveNotification(thing, newParent, index, LINK_TOPPARENT);
	} else if (topParent == this) {
		// let the tile class notify surrounding players
		if (topParent->getParent()) {
			topParent->getParent()->postRemoveNotification(thing, newParent, index, LINK_NEAR);
		}
	} else {
		topParent->postRemoveNotification(thing, newParent, index, LINK_PARENT);
	}
}

void Container::internalAddThing(Thing* thing) { internalAddThing(0, thing); }

void Container::internalAddThing(uint32_t, Thing* thing)
{
	Item* item = thing->getItem();
	if (item == nullptr) {
		return;
	}

	auto itemRef = getSharedItem(item);
	if (!itemRef) {
		logSharedItemLockFailure("Container::internalAddThing", item);
		return;
	}

	item->setParent(this);
	itemlist.push_front(std::move(itemRef));
	updateAmmoCount(item, item->getItemCount());
	updateItemWeight(item->getWeight());

	if (getID() == ITEM_REWARD_CONTAINER && item->isStackable()) {
		item->removeAttribute(ITEM_ATTRIBUTE_DATE);
		item->removeAttribute(ITEM_ATTRIBUTE_REWARDID);
	}
}

void Container::startDecaying()
{
	ItemVector snapshot = getItems(true);

	auto self = getSharedContainer(this);
	if (self) {
		g_game.startDecay(std::move(self));
	}

	for (const auto& item : snapshot) {
		if (!item->isRemoved()) {
			g_game.startDecay(item);
		}
	}
}

void Container::stopDecaying()
{
	if (auto self = getSharedContainer(this)) {
		g_game.stopDecay(self);
	}

	ItemVector snapshot = getItems(true);
	for (const auto& item : snapshot) {
		if (item && !item->isRemoved()) {
			g_game.stopDecay(item);
		}
	}
}

size_t Container::size(const bool recursive /*= false*/) const
{
	if (recursive) {
		size_t count = 0;
		for (ContainerIterator it = iterator(); it.hasNext(); it.advance()) {
			++count;
		}
		return count;
	}
	return itemlist.size();
}

ContainerIterator Container::iterator() const
{
	ContainerIterator cit;
	std::queue<const Container*> pending;

	pending.push(this);

	while (!pending.empty()) {
		auto container = pending.front();
		pending.pop();

		for (const auto& item : container->itemlist) {
			if (!item) {
				continue;
			}

			cit.items.push_back(item);
			if (auto subContainer = std::dynamic_pointer_cast<const Container>(item)) {
				pending.push(subContainer.get());
			}
		}
	}

	return cit;
}

bool Container::isRewardCorpse() const
{
	for (const auto& subItem : getItemList()) {
		if (subItem->getID() == ITEM_REWARD_CONTAINER) {
			return true;
		}
	}
	return false;
}

Item* ContainerIterator::operator*() const
{
	if (!hasNext()) {
		return nullptr;
	}
	return items[index].get();
}

void ContainerIterator::advance()
{
	if (hasNext()) {
		++index;
	}
}
