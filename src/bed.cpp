// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "bed.h"

#include "game.h"
#include "iologindata.h"
#include "scheduler.h"

using namespace std::chrono;

extern Game g_game;

static constexpr uint32_t REGEN_INTERVAL_SECONDS = 30;
static constexpr uint32_t REGEN_TICKS_PER_INTERVAL = 30000;
static constexpr uint32_t SOUL_REGEN_INTERVAL_SECONDS = 60 * 15;

BedItem::BedItem(uint16_t id) : Item(id) { internalRemoveSleeper(); }

Attr_ReadValue BedItem::readAttr(AttrTypes_t attr, PropStream& propStream)
{
	switch (attr) {
		case ATTR_SLEEPERGUID: {
			uint32_t guid;
			if (!propStream.read<uint32_t>(guid)) {
				return ATTR_READ_ERROR;
			}

			if (guid != 0) {
				if (auto name = IOLoginData::getNameByGuid(guid); !name.empty()) {
					setSpecialDescription(fmt::format("{} is sleeping there.", name));
					g_game.setBedSleeper(this, guid);
					sleeperGUID = guid;
				}
			}
			return ATTR_READ_CONTINUE;
		}

		case ATTR_SLEEPSTART: {
			uint32_t sleep_start;
			if (!propStream.read<uint32_t>(sleep_start)) {
				return ATTR_READ_ERROR;
			}

			sleepStart = static_cast<uint64_t>(sleep_start);
			return ATTR_READ_CONTINUE;
		}

		default:
			break;
	}
	return Item::readAttr(attr, propStream);
}

void BedItem::serializeAttr(PropWriteStream& propWriteStream) const
{
	if (sleeperGUID != 0) {
		propWriteStream.write<uint8_t>(ATTR_SLEEPERGUID);
		propWriteStream.write<uint32_t>(sleeperGUID);
	}

	if (sleepStart != 0) {
		propWriteStream.write<uint8_t>(ATTR_SLEEPSTART);
		// FIXME: should be stored as 64-bit, but we need to retain backwards compatibility
		propWriteStream.write<uint32_t>(static_cast<uint32_t>(sleepStart));
	}
}

BedItem* BedItem::getNextBedItem() const
{
	const auto dir = Item::items[id].bedPartnerDir;
	const auto targetPos = getNextPosition(dir, getPosition());

	auto* tile = g_game.map.getTile(targetPos);
	if (!tile) {
		return nullptr;
	}
	return tile->getBedItem();
}

bool BedItem::canUse(Player* player)
{
	if (!player || !house || !player->isPremium() || player->getZone() != ZONE_PROTECTION) {
		return false;
	}

	if (sleeperGUID == 0) {
		return true;
	}

	if (house->getHouseAccessLevel(player) == HOUSE_OWNER) {
		return true;
	}

	Player sleeper(nullptr);
	if (!IOLoginData::loadPlayerById(&sleeper, sleeperGUID)) {
		return false;
	}

	return house->getHouseAccessLevel(&sleeper) <= house->getHouseAccessLevel(player);
}

bool BedItem::trySleep(Player* player)
{
	if (!house || player->isRemoved()) {
		return false;
	}

	if (sleeperGUID != 0) {
		const auto& itemType = Item::items[id];
		if (itemType.transformToFree != 0 && house->getOwner() == player->getGUID()) {
			wakeUp(nullptr);
		}

		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}
	return true;
}

bool BedItem::sleep(Player* player)
{
	if (!house || sleeperGUID != 0) {
		return false;
	}

	auto* nextBedItem = getNextBedItem();

	internalSetSleeper(player);

	if (nextBedItem) {
		nextBedItem->internalSetSleeper(player);
	}

	// update the bedSleepersMap
	g_game.setBedSleeper(this, player->getGUID());

	// make the player walk onto the bed
	g_game.map.moveCreature(*player, *getTile());

	// display 'Zzzz'/sleep effect
	g_game.addMagicEffect(player->getPosition(), CONST_ME_SLEEP);

	// kick player after he sees himself walk onto the bed and it change id
	g_scheduler.addEvent(createSchedulerTask(SCHEDULER_MINTICKS,
	                                         [playerID = player->getID()]() { g_game.kickPlayer(playerID, false); }));

	// change self and partner's appearance
	updateAppearance(player);

	if (nextBedItem) {
		nextBedItem->updateAppearance(player);
	}

	return true;
}

