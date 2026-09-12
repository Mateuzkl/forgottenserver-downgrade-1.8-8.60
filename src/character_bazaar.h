// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CHARACTER_BAZAAR_H
#define FS_CHARACTER_BAZAAR_H

#include "account_coins.h"

#include <cstdint>
#include <string>

class Player;

namespace CharacterBazaar {

inline constexpr uint8_t CLIENT_PACKET = 0x5E;
inline constexpr uint8_t SERVER_PACKET = 0x2E;

inline constexpr uint8_t ACTION_REQUEST_REQUIREMENTS = 0x01;
inline constexpr uint8_t ACTION_CREATE_AUCTION = 0x02;
inline constexpr uint8_t AUCTION_STATUS_ACTIVE = 1;
inline constexpr uint32_t MAX_DESCRIPTION_LENGTH = 512;
inline constexpr uint64_t MAX_TIBIA_COINS = AccountCoins::MaxCoins;

bool isPlayerOnActiveAuction(uint32_t playerId);
bool canCreateAuction(Player* player, std::string& reason);
bool createAuction(Player* player, uint32_t startPrice, uint32_t durationSeconds, const std::string& description,
                   std::string& reason);

void sendRequirements(Player* player);
void sendCreateResult(Player* player, bool success, const std::string& message);
void finalizeExpiredAuctions();
void scheduleFinalization();

bool addHistory(uint32_t auctionId, const std::string& action, uint32_t accountId, uint32_t playerId,
                uint64_t amount, const std::string& message);
/// @deprecated Use AccountCoins::get() directly. Kept for backward compatibility.
inline uint64_t getTransferableCoins(uint32_t accountId) { return AccountCoins::get(accountId); }
/// @deprecated Use AccountCoins::debit() directly. Kept for backward compatibility.
inline bool debitTransferableCoins(uint32_t accountId, uint64_t amount) { return AccountCoins::debit(accountId, amount); }
/// @deprecated Use AccountCoins::credit() directly. Kept for backward compatibility.
inline bool creditTransferableCoins(uint32_t accountId, uint64_t amount) { return AccountCoins::credit(accountId, amount); }

} // namespace CharacterBazaar

#endif // FS_CHARACTER_BAZAAR_H
