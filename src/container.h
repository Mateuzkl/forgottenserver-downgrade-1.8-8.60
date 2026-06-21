// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CONTAINER_H
#define FS_CONTAINER_H

#include "cylinder.h"
#include "item.h"
#include "tile.h"

#include <memory>
#include <vector>

class Container;
class DepotChest;
class DepotLocker;
class RewardChest;
class StoreInbox;
class Player;

class ContainerIterator
{
public:
	bool hasNext() const { return index < items.size(); }

	void advance();
	Item* operator*() const;

private:
	ItemVector items;
	size_t index = 0;

	friend class Container;
};

class Container : public Item, public Cylinder
{
public:
	explicit Container(uint16_t type);
	Container(uint16_t type, uint16_t size);
	~Container();

	// non-copyable
	Container(const Container&) = delete;
	Container& operator=(const Container&) = delete;

	[[nodiscard]] std::shared_ptr<Item> clone() const override final;

	Container* getContainer() override final { return this; }
	const Container* getContainer() const override final { return this; }

	virtual DepotLocker* getDepotLocker() { return nullptr; }
	virtual const DepotLocker* getDepotLocker() const { return nullptr; }

	virtual RewardChest* getRewardChest() {
		return nullptr;
	}
	virtual const RewardChest* getRewardChest() const {
		return nullptr;
	}

	virtual StoreInbox* getStoreInbox() {
		return nullptr;
	}
	virtual const StoreInbox* getStoreInbox() const {
		return nullptr;
	}

	Attr_ReadValue readAttr(AttrTypes_t attr, PropStream& propStream) override;
	bool unserializeItemNode(OTB::Loader& loader, const OTB::Node& node, PropStream& propStream) override;

	size_t size(const bool recursive = false) const;
	bool empty() const { return itemlist.empty(); }
	uint32_t capacity() const { return maxSize; }
	bool hasPagination() const { return pagination; }
	uint32_t getAmmoCount() const { return ammoCount; }

	ContainerIterator iterator() const;

	const ItemDeque& getItemList() const { return itemlist; }
	ItemVector getItems(bool recursive = false) const;

	ItemDeque::const_reverse_iterator getReversedItems() const { return itemlist.rbegin(); }
	ItemDeque::const_reverse_iterator getReversedEnd() const { return itemlist.rend(); }

	std::string getName(bool addArticle = false) const;

	bool addItem(const std::shared_ptr<Item>& item);
	bool addItem(Item* item);
	Item* getItemByIndex(size_t index) const;
	std::shared_ptr<Item> getItemByIndexRef(size_t index) const;
	bool isHoldingItem(const Item* item) const;
	bool isRewardCorpse() const;

	uint32_t getItemHoldingCount() const;
	uint32_t getWeight() const override final;
	uint64_t getWeightReductionContentWeight() const;

	// cylinder implementations
	virtual ReturnValue queryAdd(int32_t index, const Thing& thing, uint32_t count, uint32_t flags,
	                             Creature* actor = nullptr) const override;
	ReturnValue queryMaxCount(int32_t index, const Thing& thing, uint32_t count, uint32_t& maxQueryCount,
	                          uint32_t flags) const override final;
	ReturnValue queryRemove(const Thing& thing, uint32_t count, uint32_t flags,
	                        Creature* actor = nullptr) const override;
	virtual Cylinder* queryDestination(int32_t& index, const Thing& thing, Item** destItem, uint32_t& flags,
	                                   uint32_t destinationInstanceId) override;

	void addThing(Thing* thing) override final;
	void addThing(int32_t index, Thing* thing) override final;
	void addItemBack(Item* item);

	void updateThing(Thing* thing, uint16_t itemId, uint32_t count) override final;
	void refreshThing(Thing* thing) override final;
	void replaceThing(uint32_t index, Thing* thing) override final;

	void removeThing(Thing* thing, uint32_t count) override final;

	int32_t getThingIndex(const Thing* thing) const override final;
	size_t getFirstIndex() const override final;
	size_t getLastIndex() const override final;
	uint32_t getItemTypeCount(uint16_t itemId, int32_t subType = -1, bool ignoreEquipped = false) const override final;
	std::unordered_map<uint32_t, uint32_t>& getAllItemTypeCount(std::unordered_map<uint32_t, uint32_t>& countMap) const override final;
	Thing* getThing(size_t index) const override final;

	void postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index,
	                         cylinderlink_t link = LINK_OWNER) override;
	void postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index,
	                            cylinderlink_t link = LINK_OWNER) override;

	void internalAddThing(Thing* thing) override final;
	void internalAddThing(uint32_t index, Thing* thing) override final;
	void startDecaying() override final;
	void stopDecaying() override final;

protected:
	ItemDeque itemlist;

private:
	uint32_t maxSize;
	bool pagination = false;
	uint32_t ammoCount = 0;
	uint32_t totalWeight = 0;
	uint32_t serializationCount = 0;

	void onAddContainerItem(Item* item) const;
	void onUpdateContainerItem(uint32_t index, Item* oldItem, Item* newItem) const;
	void onRemoveContainerItem(uint32_t index, Item* item) const;
	std::shared_ptr<Player> getHoldingPlayerForNotification() const;

	void updateAmmoCount(const Item* item, int32_t diff);
	void sendQuiverInventoryUpdate() const;
	void updateItemWeight(int32_t diff);
	bool hasCapacityLimit() const;
	uint32_t getFreeSlotsFor(const Item* item, uint32_t count) const;
	bool canMergeIntoExistingStack(const Item* item, int32_t index) const;
	bool hasRoomForItem(const Item* item, int32_t index, uint32_t count) const;

	friend class ContainerIterator;
	friend class IOMapSerialize;
};

#endif
