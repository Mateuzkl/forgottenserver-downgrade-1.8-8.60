// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_STORE_REPOSITORY_H
#define FS_STORE_REPOSITORY_H

#include "store/store_types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Centralized DB access for store operations.
/// All store-related SQL is contained here — no SQL in ProtocolGame/StoreService/Lua.
class StoreRepository final
{
public:
	static StoreRepository& getInstance();

	/// Load purchase history for an account. Returns up to `limit` entries, newest first.
	[[nodiscard]] std::vector<StoreHistoryEntry> loadHistory(uint32_t accountId, size_t limit = 100);

	/// Record a purchase/transfer in the shop_history table.
	[[nodiscard]] bool addHistory(uint32_t accountId, uint32_t playerGuid,
	                              std::string_view title, int32_t price,
	                              uint16_t count, std::string_view target = "");

	/// Transactional character rename with death-table history update.
	/// Returns true on success. On failure, reason is populated.
	[[nodiscard]] bool renameCharacter(uint32_t playerId, std::string_view oldName,
	                                   std::string_view newName, std::string& reason);

	/// Record a name change in change_name_history (if the table exists).
	void recordNameChange(uint32_t playerId, std::string_view oldName, std::string_view newName);

private:
	StoreRepository() = default;
};

#endif // FS_STORE_REPOSITORY_H
