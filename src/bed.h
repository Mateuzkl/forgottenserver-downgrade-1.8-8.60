// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_BED_H
#define FS_BED_H

#include "item.h"

#include <memory>

class House;
class Player;

class BedItem final : public Item
{
public:
	explicit BedItem(uint16_t id);

	[[nodiscard]] BedItem* getBed() override { return this; }
	[[nodiscard]] const BedItem* getBed() const override { return this; }

	Attr_ReadValue readAttr(AttrTypes_t attr, PropStream& propStream) override;
	void serializeAttr(PropWriteStream& propWriteStream) const override;

	[[nodiscard]] bool canRemove() const override { return house.expired(); }

	[[nodiscard]] uint32_t getSleeper() const noexcept { return sleeperGUID; }

	[[nodiscard]] std::shared_ptr<House> getHouse() const noexcept { return house.lock(); }
	void setHouse(const std::shared_ptr<House>& h) noexcept;

	[[nodiscard]] bool canUse(Player* player);

	bool trySleep(Player* player);
	bool sleep(Player* player);
	void wakeUp(Player* player);

	[[nodiscard]] std::shared_ptr<BedItem> getNextBedItem() const;

private:
	void updateAppearance(const Player* player);
	void regeneratePlayer(Player* player) const;
	void internalSetSleeper(const Player* player);
	void internalRemoveSleeper() noexcept;

	std::weak_ptr<House> house;
	uint64_t sleepStart = 0;
	uint32_t sleeperGUID = 0;
};

#endif
