// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_BED_H
#define FS_BED_H

#include "item.h"

#include <memory>
#include <vector>

class House;
class Player;

class BedItem : public Item
{
public:
	explicit BedItem(uint16_t id);

	[[nodiscard]] std::shared_ptr<BedItem> getBed() override
	{
		return std::static_pointer_cast<BedItem>(weak_from_this().lock());
	}
	[[nodiscard]] std::shared_ptr<const BedItem> getBed() const override
	{
		return std::static_pointer_cast<const BedItem>(weak_from_this().lock());
	}

	Attr_ReadValue readAttr(AttrTypes_t attr, PropStream& propStream) override;
	void serializeAttr(PropWriteStream& propWriteStream) const override;

	[[nodiscard]] bool canRemove() const override { return house.expired(); }
	void onRemoved() override;

	[[nodiscard]] uint32_t getSleeper() const noexcept { return sleeperGUID; }

	[[nodiscard]] std::shared_ptr<House> getHouse() const noexcept { return house.lock(); }
	void setHouse(const std::shared_ptr<House>& h) noexcept;

	[[nodiscard]] bool canUse(Player* player);

	bool trySleep(Player* player);
	bool sleep(Player* player);
	// A failed offline load/save leaves the sleep session intact for retry.
	bool wakeUp(Player* player);
	// Persist every offline sleeper before clearing any of the sleep sessions.
	static bool wakeUpAll(const std::vector<std::shared_ptr<BedItem>>& beds);

	[[nodiscard]] std::shared_ptr<BedItem> getNextBedItem() const;

protected:
	virtual bool loadOfflineSleeper(Player* player, uint32_t guid) const;
	// Must persist the whole batch atomically, with no deferred writes on failure.
	virtual bool saveOfflineSleepers(const std::vector<Player*>& players) const;

private:
	void updateAppearance(const Player* player);
	static void regeneratePlayer(Player* player, uint64_t sleepStart);
	void internalSetSleeper(const Player* player);
	void internalRemoveSleeper() noexcept;

	std::weak_ptr<House> house;
	uint64_t sleepStart = 0;
	uint32_t sleeperGUID = 0;
};

#endif
