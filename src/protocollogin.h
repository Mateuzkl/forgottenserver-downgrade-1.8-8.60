// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_PROTOCOLLOGIN_H
#define FS_PROTOCOLLOGIN_H

#include "protocol.h"

class NetworkMessage;
class OutputMessage;

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
