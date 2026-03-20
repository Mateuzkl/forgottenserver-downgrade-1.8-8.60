// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_PROTOCOLLOGIN_H
#define FS_PROTOCOLLOGIN_H

#include "protocol.h"

#include <map>
#include <mutex>

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

	// Returns true if the IP is allowed to attempt login
	bool allowLogin(uint32_t ip);
	// Record a failed login attempt for this IP
	void recordFailure(uint32_t ip);
	// Clear failures for an IP on successful login
	void recordSuccess(uint32_t ip);

private:
	LoginAttemptLimiter() = default;

	struct AttemptInfo
	{
		uint32_t failures = 0;
		int64_t blockUntil = 0; // OTSYS_TIME value
		int64_t firstAttempt = 0;
	};

	std::map<uint32_t, AttemptInfo> attempts;
	std::mutex mu;

	static constexpr uint32_t MAX_FAILURES = 5;
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

	void getCharacterList(std::string_view accountName, std::string_view password);
	void getCastList(const std::string& password);
};

#endif
