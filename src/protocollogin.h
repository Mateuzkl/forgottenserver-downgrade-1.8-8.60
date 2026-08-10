// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_PROTOCOLLOGIN_H
#define FS_PROTOCOLLOGIN_H

#include "protocol.h"

class NetworkMessage;
class OutputMessage;

class LoginAttemptLimiter
{
public:
	static LoginAttemptLimiter& getInstance()
	{
		static LoginAttemptLimiter instance;
		return instance;
	}

	// Failures are tracked per (IP, account) so a successful login to one account
	// cannot clear the failures recorded against another. A separate per-IP counter
	// with a higher threshold still catches account spraying, where each individual
	// account sees only a handful of guesses.
	//
	// accountName is normalized (trimmed, lower-cased) by the limiter, so callers
	// may pass whatever the client sent. An empty account name means "no account
	// dimension" - the caller is doing something that is not an account login, such
	// as a cast list request - and only the per-IP guard applies.
	[[nodiscard]] bool allowLogin(uint32_t ip, std::string_view accountName);
	void recordFailure(uint32_t ip, std::string_view accountName);
	void recordSuccess(uint32_t ip, std::string_view accountName);

private:
	LoginAttemptLimiter() = default;
	void cleanup(int64_t now);

	struct AttemptInfo
	{
		uint32_t failures = 0;
		int64_t blockUntil = 0; // OTSYS_TIME value
		int64_t firstAttempt = 0;
	};

	using AccountKey = std::pair<uint32_t, std::string>;

	struct AccountKeyHash
	{
		size_t operator()(const AccountKey& key) const noexcept
		{
			return std::hash<uint32_t>{}(key.first) ^ (std::hash<std::string>{}(key.second) << 1);
		}
	};

	// Returns true if this entry is currently blocking, updating expiry state.
	static bool isBlocked(AttemptInfo& info, int64_t now);
	static void registerFailure(AttemptInfo& info, int64_t now, uint32_t threshold, int64_t& blockedAt);

	std::unordered_map<AccountKey, AttemptInfo, AccountKeyHash> accountAttempts;
	std::unordered_map<uint32_t, AttemptInfo> ipAttempts;
	std::mutex mu;
	int64_t lastCleanup = 0;

	// Per (IP, account): a targeted guess against one account.
	static constexpr uint32_t MAX_FAILURES = 5;
	// Per IP across all accounts: catches spraying, where no single account reaches
	// MAX_FAILURES. Deliberately well above what a person mistyping their own
	// password reaches, so shared addresses (NAT, internet cafes) are not punished.
	static constexpr uint32_t MAX_IP_FAILURES = 20;
	static constexpr int64_t WINDOW_MS = 60000;      // 60 seconds
	static constexpr int64_t BLOCK_TIME_MS = 300000; // 5 minutes
};

class ProtocolLogin : public Protocol
{
public:
	// static protocol information
	enum
	{
		server_sends_first = false
	};
	enum
	{
		protocol_identifier = 0x01
	};
	enum
	{
		use_checksum = true
	};
	static const char* protocol_name() { return "login protocol"; }

	explicit ProtocolLogin(Connection_ptr connection) : Protocol(connection) {}

	void onRecvFirstMessage(NetworkMessage& msg) override;

private:
	void disconnectClient(std::string_view message);

	void getCharacterList(std::string_view accountName, std::string_view password, bool isAstraClient);
	void getCastList(const std::string& password);
	void getAstraCastList();

	bool isAstraClient_ = false;
};

#endif
