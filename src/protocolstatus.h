// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_STATUS_H
#define FS_STATUS_H

#include "networkmessage.h"
#include "protocol.h"

class ProtocolStatus final : public Protocol
{
public:
	// static protocol information
	enum
	{
		server_sends_first = false
	};
	enum
	{
		protocol_identifier = 0xFF
	};
	enum
	{
		use_checksum = false
	};
	static const char* protocol_name() { return "status protocol"; }

	explicit ProtocolStatus(Connection_ptr connection) : Protocol(connection) {}

	void onRecvFirstMessage(NetworkMessage& msg) override;

	void sendStatusString();
	void sendInfo(uint16_t requestedInfo, std::string_view characterName);

	static const uint64_t start;

private:
	static std::unordered_map<uint32_t, int64_t> ipConnectMap;
	static std::mutex ipConnectMutex;
	static int64_t lastCleanup;
};

#endif
