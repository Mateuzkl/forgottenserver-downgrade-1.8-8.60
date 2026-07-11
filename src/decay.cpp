/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2020  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include "decay.h"
#include "game.h"
#include "scheduler.h"

extern Game g_game;
Decay g_decay;

constexpr int32_t Decay::clampSchedulerDuration(int32_t duration) noexcept
{
	return std::max(SCHEDULER_MINTICKS, duration);
}

void Decay::scheduleNextCheck(DecayTimestamp nextTimestamp) noexcept
{
	const auto currentTime = OTSYS_TIME();
	const auto delay = clampSchedulerDuration(static_cast<int32_t>(nextTimestamp - currentTime));

	if (eventId != 0) {
		g_scheduler.stopEvent(eventId);
	}

	eventId = g_scheduler.addEvent(createSchedulerTask(delay, [this] { checkDecay(); }));
}

void Decay::processDecayBatch(std::span<ItemRef const> items) noexcept
{
	for (const auto& weakItem : items) {
		auto item = weakItem.lock();
		if (!item) [[unlikely]] {
			continue;
		}

		if (!item->canDecay()) {
			item->setDuration(item->getDuration());
			item->setDecaying(DECAYING_FALSE);
		} else {
			item->setDecaying(DECAYING_FALSE);
			g_game.internalDecayItem(item);
		}
	}
}

size_t Decay::getTotalDecayingItems() const noexcept
{
	size_t total = 0;
	for (const auto& decayEntry : decayMap) {
		const auto& items = decayEntry.second;
		for (const auto& weakItem : items) {
			if (auto item = weakItem.lock()) {
				++total;
			}
		}
	}
	return total;
}

void Decay::startDecay(std::shared_ptr<Item> item, int32_t duration)
{
	if (!item) [[unlikely]] {
		return;
	}

	if (item->hasAttribute(ITEM_ATTRIBUTE_DURATION_TIMESTAMP)) {
		stopDecay(std::weak_ptr<Item>(item), item->getIntAttr(ITEM_ATTRIBUTE_DURATION_TIMESTAMP));
	}

	const DecayTimestamp timestamp = OTSYS_TIME() + static_cast<int64_t>(duration);
	const auto schedulerDuration = clampSchedulerDuration(duration);

	const bool needsReschedule = decayMap.empty() || timestamp < decayMap.begin()->first;

	if (needsReschedule) {
		if (!decayMap.empty()) {
			g_scheduler.stopEvent(eventId);
		}

		eventId = g_scheduler.addEvent(createSchedulerTask(schedulerDuration, [this] { checkDecay(); }));
	}

	item->setDecaying(DECAYING_TRUE);
	item->setDurationTimestamp(timestamp);
	decayMap[timestamp].emplace_back(item);
}

void Decay::stopDecay(std::weak_ptr<Item> weakItem, int64_t timestamp) noexcept
{
	auto item = weakItem.lock();
	if (!item) [[unlikely]] {
		return;
	}

	const auto it = decayMap.find(timestamp);
	if (it == decayMap.end()) {
		return;
	}

	auto& decayItems = it->second;

	if (const auto itemIt = std::ranges::find_if(decayItems, [&item](const auto& ref) {
		auto refItem = ref.lock();
		return refItem && refItem.get() == item.get();
	}); itemIt != decayItems.end()) {
		if (item->hasAttribute(ITEM_ATTRIBUTE_DURATION)) {
			item->setDuration(item->getDuration());
		}

		item->removeAttribute(ITEM_ATTRIBUTE_DECAYSTATE);

		if (decayItems.size() == 1) {
			decayMap.erase(it);
		} else {
			std::iter_swap(itemIt, decayItems.end() - 1);
			decayItems.pop_back();
		}
	}
}

void Decay::clear() noexcept
{
	if (eventId != 0) {
		g_scheduler.stopEvent(eventId);
		eventId = 0;
	}

	for (auto& decayEntry : decayMap) {
		auto& items = decayEntry.second;
		for (const auto& weakItem : items) {
			if (auto item = weakItem.lock()) {
				item->removeAttribute(ITEM_ATTRIBUTE_DECAYSTATE);
			}
		}
	}
	decayMap.clear();
}

void Decay::checkDecay() noexcept
{
	const auto timestamp = OTSYS_TIME();

	std::vector<ItemRef> expiredItems;
	expiredItems.reserve(DEFAULT_RESERVE_SIZE);

	auto it = decayMap.begin();
	while (it != decayMap.end() && it->first <= timestamp) {
		auto& items = it->second;

		expiredItems.insert(
			expiredItems.end(),
			std::make_move_iterator(items.begin()),
			std::make_move_iterator(items.end())
		);

		it = decayMap.erase(it);
	}

	processDecayBatch(expiredItems);

	if (!decayMap.empty()) {
		scheduleNextCheck(decayMap.begin()->first);
	}
}
