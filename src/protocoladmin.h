// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_PROTOCOLADMIN_H
#define FS_PROTOCOLADMIN_H

#include "protocol.h"

class NetworkMessage;

class ProtocolAdmin : public Protocol
{
public:
	// static uint32_t protocolAdminCount; // For diagnostics if needed

	void onRecvFirstMessage(NetworkMessage& msg) override;

	ProtocolAdmin(Connection_ptr connection) : Protocol(connection)
	{
		state = NO_CONNECTED;
		loginTries = 0;
		lastCommand = 0;
		startTime = std::time(nullptr);
	}

	enum
	{
		protocolId = 0xFE
	};
	// enum {isSingleSocket = false}; // Protocol base class handles this? Check if needed.
	// enum {hasChecksum = false};
	static const char* protocol_name() { return "admin protocol"; }
	static const bool server_sends_first = true;
	static const bool use_checksum = false;
	static const uint8_t protocol_identifier = protocolId;

protected:
	enum ProtocolState_t
	{
		NO_CONNECTED,
		ENCRYPTION_NO_SET,
		ENCRYPTION_OK,
		NO_LOGGED_IN,
		LOGGED_IN
	};

	void parsePacket(NetworkMessage& msg) override;
	void release() override; // Renamed from releaseProtocol

	// commands
	void adminCommandPayHouses();
	void adminCommandReload(int8_t reload);
	void adminCommandKickPlayer(const std::string& name);
	void adminCommandSendMail(const std::string& xmlData);

private:
	void addLogLine(const std::string& message);

	int32_t loginTries;
	ProtocolState_t state;
	uint32_t lastCommand;
	uint32_t startTime;
};

#endif
