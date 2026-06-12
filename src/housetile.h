// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_HOUSETILE_H
#define FS_HOUSETILE_H

#include "tile.h"

#include <memory>

class House;

class HouseTile final : public DynamicTile
{
public:
	HouseTile(uint16_t x, uint16_t y, uint8_t z, House* house);

	using Tile::internalAddThing;

	HouseTile* getHouseTile() override final { return this; }
	const HouseTile* getHouseTile() const override final { return this; }

	// cylinder implementations
	ReturnValue queryAdd(int32_t index, const Thing& thing, uint32_t count, uint32_t flags,
	                     Creature* actor = nullptr) const override;

	Tile* queryDestination(int32_t& index, const Thing& thing, Item** destItem, uint32_t& flags,
	                       uint32_t destinationInstanceId) override;

	ReturnValue queryRemove(const Thing& thing, uint32_t count, uint32_t flags,
	                        Creature* actor = nullptr) const override;

	void addThing(int32_t index, Thing* thing) override;
	void internalAddThing(uint32_t index, Thing* thing) override;

	House* getHouse() const { return house.lock().get(); }

private:
	void updateHouse(Item* item);

	std::weak_ptr<House> house;
};

#endif
