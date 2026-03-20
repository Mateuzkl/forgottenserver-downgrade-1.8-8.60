// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_BAN_H
#define FS_BAN_H

#include "connection.h"

struct BanInfo
{
	std::string bannedBy;
	std::string reason;
	time_t expiresAt;
};

class IOBan
{
public:
	static bool isAccountBanned(uint32_t accountId, BanInfo& banInfo);
	static bool isIpBanned(const uint32_t clientIP, BanInfo& banInfo);
	static bool isPlayerNamelocked(uint32_t playerId);
	static bool accountHasNamelockedPlayer(uint32_t accountId);
	static uint32_t getNamelockedPlayerByAccount(uint32_t accountId);
};

#endif // FS_BAN_H
