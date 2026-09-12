// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_ACCOUNT_COINS_H
#define FS_ACCOUNT_COINS_H

#include <cstdint>
#include <optional>
#include <string>

/// Authoritative, transaction-safe Tibia Coin accounting.
/// Both Character Bazaar and Game Store share this implementation
/// so that all coin mutations use the same atomic SQL patterns.
namespace AccountCoins {

/// Maximum coin balance — corresponds to UINT32_MAX because the
/// 8.60 store wire protocol serializes balance as U32.
inline constexpr uint64_t MaxCoins = 4'294'967'295ULL;

/// Read current coin balance for an account (single SELECT).
[[nodiscard]] uint64_t get(uint32_t accountId);

/// Atomically debit `amount` coins from an account.
/// Uses: UPDATE ... SET tibia_coins = tibia_coins - amount WHERE ... AND tibia_coins >= amount
/// Returns true only if exactly 1 row was affected (sufficient balance existed).
[[nodiscard]] bool debit(uint32_t accountId, uint64_t amount);

/// Atomically credit `amount` coins to an account.
/// Uses: UPDATE ... SET tibia_coins = tibia_coins + amount WHERE ... AND tibia_coins <= MaxCoins - amount
/// Returns true only if exactly 1 row was affected (no overflow).
[[nodiscard]] bool credit(uint32_t accountId, uint64_t amount);

struct TransferHistoryDetails
{
	uint32_t sourcePlayerId = 0;
	std::string sourcePlayerName;
	uint32_t destPlayerId = 0;
	std::string destPlayerName;
};

/// Transfer `amount` coins between two accounts in a single DB transaction.
/// Debits source, credits destination, with deterministic lock ordering
/// (min account ID locked first) to prevent deadlocks.
/// If `history` is provided, inserts shop_history entries inside the same transaction.
/// Returns true on success; on failure the transaction is rolled back atomically.
[[nodiscard]] bool transfer(uint32_t sourceAccountId, uint32_t destAccountId, uint64_t amount,
                            const std::optional<TransferHistoryDetails>& history = std::nullopt);

struct CharacterAccountInfo
{
	uint32_t playerId = 0;
	uint32_t accountId = 0;
	std::string playerName;
};

/// Look up a character's account info by player name (case-insensitive).
/// Used by coin transfer to resolve the destination.
[[nodiscard]] std::optional<CharacterAccountInfo> findCharacterAccount(const std::string& playerName);

} // namespace AccountCoins

#endif // FS_ACCOUNT_COINS_H
