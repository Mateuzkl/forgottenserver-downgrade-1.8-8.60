/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2020  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later.
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

#ifndef FS_DECAY_H_F5229673AD6B4A2BAA2D38E5618F77B3A44B4248517D472553552E68
#define FS_DECAY_H_F5229673AD6B4A2BAA2D38E5618F77B3A44B4248517D472553552E68

#include "item.h"

class Decay
{
public:
	static constexpr size_t DEFAULT_RESERVE_SIZE = 32;

	void startDecay(Item* item, int32_t duration);
	void stopDecay(Item* item, int64_t timestamp) noexcept;

	[[nodiscard]] bool hasDecayingItems() const noexcept { return !decayMap.empty(); }

	[[nodiscard]] size_t getDecayBucketCount() const noexcept { return decayMap.size(); }

	[[nodiscard]] size_t getTotalDecayingItems() const noexcept;

private:
	using DecayTimestamp = int64_t;
	using ItemRef = Item*;
	using DecayBucket = std::vector<ItemRef>;

	void checkDecay() noexcept;
	void processDecayBatch(std::span<ItemRef const> items) noexcept;
	void scheduleNextCheck(DecayTimestamp nextTimestamp) noexcept;

	[[nodiscard]] static constexpr int32_t clampSchedulerDuration(int32_t duration) noexcept;

	uint64_t eventId{0};
	std::map<DecayTimestamp, DecayBucket> decayMap;
};

extern Decay g_decay;

#endif