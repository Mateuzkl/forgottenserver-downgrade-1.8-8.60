// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_STASH_H
#define FS_STASH_H

#include <cstdint>
#include <vector>

struct StashRecord
{
	uint16_t itemId = 0;
	uint8_t tier = 0;
	uint32_t amount = 0;
};

class Stash
{
public:
	static constexpr uint8_t MAX_ITEM_TIER = 10;

	[[nodiscard]] static uint8_t normalizeTier(uint32_t tier);
	[[nodiscard]] static std::vector<StashRecord> getRows(uint32_t playerId);
	[[nodiscard]] static bool addAmount(uint32_t playerId, uint16_t itemId, uint32_t amount, uint8_t tier);
	[[nodiscard]] static bool removeAmount(uint32_t playerId, uint16_t itemId, uint32_t amount, uint8_t tier);
	[[nodiscard]] static bool cleanup(uint32_t playerId);
};

#endif // FS_STASH_H
