// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_DEPOTCHEST_H
#define FS_DEPOTCHEST_H

#include "container.h"

class DepotChest final : public Container
{
public:
	explicit DepotChest(uint16_t type);

	// serialization
	void setMaxDepotItems(uint32_t maxitems) { maxDepotItems = maxitems; }

	// cylinder implementations
	ReturnValue queryAdd(int32_t index, const Thing& thing, uint32_t count, uint32_t flags,
	                     Creature* actor = nullptr) const override;
	ReturnValue queryRemove(const Thing& thing, uint32_t count, uint32_t flags,
	                        Creature* actor = nullptr) const override;
	Cylinder* queryDestination(int32_t& index, const Thing& thing, Item** destItem, uint32_t& flags,
	                           uint32_t destinationInstanceId) override;

	void postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index,
	                         cylinderlink_t link = LINK_OWNER) override;
	void postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index,
	                            cylinderlink_t link = LINK_OWNER) override;

	// overrides
	bool canRemove() const override { return false; }

	bool isRemoved() const override { return false; }

	// Cylinder* getParent() const override;
	Cylinder* getRealParent() const override { return parent; }

	bool needsSave() { return save; }

private:
	uint32_t maxDepotItems = 2000;
	bool save = false;
};

class DepotBox final : public Container
{
public:
	explicit DepotBox(uint16_t type);
};

#endif