void BedItem::wakeUp(Player* player)
{
	if (!house) {
		return;
	}

	if (sleeperGUID != 0) {
		if (!player) {
			Player regenPlayer(nullptr);
			if (IOLoginData::loadPlayerById(&regenPlayer, sleeperGUID)) {
				regeneratePlayer(&regenPlayer);
				IOLoginData::savePlayer(&regenPlayer);
			}
		} else {
			regeneratePlayer(player);
			g_game.addCreatureHealth(player);
		}
	}

	// update the bedSleepersMap
	g_game.removeBedSleeper(sleeperGUID);

	auto* nextBedItem = getNextBedItem();

	// unset sleep info
	internalRemoveSleeper();

	if (nextBedItem) {
		nextBedItem->internalRemoveSleeper();
	}

	// change self and partner's appearance
	updateAppearance(nullptr);

	if (nextBedItem) {
		nextBedItem->updateAppearance(nullptr);
	}
}

void BedItem::regeneratePlayer(Player* player) const
{
	const auto now = system_clock::now();
	const auto currentTime = static_cast<uint64_t>(duration_cast<seconds>(now.time_since_epoch()).count());

	if (currentTime <= sleepStart) {
		return;
	}

	const auto sleptTime = static_cast<uint32_t>(currentTime - sleepStart);

	auto* condition = player->getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT);
	if (condition) {
		uint32_t regen;

		if (condition->getTicks() != -1) {
			const auto ticksInSeconds = static_cast<uint32_t>(condition->getTicks() / 1000);
			regen = std::min(ticksInSeconds, sleptTime) / REGEN_INTERVAL_SECONDS;

			const auto newRegenTicks = condition->getTicks() - static_cast<int32_t>(regen * REGEN_TICKS_PER_INTERVAL);

			if (newRegenTicks <= 0) {
				player->removeCondition(condition);
			} else {
				condition->setTicks(newRegenTicks);
			}
		} else {
			regen = sleptTime / REGEN_INTERVAL_SECONDS;
		}

		player->changeHealth(regen, false);
		player->changeMana(regen);
	}

	const int32_t soulRegen = static_cast<int32_t>(sleptTime / SOUL_REGEN_INTERVAL_SECONDS);
	player->changeSoul(soulRegen);
}

void BedItem::updateAppearance(const Player* player)
{
	const auto& it = Item::items[id];
	if (it.type == ITEM_TYPE_BED) {
		if (player && it.transformToOnUse[player->getSex()] != 0) {
			const auto& newType = Item::items[it.transformToOnUse[player->getSex()]];
			if (newType.type == ITEM_TYPE_BED) {
				g_game.transformItem(this, it.transformToOnUse[player->getSex()]);
			}
		} else if (it.transformToFree != 0) {
			const auto& newType = Item::items[it.transformToFree];
			if (newType.type == ITEM_TYPE_BED) {
				g_game.transformItem(this, it.transformToFree);
			}
		}
	}
}

void BedItem::internalSetSleeper(const Player* player)
{
	sleeperGUID = player->getGUID();
	sleepStart = static_cast<uint64_t>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());

	setSpecialDescription(fmt::format("{} is sleeping there.", player->getName()));
}

void BedItem::internalRemoveSleeper() noexcept
{
	sleeperGUID = 0;
	sleepStart = 0;

	if (isRemoved() || !getParent()) {
		return;
	}

	setSpecialDescription("Nobody is sleeping there.");
}
