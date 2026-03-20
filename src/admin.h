// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_ADMIN_H
#define FS_ADMIN_H

#include "item.h"

class NetworkMessage;
class Player;

enum : uint16_t
{
	REQUIRE_LOGIN = 1,
	REQUIRE_ENCRYPTION = 2
};

enum
{
	ENCRYPTION_RSA1024XTEA = 1
};

class Admin
{
public:
	virtual ~Admin();
	static Admin& getInstance()
	{
		static Admin instance;
		return instance;
	}

	bool addConnection();
	void removeConnection();

	uint16_t getPolicy() const;
	uint32_t getOptions() const;

	static Item* createMail(const std::string& xmlData, std::string& name, uint32_t& depotId);
	bool allow(uint32_t ip) const;

	bool isEncrypted() const { return encryptionEnabled; }

protected:
	Admin();

	int32_t currentConnections;
	bool encryptionEnabled;
};

#endif
