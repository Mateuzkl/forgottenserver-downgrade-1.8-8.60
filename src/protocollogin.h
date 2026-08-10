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
	[[nodiscard]] bool reserveLogin(uint32_t ip, std::string_view accountName);
	void commitFailure(uint32_t ip, std::string_view accountName);
	void commitSuccess(uint32_t ip, std::string_view accountName);
	void releaseReservation(uint32_t ip, std::string_view accountName);
	void recordFailure(uint32_t ip, std::string_view accountName);
	void recordSuccess(uint32_t ip, std::string_view accountName);

private:
	LoginAttemptLimiter() = default;
	void cleanup(int64_t now);

	struct AttemptInfo
	{
		uint32_t failures = 0;
		uint32_t pending = 0;
		int64_t blockUntil = 0; // OTSYS_TIME value
		int64_t firstAttempt = 0;
	};

	using AccountKey = std::pair<uint32_t, std::string>;

	struct AccountKeyHash
	{
		size_t operator()(const AccountKey& key) const noexcept
		{
			// A plain xor with a shifted second hash discards a high bit and leaves
			// the two inputs barely mixed. This is the usual combine step: folding
			// the seed back in through the golden-ratio constant and two shifts
			// scatters bits, so keys differing only in the account name do not
			// cluster into the same buckets.
			size_t seed = std::hash<uint32_t>{}(key.first);
			seed ^= std::hash<std::string>{}(key.second) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
			return seed;
		}
	};

	// Returns true if this entry is currently blocking, updating expiry state.
	static bool isBlocked(AttemptInfo& info, int64_t now);
	static bool hasCapacity(AttemptInfo& info, int64_t now, uint32_t threshold);
	static void registerFailure(AttemptInfo& info, int64_t now, uint32_t threshold, int64_t blockDurationMs,
	                            int64_t& blockedAt);
	void releaseReservationLocked(uint32_t ip, const std::string& account);
	void recordFailureLocked(uint32_t ip, const std::string& account, int64_t now);
	void recordSuccessLocked(uint32_t ip, const std::string& account);

	// Thresholds are read from config so server owners can tune them; see
	// bruteForce* in config.lua.dist.
	static uint32_t accountFailureThreshold();
	static uint32_t ipFailureThreshold();
	static int64_t blockDurationMs();

	std::unordered_map<AccountKey, AttemptInfo, AccountKeyHash> accountAttempts;
	std::unordered_map<uint32_t, AttemptInfo> ipAttempts;
	std::mutex mu;
	int64_t lastCleanup = 0;

	static constexpr int64_t WINDOW_MS = 60000; // 60 seconds

	// Used when the config has not been loaded (unit tests) or holds a nonsensical
	// value. Falling back beats honouring a 0, which would block on first failure.
	static constexpr uint32_t DEFAULT_ACCOUNT_FAILURES = 5;
	static constexpr uint32_t DEFAULT_IP_FAILURES = 20;
	static constexpr int64_t DEFAULT_BLOCK_SECONDS = 300;
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
	static void recordRejectedCastPassword(uint32_t ip);

private:
	void disconnectClient(std::string_view message);

	void getCharacterList(std::string_view accountName, std::string_view password, bool isAstraClient,
	                      uint32_t clientIP);
	void getCastList(const std::string& password, uint32_t clientIP);
	void getAstraCastList();

	bool isAstraClient_ = false;
};

#endif
